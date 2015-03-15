/*

Copyright (c) 2006-2014, Arvid Norberg
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
#include <boost/bind.hpp>
#include <boost/ref.hpp>

#include "libtorrent/kademlia/node.hpp"
#include "libtorrent/kademlia/node_id.hpp"
#include "libtorrent/kademlia/traversal_algorithm.hpp"
#include "libtorrent/kademlia/dht_tracker.hpp"
#include "libtorrent/kademlia/msg.hpp"

#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/socket_io.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/version.hpp"
#include "libtorrent/performance_counters.hpp" // for counters

using boost::ref;
using libtorrent::dht::node_impl;
using libtorrent::dht::node_id;
using libtorrent::dht::packet_t;
using libtorrent::dht::msg;
using namespace libtorrent::detail;

enum
{
	key_refresh = 5 // generate a new write token key every 5 minutes
};

namespace
{
	const int tick_period = 1; // minutes
}

namespace libtorrent { namespace dht
{
	void incoming_error(entry& e, char const* msg);

#ifdef TORRENT_DHT_VERBOSE_LOGGING
	std::string parse_dht_client(bdecode_node const& e)
	{
		bdecode_node ver = e.dict_find_string("v");
		if (!ver) return "generic";
		std::string const& client = ver.string_value();
		if (client.size() < 2)
		{
			return client;
		}
		else if (std::equal(client.begin(), client.begin() + 2, "Az"))
		{
			return "Azureus";
		}
		else if (std::equal(client.begin(), client.begin() + 2, "UT"))
		{
			return "uTorrent";
		}
		else if (std::equal(client.begin(), client.begin() + 2, "LT"))
		{
			return "libtorrent";
		}
		else if (std::equal(client.begin(), client.begin() + 2, "MP"))
		{
			return "MooPolice";
		}
		else if (std::equal(client.begin(), client.begin() + 2, "GR"))
		{
			return "GetRight";
		}
		else if (std::equal(client.begin(), client.begin() + 2, "MO"))
		{
			return "Mono Torrent";
		}
		else
		{
			return client;
		}
	}
#endif

#ifdef TORRENT_DHT_VERBOSE_LOGGING
	TORRENT_DEFINE_LOG(dht_tracker)
#endif

	node_id extract_node_id(bdecode_node e)
	{
		if (!e || e.type() != bdecode_node::dict_t) return (node_id::min)();
		bdecode_node nid = e.dict_find_string("node-id");
		if (!nid || nid.string_length() != 20) return (node_id::min)();
		return node_id(node_id(nid.string_ptr()));
	}

	node_id extract_node_id(entry const* e)
	{
		if (e == 0 || e->type() != entry::dictionary_t) return (node_id::min)();
		entry const* nid = e->find_key("node-id");
		if (nid == 0 || nid->type() != entry::string_t || nid->string().length() != 20)
			return (node_id::min)();
		return node_id(node_id(nid->string().c_str()));
	}

	// class that puts the networking and the kademlia node in a single
	// unit and connecting them together.
	dht_tracker::dht_tracker(libtorrent::aux::session_impl& ses
		, rate_limited_udp_socket& sock
		, dht_settings const& settings, counters& cnt, entry const* state)
		: m_counters(cnt)
		, m_dht(&ses, this, settings, extract_node_id(state)
			, ses.external_address().external_address(address_v4()), &ses, cnt)
		, m_sock(sock)
		, m_last_new_key(aux::time_now() - minutes(int(key_refresh)))
		, m_timer(sock.get_io_service())
		, m_connection_timer(sock.get_io_service())
		, m_refresh_timer(sock.get_io_service())
		, m_settings(settings)
		, m_refresh_bucket(160)
		, m_abort(false)
		, m_host_resolver(sock.get_io_service())
	{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		// turns on and off individual components' logging

//		rpc_log().enable(false);
//		node_log().enable(false);
//		traversal_log().enable(false);
//		dht_tracker_log.enable(false);

		TORRENT_LOG(dht_tracker) << "starting DHT tracker with node id: " << m_dht.nid();
#endif
	}

	dht_tracker::~dht_tracker() {}

	// defined in node.cpp
	extern void nop();

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
		m_timer.expires_from_now(seconds(1), ec);
		m_timer.async_wait(boost::bind(&dht_tracker::tick, self(), _1));

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
		m_timer.cancel(ec);
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

	void dht_tracker::tick(error_code const& e)
	{
		if (e || m_abort) return;

		error_code ec;
		m_timer.expires_from_now(minutes(tick_period), ec);
		m_timer.async_wait(boost::bind(&dht_tracker::tick, self(), _1));

		time_point now = aux::time_now();
		if (now - minutes(int(key_refresh)) > m_last_new_key)
		{
			m_last_new_key = now;
			m_dht.new_write_key();
#ifdef TORRENT_DHT_VERBOSE_LOGGING
			TORRENT_LOG(dht_tracker) << " *** new write key";
#endif
		}
	}

	void dht_tracker::announce(sha1_hash const& ih, int listen_port, int flags
		, boost::function<void(std::vector<tcp::endpoint> const&)> f)
	{
		m_dht.announce(ih, listen_port, flags, f);
	}

	// these functions provide a slightly higher level
	// interface to the get/put functionality in the DHT
	bool get_immutable_item_callback(item& it, boost::function<void(item const&)> f)
	{
		// the reason to wrap here is to control the return value
		// since it controls whether we re-put the content
		TORRENT_ASSERT(!it.is_mutable());
		f(it);
		return false;
	}

	bool get_mutable_item_callback(item& it, boost::function<void(item const&)> f)
	{
		// the reason to wrap here is to control the return value
		// since it controls whether we re-put the content
		TORRENT_ASSERT(it.is_mutable());
		f(it);
		return false;
	}

	bool put_immutable_item_callback(item& it, boost::function<void()> f
		, entry data)
	{
		TORRENT_ASSERT(!it.is_mutable());
		it.assign(data);
		// TODO: ideally this function would be called when the
		// put completes
		f();
		return true;
	}

	bool put_mutable_item_callback(item& it, boost::function<void(item&)> cb)
	{
		cb(it);
		return true;
	}

	void dht_tracker::get_item(sha1_hash const& target
		, boost::function<void(item const&)> cb)
	{
		m_dht.get_item(target, boost::bind(&get_immutable_item_callback, _1, cb));
	}

	// key is a 32-byte binary string, the public key to look up.
	// the salt is optional
	void dht_tracker::get_item(char const* key
		, boost::function<void(item const&)> cb
		, std::string salt)
	{
		m_dht.get_item(key, salt, boost::bind(&get_mutable_item_callback, _1, cb));
	}

	void dht_tracker::put_item(entry data
		, boost::function<void()> cb)
	{
		std::string flat_data;
		bencode(std::back_inserter(flat_data), data);
		sha1_hash target = item_target_id(
			std::pair<char const*, int>(flat_data.c_str(), flat_data.size()));

		m_dht.get_item(target, boost::bind(&put_immutable_item_callback
			, _1, cb, data));
	}

	void dht_tracker::put_item(char const* key
		, boost::function<void(item&)> cb, std::string salt)
	{
		m_dht.get_item(key, salt, boost::bind(&put_mutable_item_callback
			, _1, cb));
	}

	// translate bittorrent kademlia message into the generice kademlia message
	// used by the library
	bool dht_tracker::incoming_packet(error_code const& ec
		, udp::endpoint const& ep, char const* buf, int size)
	{
		if (ec)
		{
			if (ec == asio::error::connection_refused
				|| ec == asio::error::connection_reset
				|| ec == asio::error::connection_aborted
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
				, 26, 28, 29, 30, 33, 34, 45, 48, 51, 52, 56, 102, 104 };

			int num = sizeof(class_a)/sizeof(class_a[0]);
			if (std::find(class_a, class_a + num, b[0]) != class_a + num)
				return true;
		}

		if (!m_blocker.incoming(ep.address(), aux::time_now()))
			return true;

		using libtorrent::entry;
		using libtorrent::bdecode;
			
		TORRENT_ASSERT(size > 0);

		int pos;
		error_code err;
		int ret = bdecode(buf, buf + size, m_msg, err, &pos, 10, 500);
		if (ret != 0)
		{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
			TORRENT_LOG(dht_tracker) << "<== " << ep << " ERROR: "
				<< err.message() << " pos: " << pos;
#endif
			return false;
		}

		if (m_msg.type() != bdecode_node::dict_t)
		{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
			TORRENT_LOG(dht_tracker) << "<== " << ep << " ERROR: not a dictionary: "
				<< print_entry(m_msg, true);
#endif
			// it's not a good idea to send invalid messages
			// especially not in response to an invalid message
//			entry r;
//			libtorrent::dht::incoming_error(r, "message is not a dictionary");
//			send_packet(r, ep, 0);
			return false;
		}

		libtorrent::dht::msg m(m_msg, ep);
		m_dht.incoming(m);
		return true;
	}

	void add_node_fun(void* userdata, node_entry const& e)
	{
		entry* n = (entry*)userdata;
		std::string node;
		std::back_insert_iterator<std::string> out(node);
		write_endpoint(e.ep(), out);
		n->list().push_back(entry(node));
	}
	
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

	void dht_tracker::add_node(std::pair<std::string, int> const& node)
	{
		char port[7];
		snprintf(port, sizeof(port), "%d", node.second);
		udp::resolver::query q(node.first, port);
		m_host_resolver.async_resolve(q,
			boost::bind(&dht_tracker::on_name_lookup, self(), _1, _2));
	}

	void dht_tracker::on_name_lookup(error_code const& e
		, udp::resolver::iterator host)
	{
		if (e || host == udp::resolver::iterator()) return;
		add_node(host->endpoint());
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

#ifdef TORRENT_DHT_VERBOSE_LOGGING
		std::stringstream log_line;
		bdecode_node print;
		int ret = bdecode(&m_send_buf[0], &m_send_buf[0] + m_send_buf.size(), print, ec);
		TORRENT_ASSERT(ret == 0);
		log_line << print_entry(print, true);
#endif

		if (m_sock.send(addr, &m_send_buf[0], (int)m_send_buf.size(), ec, send_flags))
		{
			if (ec)
			{
				m_counters.inc_stats_counter(counters::dht_messages_out_dropped);
#ifdef TORRENT_DHT_VERBOSE_LOGGING
				TORRENT_LOG(dht_tracker) << "==> " << addr << " DROPPED (" << ec.message() << ") " << log_line.str();
#endif
				return false;
			}

			m_counters.inc_stats_counter(counters::dht_bytes_out, m_send_buf.size());
			// account for IP and UDP overhead
			m_counters.inc_stats_counter(counters::sent_ip_overhead_bytes
				, addr.address().is_v6() ? 48 : 28);
			m_counters.inc_stats_counter(counters::dht_messages_out);
#ifdef TORRENT_DHT_VERBOSE_LOGGING
			TORRENT_LOG(dht_tracker) << "==> " << addr << " " << log_line.str();
#endif
			return true;
		}
		else
		{
			m_counters.inc_stats_counter(counters::dht_messages_out_dropped);

#ifdef TORRENT_DHT_VERBOSE_LOGGING
			TORRENT_LOG(dht_tracker) << "==> " << addr << " DROPPED " << log_line.str();
#endif
			return false;
		}
	}

}}

