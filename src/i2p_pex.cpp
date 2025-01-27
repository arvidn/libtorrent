/*

Copyright (c) 2025, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/config.hpp"

#ifndef TORRENT_DISABLE_EXTENSIONS
#if TORRENT_USE_I2P

#include "libtorrent/aux_/peer_connection.hpp"
#include "libtorrent/aux_/bt_peer_connection.hpp"
#include "libtorrent/aux_/array.hpp"
#include "libtorrent/peer_connection_handle.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/aux_/torrent.hpp"
#include "libtorrent/extensions.hpp"
#include "libtorrent/aux_/socket_io.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/aux_/socket_type.hpp" // for is_utp
#include "libtorrent/performance_counters.hpp" // for counters
#include "libtorrent/extensions/i2p_pex.hpp"
#include "libtorrent/aux_/time.hpp"

namespace libtorrent { namespace {

	const char extension_name[] = "i2p_pex";

	enum
	{
		extension_index = 9,
		max_peer_entries = 50
	};

	bool include_peer(aux::peer_connection const& p)
	{
		// don't send out those peers that we haven't connected to
		// (that have connected to us) and that aren't sharing their
		// listening port
		if (!p.is_outgoing() && !p.received_listen_port()) return false;
		// don't send out peers that we haven't successfully connected to
		if (p.is_connecting()) return false;
		if (p.in_handshake()) return false;
		// filter non-i2p peers. We may have them if we allow mixed-mode
		if (!is_i2p(p.get_socket())) return false;
		return true;
	}

	struct i2p_pex_plugin final
		: torrent_plugin
	{
		// randomize when we rebuild the pex message
		// to evenly spread it out across all torrents
		// the more torrents we have, the longer we can
		// delay the rebuilding
		explicit i2p_pex_plugin(aux::torrent& t)
			: m_torrent(t)
			, m_last_msg(min_time()) {}

		// explicitly disallow assignment, to silence msvc warning
		i2p_pex_plugin& operator=(i2p_pex_plugin const&) = delete;

		std::shared_ptr<peer_plugin> new_connection(
			peer_connection_handle const& pc) override;

		std::vector<char>& get_i2p_pex_msg()
		{
			return m_i2p_pex_msg;
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

			std::set<sha256_hash> dropped;
			m_old_peers.swap(dropped);

			m_peers_in_message = 0;
			int num_added = 0;
			for (auto const* peer : m_torrent)
			{
				if (!include_peer(*peer)) continue;

				auto const* pi = peer->peer_info_struct();
				if (pi == nullptr) continue;
				sha256_hash const remote = hasher256(base64decode_i2p(pi->dest())).final();
				m_old_peers.insert(remote);

				auto const di = dropped.find(remote);
				if (di == dropped.end())
				{
					// don't write too big of a package
					if (num_added >= max_peer_entries) break;

					// i->first was added since the last time
					std::copy(remote.begin(), remote.end(), pla_out);
					// none of the normal ut_pex flags apply to i2p peers, so we
					// just send 0
					aux::write_uint8(0, plf_out);
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
				std::copy(i.begin(), i.end(), pld_out);
				++m_peers_in_message;
			}

			m_i2p_pex_msg.clear();
			bencode(std::back_inserter(m_i2p_pex_msg), pex);
		}

	private:
		aux::torrent& m_torrent;

		std::set<sha256_hash> m_old_peers;
		time_point m_last_msg;
		std::vector<char> m_i2p_pex_msg;
		int m_peers_in_message = 0;
	};

	struct i2p_pex_peer_plugin final
		: peer_plugin
	{
		i2p_pex_peer_plugin(aux::torrent& t, aux::peer_connection& pc, i2p_pex_plugin& tp)
			: m_torrent(t)
			, m_pc(pc)
			, m_tp(tp)
			, m_last_msg(min_time())
		{
			for (auto& e : m_last_pex) {
				e = min_time();
			}
		}

		// explicitly disallow assignment, to silence msvc warning
		i2p_pex_peer_plugin& operator=(i2p_pex_peer_plugin const&) = delete;

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

			std::copy(m_last_pex.begin()+1, m_last_pex.end(), m_last_pex.begin());
			m_last_pex.back() = now;

			bdecode_node pex_msg;
			error_code ec;
			int const ret = bdecode(body.begin(), body.end(), pex_msg, ec);
			if (ret != 0 || pex_msg.type() != bdecode_node::dict_t)
			{
				m_pc.disconnect(errors::invalid_pex_message, operation_t::bittorrent, peer_connection_interface::peer_error);
				return true;
			}

			// we ignore the "dropped" field, because we don't need the
			// ut_pex_peer_store and was_introduced_by() for i2p
			// we also ignore the "added.f" (flags) field, since we don't have
			// any flags that apply to i2p peers (yet).

			bdecode_node p = pex_msg.dict_find_string("added");

			bool peers_added = false;
#ifndef TORRENT_DISABLE_LOGGING
			int num_added = 0;
#endif
			if (p)
			{
				int const num_peers = p.string_length() / 32;
				char const* in = p.string_ptr();

#ifndef TORRENT_DISABLE_LOGGING
				num_added = num_peers;
#endif

				for (int i = 0; i < num_peers; ++i)
				{
					sha256_hash remote;
					std::copy(in, in + 32, remote.begin());
					in += 32;
					m_torrent.add_i2p_peer(remote, peer_info::pex);
					peers_added = true;
				}
			}

#ifndef TORRENT_DISABLE_LOGGING
			m_pc.peer_log(peer_log_alert::incoming_message, "I2P_PEX", "added: %d"
				, num_added);
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
			if (now - seconds(60) < m_last_msg) return;
			int const num_peers = m_torrent.num_peers();
			if (num_peers <= 1) return;

			m_last_msg = now;

			if (m_first_time)
			{
				send_i2p_peer_list();
				m_first_time = false;
			}
			else
			{
				send_i2p_peer_diff();
			}
		}

		void send_i2p_peer_diff()
		{
			if (m_torrent.flags() & torrent_flags::disable_pex) return;

			// if there's no change in our peer set, don't send anything
			if (m_tp.peers_in_msg() == 0) return;

			std::vector<char> const& pex_msg = m_tp.get_i2p_pex_msg();

			char msg[6];
			char* ptr = msg;

			aux::write_uint32(1 + 1 + int(pex_msg.size()), ptr);
			aux::write_uint8(aux::bt_peer_connection::msg_extended, ptr);
			aux::write_uint8(m_message_index, ptr);
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
				m_pc.peer_log(peer_log_alert::outgoing_message, "I2P_PEX_DIFF", "dropped: %d added: %d msg_size: %d"
					, num_dropped, num_added, int(pex_msg.size()));
			}
#endif
		}

		void send_i2p_peer_list()
		{
			if (m_torrent.flags() & torrent_flags::disable_pex) return;

			entry pex;
			// leave the dropped string empty
			pex["dropped"].string();
			std::string& pla = pex["added"].string();
			std::string& plf = pex["added.f"].string();
			std::back_insert_iterator<std::string> pla_out(pla);
			std::back_insert_iterator<std::string> plf_out(plf);

			int num_added = 0;
			for (auto const* peer : m_torrent)
			{
				if (!include_peer(*peer)) continue;
				TORRENT_ASSERT(peer->type() == connection_type::bittorrent);

				// don't write too big of a package
				if (num_added >= max_peer_entries) break;

				auto const* pi = peer->peer_info_struct();
				if (pi == nullptr) continue;
				sha256_hash const remote = hasher256(base64decode_i2p(pi->dest())).final();

				std::copy(remote.begin(), remote.end(), pla_out);
				aux::write_uint8(0, plf_out);
				++num_added;
			}
			std::vector<char> pex_msg;
			bencode(std::back_inserter(pex_msg), pex);

			char msg[6];
			char* ptr = msg;

			aux::write_uint32(1 + 1 + int(pex_msg.size()), ptr);
			aux::write_uint8(aux::bt_peer_connection::msg_extended, ptr);
			aux::write_uint8(m_message_index, ptr);
			m_pc.send_buffer(msg);
			m_pc.send_buffer(pex_msg);

			m_pc.stats_counters().inc_stats_counter(counters::num_outgoing_extended);
			m_pc.stats_counters().inc_stats_counter(counters::num_outgoing_pex);

#ifndef TORRENT_DISABLE_LOGGING
			m_pc.peer_log(peer_log_alert::outgoing_message, "I2P_PEX_FULL"
				, "added: %d msg_size: %d", num_added, int(pex_msg.size()));
#endif
		}

		aux::torrent& m_torrent;
		aux::peer_connection& m_pc;
		i2p_pex_plugin& m_tp;

		// the last pex messages we received
		// [0] is the oldest one. There is a problem with
		// rate limited connections, because we may sit
		// for a long time, accumulating pex messages, and
		// then once we read from the socket it will look like
		// we received them all back to back. That's why
		// we look at 6 pex messages back.
		// TODO: factor this out into "sliding window"? It's shared with ut_pex
		aux::array<time_point, 6> m_last_pex;

		time_point m_last_msg;
		int m_message_index = 0;

		// this is initialized to true, and set to
		// false after the first pex message has been sent.
		// it is used to know if a diff message or a full
		// message should be sent.
		bool m_first_time = true;
	};

	std::shared_ptr<peer_plugin> i2p_pex_plugin::new_connection(peer_connection_handle const& pc)
	{
		if (pc.type() != connection_type::bittorrent) return {};
		aux::bt_peer_connection* c = static_cast<aux::bt_peer_connection*>(pc.native_handle().get());

		// this extension is only for i2p peer connections
		if (!is_i2p(c->get_socket())) return {};

		return std::make_shared<i2p_pex_peer_plugin>(m_torrent, *c, *this);
	}
} }

namespace libtorrent {

	std::shared_ptr<torrent_plugin> create_i2p_pex_plugin(torrent_handle const& th, client_data_t)
	{
		aux::torrent* t = th.native_handle().get();
		// only add extension to i2p torrents
		if (t->torrent_file().priv() || !t->is_i2p())
			return {};
		return std::make_shared<i2p_pex_plugin>(*t);
	}
}

#endif
#endif
