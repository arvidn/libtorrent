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

#include <fstream>
#include <set>
#include <numeric>
#include <stdexcept>
#include <boost/bind.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/date_time/posix_time/ptime.hpp>
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

using boost::posix_time::ptime;
using boost::posix_time::time_duration;
using boost::posix_time::second_clock;
using boost::posix_time::microsec_clock;
using boost::posix_time::seconds;
using boost::posix_time::minutes;
using boost::posix_time::hours;
using boost::posix_time::milliseconds;
using boost::ref;
using boost::lexical_cast;
using libtorrent::dht::node_impl;
using libtorrent::dht::node_id;
using libtorrent::dht::packet_t;
using libtorrent::dht::msg;
using libtorrent::dht::packet_iterator;
namespace messages = libtorrent::dht::messages;
using namespace libtorrent::detail;

using asio::ip::udp;
typedef asio::ip::address_v4 address;

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
	
	boost::optional<node_id> read_id(libtorrent::entry const& d)
	{
		using namespace libtorrent;
		using libtorrent::dht::node_id;

		if (d.type() != entry::dictionary_t) return boost::optional<node_id>();
		entry const* nid = d.find_key("node-id");
		if (!nid
			|| nid->type() != entry::string_t
			|| nid->string().length() != 40)
			return boost::optional<node_id>();
		return boost::optional<node_id>(
			boost::lexical_cast<node_id>(nid->string()));
	}

	template <class EndpointType>
	void read_endpoint_list(libtorrent::entry const* n, std::vector<EndpointType>& epl)				
	{
		using namespace libtorrent;
		entry::list_type const& contacts = n->list();
		for (entry::list_type::const_iterator i = contacts.begin()
			, end(contacts.end()); i != end; ++i)
		{
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
		assert(c != 0);
		assert(c->m_refs >= 0);
		++c->m_refs;
	}

	void intrusive_ptr_release(dht_tracker const* c)
	{
		assert(c != 0);
		assert(c->m_refs > 0);
		if (--c->m_refs == 0)
			delete c;
	}

#ifdef TORRENT_DHT_VERBOSE_LOGGING
	TORRENT_DEFINE_LOG(dht_tracker)
#endif

	// class that puts the networking and the kademlia node in a single
	// unit and connecting them together.
	dht_tracker::dht_tracker(asio::io_service& ios, dht_settings const& settings
		, asio::ip::address listen_interface, entry const& bootstrap)
		: m_strand(ios)
		, m_socket(ios, udp::endpoint(listen_interface, settings.service_port))
		, m_dht(bind(&dht_tracker::send_packet, this, _1), settings
			, read_id(bootstrap))
		, m_buffer(0)
		, m_last_refresh(second_clock::universal_time() - hours(1))
		, m_timer(ios)
		, m_connection_timer(ios)
		, m_refresh_timer(ios)
		, m_settings(settings)
		, m_refresh_bucket(160)
		, m_host_resolver(ios)
		, m_refs(0)
	{
		using boost::bind;

		m_in_buf[0].resize(1000);
		m_in_buf[1].resize(1000);
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
		std::vector<udp::endpoint> initial_nodes;

		if (bootstrap.type() == entry::dictionary_t)
		{
			try
			{
			if (entry const* nodes = bootstrap.find_key("nodes"))
				read_endpoint_list<udp::endpoint>(nodes, initial_nodes);
			} catch (std::exception&) {}
		}

		m_socket.async_receive_from(asio::buffer(&m_in_buf[m_buffer][0]
			, m_in_buf[m_buffer].size()), m_remote_endpoint[m_buffer]
			, m_strand.wrap(bind(&dht_tracker::on_receive, self(), _1, _2)));
		m_timer.expires_from_now(seconds(1));
		m_timer.async_wait(m_strand.wrap(bind(&dht_tracker::tick, self(), _1)));

		m_connection_timer.expires_from_now(seconds(10));
		m_connection_timer.async_wait(m_strand.wrap(
			bind(&dht_tracker::connection_timeout, self(), _1)));

		m_refresh_timer.expires_from_now(minutes(15));
		m_refresh_timer.async_wait(m_strand.wrap(bind(&dht_tracker::refresh_timeout, self(), _1)));

		m_dht.bootstrap(initial_nodes, bind(&dht_tracker::on_bootstrap, self()));
	}

	void dht_tracker::stop()
	{
		m_timer.cancel();
		m_connection_timer.cancel();
		m_refresh_timer.cancel();
		m_socket.close();
	}

	void dht_tracker::dht_status(session_status& s)
	{
		boost::tie(s.dht_nodes, s.dht_node_cache) = m_dht.size();
		s.dht_torrents = m_dht.data_size();
	}

	void dht_tracker::connection_timeout(asio::error_code const& e)
		try
	{
		if (e) return;
		time_duration d = m_dht.connection_timeout();
		m_connection_timer.expires_from_now(d);
		m_connection_timer.async_wait(m_strand.wrap(bind(&dht_tracker::connection_timeout, self(), _1)));
	}
	catch (std::exception& exc)
	{
#ifndef NDEBUG
		std::cerr << "exception-type: " << typeid(exc).name() << std::endl;
		std::cerr << "what: " << exc.what() << std::endl;
		assert(false);
#endif
	};

	void dht_tracker::refresh_timeout(asio::error_code const& e)
		try
	{
		if (e) return;
		time_duration d = m_dht.refresh_timeout();
		m_refresh_timer.expires_from_now(d);
		m_refresh_timer.async_wait(m_strand.wrap(
			bind(&dht_tracker::refresh_timeout, self(), _1)));
	}
	catch (std::exception&)
	{
		assert(false);
	};

	void dht_tracker::rebind(asio::ip::address listen_interface, int listen_port)
	{
		m_socket.close();
		udp::endpoint ep(listen_interface, listen_port);
		m_socket.open(ep.protocol());
		m_socket.bind(ep);
	}

	void dht_tracker::tick(asio::error_code const& e)
		try
	{
		if (e) return;
		m_timer.expires_from_now(minutes(tick_period));
		m_timer.async_wait(m_strand.wrap(bind(&dht_tracker::tick, this, _1)));

		m_dht.new_write_key();
		
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

		std::ofstream pc("libtorrent_logs/dht_stats.log", std::ios_base::app);
		if (first)
		{
			first = false;
			using boost::posix_time::to_simple_string;
			pc << "\n\n *****   starting log at " << to_simple_string(
				second_clock::universal_time()) << "   *****\n\n"
				<< "minute:active nodes:passive nodes"
				":ping replies sent:ping queries recvd:ping"
				":ping replies sent:ping queries recvd:ping"
				":find_node replies bytes sent:find_node queries bytes recv"
				":find_node replies bytes sent:find_node queries bytes recv"
				":get_peers replies sent:get_peers queries recvd:get_peers"
				":get_peers replies bytes sent:get_peers queries bytes recv"
				":announce_peer replies sent:announce_peer queries recvd:announce_peer"
				":announce_peer replies bytes sent:announce_peer queries bytes recv"
				":error replies sent:error queries recvd:error"
				":error replies bytes sent:error queries bytes recv"
				":num torrents:num peers:announces per min"
				":failed announces per min:total msgs per min"
				":ut msgs per min:lt msgs per min:mp msgs per min"
				":gr msgs per min:bytes in per sec:bytes out per sec"
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
		assert(false);
	};

	void dht_tracker::announce(sha1_hash const& ih, int listen_port
		, boost::function<void(std::vector<tcp::endpoint> const&
		, sha1_hash const&)> f)
	{
		m_dht.announce(ih, listen_port, f);
	}

	// translate bittorrent kademlia message into the generice kademlia message
	// used by the library
	void dht_tracker::on_receive(asio::error_code const& error, size_t bytes_transferred)
		try
	{
		if (error == asio::error::operation_aborted) return;
	
		int current_buffer = m_buffer;
		m_buffer = (m_buffer + 1) & 1;
		m_socket.async_receive_from(asio::buffer(&m_in_buf[m_buffer][0]
			, m_in_buf[m_buffer].size()), m_remote_endpoint[m_buffer]
			, m_strand.wrap(bind(&dht_tracker::on_receive, self(), _1, _2)));

		if (error) return;

#ifdef TORRENT_DHT_VERBOSE_LOGGING
		++m_total_message_input;
		m_total_in_bytes += bytes_transferred;
#endif

		try
		{
			using libtorrent::entry;
			using libtorrent::bdecode;
			
			assert(bytes_transferred > 0);

			entry e = bdecode(m_in_buf[current_buffer].begin()
				, m_in_buf[current_buffer].end());

#ifdef TORRENT_DHT_VERBOSE_LOGGING
			TORRENT_LOG(dht_tracker) << microsec_clock::universal_time()
				<< " RECEIVED [" << m_remote_endpoint[current_buffer]
				<< "]:";
#endif

			libtorrent::dht::msg m;
			m.message_id = 0;
			m.addr = m_remote_endpoint[current_buffer];
			m.transaction_id = e["t"].string();

#ifdef TORRENT_DHT_VERBOSE_LOGGING
			try
			{
				entry const* ver = e.find_key("v");
				if (!ver) throw std::exception();

				std::string const& client = ver->string();
				if (client.size() > 1 && std::equal(client.begin(), client.begin() + 2, "UT"))
				{
					++m_ut_message_input;
					TORRENT_LOG(dht_tracker) << "   client: uTorrent";
				}
				else if (client.size() > 1 && std::equal(client.begin(), client.begin() + 2, "LT"))
				{
					++m_lt_message_input;
					TORRENT_LOG(dht_tracker) << "   client: libtorrent";
				}
				else if (client.size() > 1 && std::equal(client.begin(), client.begin() + 2, "MP"))
				{
					++m_mp_message_input;
					TORRENT_LOG(dht_tracker) << "   client: MooPolice";
				}
				else if (client.size() > 1 && std::equal(client.begin(), client.begin() + 2, "GR"))
				{
					++m_gr_message_input;
					TORRENT_LOG(dht_tracker) << "   client: GetRight";
				}
				else if (client.size() > 1 && std::equal(client.begin(), client.begin() + 2, "MO"))
				{
					++m_mo_message_input;
					TORRENT_LOG(dht_tracker) << "   client: Mono Torrent";
				}
				else
				{
					TORRENT_LOG(dht_tracker) << "   client: generic";
				}
			}
			catch (std::exception&)
			{
				TORRENT_LOG(dht_tracker) << "   client: generic";
			};
#endif

			std::string const& msg_type = e["y"].string();

			if (msg_type == "r")
			{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
				TORRENT_LOG(dht_tracker) << "   reply: transaction: "
					<< m.transaction_id;
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
					TORRENT_LOG(dht_tracker) << "   peers: " << m.peers.size();
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
					TORRENT_LOG(dht_tracker) << "   nodes: " << m.nodes.size();
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
					TORRENT_LOG(dht_tracker) << "   nodes2 + nodes: " << m.nodes.size();
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
				TORRENT_LOG(dht_tracker) << "   query: " << request_kind;
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
					TORRENT_LOG(dht_tracker) << "   target: "
						<< boost::lexical_cast<std::string>(m.info_hash);
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
					TORRENT_LOG(dht_tracker) << "   info_hash: "
						<< boost::lexical_cast<std::string>(m.info_hash);
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
					TORRENT_LOG(dht_tracker) << "   info_hash: "
						<< boost::lexical_cast<std::string>(m.info_hash);
					TORRENT_LOG(dht_tracker) << "   port: " << m.port;

					if (!m_dht.verify_token(m))
						++m_failed_announces;
#endif
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
				TORRENT_LOG(dht_tracker) << "   error: " << m.error_code << " "
					<< m.error_msg;
#endif
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
			TORRENT_LOG(dht_tracker) << e;
#endif

			m_dht.incoming(m);
		}
		catch (std::exception& e)
		{
#ifdef TORRENT_DHT_VERBOSE_LOGGING
			TORRENT_LOG(dht_tracker) << "invalid incoming packet: "
				<< e.what();
#endif
		}
	}
	catch (std::exception& e)
	{
		assert(false);
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
		
		ret["node-id"] = boost::lexical_cast<std::string>(m_dht.nid());
		return ret;
	}

	void dht_tracker::add_node(udp::endpoint node)
	{
		m_dht.add_node(node);
	}

	void dht_tracker::add_node(std::pair<std::string, int> const& node)
	{
		udp::resolver::query q(node.first, lexical_cast<std::string>(node.second));
		m_host_resolver.async_resolve(q, m_strand.wrap(
			bind(&dht_tracker::on_name_lookup, self(), _1, _2)));
	}

	void dht_tracker::on_name_lookup(asio::error_code const& e
		, udp::resolver::iterator host) try
	{
		if (e || host == udp::resolver::iterator()) return;
		add_node(host->endpoint());
	}
	catch (std::exception&)
	{
		assert(false);
	};

	void dht_tracker::add_router_node(std::pair<std::string, int> const& node)
	{
		udp::resolver::query q(node.first, lexical_cast<std::string>(node.second));
		m_host_resolver.async_resolve(q, m_strand.wrap(
			bind(&dht_tracker::on_router_name_lookup, self(), _1, _2)));
	}

	void dht_tracker::on_router_name_lookup(asio::error_code const& e
		, udp::resolver::iterator host) try
	{
		if (e || host == udp::resolver::iterator()) return;
		m_dht.add_router_node(host->endpoint());
	}
	catch (std::exception&)
	{
		assert(false);
	};

	void dht_tracker::on_bootstrap()
	{}

	void dht_tracker::send_packet(msg const& m)
	{
		using libtorrent::bencode;
		using libtorrent::entry;
		entry e(entry::dictionary_t);
		e["t"] = m.transaction_id;
		std::string version_str("LT  ");
		std::string::iterator i = version_str.begin() + 2;
		detail::write_uint8(LIBTORRENT_VERSION_MAJOR, i);
		detail::write_uint8(LIBTORRENT_VERSION_MINOR, i);
		e["v"] = version_str;

#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(dht_tracker) << microsec_clock::universal_time()
			<< " SENDING [" << m.addr << "]:";
		TORRENT_LOG(dht_tracker) << "   transaction: " << m.transaction_id;
//		e.print(std::cerr);
#endif

		if (m.message_id == messages::error)
		{
			assert(m.reply);
			e["y"] = "e";
			entry error_list(entry::list_t);
			error_list.list().push_back(entry(m.error_code));
			error_list.list().push_back(entry(m.error_msg));
			e["e"] = error_list;
#ifdef TORRENT_DHT_VERBOSE_LOGGING
		TORRENT_LOG(dht_tracker) << "   error: " << m.error_code << " "
			<< m.error_msg;
#endif
		}
		else if (m.reply)
		{
			e["y"] = "r";
			e["r"] = entry(entry::dictionary_t);
			entry& r = e["r"];
			r["id"] = std::string(m.id.begin(), m.id.end());

#ifdef TORRENT_DHT_VERBOSE_LOGGING
			TORRENT_LOG(dht_tracker) << "   reply: "
				<< messages::ids[m.message_id];
#endif

			if (m.write_token.type() != entry::undefined_t)
				r["token"] = m.write_token;

			switch (m.message_id)
			{
				case messages::ping:
					break;
				case messages::find_node:
				{
					bool ipv6_nodes = false;
					r["nodes"] = entry(entry::string_t);
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
						r["nodes2"] = entry(entry::list_t);
						entry& p = r["nodes2"];
						std::string endpoint;
						endpoint.resize(6);
						for (msg::nodes_t::const_iterator i = m.nodes.begin()
							, end(m.nodes.end()); i != end; ++i)
						{
							std::string::iterator out = endpoint.begin();
							std::copy(i->id.begin(), i->id.end(), out);
							write_endpoint(i->addr, out);
							p.list().push_back(entry(endpoint));
						}
					}
#ifdef TORRENT_DHT_VERBOSE_LOGGING
					TORRENT_LOG(dht_tracker) << "   nodes: " << m.nodes.size();
#endif
					break;
				}
				case messages::get_peers:
				{
					if (m.peers.empty())
					{
						r["nodes"] = entry(entry::string_t);
						entry& n = r["nodes"];
						std::back_insert_iterator<std::string> out(n.string());
						for (msg::nodes_t::const_iterator i = m.nodes.begin()
							, end(m.nodes.end()); i != end; ++i)
						{
							if (!i->addr.address().is_v4()) continue;
							std::copy(i->id.begin(), i->id.end(), out);
							write_endpoint(i->addr, out);
						}
#ifdef TORRENT_DHT_VERBOSE_LOGGING
						TORRENT_LOG(dht_tracker) << "   nodes: " << m.nodes.size();
#endif
					}
					else
					{
						r["values"] = entry(entry::list_t);
						entry& p = r["values"];
						std::string endpoint;
						endpoint.resize(6);
						for (msg::peers_t::const_iterator i = m.peers.begin()
							, end(m.peers.end()); i != end; ++i)
						{
							std::string::iterator out = endpoint.begin();
							write_endpoint(*i, out);
							p.list().push_back(entry(endpoint));
						}
#ifdef TORRENT_DHT_VERBOSE_LOGGING
						TORRENT_LOG(dht_tracker) << "   peers: " << m.peers.size();
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
			assert(m.message_id <= messages::error);
			e["q"] = messages::ids[m.message_id];

#ifdef TORRENT_DHT_VERBOSE_LOGGING
			TORRENT_LOG(dht_tracker) << "   query: "
				<< messages::ids[m.message_id];
#endif

			switch (m.message_id)
			{
				case messages::find_node:
				{
					a["target"] = std::string(m.info_hash.begin(), m.info_hash.end());
#ifdef TORRENT_DHT_VERBOSE_LOGGING
					TORRENT_LOG(dht_tracker) << "   target: "
						<< boost::lexical_cast<std::string>(m.info_hash);
#endif
					break;
				}
				case messages::get_peers:
				{
					a["info_hash"] = std::string(m.info_hash.begin(), m.info_hash.end());
#ifdef TORRENT_DHT_VERBOSE_LOGGING
					TORRENT_LOG(dht_tracker) << "   info_hash: "
						<< boost::lexical_cast<std::string>(m.info_hash);
#endif
					break;	
				}
				case messages::announce_peer:
					a["port"] = m_settings.service_port;
					a["info_hash"] = std::string(m.info_hash.begin(), m.info_hash.end());
					a["token"] = m.write_token;
#ifdef TORRENT_DHT_VERBOSE_LOGGING
					TORRENT_LOG(dht_tracker) << "   port: "
						<< m_settings.service_port
						<< "   info_hash: " << boost::lexical_cast<std::string>(m.info_hash);
#endif
					break;
				default: break;
			}

		}

		m_send_buf.clear();
		bencode(std::back_inserter(m_send_buf), e);
		m_socket.send_to(asio::buffer(&m_send_buf[0]
			, (int)m_send_buf.size()), m.addr);

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
		TORRENT_LOG(dht_tracker) << e;
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

}}

