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
#include <boost/lexical_cast.hpp>
#include <boost/filesystem/operations.hpp>

#include "libtorrent/kademlia/node.hpp"
#include "libtorrent/kademlia/node_id.hpp"
#include "libtorrent/kademlia/traversal_algorithm.hpp"
#include "libtorrent/kademlia/dht_tracker.hpp"

#include "libtorrent/socket.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/version.hpp"
#include "libtorrent/escape_string.hpp"

using boost::ref;
using boost::lexical_cast;
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
			count += std::distance(t.second.peers.begin()
				, t.second.peers.end());
		}
	};
	
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
			else if (p.size() == 18)
				epl.push_back(read_v6_endpoint<EndpointType>(in));
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
	dht_tracker::dht_tracker(udp_socket& sock, dht_settings const& settings
		, entry const* state)
		: m_dht(bind(&dht_tracker::send_packet, this, _1), settings, extract_node_id(state))
		, m_sock(sock)
		, m_last_new_key(time_now() - minutes(key_refresh))
		, m_timer(sock.get_io_service())
		, m_connection_timer(sock.get_io_service())
		, m_refresh_timer(sock.get_io_service())
		, m_settings(settings)
		, m_refresh_bucket(160)
		, m_abort(false)
		, m_host_resolver(sock.get_io_service())
		, m_refs(0)
	{
		using boost::bind;

#ifdef TORRENT_DHT_VERBOSE_LOGGING
		m_counter = 0;
		std::fill_n(m_replies_bytes_sent, 5, 0);
		std::fill_n(m_queries_bytes_received, 5, 0);
		std::fill_n(m_replies_sent, 5, 0);
		std::fill_n(m_queries_received, 5, 0);
		m_announces = 0;
		m_failed_announces = 0;
		m_total_message_input = 0;
		m_ut_message_input = 0;
		m_lt_message_input = 0;
		m_mp_message_input = 0;
		m_gr_message_input = 0;
		m_mo_message_input = 0;
		m_total_in_bytes = 0;
		m_total_out_bytes = 0;
		m_queries_out_bytes = 0;
		
		// turns on and off individual components' logging

//		rpc_log().enable(false);
//		node_log().enable(false);
//		traversal_log().enable(false);
//		dht_tracker_log.enable(false);

#endif
	}

	void dht_tracker::start(entry const& bootstrap)
	{
		std::vector<udp::endpoint> initial_nodes;

		if (bootstrap.type() == entry::dictionary_t)
		{
			try
			{
			if (entry const* nodes = bootstrap.find_key("nodes"))
				read_endpoint_list<udp::endpoint>(nodes, initial_nodes);
			} catch (std::exception&) {}
		}

		error_code ec;
		m_timer.expires_from_now(seconds(1), ec);
		m_timer.async_wait(bind(&dht_tracker::tick, self(), _1));

		m_connection_timer.expires_from_now(seconds(10), ec);
		m_connection_timer.async_wait(
			bind(&dht_tracker::connection_timeout, self(), _1));

		m_refresh_timer.expires_from_now(seconds(5), ec);
		m_refresh_timer.async_wait(bind(&dht_tracker::refresh_timeout, self(), _1));

		m_dht.bootstrap(initial_nodes, boost::bind(&dht_tracker::on_bootstrap, self()));
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
		boost::tie(s.dht_nodes, s.dht_node_cache) = m_dht.size();
		s.dht_torrents = m_dht.data_size();
		s.dht_global_nodes = m_dht.num_global_nodes();
	}

	void dht_tracker::connection_timeout(error_code const& e)
		try
	{
		mutex_t::scoped_lock l(m_mutex);
		if (e || m_abort) return;

		time_duration d = m_dht.connection_timeout();
		m_connection_timer.expires_from_now(d);
		m_connection_timer.async_wait(bind(&dht_tracker::connection_timeout, self(), _1));
	}
	catch (std::exception& exc)
	{
#ifdef TORRENT_DEBUG
		std::cerr << "exception-type: " << typeid(exc).name() << std::endl;
		std::cerr << "what: " << exc.what() << std::endl;
		TORRENT_ASSERT(false);
#endif
	};

	void dht_tracker::refresh_timeout(error_code const& e)
		try
	{
		mutex_t::scoped_lock l(m_mutex);
		if (e || m_abort) return;

		time_duration d = m_dht.refresh_timeout();
		m_refresh_timer.expires_from_now(d);
		m_refresh_timer.async_wait(
			bind(&dht_tracker::refresh_timeout, self(), _1));
	}
	catch (std::exception&)
	{
		TORRENT_ASSERT(false);
	};

	void dht_tracker::tick(error_code const& e)
		try
	{
		mutex_t::scoped_lock l(m_mutex);
		if (e || m_abort) return;

		m_timer.expires_from_now(minutes(tick_period));
		m_timer.async_wait(bind(&dht_tracker::tick, self(), _1));

		ptime now = time_now();
		if (now - m_last_new_key > minutes(key_refresh))
		{
			m_last_new_key = now;
			m_dht.new_write_key();
#ifdef TORRENT_DHT_VERBOSE_LOGGING
			TORRENT_LOG(dht_tracker) << time_now_string() << " new write key";
#endif
		}
		
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		static bool first = true;
		if (first)
		{
			boost::filesystem::create_directory("libtorrent_logs");
		}

		std::ofstream st("libtorrent_logs/routing_table_state.txt", std::ios_base::trunc);
		m_dht.print_state(st);
		
		// count torrents
		int torrents = std::distance(m_dht.begin_data(), m_dht.end_data());
		
		// count peers
		int peers = 0;
		std::for_each(m_dht.begin_data(), m_dht.end_data(), count_peers(peers));

		std::ofstream pc("libtorrent_logs/dht_stats.log", first ? std::ios_base::trunc : std::ios_base::app);
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
				":ut msgs per min:lt msgs per min:mp msgs per min"
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
		m_ut_message_input = 0;
		m_lt_message_input = 0;
		m_total_in_bytes = 0;
		m_total_out_bytes = 0;
		m_queries_out_bytes = 0;
#endif
	}
	catch (std::exception&)
	{
		TORRENT_ASSERT(false);
	};

	void dht_tracker::announce(sha1_hash const& ih, int listen_port
		, boost::function<void(std::vector<tcp::endpoint> const&
		, sha1_hash const&)> f)
	{
		m_dht.announce(ih, listen_port, f);
	}


	void dht_tracker::on_unreachable(udp::endpoint const& ep)
	{
		m_dht.unreachable(ep);
	}

	// translate bittorrent kademlia message into the generice kademlia message
	// used by the library
	void dht_tracker::on_receive(udp::endpoint const& ep, char const* buf, int bytes_transferred)
		try
	{
		node_ban_entry* match = 0;
		node_ban_entry* min = m_ban_nodes;
		ptime now = time_now();
		for (node_ban_entry* i = m_ban_nodes; i < m_ban_nodes + num_ban_nodes; ++i)
		{
			if (i->src == ep)
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
			min->src = ep;
		}

#ifdef TORRENT_DHT_VERBOSE_LOGGING
		++m_total_message_input;
		m_total_in_bytes += bytes_transferred;
#endif

		try
		{
			using libtorrent::entry;
			using libtorrent::bdecode;
			
			TORRENT_ASSERT(bytes_transferred > 0);

			entry e = bdecode(buf, buf + bytes_transferred);
			if (e.type() == entry::undefined_t)
			{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
				std::string msg(buf, buf + bytes_transferred);
				TORRENT_LOG(dht_tracker) << "invalid incoming packet\n";
#endif
				return;
			}

#ifdef TORRENT_DHT_VERBOSE_LOGGING
			std::stringstream log_line;
			log_line << time_now_string() << " RECEIVED ["
				" ip: " << ep;
#endif

			libtorrent::dht::msg m;
			m.message_id = 0;
			m.addr = ep;
			m.transaction_id = e["t"].string();

#ifdef TORRENT_DHT_VERBOSE_LOGGING
			log_line << " t: " << to_hex(m.transaction_id);

			try
			{
				entry const* ver = e.find_key("v");
				if (!ver) throw std::exception();

				std::string const& client = ver->string();
				if (client.size() > 1 && std::equal(client.begin(), client.begin() + 2, "UT"))
				{
					++m_ut_message_input;
					log_line << " c: uTorrent";
				}
				else if (client.size() > 1 && std::equal(client.begin(), client.begin() + 2, "LT"))
				{
					++m_lt_message_input;
					log_line << " c: libtorrent";
				}
				else if (client.size() > 1 && std::equal(client.begin(), client.begin() + 2, "MP"))
				{
					++m_mp_message_input;
					log_line << " c: MooPolice";
				}
				else if (client.size() > 1 && std::equal(client.begin(), client.begin() + 2, "GR"))
				{
					++m_gr_message_input;
					log_line << " c: GetRight";
				}
				else if (client.size() > 1 && std::equal(client.begin(), client.begin() + 2, "MO"))
				{
					++m_mo_message_input;
					log_line << " c: Mono Torrent";
				}
				else
				{
					log_line << " c: " << client;
				}
			}
			catch (std::exception&)
			{
				log_line << " c: generic";
			};
#endif

			std::string const& msg_type = e["y"].string();

			if (msg_type == "r")
			{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
				log_line << " r: " << messages::ids[m.message_id];
#endif

				m.reply = true;
				entry const& r = e["r"];
				std::string const& id = r["id"].string();
				if (id.size() != 20) throw std::runtime_error("invalid size of id");
				std::copy(id.begin(), id.end(), m.id.begin());

				if (entry const* n = r.find_key("values"))
				{
					m.peers.clear();
					if (n->list().size() == 1)
					{
						// assume it's mainline format
						std::string const& peers = n->list().front().string();
						std::string::const_iterator i = peers.begin();
						std::string::const_iterator end = peers.end();

						while (std::distance(i, end) >= 6)
							m.peers.push_back(read_v4_endpoint<tcp::endpoint>(i));
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
				if (entry const* n = r.find_key("nodes"))				
				{
					std::string const& nodes = n->string();
					std::string::const_iterator i = nodes.begin();
					std::string::const_iterator end = nodes.end();

					while (std::distance(i, end) >= 26)
					{
						node_id id;
						std::copy(i, i + 20, id.begin());
						i += 20;
						m.nodes.push_back(libtorrent::dht::node_entry(
							id, read_v4_endpoint<udp::endpoint>(i)));
					}
#ifdef TORRENT_DHT_VERBOSE_LOGGING
					log_line << " n: " << m.nodes.size();
#endif
				}

				if (entry const* n = r.find_key("nodes2"))				
				{
					entry::list_type const& contacts = n->list();
					for (entry::list_type::const_iterator i = contacts.begin()
						, end(contacts.end()); i != end; ++i)
					{
						std::string const& p = i->string();
						if (p.size() < 6 + 20) continue;
						std::string::const_iterator in = p.begin();

						node_id id;
						std::copy(in, in + 20, id.begin());
						in += 20;
						if (p.size() == 6 + 20)
							m.nodes.push_back(libtorrent::dht::node_entry(
								id, read_v4_endpoint<udp::endpoint>(in)));
						else if (p.size() == 18 + 20)
							m.nodes.push_back(libtorrent::dht::node_entry(
								id, read_v6_endpoint<udp::endpoint>(in)));
					}
#ifdef TORRENT_DHT_VERBOSE_LOGGING
					log_line << " n2: " << m.nodes.size();
#endif
				}

				entry const* token = r.find_key("token");
				if (token) m.write_token = *token;
			}
			else if (msg_type == "q")
			{
				m.reply = false;
				entry const& a = e["a"];
				std::string const& id = a["id"].string();
				if (id.size() != 20) throw std::runtime_error("invalid size of id");
				std::copy(id.begin(), id.end(), m.id.begin());

				std::string request_kind(e["q"].string());
#ifdef TORRENT_DHT_VERBOSE_LOGGING
				log_line << " q: " << request_kind;
#endif

				if (request_kind == "ping")
				{
					m.message_id = libtorrent::dht::messages::ping;
				}
				else if (request_kind == "find_node")
				{
					std::string const& target = a["target"].string();
					if (target.size() != 20) throw std::runtime_error("invalid size of target id");
					std::copy(target.begin(), target.end(), m.info_hash.begin());
#ifdef TORRENT_DHT_VERBOSE_LOGGING
					log_line << " target: " << boost::lexical_cast<std::string>(m.info_hash);
#endif

					m.message_id = libtorrent::dht::messages::find_node;
				}
				else if (request_kind == "get_peers")
				{
					std::string const& info_hash = a["info_hash"].string();
					if (info_hash.size() != 20) throw std::runtime_error("invalid size of info-hash");
					std::copy(info_hash.begin(), info_hash.end(), m.info_hash.begin());
					m.message_id = libtorrent::dht::messages::get_peers;
#ifdef TORRENT_DHT_VERBOSE_LOGGING
					log_line << " ih: " << boost::lexical_cast<std::string>(m.info_hash);
#endif
				}
				else if (request_kind == "announce_peer")
				{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
					++m_announces;
#endif
					std::string const& info_hash = a["info_hash"].string();
					if (info_hash.size() != 20)
						throw std::runtime_error("invalid size of info-hash");
					std::copy(info_hash.begin(), info_hash.end(), m.info_hash.begin());
					m.port = a["port"].integer();
					m.write_token = a["token"];
					m.message_id = libtorrent::dht::messages::announce_peer;
#ifdef TORRENT_DHT_VERBOSE_LOGGING
					log_line << " ih: " << boost::lexical_cast<std::string>(m.info_hash);
					log_line << " p: " << m.port;

					if (!m_dht.verify_token(m))
						++m_failed_announces;
#endif
				}
				// if we don't recognize the request, but we see a 'target' or 'info_hash'
				// in the arguments, treat it as a find_node request to be forward compatible
				else if (a.find_key("target"))
				{
					std::string const& target = a["target"].string();
					if (target.size() != 20) throw std::runtime_error("invalid size of target id");
					std::copy(target.begin(), target.end(), m.info_hash.begin());
					m.message_id = libtorrent::dht::messages::find_node;
				}
				else if (a.find_key("info_hash"))
				{
					std::string const& target = a["info_hash"].string();
					if (target.size() != 20) throw std::runtime_error("invalid size of info-hash");
					std::copy(target.begin(), target.end(), m.info_hash.begin());
					m.message_id = libtorrent::dht::messages::find_node;
				}
				else
				{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
					TORRENT_LOG(dht_tracker) << "  *** UNSUPPORTED REQUEST *** : "
						<< request_kind;
#endif
					throw std::runtime_error("unsupported request: " + request_kind);
				}
			}
			else if (msg_type == "e")
			{
				entry::list_type const& list = e["e"].list();
				m.message_id = messages::error;
				m.error_msg = list.back().string();
				m.error_code = list.front().integer();
#ifdef TORRENT_DHT_VERBOSE_LOGGING
				log_line << " incoming error: " << m.error_code
					<< " " << m.error_msg;
#endif
				throw std::runtime_error("DHT error message");
			}
			else
			{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
				TORRENT_LOG(dht_tracker) << "  *** UNSUPPORTED MESSAGE TYPE *** : "
					<< msg_type;
#endif
				throw std::runtime_error("unsupported message type: " + msg_type);
			}

#ifdef TORRENT_DHT_VERBOSE_LOGGING
			if (!m.reply)
			{
				++m_queries_received[m.message_id];
				m_queries_bytes_received[m.message_id] += int(bytes_transferred);
			}
			TORRENT_LOG(dht_tracker) << log_line.str() << " ]";
#endif
			TORRENT_ASSERT(m.message_id != messages::error);
			m_dht.incoming(m);
		}
		catch (std::exception& e)
		{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
			std::string msg(buf, buf + bytes_transferred);
			TORRENT_LOG(dht_tracker) << "invalid incoming packet: "
				<< e.what() << "\n" << msg << "\n";
#endif
		}
	}
	catch (std::exception& e)
	{
		TORRENT_ASSERT(false);
	};

	entry dht_tracker::state() const
	{
		entry ret(entry::dictionary_t);
		{
			entry nodes(entry::list_t);
			for (node_impl::iterator i(m_dht.begin())
				, end(m_dht.end()); i != end; ++i)
			{
				std::string node;
				std::back_insert_iterator<std::string> out(node);
				write_endpoint(i->addr, out);
				nodes.list().push_back(entry(node));
			}
			bucket_t cache;
			m_dht.replacement_cache(cache);
			for (bucket_t::iterator i(cache.begin())
				, end(cache.end()); i != end; ++i)
			{
				std::string node;
				std::back_insert_iterator<std::string> out(node);
				write_endpoint(i->addr, out);
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
		udp::resolver::query q(node.first, lexical_cast<std::string>(node.second));
		m_host_resolver.async_resolve(q,
			bind(&dht_tracker::on_name_lookup, self(), _1, _2));
	}

	void dht_tracker::on_name_lookup(error_code const& e
		, udp::resolver::iterator host) try
	{
		if (e || host == udp::resolver::iterator()) return;
		add_node(host->endpoint());
	}
	catch (std::exception&)
	{
		TORRENT_ASSERT(false);
	};

	void dht_tracker::add_router_node(udp::endpoint const& node)
	{
		m_dht.add_router_node(node);
	}

	void dht_tracker::on_bootstrap()
	{}

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
				if (!i->addr.address().is_v4())
				{
					ipv6_nodes = true;
					continue;
				}
				std::copy(i->id.begin(), i->id.end(), out);
				write_endpoint(i->addr, out);
			}

			if (ipv6_nodes)
			{
				entry& p = r["nodes2"];
				std::string endpoint;
				for (msg::nodes_t::const_iterator i = m.nodes.begin()
					, end(m.nodes.end()); i != end; ++i)
				{
					if (!i->addr.address().is_v6()) continue;
					endpoint.resize(18 + 20);
					std::string::iterator out = endpoint.begin();
					std::copy(i->id.begin(), i->id.end(), out);
					out += 20;
					write_endpoint(i->addr, out);
					endpoint.resize(out - endpoint.begin());
					p.list().push_back(entry(endpoint));
				}
			}
		}
	}

	void dht_tracker::send_packet(msg const& m)
		try
	{
		using libtorrent::bencode;
		using libtorrent::entry;
		entry e(entry::dictionary_t);
		TORRENT_ASSERT(!m.transaction_id.empty() || m.message_id == messages::error);
		e["t"] = m.transaction_id;
		static char const version_str[] = {'L', 'T'
			, LIBTORRENT_VERSION_MAJOR, LIBTORRENT_VERSION_MINOR};
		e["v"] = std::string(version_str, version_str + 4);

#ifdef TORRENT_DHT_VERBOSE_LOGGING
		std::stringstream log_line;
		log_line << time_now_string()
			<< " SENDING [ ip: " << m.addr
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
			r["id"] = std::string(m.id.begin(), m.id.end());

#ifdef TORRENT_DHT_VERBOSE_LOGGING
			log_line << " r: " << messages::ids[m.message_id];
#endif

			if (m.write_token.type() != entry::undefined_t)
				r["token"] = m.write_token;

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
					if (m.peers.empty())
					{
						write_nodes_entry(r, m);
					}
					else
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
						log_line << " p: " << m.peers.size();
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
			e["y"] = "q";
			e["a"] = entry(entry::dictionary_t);
			entry& a = e["a"];
			a["id"] = std::string(m.id.begin(), m.id.end());

			if (m.write_token.type() != entry::undefined_t)
				a["token"] = m.write_token;
			TORRENT_ASSERT(m.message_id <= messages::error);
			e["q"] = messages::ids[m.message_id];

#ifdef TORRENT_DHT_VERBOSE_LOGGING
			log_line << " q: " << messages::ids[m.message_id];
#endif

			switch (m.message_id)
			{
				case messages::find_node:
				{
					a["target"] = std::string(m.info_hash.begin(), m.info_hash.end());
#ifdef TORRENT_DHT_VERBOSE_LOGGING
					log_line << " target: " << boost::lexical_cast<std::string>(m.info_hash);
#endif
					break;
				}
				case messages::get_peers:
				{
					a["info_hash"] = std::string(m.info_hash.begin(), m.info_hash.end());
#ifdef TORRENT_DHT_VERBOSE_LOGGING
					log_line << " ih: " << boost::lexical_cast<std::string>(m.info_hash);
#endif
					break;	
				}
				case messages::announce_peer:
					a["port"] = m.port;
					a["info_hash"] = std::string(m.info_hash.begin(), m.info_hash.end());
					a["token"] = m.write_token;
#ifdef TORRENT_DHT_VERBOSE_LOGGING
					log_line << " p: " << m.port
						<< " ih: " << boost::lexical_cast<std::string>(m.info_hash);
#endif
					break;
				default: break;
			}

		}

		m_send_buf.clear();
		bencode(std::back_inserter(m_send_buf), e);
		error_code ec;
		m_sock.send(m.addr, &m_send_buf[0], (int)m_send_buf.size(), ec);

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
		TORRENT_LOG(dht_tracker) << log_line.str() << " ]";
#endif

		if (!m.piggy_backed_ping) return;
		
		msg pm;
		pm.reply = false;
		pm.piggy_backed_ping = false;
		pm.message_id = messages::ping;
		pm.transaction_id = m.ping_transaction_id;
		pm.id = m.id;
		pm.addr = m.addr;
	
		send_packet(pm);	
	}
	catch (std::exception&)
	{
		// m_send may fail with "no route to host"
		// but it shouldn't throw since an error code
		// is passed in instead
		TORRENT_ASSERT(false);
	}

}}

