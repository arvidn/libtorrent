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

#include <set>
#include <numeric>

#include "libtorrent/config.hpp"

#include "libtorrent/kademlia/node.hpp"
#include "libtorrent/kademlia/node_id.hpp"
#include "libtorrent/kademlia/traversal_algorithm.hpp"
#include "libtorrent/kademlia/dht_tracker.hpp"
#include "libtorrent/kademlia/msg.hpp"
#include "libtorrent/kademlia/dht_observer.hpp"

#include "libtorrent/socket.hpp"
#include "libtorrent/socket_io.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/version.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/performance_counters.hpp" // for counters

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/bind.hpp>
#include <boost/function/function0.hpp>
#include <boost/ref.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

using boost::ref;
using libtorrent::dht::node;
using libtorrent::dht::node_id;
using libtorrent::dht::packet_t;
using libtorrent::dht::msg;
using libtorrent::detail::write_endpoint;

namespace libtorrent { namespace dht
{
	void incoming_error(entry& e, char const* msg);

	namespace {

	// generate a new write token key every 5 minutes
	time_duration const key_refresh
		= duration_cast<time_duration>(minutes(5));

	node_id extract_node_id(entry const& e)
	{
		if (e.type() != entry::dictionary_t) return (node_id::min)();
		entry const* nid = e.find_key("node-id");
		if (nid == NULL || nid->type() != entry::string_t || nid->string().length() != 20)
			return (node_id::min)();
		return node_id(nid->string().c_str());
	}

	} // anonymous namespace

	// class that puts the networking and the kademlia node in a single
	// unit and connecting them together.
	dht_tracker::dht_tracker(dht_observer* observer
		, rate_limited_udp_socket& sock
		, dht_settings const& settings
		, counters& cnt
		, dht_storage_constructor_type storage_constructor
		, entry const& state)
		: m_counters(cnt)
		, m_dht(this, settings, extract_node_id(state), observer, cnt, storage_constructor)
		, m_sock(sock)
		, m_log(observer)
		, m_key_refresh_timer(sock.get_io_service())
		, m_connection_timer(sock.get_io_service())
		, m_refresh_timer(sock.get_io_service())
		, m_settings(settings)
		, m_abort(false)
		, m_host_resolver(sock.get_io_service())
	{
#ifndef TORRENT_DISABLE_LOGGING
		m_log->log(dht_logger::tracker, "starting DHT tracker with node id: %s"
			, to_hex(m_dht.nid().to_string()).c_str());
#endif
	}

	dht_tracker::~dht_tracker() {}

	void dht_tracker::update_node_id()
	{
		m_dht.update_node_id();
	}

	// defined in node.cpp
	void nop();

	void dht_tracker::start(entry const& bootstrap
		, find_data::nodes_callback const& f)
	{
		std::vector<udp::endpoint> initial_nodes;

		if (bootstrap.type() == entry::dictionary_t)
		{
			TORRENT_TRY {
				if (entry const* nodes = bootstrap.find_key("nodes"))
					read_endpoint_list<udp::endpoint>(nodes, initial_nodes);
			} TORRENT_CATCH(std::exception&) {}
		}

		error_code ec;
		refresh_key(ec);

		m_connection_timer.expires_from_now(seconds(1), ec);
		m_connection_timer.async_wait(
			boost::bind(&dht_tracker::connection_timeout, self(), _1));

		m_refresh_timer.expires_from_now(seconds(5), ec);
		m_refresh_timer.async_wait(boost::bind(&dht_tracker::refresh_timeout, self(), _1));
		m_dht.bootstrap(initial_nodes, f);
	}

	void dht_tracker::stop()
	{
		m_abort = true;
		error_code ec;
		m_key_refresh_timer.cancel(ec);
		m_connection_timer.cancel(ec);
		m_refresh_timer.cancel(ec);
		m_host_resolver.cancel();
	}

#ifndef TORRENT_NO_DEPRECATE
	void dht_tracker::dht_status(session_status& s)
	{
		m_dht.status(s);
	}
#endif

	void dht_tracker::dht_status(std::vector<dht_routing_bucket>& table
		, std::vector<dht_lookup>& requests)
	{
		m_dht.status(table, requests);
	}

	void dht_tracker::update_stats_counters(counters& c) const
	{
		m_dht.update_stats_counters(c);
	}

	void dht_tracker::connection_timeout(error_code const& e)
	{
		if (e || m_abort) return;

		time_duration d = m_dht.connection_timeout();
		error_code ec;
		m_connection_timer.expires_from_now(d, ec);
		m_connection_timer.async_wait(boost::bind(&dht_tracker::connection_timeout, self(), _1));
	}

	void dht_tracker::refresh_timeout(error_code const& e)
	{
		if (e || m_abort) return;

		m_dht.tick();

		// periodically update the DOS blocker's settings from the dht_settings
		m_blocker.set_block_timer(m_settings.block_timeout);
		m_blocker.set_rate_limit(m_settings.block_ratelimit);

		error_code ec;
		m_refresh_timer.expires_from_now(seconds(5), ec);
		m_refresh_timer.async_wait(
			boost::bind(&dht_tracker::refresh_timeout, self(), _1));
	}

	void dht_tracker::refresh_key(error_code const& e)
	{
		if (e || m_abort) return;

		error_code ec;
		m_key_refresh_timer.expires_from_now(key_refresh, ec);
		m_key_refresh_timer.async_wait(boost::bind(&dht_tracker::refresh_key, self(), _1));

		m_dht.new_write_key();
#ifndef TORRENT_DISABLE_LOGGING
		m_log->log(dht_logger::tracker, "*** new write key***");
#endif
	}

/*
#if defined TORRENT_DEBUG && TORRENT_USE_IOSTREAM
		std::ofstream st("dht_routing_table_state.txt", std::ios_base::trunc);
		m_dht.print_state(st);
#endif
*/

	void dht_tracker::get_peers(sha1_hash const& ih
		, boost::function<void(std::vector<tcp::endpoint> const&)> f)
	{
		m_dht.get_peers(ih, f, NULL, false);
	}

	void dht_tracker::announce(sha1_hash const& ih, int listen_port, int flags
		, boost::function<void(std::vector<tcp::endpoint> const&)> f)
	{
		m_dht.announce(ih, listen_port, flags, f);
	}

	void dht_tracker::get_item(sha1_hash const& target
		, boost::function<void(item const&)> cb)
	{
		m_dht.get_item(target, cb);
	}

	// key is a 32-byte binary string, the public key to look up.
	// the salt is optional
	void dht_tracker::get_item(char const* key
		, boost::function<void(item const&, bool)> cb
		, std::string salt)
	{
		m_dht.get_item(key, salt, cb);
	}

	void dht_tracker::put_item(entry const& data
		, boost::function<void(int)> cb)
	{
		std::string flat_data;
		bencode(std::back_inserter(flat_data), data);
		sha1_hash target = item_target_id(
			std::pair<char const*, int>(flat_data.c_str(), flat_data.size()));

		m_dht.put_item(target, data, cb);
	}

	void dht_tracker::put_item(char const* key
		, boost::function<void(item const&, int)> cb
		, boost::function<void(item&)> data_cb, std::string salt)
	{
		m_dht.put_item(key, salt, cb, data_cb);
	}

	void dht_tracker::direct_request(udp::endpoint ep, entry& e
		, boost::function<void(msg const&)> f)
	{
		m_dht.direct_request(ep, e, f);
	}

	// translate bittorrent kademlia message into the generice kademlia message
	// used by the library
	bool dht_tracker::incoming_packet(error_code const& ec
		, udp::endpoint const& ep, char const* buf, int size)
	{
		if (ec)
		{
			if (ec == boost::asio::error::connection_refused
				|| ec == boost::asio::error::connection_reset
				|| ec == boost::asio::error::connection_aborted
#ifdef WIN32
				|| ec == error_code(ERROR_HOST_UNREACHABLE, system_category())
				|| ec == error_code(ERROR_PORT_UNREACHABLE, system_category())
				|| ec == error_code(ERROR_CONNECTION_REFUSED, system_category())
				|| ec == error_code(ERROR_CONNECTION_ABORTED, system_category())
#endif
				)
			{
				m_dht.unreachable(ep);
			}
			return false;
		}

		if (size <= 20 || *buf != 'd' || buf[size-1] != 'e') return false;
		// remove this line/check once the DHT supports IPv6
		if (!ep.address().is_v4()) return false;

		m_counters.inc_stats_counter(counters::dht_bytes_in, size);
		// account for IP and UDP overhead
		m_counters.inc_stats_counter(counters::recv_ip_overhead_bytes
			, ep.address().is_v6() ? 48 : 28);
		m_counters.inc_stats_counter(counters::dht_messages_in);

		if (m_settings.ignore_dark_internet && ep.address().is_v4())
		{
			address_v4::bytes_type b = ep.address().to_v4().to_bytes();

			// these are class A networks not available to the public
			// if we receive messages from here, that seems suspicious
			boost::uint8_t class_a[] = { 3, 6, 7, 9, 11, 19, 21, 22, 25
				, 26, 28, 29, 30, 33, 34, 48, 51, 56 };

			int num = sizeof(class_a)/sizeof(class_a[0]);
			if (std::find(class_a, class_a + num, b[0]) != class_a + num)
				return true;
		}

		if (!m_blocker.incoming(ep.address(), clock_type::now(), m_log))
			return true;

		using libtorrent::entry;
		using libtorrent::bdecode;

		TORRENT_ASSERT(size > 0);

		int pos;
		error_code err;
		int ret = bdecode(buf, buf + size, m_msg, err, &pos, 10, 500);
		if (ret != 0)
		{
#ifndef TORRENT_DISABLE_LOGGING
			m_log->log_packet(dht_logger::incoming_message, buf, size, ep);
#endif
			return false;
		}

		if (m_msg.type() != bdecode_node::dict_t)
		{
#ifndef TORRENT_DISABLE_LOGGING
			m_log->log_packet(dht_logger::incoming_message, buf, size, ep);
#endif
			// it's not a good idea to send a response to an invalid messages
			return false;
		}

#ifndef TORRENT_DISABLE_LOGGING
		m_log->log_packet(dht_logger::incoming_message, buf
			, size, ep);
#endif

		libtorrent::dht::msg m(m_msg, ep);
		m_dht.incoming(m);
		return true;
	}

	namespace {

	void add_node_fun(void* userdata, node_entry const& e)
	{
		entry* n = static_cast<entry*>(userdata);
		std::string node;
		std::back_insert_iterator<std::string> out(node);
		write_endpoint(e.ep(), out);
		n->list().push_back(entry(node));
	}

	} // anonymous namespace

	entry dht_tracker::state() const
	{
		entry ret(entry::dictionary_t);
		{
			entry nodes(entry::list_t);
			m_dht.m_table.for_each_node(&add_node_fun, &add_node_fun, &nodes);
			bucket_t cache;
			m_dht.replacement_cache(cache);
			for (bucket_t::iterator i(cache.begin())
				, end(cache.end()); i != end; ++i)
			{
				std::string node;
				std::back_insert_iterator<std::string> out(node);
				write_endpoint(i->ep(), out);
				nodes.list().push_back(entry(node));
			}
			if (!nodes.list().empty())
				ret["nodes"] = nodes;
		}

		ret["node-id"] = m_dht.nid().to_string();
		return ret;
	}

	void dht_tracker::add_node(udp::endpoint node)
	{
		m_dht.add_node(node);
	}

	void dht_tracker::add_router_node(udp::endpoint const& node)
	{
		m_dht.add_router_node(node);
	}

	bool dht_tracker::has_quota()
	{
		return m_sock.has_quota();
	}

	bool dht_tracker::send_packet(libtorrent::entry& e, udp::endpoint const& addr, int send_flags)
	{
		using libtorrent::bencode;
		using libtorrent::entry;

		static char const version_str[] = {'L', 'T'
			, LIBTORRENT_VERSION_MAJOR, LIBTORRENT_VERSION_MINOR};
		e["v"] = std::string(version_str, version_str + 4);

		m_send_buf.clear();
		bencode(std::back_inserter(m_send_buf), e);
		error_code ec;

		bool ret = m_sock.send(addr, &m_send_buf[0], int(m_send_buf.size()), ec, send_flags);
		if (!ret || ec)
		{
			m_counters.inc_stats_counter(counters::dht_messages_out_dropped);
#ifndef TORRENT_DISABLE_LOGGING
			m_log->log_packet(dht_logger::outgoing_message, &m_send_buf[0]
				, m_send_buf.size(), addr);
#endif
			return false;
		}

		m_counters.inc_stats_counter(counters::dht_bytes_out, m_send_buf.size());
		// account for IP and UDP overhead
		m_counters.inc_stats_counter(counters::sent_ip_overhead_bytes
			, addr.address().is_v6() ? 48 : 28);
		m_counters.inc_stats_counter(counters::dht_messages_out);
#ifndef TORRENT_DISABLE_LOGGING
		m_log->log_packet(dht_logger::outgoing_message, &m_send_buf[0]
			, m_send_buf.size(), addr);
#endif
		return true;
	}

}}

