/*

Copyright (c) 2006-2018, MassaRoddel, Arvid Norberg
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

#include "libtorrent/config.hpp"
#include "libtorrent/bt_peer_connection.hpp"
#include "libtorrent/peer_connection_handle.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/extensions.hpp"
#include "libtorrent/broadcast_socket.hpp"
#include "libtorrent/socket_io.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/aux_/socket_type.hpp" // for is_utp
#include "libtorrent/performance_counters.hpp" // for counters
#include "libtorrent/extensions/ut_pex.hpp"
#include "libtorrent/aux_/time.hpp"

#ifndef TORRENT_DISABLE_EXTENSIONS

namespace libtorrent { namespace {

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

	struct ut_pex_plugin final
		: torrent_plugin
	{
		// randomize when we rebuild the pex message
		// to evenly spread it out across all torrents
		// the more torrents we have, the longer we can
		// delay the rebuilding
		explicit ut_pex_plugin(torrent& t)
			: m_torrent(t)
			, m_last_msg(min_time())
			, m_peers_in_message(0) {}

		std::shared_ptr<peer_plugin> new_connection(
			peer_connection_handle const& pc) override;

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
		void tick() override
		{
			if (m_torrent.flags() & torrent_flags::disable_pex) return;

			time_point const now = aux::time_now();
			if (now - seconds(60) < m_last_msg) return;
			m_last_msg = now;

			if (m_torrent.num_peers() == 0) return;

			entry pex;
			std::string& pla = pex["added"].string();
			std::string& pld = pex["dropped"].string();
			std::string& plf = pex["added.f"].string();
			std::back_insert_iterator<std::string> pla_out(pla);
			std::back_insert_iterator<std::string> pld_out(pld);
			std::back_insert_iterator<std::string> plf_out(plf);
			std::string& pla6 = pex["added6"].string();
			std::string& pld6 = pex["dropped6"].string();
			std::string& plf6 = pex["added6.f"].string();
			std::back_insert_iterator<std::string> pla6_out(pla6);
			std::back_insert_iterator<std::string> pld6_out(pld6);
			std::back_insert_iterator<std::string> plf6_out(plf6);

			std::set<tcp::endpoint> dropped;
			m_old_peers.swap(dropped);

			m_peers_in_message = 0;
			int num_added = 0;
			for (auto const peer : m_torrent)
			{
				if (!send_peer(*peer)) continue;

				tcp::endpoint remote = peer->remote();
				m_old_peers.insert(remote);

				auto const di = dropped.find(remote);
				if (di == dropped.end())
				{
					// don't write too big of a package
					if (num_added >= max_peer_entries) break;

					// only send proper bittorrent peers
					if (peer->type() != connection_type::bittorrent)
						continue;

					bt_peer_connection* p = static_cast<bt_peer_connection*>(peer);

					// if the peer has told us which port its listening on,
					// use that port. But only if we didn't connect to the peer.
					// if we connected to it, use the port we know works
					if (!p->is_outgoing())
					{
						torrent_peer const* const pi = peer->peer_info_struct();
						if (pi != nullptr && pi->port > 0)
							remote.port(pi->port);
					}

					// no supported flags to set yet
					// 0x01 - peer supports encryption
					// 0x02 - peer is a seed
					// 0x04 - supports uTP. This is only a positive flags
					//        passing 0 doesn't mean the peer doesn't
					//        support uTP
					// 0x08 - supports hole punching protocol. If this
					//        flag is received from a peer, it can be
					//        used as a rendezvous point in case direct
					//        connections to the peer fail
					pex_flags_t flags = p->is_seed() ? pex_seed : pex_flags_t{};
#if !defined TORRENT_DISABLE_ENCRYPTION
					flags |= p->supports_encryption() ? pex_encryption : pex_flags_t{};
#endif
					flags |= is_utp(*p->get_socket()) ? pex_utp : pex_flags_t{};
					flags |= p->supports_holepunch() ? pex_holepunch : pex_flags_t{};

					// i->first was added since the last time
					if (is_v4(remote))
					{
						detail::write_endpoint(remote, pla_out);
						detail::write_uint8(static_cast<std::uint8_t>(flags), plf_out);
					}
					else
					{
						detail::write_endpoint(remote, pla6_out);
						detail::write_uint8(static_cast<std::uint8_t>(flags), plf6_out);
					}
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

			for (auto const& i : dropped)
			{
				if (is_v4(i))
					detail::write_endpoint(i, pld_out);
				else
					detail::write_endpoint(i, pld6_out);
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

		// explicitly disallow assignment, to silence msvc warning
		ut_pex_plugin& operator=(ut_pex_plugin const&) = delete;
	};

	struct ut_pex_peer_plugin final
		: ut_pex_peer_store, peer_plugin
	{
		ut_pex_peer_plugin(torrent& t, peer_connection& pc, ut_pex_plugin& tp)
			: m_torrent(t)
			, m_pc(pc)
			, m_tp(tp)
			, m_last_msg(min_time())
			, m_message_index(0)
			, m_first_time(true)
		{
			const int num_pex_timers = sizeof(m_last_pex) / sizeof(m_last_pex[0]);
			for (int i = 0; i < num_pex_timers; ++i)
			{
				m_last_pex[i] = min_time();
			}
		}

		void add_handshake(entry& h) override
		{
			entry& messages = h["m"];
			messages[extension_name] = extension_index;
		}

		bool on_extension_handshake(bdecode_node const& h) override
		{
			m_message_index = 0;
			if (h.type() != bdecode_node::dict_t) return false;
			bdecode_node const messages = h.dict_find_dict("m");
			if (!messages) return false;

			int const index = int(messages.dict_find_int_value(extension_name, -1));
			if (index == -1) return false;
			m_message_index = index;
			return true;
		}

		bool on_extended(int const length, int const msg, span<char const> body) override
		{
			if (msg != extension_index) return false;
			if (m_message_index == 0) return false;

			if (m_torrent.flags() & torrent_flags::disable_pex) return true;

			if (length > 500 * 1024)
			{
				m_pc.disconnect(errors::pex_message_too_large, operation_t::bittorrent, peer_connection_interface::peer_error);
				return true;
			}

			if (int(body.size()) < length) return true;

			time_point const now = aux::time_now();
			if (now - seconds(60) <  m_last_pex[0])
			{
				// this client appears to be trying to flood us
				// with pex messages. Don't allow that.
				m_pc.disconnect(errors::too_frequent_pex, operation_t::bittorrent);
				return true;
			}

			int const num_pex_timers = sizeof(m_last_pex) / sizeof(m_last_pex[0]);
			for (int i = 0; i < num_pex_timers - 1; ++i)
				m_last_pex[i] = m_last_pex[i + 1];
			m_last_pex[num_pex_timers - 1] = now;

			bdecode_node pex_msg;
			error_code ec;
			int const ret = bdecode(body.begin(), body.end(), pex_msg, ec);
			if (ret != 0 || pex_msg.type() != bdecode_node::dict_t)
			{
				m_pc.disconnect(errors::invalid_pex_message, operation_t::bittorrent, peer_connection_interface::peer_error);
				return true;
			}

			bdecode_node p = pex_msg.dict_find_string("dropped");

#ifndef TORRENT_DISABLE_LOGGING
			int num_dropped = 0;
			int num_added = 0;
			if (p) num_dropped += p.string_length() / 6;
#endif
			if (p)
			{
				int const num_peers = p.string_length() / 6;
				char const* in = p.string_ptr();

				for (int i = 0; i < num_peers; ++i)
				{
					tcp::endpoint const adr = detail::read_v4_endpoint<tcp::endpoint>(in);
					peers4_t::value_type const v(adr.address().to_v4().to_bytes(), adr.port());
					auto const j = std::lower_bound(m_peers.begin(), m_peers.end(), v);
					if (j != m_peers.end() && *j == v) m_peers.erase(j);
				}
			}

			p = pex_msg.dict_find_string("added");
			bdecode_node const pf = pex_msg.dict_find_string("added.f");

			bool peers_added = false;
#ifndef TORRENT_DISABLE_LOGGING
			if (p) num_added += p.string_length() / 6;
#endif
			if (p && pf && pf.string_length() == p.string_length() / 6)
			{
				int const num_peers = pf.string_length();
				char const* in = p.string_ptr();
				char const* fin = pf.string_ptr();

				for (int i = 0; i < num_peers; ++i)
				{
					tcp::endpoint const adr = detail::read_v4_endpoint<tcp::endpoint>(in);
					pex_flags_t const flags(static_cast<std::uint8_t>(*fin++));

					if (int(m_peers.size()) >= m_torrent.settings().get_int(settings_pack::max_pex_peers))
						break;

					// ignore local addresses unless the peer is local to us
					if (is_local(adr.address()) && !is_local(m_pc.remote().address())) continue;

					peers4_t::value_type const v(adr.address().to_v4().to_bytes(), adr.port());
					auto const j = std::lower_bound(m_peers.begin(), m_peers.end(), v);
					// do we already know about this peer?
					if (j != m_peers.end() && *j == v) continue;
					m_peers.insert(j, v);
					m_torrent.add_peer(adr, peer_info::pex, flags);
					peers_added = true;
				}
			}

			bdecode_node p6 = pex_msg.dict_find_string("dropped6");
			if (p6)
			{
#ifndef TORRENT_DISABLE_LOGGING
				num_dropped += p6.string_length() / 18;
#endif
				int const num_peers = p6.string_length() / 18;
				char const* in = p6.string_ptr();

				for (int i = 0; i < num_peers; ++i)
				{
					tcp::endpoint const adr = detail::read_v6_endpoint<tcp::endpoint>(in);
					peers6_t::value_type const v(adr.address().to_v6().to_bytes(), adr.port());
					auto const j = std::lower_bound(m_peers6.begin(), m_peers6.end(), v);
					if (j != m_peers6.end() && *j == v) m_peers6.erase(j);
				}
			}

			p6 = pex_msg.dict_find_string("added6");
#ifndef TORRENT_DISABLE_LOGGING
			if (p6) num_added += p6.string_length() / 18;
#endif
			bdecode_node const p6f = pex_msg.dict_find_string("added6.f");
			if (p6 && p6f && p6f.string_length() == p6.string_length() / 18)
			{
				int const num_peers = p6f.string_length();
				char const* in = p6.string_ptr();
				char const* fin = p6f.string_ptr();

				for (int i = 0; i < num_peers; ++i)
				{
					tcp::endpoint const adr = detail::read_v6_endpoint<tcp::endpoint>(in);
					pex_flags_t const flags(static_cast<std::uint8_t>(*fin++));
					// ignore local addresses unless the peer is local to us
					if (is_local(adr.address()) && !is_local(m_pc.remote().address())) continue;
					if (int(m_peers6.size()) >= m_torrent.settings().get_int(settings_pack::max_pex_peers))
						break;

					peers6_t::value_type const v(adr.address().to_v6().to_bytes(), adr.port());
					auto const j = std::lower_bound(m_peers6.begin(), m_peers6.end(), v);
					// do we already know about this peer?
					if (j != m_peers6.end() && *j == v) continue;
					m_peers6.insert(j, v);
					m_torrent.add_peer(adr, peer_info::pex, flags);
					peers_added = true;
				}
			}
#ifndef TORRENT_DISABLE_LOGGING
			m_pc.peer_log(peer_log_alert::incoming_message, "PEX", "dropped: %d added: %d"
				, num_dropped, num_added);
#endif

			m_pc.stats_counters().inc_stats_counter(counters::num_incoming_pex);

			if (peers_added) m_torrent.do_connect_boost();
			return true;
		}

		// the peers second tick
		// every minute we send a pex message
		void tick() override
		{
			// no handshake yet
			if (!m_message_index) return;

			time_point const now = aux::time_now();
			if (now - seconds(60) < m_last_msg)
			{
#ifndef TORRENT_DISABLE_LOGGING
//				m_pc.peer_log(peer_log_alert::info, "PEX", "waiting: %d seconds to next msg"
//					, int(total_seconds(seconds(60) - (now - m_last_msg))));
#endif
				return;
			}
			static time_point global_last = min_time();

			int const num_peers = m_torrent.num_peers();
			if (num_peers <= 1) return;

			// don't send pex messages more often than 1 every 100 ms, and
			// allow pex messages to be sent 5 seconds apart if there isn't
			// contention
			int const delay = std::min(std::max(60000 / num_peers, 100), 3000);

			if (now - milliseconds(delay) < global_last)
			{
#ifndef TORRENT_DISABLE_LOGGING
//				m_pc.peer_log(peer_log_alert::info, "PEX", "global-wait: %d"
//					, int(total_seconds(milliseconds(delay) - (now - global_last))));
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
			if (m_torrent.flags() & torrent_flags::disable_pex) return;

			// if there's no change in out peer set, don't send anything
			if (m_tp.peers_in_msg() == 0) return;

			std::vector<char> const& pex_msg = m_tp.get_ut_pex_msg();

			char msg[6];
			char* ptr = msg;

			detail::write_uint32(1 + 1 + int(pex_msg.size()), ptr);
			detail::write_uint8(bt_peer_connection::msg_extended, ptr);
			detail::write_uint8(m_message_index, ptr);
			m_pc.send_buffer(msg);
			m_pc.send_buffer(pex_msg);

			m_pc.stats_counters().inc_stats_counter(counters::num_outgoing_extended);
			m_pc.stats_counters().inc_stats_counter(counters::num_outgoing_pex);

#ifndef TORRENT_DISABLE_LOGGING
			if (m_pc.should_log(peer_log_alert::outgoing_message))
			{
				bdecode_node m;
				error_code ec;
				int const ret = bdecode(&pex_msg[0], &pex_msg[0] + pex_msg.size(), m, ec);
				TORRENT_ASSERT(ret == 0);
				TORRENT_ASSERT(!ec);
				TORRENT_UNUSED(ret);
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
				m_pc.peer_log(peer_log_alert::outgoing_message, "PEX_DIFF", "dropped: %d added: %d msg_size: %d"
					, num_dropped, num_added, int(pex_msg.size()));
			}
#endif
		}

		void send_ut_peer_list()
		{
			if (m_torrent.flags() & torrent_flags::disable_pex) return;

			entry pex;
			// leave the dropped string empty
			pex["dropped"].string();
			std::string& pla = pex["added"].string();
			std::string& plf = pex["added.f"].string();
			std::back_insert_iterator<std::string> pla_out(pla);
			std::back_insert_iterator<std::string> plf_out(plf);

			pex["dropped6"].string();
			std::string& pla6 = pex["added6"].string();
			std::string& plf6 = pex["added6.f"].string();
			std::back_insert_iterator<std::string> pla6_out(pla6);
			std::back_insert_iterator<std::string> plf6_out(plf6);

			int num_added = 0;
			for (auto const peer : m_torrent)
			{
				if (!send_peer(*peer)) continue;

				// don't write too big of a package
				if (num_added >= max_peer_entries) break;

				// only send proper bittorrent peers
				if (peer->type() != connection_type::bittorrent)
					continue;

				bt_peer_connection* p = static_cast<bt_peer_connection*>(peer);

				// no supported flags to set yet
				// 0x01 - peer supports encryption
				// 0x02 - peer is a seed
				// 0x04 - supports uTP. This is only a positive flags
				//        passing 0 doesn't mean the peer doesn't
				//        support uTP
				// 0x08 - supports hole punching protocol. If this
				//        flag is received from a peer, it can be
				//        used as a rendezvous point in case direct
				//        connections to the peer fail
				int flags = p->is_seed() ? 2 : 0;
#if !defined TORRENT_DISABLE_ENCRYPTION
				flags |= p->supports_encryption() ? 1 : 0;
#endif
				flags |= is_utp(*p->get_socket()) ? 4 :  0;
				flags |= p->supports_holepunch() ? 8 : 0;

				tcp::endpoint remote = peer->remote();

				if (!p->is_outgoing())
				{
					torrent_peer const* const pi = peer->peer_info_struct();
					if (pi != nullptr && pi->port > 0)
						remote.port(pi->port);
				}

				// i->first was added since the last time
				if (is_v4(remote))
				{
					detail::write_endpoint(remote, pla_out);
					detail::write_uint8(flags, plf_out);
				}
				else
				{
					detail::write_endpoint(remote, pla6_out);
					detail::write_uint8(flags, plf6_out);
				}
				++num_added;
			}
			std::vector<char> pex_msg;
			bencode(std::back_inserter(pex_msg), pex);

			char msg[6];
			char* ptr = msg;

			detail::write_uint32(1 + 1 + int(pex_msg.size()), ptr);
			detail::write_uint8(bt_peer_connection::msg_extended, ptr);
			detail::write_uint8(m_message_index, ptr);
			m_pc.send_buffer(msg);
			m_pc.send_buffer(pex_msg);

			m_pc.stats_counters().inc_stats_counter(counters::num_outgoing_extended);
			m_pc.stats_counters().inc_stats_counter(counters::num_outgoing_pex);

#ifndef TORRENT_DISABLE_LOGGING
			m_pc.peer_log(peer_log_alert::outgoing_message, "PEX_FULL"
				, "added: %d msg_size: %d", num_added, int(pex_msg.size()));
#endif
		}

		torrent& m_torrent;
		peer_connection& m_pc;
		ut_pex_plugin& m_tp;

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

		// explicitly disallow assignment, to silence msvc warning
		ut_pex_peer_plugin& operator=(ut_pex_peer_plugin const&) = delete;
	};

	std::shared_ptr<peer_plugin> ut_pex_plugin::new_connection(peer_connection_handle const& pc)
	{
		if (pc.type() != connection_type::bittorrent) return {};

		bt_peer_connection* c = static_cast<bt_peer_connection*>(pc.native_handle().get());
		auto p = std::make_shared<ut_pex_peer_plugin>(m_torrent, *c, *this);
		c->set_ut_pex(p);
		return p;
	}
} }

namespace libtorrent {

	std::shared_ptr<torrent_plugin> create_ut_pex_plugin(torrent_handle const& th, void*)
	{
		torrent* t = th.native_handle().get();
		if (t->torrent_file().priv() || (t->torrent_file().is_i2p()
			&& !t->settings().get_bool(settings_pack::allow_i2p_mixed)))
		{
			return {};
		}
		return std::make_shared<ut_pex_plugin>(*t);
	}
}

#endif
