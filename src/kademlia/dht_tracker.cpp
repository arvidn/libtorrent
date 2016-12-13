/*

Copyright (c) 2006-2016, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#include "libtorrent/kademlia/dht_tracker.hpp"

#include <libtorrent/config.hpp>

#include <libtorrent/kademlia/msg.hpp>
#include <libtorrent/kademlia/dht_observer.hpp>

#include <libtorrent/bencode.hpp>
#include <libtorrent/version.hpp>
#include <libtorrent/time.hpp>
#include <libtorrent/performance_counters.hpp> // for counters
#include <libtorrent/aux_/time.hpp>
#include <libtorrent/session_status.hpp>
#include <libtorrent/session_settings.hpp>

#ifndef TORRENT_DISABLE_LOGGING
#include <libtorrent/hex.hpp> // to_hex
#endif

using namespace std::placeholders;

namespace libtorrent { namespace dht
{
	namespace {

	// generate a new write token key every 5 minutes
	time_duration const key_refresh
		= duration_cast<time_duration>(minutes(5));

	void add_dht_counters(node const& dht, counters& c)
	{
		int nodes, replacements, allocated_observers;
		std::tie(nodes, replacements, allocated_observers) = dht.get_stats_counters();

		c.inc_stats_counter(counters::dht_nodes, nodes);
		c.inc_stats_counter(counters::dht_node_cache, replacements);
		c.inc_stats_counter(counters::dht_allocated_observers, allocated_observers);
	}

	} // anonymous namespace

	// class that puts the networking and the kademlia node in a single
	// unit and connecting them together.
	dht_tracker::dht_tracker(dht_observer* observer
		, io_service& ios
		, send_fun_t const& send_fun
		, dht_settings const& settings
		, counters& cnt
		, dht_storage_interface& storage
		, dht_state state)
		: m_counters(cnt)
		, m_storage(storage)
		, m_state(std::move(state))
		, m_dht(udp::v4(), this, settings, m_state.nid
			, observer, cnt, m_nodes, storage)
#if TORRENT_USE_IPV6
		, m_dht6(udp::v6(), this, settings, m_state.nid6
			, observer, cnt, m_nodes, storage)
#endif
		, m_send_fun(send_fun)
		, m_log(observer)
		, m_key_refresh_timer(ios)
		, m_connection_timer(ios)
#if TORRENT_USE_IPV6
		, m_connection_timer6(ios)
#endif
		, m_refresh_timer(ios)
		, m_settings(settings)
		, m_abort(false)
		, m_host_resolver(ios)
		, m_send_quota(settings.upload_rate_limit)
		, m_last_tick(aux::time_now())
	{
		m_blocker.set_block_timer(m_settings.block_timeout);
		m_blocker.set_rate_limit(m_settings.block_ratelimit);

		m_nodes.insert(std::make_pair(m_dht.protocol_family_name(), &m_dht));
#if TORRENT_USE_IPV6
		m_nodes.insert(std::make_pair(m_dht6.protocol_family_name(), &m_dht6));
#endif

		update_storage_node_ids();

#ifndef TORRENT_DISABLE_LOGGING
		if (m_log->should_log(dht_logger::tracker))
		{
			m_log->log(dht_logger::tracker, "starting IPv4 DHT tracker with node id: %s"
				, aux::to_hex(m_dht.nid()).c_str());
#if TORRENT_USE_IPV6
			m_log->log(dht_logger::tracker, "starting IPv6 DHT tracker with node id: %s"
				, aux::to_hex(m_dht6.nid()).c_str());
#endif
		}
#endif
	}

	dht_tracker::~dht_tracker() = default;

	void dht_tracker::update_node_id()
	{
		m_dht.update_node_id();
#if TORRENT_USE_IPV6
		m_dht6.update_node_id();
#endif
		update_storage_node_ids();
	}

	void dht_tracker::start(find_data::nodes_callback const& f)
	{
		error_code ec;
		refresh_key(ec);

		m_connection_timer.expires_from_now(seconds(1), ec);
		m_connection_timer.async_wait(
			std::bind(&dht_tracker::connection_timeout, self(), std::ref(m_dht), _1));

#if TORRENT_USE_IPV6
		m_connection_timer6.expires_from_now(seconds(1), ec);
		m_connection_timer6.async_wait(
			std::bind(&dht_tracker::connection_timeout, self(), std::ref(m_dht6), _1));
#endif

		m_refresh_timer.expires_from_now(seconds(5), ec);
		m_refresh_timer.async_wait(std::bind(&dht_tracker::refresh_timeout, self(), _1));

		m_dht.bootstrap(m_state.nodes, f);
#if TORRENT_USE_IPV6
		m_dht6.bootstrap(m_state.nodes6, f);
#endif
		m_state.clear();
	}

	void dht_tracker::stop()
	{
		m_abort = true;
		error_code ec;
		m_key_refresh_timer.cancel(ec);
		m_connection_timer.cancel(ec);
#if TORRENT_USE_IPV6
		m_connection_timer6.cancel(ec);
#endif
		m_refresh_timer.cancel(ec);
		m_host_resolver.cancel();
	}

#ifndef TORRENT_NO_DEPRECATE
	void dht_tracker::dht_status(session_status& s)
	{
		s.dht_torrents += int(m_storage.num_torrents());

		s.dht_nodes = 0;
		s.dht_node_cache = 0;
		s.dht_global_nodes = 0;
		s.dht_torrents = 0;
		s.active_requests.clear();
		s.dht_total_allocations = 0;

		m_dht.status(s);
#if TORRENT_USE_IPV6
		m_dht6.status(s);
#endif
	}
#endif

	void dht_tracker::dht_status(std::vector<dht_routing_bucket>& table
		, std::vector<dht_lookup>& requests)
	{
		m_dht.status(table, requests);
#if TORRENT_USE_IPV6
		m_dht6.status(table, requests);
#endif
	}

	void dht_tracker::update_stats_counters(counters& c) const
	{
		const dht_storage_counters& dht_cnt = m_storage.counters();
		c.set_value(counters::dht_torrents, dht_cnt.torrents);
		c.set_value(counters::dht_peers, dht_cnt.peers);
		c.set_value(counters::dht_immutable_data, dht_cnt.immutable_data);
		c.set_value(counters::dht_mutable_data, dht_cnt.mutable_data);

		c.set_value(counters::dht_nodes, 0);
		c.set_value(counters::dht_node_cache, 0);
		c.set_value(counters::dht_allocated_observers, 0);

		add_dht_counters(m_dht, c);
#if TORRENT_USE_IPV6
		add_dht_counters(m_dht6, c);
#endif
	}

	void dht_tracker::connection_timeout(node& n, error_code const& e)
	{
		if (e || m_abort) return;

		time_duration d = n.connection_timeout();
		error_code ec;
#if TORRENT_USE_IPV6
		deadline_timer& timer = n.protocol() == udp::v4() ? m_connection_timer : m_connection_timer6;
#else
		deadline_timer& timer = m_connection_timer;
#endif
		timer.expires_from_now(d, ec);
		timer.async_wait(std::bind(&dht_tracker::connection_timeout, self(), std::ref(n), _1));
	}

	void dht_tracker::refresh_timeout(error_code const& e)
	{
		if (e || m_abort) return;

		m_dht.tick();
#if TORRENT_USE_IPV6
		m_dht6.tick();
#endif

		// periodically update the DOS blocker's settings from the dht_settings
		m_blocker.set_block_timer(m_settings.block_timeout);
		m_blocker.set_rate_limit(m_settings.block_ratelimit);

		error_code ec;
		m_refresh_timer.expires_from_now(seconds(5), ec);
		m_refresh_timer.async_wait(
			std::bind(&dht_tracker::refresh_timeout, self(), _1));
	}

	void dht_tracker::refresh_key(error_code const& e)
	{
		if (e || m_abort) return;

		error_code ec;
		m_key_refresh_timer.expires_from_now(key_refresh, ec);
		m_key_refresh_timer.async_wait(std::bind(&dht_tracker::refresh_key, self(), _1));

		m_dht.new_write_key();
#if TORRENT_USE_IPV6
		m_dht6.new_write_key();
#endif
#ifndef TORRENT_DISABLE_LOGGING
		m_log->log(dht_logger::tracker, "*** new write key***");
#endif
	}

	void dht_tracker::update_storage_node_ids()
	{
		std::vector<sha1_hash> ids;
		ids.push_back(m_dht.nid());
#if TORRENT_USE_IPV6
		ids.push_back(m_dht6.nid());
#endif
		m_storage.update_node_ids(ids);
	}

	void dht_tracker::get_peers(sha1_hash const& ih
		, std::function<void(std::vector<tcp::endpoint> const&)> f)
	{
		std::function<void(std::vector<std::pair<node_entry, std::string>> const&)> empty;
		m_dht.get_peers(ih, f, empty, false);
#if TORRENT_USE_IPV6
		m_dht6.get_peers(ih, f, empty, false);
#endif
	}

	void dht_tracker::announce(sha1_hash const& ih, int listen_port, int flags
		, std::function<void(std::vector<tcp::endpoint> const&)> f)
	{
		m_dht.announce(ih, listen_port, flags, f);
#if TORRENT_USE_IPV6
		m_dht6.announce(ih, listen_port, flags, f);
#endif
	}

	namespace {

	struct get_immutable_item_ctx
	{
		explicit get_immutable_item_ctx(int traversals)
			: active_traversals(traversals)
			, item_posted(false)
		{}
		int active_traversals;
		bool item_posted;
	};

	// these functions provide a slightly higher level
	// interface to the get/put functionality in the DHT
	void get_immutable_item_callback(item const& it, std::shared_ptr<get_immutable_item_ctx> ctx
		, std::function<void(item const&)> f)
	{
		// the reason to wrap here is to control the return value
		// since it controls whether we re-put the content
		TORRENT_ASSERT(!it.is_mutable());
		--ctx->active_traversals;
		if (!ctx->item_posted && (!it.empty() || ctx->active_traversals == 0))
		{
			ctx->item_posted = true;
			f(it);
		}
	}

	struct get_mutable_item_ctx
	{
		explicit get_mutable_item_ctx(int traversals) : active_traversals(traversals) {}
		int active_traversals;
		item it;
	};

	void get_mutable_item_callback(item const& it, bool authoritative
		, std::shared_ptr<get_mutable_item_ctx> ctx
		, std::function<void(item const&, bool)> f)
	{
		TORRENT_ASSERT(it.is_mutable());
		if (authoritative) --ctx->active_traversals;
		authoritative = authoritative && ctx->active_traversals == 0;
		if ((ctx->it.empty() && !it.empty()) || (ctx->it.seq() < it.seq()))
		{
			ctx->it = it;
			f(it, authoritative);
		}
		else if (authoritative)
			f(it, authoritative);
	}

	struct put_item_ctx
	{
		explicit put_item_ctx(int traversals)
			: active_traversals(traversals)
			, response_count(0)
		{}

		int active_traversals;
		int response_count;
	};

	void put_immutable_item_callback(int responses, std::shared_ptr<put_item_ctx> ctx
		, std::function<void(int)> f)
	{
		ctx->response_count += responses;
		if (--ctx->active_traversals == 0)
			f(ctx->response_count);
	}

	void put_mutable_item_callback(item const& it, int responses, std::shared_ptr<put_item_ctx> ctx
		, std::function<void(item const&, int)> cb)
	{
		ctx->response_count += responses;
		if (--ctx->active_traversals == 0)
			cb(it, ctx->response_count);
	}

	} // anonymous namespace

	void dht_tracker::get_item(sha1_hash const& target
		, std::function<void(item const&)> cb)
	{
		auto ctx = std::make_shared<get_immutable_item_ctx>((TORRENT_USE_IPV6) ? 2 : 1);
		m_dht.get_item(target, std::bind(&get_immutable_item_callback, _1, ctx, cb));
#if TORRENT_USE_IPV6
		m_dht6.get_item(target, std::bind(&get_immutable_item_callback, _1, ctx, cb));
#endif
	}

	// key is a 32-byte binary string, the public key to look up.
	// the salt is optional
	void dht_tracker::get_item(public_key const& key
		, std::function<void(item const&, bool)> cb
		, std::string salt)
	{
		auto ctx = std::make_shared<get_mutable_item_ctx>((TORRENT_USE_IPV6) ? 2 : 1);
		m_dht.get_item(key, salt, std::bind(&get_mutable_item_callback, _1, _2, ctx, cb));
#if TORRENT_USE_IPV6
		m_dht6.get_item(key, salt, std::bind(&get_mutable_item_callback, _1, _2, ctx, cb));
#endif
	}

	void dht_tracker::put_item(entry const& data
		, std::function<void(int)> cb)
	{
		std::string flat_data;
		bencode(std::back_inserter(flat_data), data);
		sha1_hash const target = item_target_id(flat_data);

		auto ctx = std::make_shared<put_item_ctx>((TORRENT_USE_IPV6) ? 2 : 1);
		m_dht.put_item(target, data, std::bind(&put_immutable_item_callback
			, _1, ctx, cb));
#if TORRENT_USE_IPV6
		m_dht6.put_item(target, data, std::bind(&put_immutable_item_callback
			, _1, ctx, cb));
#endif
	}

	void dht_tracker::put_item(public_key const& key
		, std::function<void(item const&, int)> cb
		, std::function<void(item&)> data_cb, std::string salt)
	{
		auto ctx = std::make_shared<put_item_ctx>((TORRENT_USE_IPV6) ? 2 : 1);
		m_dht.put_item(key, salt, std::bind(&put_mutable_item_callback
			, _1, _2, ctx, cb), data_cb);
#if TORRENT_USE_IPV6
		m_dht6.put_item(key, salt, std::bind(&put_mutable_item_callback
			, _1, _2, ctx, cb), data_cb);
#endif
	}

	void dht_tracker::direct_request(udp::endpoint const& ep, entry& e
		, std::function<void(msg const&)> f)
	{
#if TORRENT_USE_IPV6
		if (ep.protocol() == udp::v6())
			m_dht6.direct_request(ep, e, f);
		else
#endif
			m_dht.direct_request(ep, e, f);
	}

	void dht_tracker::incoming_error(error_code const& ec, udp::endpoint const& ep)
	{
		if (ec == boost::asio::error::connection_refused
			|| ec == boost::asio::error::connection_reset
			|| ec == boost::asio::error::connection_aborted
#ifdef _WIN32
			|| ec == error_code(ERROR_HOST_UNREACHABLE, system_category())
			|| ec == error_code(ERROR_PORT_UNREACHABLE, system_category())
			|| ec == error_code(ERROR_CONNECTION_REFUSED, system_category())
			|| ec == error_code(ERROR_CONNECTION_ABORTED, system_category())
#endif
			)
		{
			m_dht.unreachable(ep);
#if TORRENT_USE_IPV6
			m_dht6.unreachable(ep);
#endif
		}
	}

	bool dht_tracker::incoming_packet(udp::endpoint const& ep
		, span<char const> const buf)
	{
		int const buf_size = int(buf.size());
		if (buf_size <= 20
			|| buf.front() != 'd'
			|| buf.back() != 'e') return false;

		m_counters.inc_stats_counter(counters::dht_bytes_in, buf_size);
		// account for IP and UDP overhead
		m_counters.inc_stats_counter(counters::recv_ip_overhead_bytes
			, ep.address().is_v6() ? 48 : 28);
		m_counters.inc_stats_counter(counters::dht_messages_in);

		if (m_settings.ignore_dark_internet && ep.address().is_v4())
		{
			address_v4::bytes_type b = ep.address().to_v4().to_bytes();

			// these are class A networks not available to the public
			// if we receive messages from here, that seems suspicious
			static std::uint8_t const class_a[] = { 3, 6, 7, 9, 11, 19, 21, 22, 25
				, 26, 28, 29, 30, 33, 34, 48, 51, 56 };

			if (std::find(std::begin(class_a), std::end(class_a), b[0]) != std::end(class_a))
			{
				m_counters.inc_stats_counter(counters::dht_messages_in_dropped);
				return true;
			}
		}

		if (!m_blocker.incoming(ep.address(), clock_type::now(), m_log))
		{
			m_counters.inc_stats_counter(counters::dht_messages_in_dropped);
			return true;
		}

		TORRENT_ASSERT(buf_size > 0);

		int pos;
		error_code err;
		int ret = bdecode(buf.data(), buf.data() + buf_size, m_msg, err, &pos, 10, 500);
		if (ret != 0)
		{
			m_counters.inc_stats_counter(counters::dht_messages_in_dropped);
#ifndef TORRENT_DISABLE_LOGGING
			m_log->log_packet(dht_logger::incoming_message, buf, ep);
#endif
			return false;
		}

		if (m_msg.type() != bdecode_node::dict_t)
		{
			m_counters.inc_stats_counter(counters::dht_messages_in_dropped);
#ifndef TORRENT_DISABLE_LOGGING
			m_log->log_packet(dht_logger::incoming_message, buf, ep);
#endif
			// it's not a good idea to send a response to an invalid messages
			return false;
		}

#ifndef TORRENT_DISABLE_LOGGING
		m_log->log_packet(dht_logger::incoming_message, buf, ep);
#endif

		libtorrent::dht::msg m(m_msg, ep);
		m_dht.incoming(m);
#if TORRENT_USE_IPV6
		m_dht6.incoming(m);
#endif
		return true;
	}

	namespace {

	void add_node_fun(void* userdata, node_entry const& e)
	{
		auto v = static_cast<std::vector<udp::endpoint>*>(userdata);
		v->push_back(e.ep());
	}

	std::vector<udp::endpoint> save_nodes(node const& dht)
	{
		std::vector<udp::endpoint> ret;
		dht.m_table.for_each_node(&add_node_fun, &add_node_fun, &ret);
		bucket_t cache;
		dht.replacement_cache(cache);
		for (auto const& b : cache)
		{
			ret.push_back(b.ep());
		}
		return ret;
	}

	} // anonymous namespace

	dht_state dht_tracker::state() const
	{
		dht_state ret;
		ret.nid = m_dht.nid();
		ret.nodes = save_nodes(m_dht);
#if TORRENT_USE_IPV6
		ret.nid6 = m_dht6.nid();
		ret.nodes6 = save_nodes(m_dht6);
#endif
		return ret;
	}

	void dht_tracker::add_node(udp::endpoint const& node)
	{
		m_dht.add_node(node);
#if TORRENT_USE_IPV6
		m_dht6.add_node(node);
#endif
	}

	void dht_tracker::add_router_node(udp::endpoint const& node)
	{
		m_dht.add_router_node(node);
#if TORRENT_USE_IPV6
		m_dht6.add_router_node(node);
#endif
	}

	bool dht_tracker::has_quota()
	{
		time_point now = clock_type::now();
		time_duration delta = now - m_last_tick;
		m_last_tick = now;

		// add any new quota we've accrued since last time
		m_send_quota += int(std::uint64_t(m_settings.upload_rate_limit)
			* total_microseconds(delta) / 1000000);

		// allow 3 seconds worth of burst
		if (m_send_quota > 3 * m_settings.upload_rate_limit)
			m_send_quota = 3 * m_settings.upload_rate_limit;

		return m_send_quota > 0;
	}

	bool dht_tracker::send_packet(entry& e, udp::endpoint const& addr)
	{
		static char const version_str[] = {'L', 'T'
			, LIBTORRENT_VERSION_MAJOR, LIBTORRENT_VERSION_MINOR};
		e["v"] = std::string(version_str, version_str + 4);

		m_send_buf.clear();
		bencode(std::back_inserter(m_send_buf), e);

		// update the quota. We won't prevent the packet to be sent if we exceed
		// the quota, we'll just (potentially) block the next incoming request.

		m_send_quota -= int(m_send_buf.size());

		error_code ec;
		m_send_fun(addr, m_send_buf, ec, 0);
		if (ec)
		{
			m_counters.inc_stats_counter(counters::dht_messages_out_dropped);
#ifndef TORRENT_DISABLE_LOGGING
			m_log->log_packet(dht_logger::outgoing_message, m_send_buf, addr);
#endif
			return false;
		}

		m_counters.inc_stats_counter(counters::dht_bytes_out, m_send_buf.size());
		// account for IP and UDP overhead
		m_counters.inc_stats_counter(counters::sent_ip_overhead_bytes
			, addr.address().is_v6() ? 48 : 28);
		m_counters.inc_stats_counter(counters::dht_messages_out);
#ifndef TORRENT_DISABLE_LOGGING
		m_log->log_packet(dht_logger::outgoing_message, m_send_buf, addr);
#endif
		return true;
	}

}}
