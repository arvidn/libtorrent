/*

Copyright (c) 2006-2014, MassaRoddel, Arvid Norberg
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

#ifndef TORRENT_DISABLE_EXTENSIONS

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/shared_ptr.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/peer_connection.hpp"
#include "libtorrent/bt_peer_connection.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/extensions.hpp"
#include "libtorrent/broadcast_socket.hpp"
#include "libtorrent/socket_io.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/socket_type.hpp" // for is_utp
#include "libtorrent/performance_counters.hpp" // for counters

#include "libtorrent/extensions/ut_pex.hpp"

#ifdef TORRENT_LOGGING
#include "libtorrent/lazy_entry.hpp"
#endif

namespace libtorrent { namespace
{
	const char extension_name[] = "ut_pex";

	enum
	{
		extension_index = 1,
		max_peer_entries = 100
	};

	bool send_peer(peer_connection const& p)
	{
		// don't send out those peers that we haven't connected to
		// (that have connected to us) and that aren't sharing their
		// listening port 
		if (!p.is_outgoing() && !p.received_listen_port()) return false;
		// don't send out peers that we haven't successfully connected to
		if (p.is_connecting()) return false;
		if (p.in_handshake()) return false;
		return true;
	}

	struct ut_pex_plugin: torrent_plugin
	{
		// randomize when we rebuild the pex message
		// to evenly spread it out across all torrents
		// the more torrents we have, the longer we can
		// delay the rebuilding
		ut_pex_plugin(torrent& t)
			: m_torrent(t)
			, m_last_msg(min_time())
			, m_peers_in_message(0) {}
	
		virtual boost::shared_ptr<peer_plugin> new_connection(peer_connection* pc);

		std::vector<char>& get_ut_pex_msg()
		{
			return m_ut_pex_msg;
		}

		int peers_in_msg() const
		{
			return m_peers_in_message;
		}

		// the second tick of the torrent
		// each minute the new lists of "added" + "added.f" and "dropped"
		// are calculated here and the pex message is created
		// each peer connection will use this message
		// max_peer_entries limits the packet size
		virtual void tick()
		{
			time_point now = aux::time_now();
			if (now - seconds(60) < m_last_msg) return;
			m_last_msg = now;

			int num_peers = m_torrent.num_peers();
			if (num_peers == 0) return;

			entry pex;
			std::string& pla = pex["added"].string();
			std::string& pld = pex["dropped"].string();
			std::string& plf = pex["added.f"].string();
			std::back_insert_iterator<std::string> pla_out(pla);
			std::back_insert_iterator<std::string> pld_out(pld);
			std::back_insert_iterator<std::string> plf_out(plf);
#if TORRENT_USE_IPV6
			std::string& pla6 = pex["added6"].string();
			std::string& pld6 = pex["dropped6"].string();
			std::string& plf6 = pex["added6.f"].string();
			std::back_insert_iterator<std::string> pla6_out(pla6);
			std::back_insert_iterator<std::string> pld6_out(pld6);
			std::back_insert_iterator<std::string> plf6_out(plf6);
#endif

			std::set<tcp::endpoint> dropped;
			m_old_peers.swap(dropped);

			m_peers_in_message = 0;
			int num_added = 0;
			for (torrent::peer_iterator i = m_torrent.begin()
				, end(m_torrent.end()); i != end; ++i)
			{
				peer_connection* peer = *i;
				if (!send_peer(*peer)) continue;

				tcp::endpoint remote = peer->remote();
				m_old_peers.insert(remote);

				std::set<tcp::endpoint>::iterator di = dropped.find(remote);
				if (di == dropped.end())
				{
					// don't write too big of a package
					if (num_added >= max_peer_entries) break;

					// only send proper bittorrent peers
					if (peer->type() != peer_connection::bittorrent_connection)
						continue;

					bt_peer_connection* p = static_cast<bt_peer_connection*>(peer);

					// if the peer has told us which port its listening on,
					// use that port. But only if we didn't connect to the peer.
					// if we connected to it, use the port we know works
					torrent_peer *pi = 0;
					if (!p->is_outgoing() && (pi = peer->peer_info_struct()) && pi->port > 0)
						remote.port(pi->port);

					// no supported flags to set yet
					// 0x01 - peer supports encryption
					// 0x02 - peer is a seed
					// 0x04 - supports uTP. This is only a positive flags
					//        passing 0 doesn't mean the peer doesn't
					//        support uTP
					// 0x08 - supports holepunching protocol. If this
					//        flag is received from a peer, it can be
					//        used as a rendezvous point in case direct
					//        connections to the peer fail
					int flags = p->is_seed() ? 2 : 0;
#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
					flags |= p->supports_encryption() ? 1 : 0;
#endif
					flags |= is_utp(*p->get_socket()) ? 4 :  0;
					flags |= p->supports_holepunch() ? 8 : 0;

					// i->first was added since the last time
					if (remote.address().is_v4())
					{
						detail::write_endpoint(remote, pla_out);
						detail::write_uint8(flags, plf_out);
					}
#if TORRENT_USE_IPV6
					else
					{
						detail::write_endpoint(remote, pla6_out);
						detail::write_uint8(flags, plf6_out);
					}
#endif
					++num_added;
					++m_peers_in_message;
				}
				else
				{
					// this was in the previous message
					// so, it wasn't dropped
					dropped.erase(di);
				}
			}

			for (std::set<tcp::endpoint>::const_iterator i = dropped.begin()
				, end(dropped.end()); i != end; ++i)
			{	
				if (i->address().is_v4())
					detail::write_endpoint(*i, pld_out);
#if TORRENT_USE_IPV6
				else
					detail::write_endpoint(*i, pld6_out);
#endif
				++m_peers_in_message;
			}

			m_ut_pex_msg.clear();
			bencode(std::back_inserter(m_ut_pex_msg), pex);
		}

	private:
		torrent& m_torrent;

		std::set<tcp::endpoint> m_old_peers;
		time_point m_last_msg;
		std::vector<char> m_ut_pex_msg;
		int m_peers_in_message;
	};


	struct ut_pex_peer_plugin : peer_plugin
	{	
		ut_pex_peer_plugin(torrent& t, peer_connection& pc, ut_pex_plugin& tp)
			: m_torrent(t)
			, m_pc(pc)
			, m_tp(tp)
			, m_last_msg(min_time())
			, m_message_index(0)
			, m_first_time(true)
		{
			const int num_pex_timers = sizeof(m_last_pex)/sizeof(m_last_pex[0]);
			for (int i = 0; i < num_pex_timers; ++i)
			{
				m_last_pex[i]= min_time();
			}
		}

		virtual char const* type() const { return "ut_pex"; }

		virtual void add_handshake(entry& h)
		{
			entry& messages = h["m"];
			messages[extension_name] = extension_index;
		}

		virtual bool on_extension_handshake(bdecode_node const& h)
		{
			m_message_index = 0;
			if (h.type() != bdecode_node::dict_t) return false;
			bdecode_node messages = h.dict_find_dict("m");
			if (!messages) return false;

			int index = int(messages.dict_find_int_value(extension_name, -1));
			if (index == -1) return false;
			m_message_index = index;
			return true;
		}

		virtual bool on_extended(int length, int msg, buffer::const_interval body)
		{
			if (msg != extension_index) return false;
			if (m_message_index == 0) return false;

			if (length > 500 * 1024)
			{
				m_pc.disconnect(errors::pex_message_too_large, op_bittorrent, 2);
				return true;
			}
 
			if (body.left() < length) return true;

			time_point now = aux::time_now();
			if (now - seconds(60) <  m_last_pex[0])
			{
				// this client appears to be trying to flood us
				// with pex messages. Don't allow that.
				m_pc.disconnect(errors::too_frequent_pex, op_bittorrent);
				return true;
			}

			const int num_pex_timers = sizeof(m_last_pex)/sizeof(m_last_pex[0]);
			for (int i = 0; i < num_pex_timers-1; ++i)
				m_last_pex[i] = m_last_pex[i+1];
			m_last_pex[num_pex_timers-1] = now;

			bdecode_node pex_msg;
			error_code ec;
			int ret = bdecode(body.begin, body.end, pex_msg, ec);
			if (ret != 0 || pex_msg.type() != bdecode_node::dict_t)
			{
				m_pc.disconnect(errors::invalid_pex_message, op_bittorrent, 2);
				return true;
			}

			bdecode_node p = pex_msg.dict_find_string("dropped");

#ifdef TORRENT_LOGGING
			int num_dropped = 0;
			int num_added = 0;
			if (p) num_dropped += p.string_length()/6;
#endif
			if (p)
			{
				int num_peers = p.string_length() / 6;
				char const* in = p.string_ptr();

				for (int i = 0; i < num_peers; ++i)
				{
					tcp::endpoint adr = detail::read_v4_endpoint<tcp::endpoint>(in);
					peers4_t::value_type v(adr.address().to_v4().to_bytes(), adr.port());
					peers4_t::iterator j = std::lower_bound(m_peers.begin(), m_peers.end(), v);
					if (j != m_peers.end() && *j == v) m_peers.erase(j);
				} 
			}

			p = pex_msg.dict_find_string("added");
			bdecode_node pf = pex_msg.dict_find_string("added.f");

#ifdef TORRENT_LOGGING
			if (p) num_added += p.string_length() / 6;
#endif
			if (p && pf && pf.string_length() == p.string_length() / 6)
			{
				int num_peers = pf.string_length();
				char const* in = p.string_ptr();
				char const* fin = pf.string_ptr();

				for (int i = 0; i < num_peers; ++i)
				{
					tcp::endpoint adr = detail::read_v4_endpoint<tcp::endpoint>(in);
					char flags = *fin++;

					if (int(m_peers.size()) >= m_torrent.settings().get_int(settings_pack::max_pex_peers))
						break;

					// ignore local addresses unless the peer is local to us
					if (is_local(adr.address()) && !is_local(m_pc.remote().address())) continue;

					peers4_t::value_type v(adr.address().to_v4().to_bytes(), adr.port());
					peers4_t::iterator j = std::lower_bound(m_peers.begin(), m_peers.end(), v);
					// do we already know about this peer?
					if (j != m_peers.end() && *j == v) continue;
					m_peers.insert(j, v);
					m_torrent.add_peer(adr, peer_info::pex, flags);
				} 
			}

#if TORRENT_USE_IPV6

			bdecode_node p6 = pex_msg.dict_find("dropped6");
#ifdef TORRENT_LOGGING
			if (p6) num_dropped += p6.string_length() / 18;
#endif
			if (p6 != 0 && p6.type() == bdecode_node::string_t)
			{
				int num_peers = p6.string_length() / 18;
				char const* in = p6.string_ptr();

				for (int i = 0; i < num_peers; ++i)
				{
					tcp::endpoint adr = detail::read_v6_endpoint<tcp::endpoint>(in);
					peers6_t::value_type v(adr.address().to_v6().to_bytes(), adr.port());
					peers6_t::iterator j = std::lower_bound(m_peers6.begin(), m_peers6.end(), v);
					if (j != m_peers6.end() && *j == v) m_peers6.erase(j);
				} 
			}

			p6 = pex_msg.dict_find("added6");
#ifdef TORRENT_LOGGING
			if (p6) num_added += p6.string_length() / 18;
#endif
			bdecode_node p6f = pex_msg.dict_find("added6.f");
			if (p6 != 0
				&& p6f != 0
				&& p6.type() == bdecode_node::string_t
				&& p6f.type() == bdecode_node::string_t
				&& p6f.string_length() == p6.string_length() / 18)
			{
				int num_peers = p6f.string_length();
				char const* in = p6.string_ptr();
				char const* fin = p6f.string_ptr();

				for (int i = 0; i < num_peers; ++i)
				{
					tcp::endpoint adr = detail::read_v6_endpoint<tcp::endpoint>(in);
					char flags = *fin++;
					// ignore local addresses unless the peer is local to us
					if (is_local(adr.address()) && !is_local(m_pc.remote().address())) continue;
					if (int(m_peers6.size()) >= m_torrent.settings().get_int(settings_pack::max_pex_peers))
						break;

					peers6_t::value_type v(adr.address().to_v6().to_bytes(), adr.port());
					peers6_t::iterator j = std::lower_bound(m_peers6.begin(), m_peers6.end(), v);
					// do we already know about this peer?
					if (j != m_peers6.end() && *j == v) continue;
					m_peers6.insert(j, v);
					m_torrent.add_peer(adr, peer_info::pex, flags);
				} 
			}
#endif
#ifdef TORRENT_LOGGING
			m_pc.peer_log("<== PEX [ dropped: %d added: %d ]"
				, num_dropped, num_added);
#endif

			m_pc.stats_counters().inc_stats_counter(counters::num_incoming_pex);
			return true;
		}

		// the peers second tick
		// every minute we send a pex message
		virtual void tick()
		{
			// no handshake yet
			if (!m_message_index) return;

			time_point now = aux::time_now();
			if (now - seconds(60) < m_last_msg)
			{
#ifdef TORRENT_LOGGING
				m_pc.peer_log("*** PEX [ waiting: %d seconds to next msg ]"
					, total_seconds(seconds(60) - (now - m_last_msg)));
#endif
				return;
			}
			static time_point global_last = min_time();

			int num_peers = m_torrent.num_peers();
			if (num_peers <= 1) return;

			// don't send pex messages more often than 1 every 100 ms, and
			// allow pex messages to be sent 5 seconds apart if there isn't
			// contention
			int delay = (std::min)((std::max)(60000 / num_peers, 100), 3000);

			if (now - milliseconds(delay) < global_last)
			{
#ifdef TORRENT_LOGGING
				m_pc.peer_log("*** PEX [ global-wait: %d ]", total_seconds(milliseconds(delay) - (now - global_last)));
#endif
				return;
			}

			// this will allow us to catch up, even if our timer
			// has lower resolution than delay
			if (global_last == min_time())
				global_last = now;
			else
				global_last += milliseconds(delay);

			m_last_msg = now;

			if (m_first_time)
			{
				send_ut_peer_list();
				m_first_time = false;
			}
			else
			{
				send_ut_peer_diff();
			}
		}

		void send_ut_peer_diff()
		{
			// if there's no change in out peer set, don't send anything
			if (m_tp.peers_in_msg() == 0) return;

			std::vector<char> const& pex_msg = m_tp.get_ut_pex_msg();

			char msg[6];
			char* ptr = msg;

			detail::write_uint32(1 + 1 + pex_msg.size(), ptr);
			detail::write_uint8(bt_peer_connection::msg_extended, ptr);
			detail::write_uint8(m_message_index, ptr);
			m_pc.send_buffer(msg, sizeof(msg));
			m_pc.send_buffer(&pex_msg[0], pex_msg.size());

			m_pc.stats_counters().inc_stats_counter(counters::num_outgoing_extended);
			m_pc.stats_counters().inc_stats_counter(counters::num_outgoing_pex);

#ifdef TORRENT_LOGGING
			bdecode_node m;
			error_code ec;
			int ret = bdecode(&pex_msg[0], &pex_msg[0] + pex_msg.size(), m, ec);
			TORRENT_ASSERT(ret == 0);
			TORRENT_ASSERT(!ec);
			int num_dropped = 0;
			int num_added = 0;
			bdecode_node e = m.dict_find_string("added");
			if (e) num_added += e.string_length() / 6;
			e = m.dict_find_string("dropped");
			if (e) num_dropped += e.string_length() / 6;
			e = m.dict_find_string("added6");
			if (e) num_added += e.string_length() / 18;
			e = m.dict_find_string("dropped6");
			if (e) num_dropped += e.string_length() / 18;
			m_pc.peer_log("==> PEX_DIFF [ dropped: %d added: %d msg_size: %d ]"
				, num_dropped, num_added, int(pex_msg.size()));
#endif
		}

		void send_ut_peer_list()
		{
			entry pex;
			// leave the dropped string empty
			pex["dropped"].string();
			std::string& pla = pex["added"].string();
			std::string& plf = pex["added.f"].string();
			std::back_insert_iterator<std::string> pla_out(pla);
			std::back_insert_iterator<std::string> plf_out(plf);

#if TORRENT_USE_IPV6
			pex["dropped6"].string();
			std::string& pla6 = pex["added6"].string();
			std::string& plf6 = pex["added6.f"].string();
			std::back_insert_iterator<std::string> pla6_out(pla6);
			std::back_insert_iterator<std::string> plf6_out(plf6);
#endif

			int num_added = 0;
			for (torrent::peer_iterator i = m_torrent.begin()
				, end(m_torrent.end()); i != end; ++i)
			{
				peer_connection* peer = *i;
				if (!send_peer(*peer)) continue;

				// don't write too big of a package
				if (num_added >= max_peer_entries) break;

				// only send proper bittorrent peers
				if (peer->type() != peer_connection::bittorrent_connection)
					continue;

				bt_peer_connection* p = static_cast<bt_peer_connection*>(peer);

				// no supported flags to set yet
				// 0x01 - peer supports encryption
				// 0x02 - peer is a seed
				// 0x04 - supports uTP. This is only a positive flags
				//        passing 0 doesn't mean the peer doesn't
				//        support uTP
				// 0x08 - supports holepunching protocol. If this
				//        flag is received from a peer, it can be
				//        used as a rendezvous point in case direct
				//        connections to the peer fail
				int flags = p->is_seed() ? 2 : 0;
#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
				flags |= p->supports_encryption() ? 1 : 0;
#endif
				flags |= is_utp(*p->get_socket()) ? 4 :  0;
				flags |= p->supports_holepunch() ? 8 : 0;

				tcp::endpoint remote = peer->remote();

				torrent_peer *pi = 0;
				if (!p->is_outgoing() && (pi = peer->peer_info_struct()) && pi->port > 0)
					remote.port(pi->port);

				// i->first was added since the last time
				if (remote.address().is_v4())
				{
					detail::write_endpoint(remote, pla_out);
					detail::write_uint8(flags, plf_out);
				}
#if TORRENT_USE_IPV6
				else
				{
					detail::write_endpoint(remote, pla6_out);
					detail::write_uint8(flags, plf6_out);
				}
#endif
				++num_added;
			}
			std::vector<char> pex_msg;
			bencode(std::back_inserter(pex_msg), pex);

			char msg[6];
			char* ptr = msg;

			detail::write_uint32(1 + 1 + pex_msg.size(), ptr);
			detail::write_uint8(bt_peer_connection::msg_extended, ptr);
			detail::write_uint8(m_message_index, ptr);
			m_pc.send_buffer(msg, sizeof(msg));
			m_pc.send_buffer(&pex_msg[0], pex_msg.size());

			m_pc.stats_counters().inc_stats_counter(counters::num_outgoing_extended);
			m_pc.stats_counters().inc_stats_counter(counters::num_outgoing_pex);

#ifdef TORRENT_LOGGING
			m_pc.peer_log("==> PEX_FULL [ added: %d msg_size: %d ]", num_added, int(pex_msg.size()));
#endif
		}

		torrent& m_torrent;
		peer_connection& m_pc;
		ut_pex_plugin& m_tp;
		// stores all peers this this peer is connected to. These lists
		// are updated with each pex message and are limited in size
		// to protect against malicious clients. These lists are also
		// used for looking up which peer a peer that supports holepunch
		// came from.
		// these are vectors to save memory and keep the items close
		// together for performance. Inserting and removing is relatively
		// cheap since the lists' size is limited
		typedef std::vector<std::pair<address_v4::bytes_type, boost::uint16_t> > peers4_t;
		peers4_t m_peers;
#if TORRENT_USE_IPV6
		typedef std::vector<std::pair<address_v6::bytes_type, boost::uint16_t> > peers6_t;
		peers6_t m_peers6;
#endif
		// the last pex messages we received
		// [0] is the oldest one. There is a problem with
		// rate limited connections, because we may sit
		// for a long time, accumulating pex messages, and
		// then once we read from the socket it will look like
		// we received them all back to back. That's why
		// we look at 6 pex messages back.
		time_point m_last_pex[6];

		time_point m_last_msg;
		int m_message_index;

		// this is initialized to true, and set to
		// false after the first pex message has been sent.
		// it is used to know if a diff message or a) ful
		// message should be sent.
		bool m_first_time;
	};

	boost::shared_ptr<peer_plugin> ut_pex_plugin::new_connection(peer_connection* pc)
	{
		if (pc->type() != peer_connection::bittorrent_connection)
			return boost::shared_ptr<peer_plugin>();

		return boost::shared_ptr<peer_plugin>(new ut_pex_peer_plugin(m_torrent
			, *pc, *this));
	}
} }

namespace libtorrent
{
	boost::shared_ptr<torrent_plugin> create_ut_pex_plugin(torrent* t, void*)
	{
		if (t->torrent_file().priv() || (t->torrent_file().is_i2p()
			&& !t->settings().get_bool(settings_pack::allow_i2p_mixed)))
		{
			return boost::shared_ptr<torrent_plugin>();
		}
		return boost::shared_ptr<torrent_plugin>(new ut_pex_plugin(*t));
	}

	bool was_introduced_by(peer_plugin const* pp, tcp::endpoint const& ep)
	{
		ut_pex_peer_plugin* p = (ut_pex_peer_plugin*)pp;
#if TORRENT_USE_IPV6
		if (ep.address().is_v4())
		{
#endif
			ut_pex_peer_plugin::peers4_t::value_type v(ep.address().to_v4().to_bytes(), ep.port());
			ut_pex_peer_plugin::peers4_t::const_iterator i
				= std::lower_bound(p->m_peers.begin(), p->m_peers.end(), v);
			return i != p->m_peers.end() && *i == v;
#if TORRENT_USE_IPV6
		}
		else
		{
			ut_pex_peer_plugin::peers6_t::value_type v(ep.address().to_v6().to_bytes(), ep.port());
			ut_pex_peer_plugin::peers6_t::iterator i
				= std::lower_bound(p->m_peers6.begin(), p->m_peers6.end(), v);
			return i != p->m_peers6.end() && *i == v;
		}
#endif
	}
}

#endif

