/*

Copyright (c) 2006, Arvid Norberg
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

#include "libtorrent/pch.hpp"

#include <fstream>
#include <set>
#include <numeric>
#include <stdexcept>
#include <boost/bind.hpp>
#include <boost/ref.hpp>
#include <boost/optional.hpp>
#include <boost/filesystem/operations.hpp>

#include "libtorrent/kademlia/node.hpp"
#include "libtorrent/kademlia/node_id.hpp"
#include "libtorrent/kademlia/traversal_algorithm.hpp"
#include "libtorrent/kademlia/dht_tracker.hpp"
#include "libtorrent/kademlia/msg.hpp"

#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/version.hpp"
#include "libtorrent/escape_string.hpp"

using boost::ref;
using libtorrent::dht::node_impl;
using libtorrent::dht::node_id;
using libtorrent::dht::packet_t;
using libtorrent::dht::msg;
namespace messages = libtorrent::dht::messages;
using namespace libtorrent::detail;

enum
{
	key_refresh = 5 // generate a new write token key every 5 minutes
};

namespace
{
	const int tick_period = 1; // minutes

	struct count_peers
	{
		int& count;
		count_peers(int& c): count(c) {}
		void operator()(std::pair<libtorrent::dht::node_id
			, libtorrent::dht::torrent_entry> const& t)
		{
			count += t.second.peers.size();
		}
	};
	
	template <class EndpointType>
	void read_endpoint_list(libtorrent::lazy_entry const* n, std::vector<EndpointType>& epl)
	{
		using namespace libtorrent;
		if (n->type() != lazy_entry::list_t) return;
		for (int i = 0; i < n->list_size(); ++i)
		{
			lazy_entry const* e = n->list_at(i);
			if (e->type() != lazy_entry::string_t) return;
			if (e->string_length() < 6) continue;
			char const* in = e->string_ptr();
			if (e->string_length() == 6)
				epl.push_back(read_v4_endpoint<EndpointType>(in));
#if TORRENT_USE_IPV6
			else if (e->string_length() == 18)
				epl.push_back(read_v6_endpoint<EndpointType>(in));
#endif
		}
	}

	template <class EndpointType>
	void read_endpoint_list(libtorrent::entry const* n, std::vector<EndpointType>& epl)
	{
		using namespace libtorrent;
		if (n->type() != entry::list_t) return;
		entry::list_type const& contacts = n->list();
		for (entry::list_type::const_iterator i = contacts.begin()
			, end(contacts.end()); i != end; ++i)
		{
			if (i->type() != entry::string_t) return;
			std::string const& p = i->string();
			if (p.size() < 6) continue;
			std::string::const_iterator in = p.begin();
			if (p.size() == 6)
				epl.push_back(read_v4_endpoint<EndpointType>(in));
#if TORRENT_USE_IPV6
			else if (p.size() == 18)
				epl.push_back(read_v6_endpoint<EndpointType>(in));
#endif
		}
	}

}

namespace libtorrent { namespace dht
{

	void intrusive_ptr_add_ref(dht_tracker const* c)
	{
		TORRENT_ASSERT(c != 0);
		TORRENT_ASSERT(c->m_refs >= 0);
		++c->m_refs;
	}

	void intrusive_ptr_release(dht_tracker const* c)
	{
		TORRENT_ASSERT(c != 0);
		TORRENT_ASSERT(c->m_refs > 0);
		if (--c->m_refs == 0)
			delete c;
	}

#ifdef TORRENT_DHT_VERBOSE_LOGGING
	TORRENT_DEFINE_LOG(dht_tracker)
#endif

	boost::optional<node_id> extract_node_id(lazy_entry const* e)
	{
		if (e == 0 || e->type() != lazy_entry::dict_t) return boost::optional<node_id>();
		lazy_entry const* nid = e->dict_find_string("node-id");
		if (nid == 0 || nid->string_length() != 20) return boost::optional<node_id>();
		return boost::optional<node_id>(node_id(nid->string_ptr()));
	}

	boost::optional<node_id> extract_node_id(entry const* e)
	{
		if (e == 0 || e->type() != entry::dictionary_t) return boost::optional<node_id>();
		entry const* nid = e->find_key("node-id");
		if (nid == 0 || nid->type() != entry::string_t || nid->string().length() != 20)
			return boost::optional<node_id>();
		return boost::optional<node_id>(node_id(nid->string().c_str()));
	}

	// class that puts the networking and the kademlia node in a single
	// unit and connecting them together.
	dht_tracker::dht_tracker(libtorrent::aux::session_impl& ses, rate_limited_udp_socket& sock
		, dht_settings const& settings, entry const* state)
		: m_dht(ses, bind(&dht_tracker::send_packet, this, _1), settings, extract_node_id(state))
		, m_ses(ses)
		, m_sock(sock)
		, m_last_new_key(time_now() - minutes(key_refresh))
		, m_timer(sock.get_io_service())
		, m_connection_timer(sock.get_io_service())
		, m_refresh_timer(sock.get_io_service())
		, m_settings(settings)
		, m_refresh_bucket(160)
		, m_abort(false)
		, m_host_resolver(sock.get_io_service())
		, m_sent_bytes(0)
		, m_received_bytes(0)
		, m_refs(0)
	{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		m_counter = 0;
		std::fill_n(m_replies_bytes_sent, 5, 0);
		std::fill_n(m_queries_bytes_received, 5, 0);
		std::fill_n(m_replies_sent, 5, 0);
		std::fill_n(m_queries_received, 5, 0);
		m_announces = 0;
		m_failed_announces = 0;
		m_total_message_input = 0;
		m_az_message_input = 0;
		m_ut_message_input = 0;
		m_lt_message_input = 0;
		m_mp_message_input = 0;
		m_gr_message_input = 0;
		m_mo_message_input = 0;
		m_total_in_bytes = 0;
		m_total_out_bytes = 0;
		m_queries_out_bytes = 0;
		
		// turns on and off individual components' logging

		rpc_log().enable(false);
//		node_log().enable(false);
		traversal_log().enable(false);
//		dht_tracker_log.enable(false);

		TORRENT_LOG(dht_tracker) << "starting DHT tracker with node id: " << m_dht.nid();
#endif
	}

	void dht_tracker::incoming_error(char const* message, lazy_entry const& e, udp::endpoint const& ep)
	{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(dht_tracker) << "ERROR: '" << message << "' " << e;
#endif
		msg reply;
		reply.reply = true;
		reply.message_id = messages::error;
		reply.error_code = 203; // Protocol error
		reply.error_msg = message;
		reply.addr = ep;
		reply.transaction_id = e.dict_find_string_value("t");
		send_packet(reply);
	}

	// defined in node.cpp
	extern void nop();

	void dht_tracker::start(entry const& bootstrap)
	{
		std::vector<udp::endpoint> initial_nodes;

		mutex_t::scoped_lock l(m_mutex);
		if (bootstrap.type() == entry::dictionary_t)
		{
#ifndef BOOST_NO_EXCEPTIONS
			try
			{
#endif
			if (entry const* nodes = bootstrap.find_key("nodes"))
				read_endpoint_list<udp::endpoint>(nodes, initial_nodes);
#ifndef BOOST_NO_EXCEPTIONS
			} catch (std::exception&) {}
#endif
		}

		error_code ec;
		m_timer.expires_from_now(seconds(1), ec);
		m_timer.async_wait(bind(&dht_tracker::tick, self(), _1));

		m_connection_timer.expires_from_now(seconds(10), ec);
		m_connection_timer.async_wait(
			bind(&dht_tracker::connection_timeout, self(), _1));

		m_refresh_timer.expires_from_now(seconds(5), ec);
		m_refresh_timer.async_wait(bind(&dht_tracker::refresh_timeout, self(), _1));

		m_dht.bootstrap(initial_nodes, boost::bind(&nop));
	}

	void dht_tracker::stop()
	{
		mutex_t::scoped_lock l(m_mutex);
		m_abort = true;
		error_code ec;
		m_timer.cancel(ec);
		m_connection_timer.cancel(ec);
		m_refresh_timer.cancel(ec);
		m_host_resolver.cancel();
	}

	void dht_tracker::dht_status(session_status& s)
	{
		mutex_t::scoped_lock l(m_mutex);
		m_dht.status(s);
	}

	void dht_tracker::network_stats(int& sent, int& received)
	{
		mutex_t::scoped_lock l(m_mutex);
		sent = m_sent_bytes;
		received = m_received_bytes;
		m_sent_bytes = 0;
		m_received_bytes = 0;
	}

	void dht_tracker::connection_timeout(error_code const& e)
	{
		mutex_t::scoped_lock l(m_mutex);
		if (e || m_abort) return;

		time_duration d = m_dht.connection_timeout();
		error_code ec;
		m_connection_timer.expires_from_now(d, ec);
		m_connection_timer.async_wait(boost::bind(&dht_tracker::connection_timeout, self(), _1));
	}

	void dht_tracker::refresh_timeout(error_code const& e)
	{
		mutex_t::scoped_lock l(m_mutex);
		if (e || m_abort) return;

		time_duration d = m_dht.refresh_timeout();
		error_code ec;
		m_refresh_timer.expires_from_now(d, ec);
		m_refresh_timer.async_wait(
			boost::bind(&dht_tracker::refresh_timeout, self(), _1));
	}

	void dht_tracker::tick(error_code const& e)
	{
		mutex_t::scoped_lock l(m_mutex);
		if (e || m_abort) return;

		error_code ec;
		m_timer.expires_from_now(minutes(tick_period), ec);
		m_timer.async_wait(boost::bind(&dht_tracker::tick, self(), _1));

		ptime now = time_now();
		if (now - m_last_new_key > minutes(key_refresh))
		{
			m_last_new_key = now;
			m_dht.new_write_key();
#ifdef TORRENT_DHT_VERBOSE_LOGGING
			TORRENT_LOG(dht_tracker) << " *** new write key";
#endif
		}
		
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		static bool first = true;

		std::ofstream st("dht_routing_table_state.txt", std::ios_base::trunc);
		m_dht.print_state(st);
		
		// count torrents
		int torrents = std::distance(m_dht.begin_data(), m_dht.end_data());
		
		// count peers
		int peers = 0;
		std::for_each(m_dht.begin_data(), m_dht.end_data(), count_peers(peers));

		std::ofstream pc("dht_stats.log", first ? std::ios_base::trunc : std::ios_base::app);
		if (first)
		{
			first = false;
			pc << "\n\n *****   starting log at " << time_now_string() << "   *****\n\n"
				<< "minute:active nodes:passive nodes"
				":ping replies sent:ping queries recvd"
				":ping replies bytes sent:ping queries bytes recvd"
				":find_node replies sent:find_node queries recv"
				":find_node replies bytes sent:find_node queries bytes recv"
				":get_peers replies sent:get_peers queries recvd"
				":get_peers replies bytes sent:get_peers queries bytes recv"
				":announce_peer replies sent:announce_peer queries recvd"
				":announce_peer replies bytes sent:announce_peer queries bytes recv"
				":error replies sent:error queries recvd"
				":error replies bytes sent:error queries bytes recv"
				":num torrents:num peers:announces per min"
				":failed announces per min:total msgs per min"
				":az msgs per min:ut msgs per min:lt msgs per min:mp msgs per min"
				":gr msgs per min:mo msgs per min:bytes in per sec:bytes out per sec"
				":queries out bytes per sec\n\n";
		}

		int active;
		int passive;
		boost::tie(active, passive) = m_dht.size();
		pc << (m_counter * tick_period)
			<< "\t" << active
			<< "\t" << passive;
		for (int i = 0; i < 5; ++i)
			pc << "\t" << (m_replies_sent[i] / float(tick_period))
				<< "\t" << (m_queries_received[i] / float(tick_period))
				<< "\t" << (m_replies_bytes_sent[i] / float(tick_period*60))
				<< "\t" << (m_queries_bytes_received[i] / float(tick_period*60));
		
		pc << "\t" << torrents
			<< "\t" << peers
			<< "\t" << m_announces / float(tick_period)
			<< "\t" << m_failed_announces / float(tick_period)
			<< "\t" << (m_total_message_input / float(tick_period))
			<< "\t" << (m_az_message_input / float(tick_period))
			<< "\t" << (m_ut_message_input / float(tick_period))
			<< "\t" << (m_lt_message_input / float(tick_period))
			<< "\t" << (m_mp_message_input / float(tick_period))
			<< "\t" << (m_gr_message_input / float(tick_period))
			<< "\t" << (m_mo_message_input / float(tick_period))
			<< "\t" << (m_total_in_bytes / float(tick_period*60))
			<< "\t" << (m_total_out_bytes / float(tick_period*60))
			<< "\t" << (m_queries_out_bytes / float(tick_period*60))
			<< std::endl;
		++m_counter;
		std::fill_n(m_replies_bytes_sent, 5, 0);
		std::fill_n(m_queries_bytes_received, 5, 0);
		std::fill_n(m_replies_sent, 5, 0);
		std::fill_n(m_queries_received, 5, 0);
		m_announces = 0;
		m_failed_announces = 0;
		m_total_message_input = 0;
		m_az_message_input = 0;
		m_ut_message_input = 0;
		m_lt_message_input = 0;
		m_total_in_bytes = 0;
		m_total_out_bytes = 0;
		m_queries_out_bytes = 0;
#endif
	}

	void dht_tracker::announce(sha1_hash const& ih, int listen_port
		, boost::function<void(std::vector<tcp::endpoint> const&)> f)
	{
		mutex_t::scoped_lock l(m_mutex);
		m_dht.announce(ih, listen_port, f);
	}


	void dht_tracker::on_unreachable(udp::endpoint const& ep)
	{
		mutex_t::scoped_lock l(m_mutex);
		m_dht.unreachable(ep);
	}

	// translate bittorrent kademlia message into the generice kademlia message
	// used by the library
	void dht_tracker::on_receive(udp::endpoint const& ep, char const* buf, int bytes_transferred)
	{
		mutex_t::scoped_lock l(m_mutex);
		// account for IP and UDP overhead
		m_received_bytes += bytes_transferred + (ep.address().is_v6() ? 48 : 28);

		node_ban_entry* match = 0;
		node_ban_entry* min = m_ban_nodes;
		ptime now = time_now();
		for (node_ban_entry* i = m_ban_nodes; i < m_ban_nodes + num_ban_nodes; ++i)
		{
			if (i->src == ep.address())
			{
				match = i;
				break;
			}
			if (i->count < min->count) min = i;
		}

		if (match)
		{
			++match->count;
			if (match->count >= 20)
			{
				if (now < match->limit)
				{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
					if (match->count == 20)
					{
						TORRENT_LOG(dht_tracker) << time_now_string() << " BANNING PEER [ ip: "
							<< ep << " time: " << total_milliseconds((now - match->limit) + seconds(5)) / 1000.f
							<< " count: " << match->count << " ]";
					}
#endif
					// we've received 20 messages in less than 5 seconds from
					// this node. Ignore it until it's silent for 5 minutes
					match->limit = now + minutes(5);
					return;
				}

				// we got 50 messages from this peer, but it was in
				// more than 5 seconds. Reset the counter and the timer
				match->count = 0;
				match->limit = now + seconds(5);
			}
		}
		else
		{
			min->count = 1;
			min->limit = now + seconds(5);
			min->src = ep.address();
		}

#ifdef TORRENT_DHT_VERBOSE_LOGGING
		++m_total_message_input;
		m_total_in_bytes += bytes_transferred;
#endif

		using libtorrent::entry;
		using libtorrent::bdecode;
			
		TORRENT_ASSERT(bytes_transferred > 0);

		lazy_entry e;
		int ret = lazy_bdecode(buf, buf + bytes_transferred, e);
		if (ret != 0)
		{
			// it's not a good idea to send invalid messages
			// especially not in response to an invalid message
			//incoming_error("invalid bencoding", e, ep);
			return;
		}

#ifdef TORRENT_DHT_VERBOSE_LOGGING
		std::stringstream log_line;
		log_line << "RECEIVED ["
			" ip: " << ep;
#endif

		if (e.type() != lazy_entry::dict_t)
		{
			// it's not a good idea to send invalid messages
			// especially not in response to an invalid message
			//incoming_error("message is not a dictionary", e, ep);
			return;
		}

		libtorrent::dht::msg m;
		m.message_id = 0;
		m.addr = ep;

		lazy_entry const* transaction = e.dict_find_string("t");
		if (!transaction)
		{
			// it's not a good idea to send invalid messages
			// especially not in response to an invalid message
			//incoming_error("missing or invalid transaction id", e, ep);
			return;
		}

		m.transaction_id = transaction->string_value();

#ifdef TORRENT_DHT_VERBOSE_LOGGING
		lazy_entry const* ver = e.dict_find_string("v");
		if (!ver)
		{
			log_line << " c: generic";
		}
		else
		{
			std::string const& client = ver->string_value();
			if (client.size() < 2)
			{
				log_line << " c: " << client;
			}
			else if (std::equal(client.begin(), client.begin() + 2, "Az"))
			{
				++m_az_message_input;
				log_line << " c: Azureus";
			}
			else if (std::equal(client.begin(), client.begin() + 2, "UT"))
			{
				++m_ut_message_input;
				log_line << " c: uTorrent";
			}
			else if (std::equal(client.begin(), client.begin() + 2, "LT"))
			{
				++m_lt_message_input;
				log_line << " c: libtorrent";
			}
			else if (std::equal(client.begin(), client.begin() + 2, "MP"))
			{
				++m_mp_message_input;
				log_line << " c: MooPolice";
			}
			else if (std::equal(client.begin(), client.begin() + 2, "GR"))
			{
				++m_gr_message_input;
				log_line << " c: GetRight";
			}
			else if (std::equal(client.begin(), client.begin() + 2, "MO"))
			{
				++m_mo_message_input;
				log_line << " c: Mono Torrent";
			}
			else
			{
				log_line << " c: " << client;
			}
		}
#endif

		lazy_entry const* y = e.dict_find_string("y");
		if (!y || y->string_length() < 1)
		{
			incoming_error("missing or invalid message type", e, ep);
			return;
		}

		char msg_type = *y->string_ptr();

		if (msg_type == 'r')
		{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
			log_line << " t: " << to_hex(m.transaction_id);
#endif

			m.reply = true;
			lazy_entry const* r = e.dict_find_dict("r");
			if (!r)
			{
				incoming_error("missing or invalid reply dict", e, ep);
				return;
			}

			lazy_entry const* id = r->dict_find_string("id");
			if (!id)
			{
				incoming_error("missing or invalid id", e, ep);
				return;
			}
			if (id->string_length() != 20)
			{
				incoming_error("invalid node id (not 20 bytes)", e, ep);
				return;
			}
			std::copy(id->string_ptr(), id->string_ptr()
				+ id->string_length(), m.id.begin());

#ifdef TORRENT_DHT_VERBOSE_LOGGING
			log_line << " id: " << m.id;
#endif
			lazy_entry const* n = r->dict_find_list("values");
			if (n)
			{
				m.peers.clear();
				if (n->list_size() == 1 && n->list_at(0)->type() == lazy_entry::string_t)
				{
					// assume it's mainline format
					char const* peers = n->list_at(0)->string_ptr();
					char const* end = peers + n->list_at(0)->string_length();

					while (end - peers >= 6)
						m.peers.push_back(read_v4_endpoint<tcp::endpoint>(peers));
				}
				else
				{
					// assume it's uTorrent/libtorrent format
					read_endpoint_list<tcp::endpoint>(n, m.peers);
				}
#ifdef TORRENT_DHT_VERBOSE_LOGGING
				log_line << " p: " << m.peers.size();
#endif
			}

			m.nodes.clear();
			n = r->dict_find_string("nodes");
			if (n)
			{
				char const* nodes = n->string_ptr();
				char const* end = nodes + n->string_length();

				while (end - nodes >= 26)
				{
					node_id id;
					std::copy(nodes, nodes + 20, id.begin());
					nodes += 20;
					m.nodes.push_back(libtorrent::dht::node_entry(
						id, read_v4_endpoint<udp::endpoint>(nodes)));
				}

#ifdef TORRENT_DHT_VERBOSE_LOGGING
				log_line << " n: " << m.nodes.size();
#endif
			}

			n = r->dict_find_list("nodes2");
			if (n)
			{
				for (int i = 0; i < n->list_size(); ++i)
				{
					lazy_entry const* p = n->list_at(0);
					if (p->type() != lazy_entry::string_t) continue;
					if (p->string_length() < 6 + 20) continue;
					char const* in = p->string_ptr();

					node_id id;
					std::copy(in, in + 20, id.begin());
					in += 20;
					if (p->string_length() == 6 + 20)
						m.nodes.push_back(libtorrent::dht::node_entry(
							id, read_v4_endpoint<udp::endpoint>(in)));
#if TORRENT_USE_IPV6
					else if (p->string_length() == 18 + 20)
						m.nodes.push_back(libtorrent::dht::node_entry(
							id, read_v6_endpoint<udp::endpoint>(in)));
#endif
				}
#ifdef TORRENT_DHT_VERBOSE_LOGGING
				log_line << " n2: " << m.nodes.size();
#endif
			}

			lazy_entry const* token = r->dict_find_string("token");
			if (token)
			{
				m.write_token = token->string_value();
				TORRENT_ASSERT(int(m.write_token.size()) == token->string_length());
#ifdef TORRENT_DHT_VERBOSE_LOGGING
				log_line << " token: " << to_hex(m.write_token);
#endif
			}
		}
		else if (msg_type == 'q')
		{
			m.reply = false;
			lazy_entry const* a = e.dict_find_dict("a");
			if (!a)
			{
				incoming_error("missing or invalid argument dictionary", e, ep);
				return;
			}

			lazy_entry const* id = a->dict_find_string("id");
			if (!id)
			{
				incoming_error("missing or invalid node id", e, ep);
				return;
			}
			if (id->string_length() != 20)
			{
				incoming_error("invalid node id (not 20 bytes)", e, ep);
				return;
			}
			std::copy(id->string_ptr(), id->string_ptr()
				+ id->string_length(), m.id.begin());

			lazy_entry const* q = e.dict_find_string("q");
			if (!q)
			{
				incoming_error("invalid or missing query string", e, ep);
				return;
			}
			std::string request_kind = q->string_value();
#ifdef TORRENT_DHT_VERBOSE_LOGGING
			log_line << " q: " << request_kind;
#endif

			if (request_kind == "ping")
			{
				m.message_id = libtorrent::dht::messages::ping;
			}
			else if (request_kind == "find_node")
			{
				lazy_entry const* target = a->dict_find_string("target");
				if (!target)
				{
					incoming_error("missing or invalid target", e, ep);
					return;
				}

				if (target->string_length() != 20)
				{
					incoming_error("invalid target (not 20 bytes)", e, ep);
					return;
				}
				std::copy(target->string_ptr(), target->string_ptr()
					+ target->string_length(), m.info_hash.begin());
#ifdef TORRENT_DHT_VERBOSE_LOGGING
				char out[41];
				to_hex((char const*)&m.info_hash[0], big_number::size, out);
				log_line << " t: " << out;
#endif

				m.message_id = libtorrent::dht::messages::find_node;
			}
			else if (request_kind == "get_peers")
			{
				lazy_entry const* info_hash = a->dict_find_string("info_hash");
				if (!info_hash)
				{
					incoming_error("missing or invalid info_hash", e, ep);
					return;
				}

				if (info_hash->string_length() != 20)
				{
					incoming_error("invalid info_hash (not 20 bytes)", e, ep);
					return;
				}
				std::copy(info_hash->string_ptr(), info_hash->string_ptr()
					+ info_hash->string_length(), m.info_hash.begin());
				m.message_id = libtorrent::dht::messages::get_peers;
#ifdef TORRENT_DHT_VERBOSE_LOGGING
				char out[41];
				to_hex((char const*)&m.info_hash[0], big_number::size, out);
				log_line << " ih: " << out;
#endif
			}
			else if (request_kind == "announce_peer")
			{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
				++m_announces;
#endif
				lazy_entry const* info_hash = a->dict_find_string("info_hash");
				if (!info_hash)
				{
					incoming_error("missing or invalid info_hash", e, ep);
					return;
				}

				if (info_hash->string_length() != 20)
				{
					incoming_error("invalid info_hash (not 20 bytes)", e, ep);
					return;
				}
				std::copy(info_hash->string_ptr(), info_hash->string_ptr()
					+ info_hash->string_length(), m.info_hash.begin());
				m.port = a->dict_find_int_value("port", -1);
				if (m.port == -1)
				{
					incoming_error("missing or invalid port in announce_peer message", e, ep);
					return;
				}
				lazy_entry const* token = a->dict_find_string("token");
				if (!token)
				{
					incoming_error("missing or invalid token in announce peer", e, ep);
					return;
				}
				m.write_token = token->string_value();
				m.message_id = libtorrent::dht::messages::announce_peer;
#ifdef TORRENT_DHT_VERBOSE_LOGGING
				log_line << " token: " << to_hex(m.write_token);
				char out[41];
				to_hex((char const*)&m.info_hash[0], big_number::size, out);
				log_line << " ih: " << out;
				log_line << " p: " << m.port;

				if (!m_dht.verify_token(m))
					++m_failed_announces;
#endif
			}
			else
			{
				// if we don't recognize the message but there's a
				// 'target' or 'info_hash' in the arguments, treat it
				// as find_node to be future compatible
				lazy_entry const* target_ent = a->dict_find_string("target");
				if (target_ent == 0 || target_ent->string_length() != 20)
				{
					target_ent = a->dict_find_string("info_hash");
					if (target_ent == 0 || target_ent->string_length() != 20)
					{
						incoming_error("unknown query", e, ep);
						return;
					}
				}
				std::copy(target_ent->string_ptr(), target_ent->string_ptr() + 20
					, m.info_hash.begin());
				m.message_id = libtorrent::dht::messages::find_node;
			}
		}
		else if (msg_type == 'e')
		{
			m.message_id = messages::error;
			m.error_code = 0;
			lazy_entry const* list = e.dict_find_list("e");
			if (!list)
			{
				list = e.dict_find_string("e");
				if (!list)
				{
					incoming_error("missing or invalid 'e' in error message", e, ep);
					return;
				}
				m.error_msg = list->string_value();
			}
			else
			{
				if (list->list_size() > 0 && list->list_at(0)->type() == lazy_entry::int_t)
					m.error_code = list->list_at(0)->int_value();
				if (list->list_size() > 1 && list->list_at(1)->type() == lazy_entry::string_t)
					m.error_msg = list->list_at(1)->string_value();
			}
#ifdef TORRENT_DHT_VERBOSE_LOGGING
			TORRENT_LOG(dht_tracker) << log_line.str() << " ]";
			TORRENT_LOG(dht_tracker) << "ERROR: incoming error: " << m.error_code
				<< " " << m.error_msg;
#endif
			return;
		}
		else
		{
			incoming_error("unknown message", e, ep);
			return;
		}

#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(dht_tracker) << log_line.str() << " ]";
//		TORRENT_LOG(dht_tracker) << std::string(buf, buf + bytes_transferred);
		if (!m.reply)
		{
			++m_queries_received[m.message_id];
			m_queries_bytes_received[m.message_id] += int(bytes_transferred);
		}
#endif
		TORRENT_ASSERT(m.message_id != messages::error);
		m_dht.incoming(m);
	}

	entry dht_tracker::state() const
	{
		mutex_t::scoped_lock l(m_mutex);
		entry ret(entry::dictionary_t);
		{
			entry nodes(entry::list_t);
			for (node_impl::iterator i(m_dht.begin())
				, end(m_dht.end()); i != end; ++i)
			{
				std::string node;
				std::back_insert_iterator<std::string> out(node);
				write_endpoint(udp::endpoint(i->addr, i->port), out);
				nodes.list().push_back(entry(node));
			}
			bucket_t cache;
			m_dht.replacement_cache(cache);
			for (bucket_t::iterator i(cache.begin())
				, end(cache.end()); i != end; ++i)
			{
				std::string node;
				std::back_insert_iterator<std::string> out(node);
				write_endpoint(udp::endpoint(i->addr, i->port), out);
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
		mutex_t::scoped_lock l(m_mutex);
		m_dht.add_node(node);
	}

	void dht_tracker::add_node(std::pair<std::string, int> const& node)
	{
		mutex_t::scoped_lock l(m_mutex);
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
		mutex_t::scoped_lock l(m_mutex);
		m_dht.add_router_node(node);
	}

	namespace
	{
		void write_nodes_entry(entry& r, libtorrent::dht::msg const& m)
		{
			bool ipv6_nodes = false;
			entry& n = r["nodes"];
			std::back_insert_iterator<std::string> out(n.string());
			for (msg::nodes_t::const_iterator i = m.nodes.begin()
				, end(m.nodes.end()); i != end; ++i)
			{
				if (!i->addr.is_v4())
				{
					ipv6_nodes = true;
					continue;
				}
				std::copy(i->id.begin(), i->id.end(), out);
				write_endpoint(udp::endpoint(i->addr, i->port), out);
			}

			if (ipv6_nodes)
			{
				entry& p = r["nodes2"];
				std::string endpoint;
				for (msg::nodes_t::const_iterator i = m.nodes.begin()
					, end(m.nodes.end()); i != end; ++i)
				{
					if (!i->addr.is_v6()) continue;
					endpoint.resize(18 + 20);
					std::string::iterator out = endpoint.begin();
					std::copy(i->id.begin(), i->id.end(), out);
					out += 20;
					write_endpoint(udp::endpoint(i->addr, i->port), out);
					endpoint.resize(out - endpoint.begin());
					p.list().push_back(entry(endpoint));
				}
			}
		}
	}

	void dht_tracker::send_packet(msg const& m)
	{
		using libtorrent::bencode;
		using libtorrent::entry;
		int send_flags = 0;
		entry e(entry::dictionary_t);
		TORRENT_ASSERT(!m.transaction_id.empty() || m.message_id == messages::error);
		e["t"] = m.transaction_id;
		static char const version_str[] = {'L', 'T'
			, LIBTORRENT_VERSION_MAJOR, LIBTORRENT_VERSION_MINOR};
		e["v"] = std::string(version_str, version_str + 4);

#ifdef TORRENT_DHT_VERBOSE_LOGGING
		std::stringstream log_line;
		log_line << "SENDING  [ ip: " << m.addr
			<< " t: " << to_hex(m.transaction_id);
#endif

		if (m.message_id == messages::error)
		{
			TORRENT_ASSERT(m.reply);
			e["y"] = "e";
			entry error_list(entry::list_t);
			TORRENT_ASSERT(m.error_code > 200 && m.error_code <= 204);
			error_list.list().push_back(entry(m.error_code));
			error_list.list().push_back(entry(m.error_msg));
			e["e"] = error_list;
#ifdef TORRENT_DHT_VERBOSE_LOGGING
			log_line << " err: " << m.error_code
				<< " msg: " << m.error_msg;
#endif
		}
		else if (m.reply)
		{
			e["y"] = "r";
			e["r"] = entry(entry::dictionary_t);
			entry& r = e["r"];
			r["id"] = std::string((char*)m.id.begin(), (char*)m.id.end());
			if (!m.ip.empty())
			{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
				log_line << " ip: " << to_hex(m.ip);
#endif
				r["ip"] = m.ip;
			}
			if (!m.write_token.empty())
			{
			 	r["token"] = m.write_token;
#ifdef TORRENT_DHT_VERBOSE_LOGGING
				log_line << " token: " << to_hex(m.write_token);
#endif
			}

#ifdef TORRENT_DHT_VERBOSE_LOGGING
			log_line << " r: " << messages::ids[m.message_id]
				<< " id: " << m.id;
#endif

			switch (m.message_id)
			{
				case messages::ping:
					break;
				case messages::find_node:
				{
					write_nodes_entry(r, m);
					break;
				}
				case messages::get_peers:
				{
					write_nodes_entry(r, m);

					if (!m.peers.empty())
					{
						r["values"] = entry(entry::list_t);
						entry& p = r["values"];
						std::string endpoint;
						for (msg::peers_t::const_iterator i = m.peers.begin()
							, end(m.peers.end()); i != end; ++i)
						{
							endpoint.resize(18);
							std::string::iterator out = endpoint.begin();
							write_endpoint(*i, out);
							endpoint.resize(out - endpoint.begin());
							p.list().push_back(entry(endpoint));
						}
#ifdef TORRENT_DHT_VERBOSE_LOGGING
						log_line << " values: " << m.peers.size();
#endif
					}
					break;
				}

				case messages::announce_peer:
					break;
				break;
			}
		}
		else
		{
			// set bit 1 of send_flags to indicate that
			// this packet should not be dropped by the
			// rate limiter.
			e["y"] = "q";
			e["a"] = entry(entry::dictionary_t);
			entry& a = e["a"];
			a["id"] = std::string((char*)m.id.begin(), (char*)m.id.end());

			TORRENT_ASSERT(m.message_id <= messages::error);
			e["q"] = messages::ids[m.message_id];

#ifdef TORRENT_DHT_VERBOSE_LOGGING
			log_line << " q: " << messages::ids[m.message_id]
				<< " id: " << m.id;
#endif

			switch (m.message_id)
			{
				case messages::find_node:
				{
					send_flags = 1;
					a["target"] = std::string((char*)m.info_hash.begin(), (char*)m.info_hash.end());
#ifdef TORRENT_DHT_VERBOSE_LOGGING
					char out[41];
					to_hex((char const*)&m.info_hash[0], big_number::size, out);
					log_line << " target: " << out;
#endif
					break;
				}
				case messages::get_peers:
				{
					send_flags = 1;
					a["info_hash"] = std::string((char*)m.info_hash.begin(), (char*)m.info_hash.end());
#ifdef TORRENT_DHT_VERBOSE_LOGGING
					char out[41];
					to_hex((char const*)&m.info_hash[0], big_number::size, out);
					log_line << " ih: " << out;
#endif
					break;	
				}
				case messages::announce_peer:
				{
					send_flags = 1;
					a["port"] = m.port;
					a["info_hash"] = std::string((char*)m.info_hash.begin(), (char*)m.info_hash.end());
					a["token"] = m.write_token;
#ifdef TORRENT_DHT_VERBOSE_LOGGING
					char out[41];
					to_hex((char const*)&m.info_hash[0], big_number::size, out);
					log_line << " port: " << m.port
						<< " ih: " << out
						<< " token: " << to_hex(m.write_token);
#endif
					break;
				}
				default: break;
			}

		}

		m_send_buf.clear();
		bencode(std::back_inserter(m_send_buf), e);
		error_code ec;
		if (m_sock.send(m.addr, &m_send_buf[0], (int)m_send_buf.size(), ec, send_flags))
		{
			// account for IP and UDP overhead
			m_sent_bytes += m_send_buf.size() + (m.addr.address().is_v6() ? 48 : 28);

#ifdef TORRENT_DHT_VERBOSE_LOGGING
			m_total_out_bytes += m_send_buf.size();
		
			if (m.reply)
			{
				++m_replies_sent[m.message_id];
				m_replies_bytes_sent[m.message_id] += int(m_send_buf.size());
			}
			else
			{
				m_queries_out_bytes += m_send_buf.size();
			}
#endif
		}
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(dht_tracker) << log_line.str() << " ]";
//		TORRENT_LOG(dht_tracker) << std::string(m_send_buf.begin(), m_send_buf.end());
#endif
	}

}}

