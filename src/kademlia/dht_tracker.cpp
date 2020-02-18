/*

Copyright (c) 2006-2018, Arvid Norberg
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
#include <libtorrent/kademlia/dht_settings.hpp>

#include <libtorrent/bencode.hpp>
#include <libtorrent/version.hpp>
#include <libtorrent/time.hpp>
#include <libtorrent/performance_counters.hpp> // for counters
#include <libtorrent/aux_/time.hpp>
#include <libtorrent/session_status.hpp>
#include <libtorrent/broadcast_socket.hpp> // for is_local

#ifndef TORRENT_DISABLE_LOGGING
#include <libtorrent/hex.hpp> // to_hex
#endif

using namespace std::placeholders;

namespace libtorrent { namespace dht {

	namespace {

	// generate a new write token key every 5 minutes
	auto const key_refresh
		= duration_cast<time_duration>(minutes(5));

	void add_dht_counters(node const& dht, counters& c)
	{
		int nodes, replacements, allocated_observers;
		std::tie(nodes, replacements, allocated_observers) = dht.get_stats_counters();

		c.inc_stats_counter(counters::dht_nodes, nodes);
		c.inc_stats_counter(counters::dht_node_cache, replacements);
		c.inc_stats_counter(counters::dht_allocated_observers, allocated_observers);
	}

	std::vector<udp::endpoint> concat(std::vector<udp::endpoint> const& v1
		, std::vector<udp::endpoint> const& v2)
	{
		std::vector<udp::endpoint> r = v1;
		r.insert(r.end(), v2.begin(), v2.end());
		return r;
	}

	} // anonymous namespace

	// class that puts the networking and the kademlia node in a single
	// unit and connecting them together.
	dht_tracker::dht_tracker(dht_observer* observer
		, io_service& ios
		, send_fun_t const& send_fun
		, dht::settings const& settings
		, counters& cnt
		, dht_storage_interface& storage
		, dht_state&& state)
		: m_counters(cnt)
		, m_storage(storage)
		, m_state(std::move(state))
		, m_send_fun(send_fun)
		, m_log(observer)
		, m_key_refresh_timer(ios)
		, m_refresh_timer(ios)
		, m_settings(settings)
		, m_running(false)
		, m_host_resolver(ios)
		, m_send_quota(settings.upload_rate_limit)
		, m_last_tick(aux::time_now())
	{
		m_blocker.set_block_timer(m_settings.block_timeout);
		m_blocker.set_rate_limit(m_settings.block_ratelimit);
	}

	void dht_tracker::update_node_id(aux::listen_socket_handle const& s)
	{
		auto n = m_nodes.find(s);
		if (n != m_nodes.end())
			n->second.dht.update_node_id();
		update_storage_node_ids();
	}

	void dht_tracker::new_socket(aux::listen_socket_handle const& s)
	{
		address const local_address = s.get_local_endpoint().address();
		auto stored_nid = std::find_if(m_state.nids.begin(), m_state.nids.end()
			, [&](node_ids_t::value_type const& nid) { return nid.first == local_address; });
		node_id const nid = stored_nid != m_state.nids.end() ? stored_nid->second : node_id();
		// must use piecewise construction because tracker_node::connection_timer
		// is neither copyable nor movable
		auto n = m_nodes.emplace(std::piecewise_construct_t(), std::forward_as_tuple(s)
			, std::forward_as_tuple(get_io_service(m_key_refresh_timer)
			, s, this, m_settings, nid, m_log, m_counters
			, std::bind(&dht_tracker::get_node, this, _1, _2)
			, m_storage));

#ifndef TORRENT_DISABLE_LOGGING
		if (m_log->should_log(dht_logger::tracker))
		{
			m_log->log(dht_logger::tracker, "starting %s DHT tracker with node id: %s"
				, local_address.is_v4() ? "IPv4" : "IPv6"
				, aux::to_hex(n.first->second.dht.nid()).c_str());
		}
#endif

		if (m_running && n.second)
		{
			ADD_OUTSTANDING_ASYNC("dht_tracker::connection_timeout");
			error_code ec;
			n.first->second.connection_timer.expires_from_now(seconds(1), ec);
			n.first->second.connection_timer.async_wait(
				std::bind(&dht_tracker::connection_timeout, self(), n.first->first, _1));
			n.first->second.dht.bootstrap({}, find_data::nodes_callback());
		}
	}

	void dht_tracker::delete_socket(aux::listen_socket_handle const& s)
	{
		m_nodes.erase(s);
	}

	void dht_tracker::start(find_data::nodes_callback const& f)
	{
		m_running = true;
		error_code ec;

		ADD_OUTSTANDING_ASYNC("dht_tracker::refresh_key");
		refresh_key(ec);

		for (auto& n : m_nodes)
		{
			ADD_OUTSTANDING_ASYNC("dht_tracker::connection_timeout");
			n.second.connection_timer.expires_from_now(seconds(1), ec);
			n.second.connection_timer.async_wait(
				std::bind(&dht_tracker::connection_timeout, self(), n.first, _1));
			if (is_v6(n.first.get_local_endpoint()))
				n.second.dht.bootstrap(concat(m_state.nodes6, m_state.nodes), f);
			else
				n.second.dht.bootstrap(concat(m_state.nodes, m_state.nodes6), f);
		}

		ADD_OUTSTANDING_ASYNC("dht_tracker::refresh_timeout");
		m_refresh_timer.expires_from_now(seconds(5), ec);
		m_refresh_timer.async_wait(std::bind(&dht_tracker::refresh_timeout, self(), _1));

		m_state.clear();
	}

	void dht_tracker::stop()
	{
		m_running = false;
		error_code ec;
		m_key_refresh_timer.cancel(ec);
		for (auto& n : m_nodes)
			n.second.connection_timer.cancel(ec);
		m_refresh_timer.cancel(ec);
		m_host_resolver.cancel();
	}

#if TORRENT_ABI_VERSION == 1
	void dht_tracker::dht_status(session_status& s)
	{
		s.dht_torrents += int(m_storage.num_torrents());

		s.dht_nodes = 0;
		s.dht_node_cache = 0;
		s.dht_global_nodes = 0;
		s.dht_torrents = 0;
		s.active_requests.clear();
		s.dht_total_allocations = 0;

		for (auto& n : m_nodes)
			n.second.dht.status(s);
	}
#endif

	void dht_tracker::dht_status(std::vector<dht_routing_bucket>& table
		, std::vector<dht_lookup>& requests)
	{
		for (auto& n : m_nodes)
			n.second.dht.status(table, requests);
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

		for (auto& n : m_nodes)
			add_dht_counters(n.second.dht, c);
	}

	void dht_tracker::connection_timeout(aux::listen_socket_handle const& s, error_code const& e)
	{
		COMPLETE_ASYNC("dht_tracker::connection_timeout");
		if (e || !m_running) return;

		auto const it = m_nodes.find(s);
		// this could happen if the task is about to be executed (and not cancellable) and
		// the socket is just removed
		if (it == m_nodes.end()) return; // node already destroyed

		tracker_node& n = it->second;
		time_duration const d = n.dht.connection_timeout();
		error_code ec;
		deadline_timer& timer = n.connection_timer;
		timer.expires_from_now(d, ec);
		ADD_OUTSTANDING_ASYNC("dht_tracker::connection_timeout");
		timer.async_wait(std::bind(&dht_tracker::connection_timeout, self(), s, _1));
	}

	void dht_tracker::refresh_timeout(error_code const& e)
	{
		COMPLETE_ASYNC("dht_tracker::refresh_timeout");
		if (e || !m_running) return;

		for (auto& n : m_nodes)
			n.second.dht.tick();

		// periodically update the DOS blocker's settings from the dht_settings
		m_blocker.set_block_timer(m_settings.block_timeout);
		m_blocker.set_rate_limit(m_settings.block_ratelimit);

		error_code ec;
		m_refresh_timer.expires_from_now(seconds(5), ec);
		ADD_OUTSTANDING_ASYNC("dht_tracker::refresh_timeout");
		m_refresh_timer.async_wait(
			std::bind(&dht_tracker::refresh_timeout, self(), _1));
	}

	void dht_tracker::refresh_key(error_code const& e)
	{
		COMPLETE_ASYNC("dht_tracker::refresh_key");
		if (e || !m_running) return;

		ADD_OUTSTANDING_ASYNC("dht_tracker::refresh_key");
		error_code ec;
		m_key_refresh_timer.expires_from_now(key_refresh, ec);
		m_key_refresh_timer.async_wait(std::bind(&dht_tracker::refresh_key, self(), _1));

		for (auto& n : m_nodes)
			n.second.dht.new_write_key();

#ifndef TORRENT_DISABLE_LOGGING
		m_log->log(dht_logger::tracker, "*** new write key***");
#endif
	}

	void dht_tracker::update_storage_node_ids()
	{
		std::vector<sha1_hash> ids;
		for (auto& n : m_nodes)
			ids.push_back(n.second.dht.nid());
		m_storage.update_node_ids(ids);
	}

	node* dht_tracker::get_node(node_id const& id, std::string const& family_name)
	{
		TORRENT_UNUSED(id);
		for (auto& n : m_nodes)
		{
			// TODO: pick the closest node rather than the first
			if (n.second.dht.protocol_family_name() == family_name)
				return &n.second.dht;
		}

		return nullptr;
	}

	void dht_tracker::get_peers(sha1_hash const& ih
		, std::function<void(std::vector<tcp::endpoint> const&)> f)
	{
		for (auto& n : m_nodes)
			n.second.dht.get_peers(ih, f, {}, {});
	}

	void dht_tracker::announce(sha1_hash const& ih, int listen_port
		, announce_flags_t const flags
		, std::function<void(std::vector<tcp::endpoint> const&)> f)
	{
		for (auto& n : m_nodes)
			n.second.dht.announce(ih, listen_port, flags, f);
	}

	void dht_tracker::sample_infohashes(udp::endpoint const& ep, sha1_hash const& target
		, std::function<void(time_duration
			, int, std::vector<sha1_hash>
			, std::vector<std::pair<sha1_hash, udp::endpoint>>)> f)
	{
		for (auto& n : m_nodes)
		{
			if (ep.protocol() != (n.first.get_external_address().is_v4() ? udp::v4() : udp::v6()))
				continue;
			n.second.dht.sample_infohashes(ep, target, f);
			break;
		}
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
	void get_immutable_item_callback(item const& it
		, std::shared_ptr<get_immutable_item_ctx> ctx
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
		auto ctx = std::make_shared<get_immutable_item_ctx>(int(m_nodes.size()));
		for (auto& n : m_nodes)
			n.second.dht.get_item(target, std::bind(&get_immutable_item_callback, _1, ctx, cb));
	}

	// key is a 32-byte binary string, the public key to look up.
	// the salt is optional
	void dht_tracker::get_item(public_key const& key
		, std::function<void(item const&, bool)> cb
		, std::string salt)
	{
		auto ctx = std::make_shared<get_mutable_item_ctx>(int(m_nodes.size()));
		for (auto& n : m_nodes)
			n.second.dht.get_item(key, salt, std::bind(&get_mutable_item_callback, _1, _2, ctx, cb));
	}

	void dht_tracker::put_item(entry const& data
		, std::function<void(int)> cb)
	{
		std::string flat_data;
		bencode(std::back_inserter(flat_data), data);
		sha1_hash const target = item_target_id(flat_data);

		auto ctx = std::make_shared<put_item_ctx>(int(m_nodes.size()));
		for (auto& n : m_nodes)
			n.second.dht.put_item(target, data, std::bind(&put_immutable_item_callback
			, _1, ctx, cb));
	}

	void dht_tracker::put_item(public_key const& key
		, std::function<void(item const&, int)> cb
		, std::function<void(item&)> data_cb, std::string salt)
	{
		auto ctx = std::make_shared<put_item_ctx>(int(m_nodes.size()));
		for (auto& n : m_nodes)
			n.second.dht.put_item(key, salt, std::bind(&put_mutable_item_callback
				, _1, _2, ctx, cb), data_cb);
	}

	void dht_tracker::direct_request(udp::endpoint const& ep, entry& e
		, std::function<void(msg const&)> f)
	{
		for (auto& n : m_nodes)
		{
			if (ep.protocol() != (n.first.get_external_address().is_v4() ? udp::v4() : udp::v6()))
				continue;
			n.second.dht.direct_request(ep, e, f);
			break;
		}
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
			for (auto& n : m_nodes)
				n.second.dht.unreachable(ep);
		}
	}

	bool dht_tracker::incoming_packet(aux::listen_socket_handle const& s
		, udp::endpoint const& ep, span<char const> const buf)
	{
		int const buf_size = int(buf.size());
		if (buf_size <= 20
			|| buf.front() != 'd'
			|| buf.back() != 'e') return false;

		m_counters.inc_stats_counter(counters::dht_bytes_in, buf_size);
		// account for IP and UDP overhead
		m_counters.inc_stats_counter(counters::recv_ip_overhead_bytes
			, is_v6(ep) ? 48 : 28);
		m_counters.inc_stats_counter(counters::dht_messages_in);

		if (m_settings.ignore_dark_internet && is_v4(ep))
		{
			address_v4::bytes_type b = ep.address().to_v4().to_bytes();

			// these are class A networks not available to the public
			// if we receive messages from here, that seems suspicious
			static std::uint8_t const class_a[] = { 3, 6, 7, 9, 11, 19, 21, 22, 25
				, 26, 28, 29, 30, 33, 34, 48, 56 };

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
		int const ret = bdecode(buf.data(), buf.data() + buf_size, m_msg, err, &pos, 10, 500);
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

		libtorrent::dht::msg const m(m_msg, ep);
		for (auto& n : m_nodes)
			n.second.dht.incoming(s, m);
		return true;
	}

	dht_tracker::tracker_node::tracker_node(io_service& ios
		, aux::listen_socket_handle const& s, socket_manager* sock
		, dht::settings const& settings
		, node_id const& nid
		, dht_observer* observer, counters& cnt
		, get_foreign_node_t get_foreign_node
		, dht_storage_interface& storage)
		: dht(s, sock, settings, nid, observer, cnt, std::move(get_foreign_node), storage)
		, connection_timer(ios)
	{}

	std::vector<std::pair<node_id, udp::endpoint>> dht_tracker::live_nodes(node_id const& nid)
	{
		std::vector<std::pair<node_id, udp::endpoint>> ret;

		auto n = std::find_if(m_nodes.begin(), m_nodes.end()
			, [&](tracker_nodes_t::value_type const& v) { return v.second.dht.nid() == nid; });

		if (n != m_nodes.end())
		{
			n->second.dht.m_table.for_each_node([&ret](node_entry const& e)
				{ ret.emplace_back(e.id, e.endpoint); }, nullptr);
		}

		return ret;
	}

namespace {

	std::vector<udp::endpoint> save_nodes(node const& dht)
	{
		std::vector<udp::endpoint> ret;

		dht.m_table.for_each_node([&ret](node_entry const& e)
		{ ret.push_back(e.ep()); });

		return ret;
	}

} // anonymous namespace

	dht_state dht_tracker::state() const
	{
		dht_state ret;
		for (auto& n : m_nodes)
		{
			// use the local rather than external address because if the user is behind NAT
			// we won't know the external IP on startup
			ret.nids.emplace_back(n.first.get_local_endpoint().address(), n.second.dht.nid());
			auto nodes = save_nodes(n.second.dht);
			ret.nodes.insert(ret.nodes.end(), nodes.begin(), nodes.end());
		}
		return ret;
	}

	void dht_tracker::add_node(udp::endpoint const& node)
	{
		for (auto& n : m_nodes)
			n.second.dht.add_node(node);
	}

	void dht_tracker::add_router_node(udp::endpoint const& node)
	{
		for (auto& n : m_nodes)
			n.second.dht.add_router_node(node);
	}

	bool dht_tracker::has_quota()
	{
		time_point const now = clock_type::now();
		time_duration const delta = now - m_last_tick;
		m_last_tick = now;

		// add any new quota we've accrued since last time
		m_send_quota += int(std::int64_t(m_settings.upload_rate_limit)
			* total_microseconds(delta) / 1000000);

		// allow 3 seconds worth of burst
		if (m_send_quota > 3 * m_settings.upload_rate_limit)
			m_send_quota = 3 * m_settings.upload_rate_limit;

		return m_send_quota > 0;
	}

	bool dht_tracker::send_packet(aux::listen_socket_handle const& s, entry& e, udp::endpoint const& addr)
	{
		TORRENT_ASSERT(m_nodes.find(s) != m_nodes.end());

		static_assert(LIBTORRENT_VERSION_MINOR < 16, "version number not supported by DHT");
		static_assert(LIBTORRENT_VERSION_TINY < 16, "version number not supported by DHT");
		static char const version_str[] = {'L', 'T'
			, LIBTORRENT_VERSION_MAJOR, (LIBTORRENT_VERSION_MINOR << 4) | LIBTORRENT_VERSION_TINY};
		e["v"] = std::string(version_str, version_str + 4);

		m_send_buf.clear();
		bencode(std::back_inserter(m_send_buf), e);

		// update the quota. We won't prevent the packet to be sent if we exceed
		// the quota, we'll just (potentially) block the next incoming request.

		m_send_quota -= int(m_send_buf.size());

		error_code ec;
		if (s.get_local_endpoint().protocol().family() != addr.protocol().family())
		{
			// the node is trying to send a packet to a different address family
			// than its socket, this can happen during bootstrap
			// pick a node with the right address family and use its socket
			auto n = std::find_if(m_nodes.begin(), m_nodes.end()
				, [&](tracker_nodes_t::value_type const& v)
					{ return v.first.get_local_endpoint().protocol().family() == addr.protocol().family(); });

			if (n != m_nodes.end())
				m_send_fun(n->first, addr, m_send_buf, ec, {});
			else
				ec = boost::asio::error::address_family_not_supported;
		}
		else
		{
			m_send_fun(s, addr, m_send_buf, ec, {});
		}

		if (ec)
		{
			m_counters.inc_stats_counter(counters::dht_messages_out_dropped);
#ifndef TORRENT_DISABLE_LOGGING
			m_log->log_packet(dht_logger::outgoing_message, m_send_buf, addr);
#endif
			return false;
		}

		m_counters.inc_stats_counter(counters::dht_bytes_out, int(m_send_buf.size()));
		// account for IP and UDP overhead
		m_counters.inc_stats_counter(counters::sent_ip_overhead_bytes
			, is_v6(addr) ? 48 : 28);
		m_counters.inc_stats_counter(counters::dht_messages_out);
#ifndef TORRENT_DISABLE_LOGGING
		m_log->log_packet(dht_logger::outgoing_message, m_send_buf, addr);
#endif
		return true;
	}

}}
