/*

Copyright (c) 2006, MassaRoddel, Arvid Norberg
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

#include "libtorrent/extensions/ut_pex.hpp"

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
		// don't send out peers that we haven't connected to
		// (that have connected to us)
		if (!p.is_local()) return false;
		// don't send out peers that we haven't successfully connected to
		if (p.is_connecting()) return false;
		return true;
	}

	struct ut_pex_plugin: torrent_plugin
	{
		ut_pex_plugin(torrent& t): m_torrent(t), m_1_minute(55), m_peers_in_message(0) {}
	
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
			if (++m_1_minute < 60) return;

			m_1_minute = 0;

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

				tcp::endpoint const& remote = peer->remote();
				m_old_peers.insert(remote);

				std::set<tcp::endpoint>::iterator di = dropped.find(remote);
				if (di == dropped.end())
				{
					// don't write too big of a package
					if (num_added >= max_peer_entries) break;

					// only send proper bittorrent peers
					bt_peer_connection* p = dynamic_cast<bt_peer_connection*>(peer);
					if (!p) continue;

					// no supported flags to set yet
					// 0x01 - peer supports encryption
					// 0x02 - peer is a seed
					int flags = p->is_seed() ? 2 : 0;
#ifndef TORRENT_DISABLE_ENCRYPTION
					flags |= p->supports_encryption() ? 1 : 0;
#endif
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
		int m_1_minute;
		std::vector<char> m_ut_pex_msg;
		int m_peers_in_message;
	};


	struct ut_pex_peer_plugin : peer_plugin
	{	
		ut_pex_peer_plugin(torrent& t, peer_connection& pc, ut_pex_plugin& tp)
			: m_torrent(t)
			, m_pc(pc)
			, m_tp(tp)
			, m_1_minute(55)
			, m_message_index(0)
			, m_first_time(true)
		{}

		virtual void add_handshake(entry& h)
		{
			entry& messages = h["m"];
			messages[extension_name] = extension_index;
		}

		virtual bool on_extension_handshake(lazy_entry const& h)
		{
			m_message_index = 0;
			if (h.type() != lazy_entry::dict_t) return false;
			lazy_entry const* messages = h.dict_find("m");
			if (!messages || messages->type() != lazy_entry::dict_t) return false;

			int index = messages->dict_find_int_value(extension_name, -1);
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
				m_pc.disconnect(errors::pex_message_too_large, 2);
				return true;
			}

			if (body.left() < length) return true;

			lazy_entry pex_msg;
			int ret = lazy_bdecode(body.begin, body.end, pex_msg);
			if (ret != 0 || pex_msg.type() != lazy_entry::dict_t)
			{
				m_pc.disconnect(errors::invalid_pex_message, 2);
				return true;
			}

			lazy_entry const* p = pex_msg.dict_find("added");
			lazy_entry const* pf = pex_msg.dict_find("added.f");

			if (p != 0
				&& pf != 0
				&& p->type() == lazy_entry::string_t
				&& pf->type() == lazy_entry::string_t
				&& pf->string_length() == p->string_length() / 6)
			{
				int num_peers = pf->string_length();
				char const* in = p->string_ptr();
				char const* fin = pf->string_ptr();

				peer_id pid(0);
				policy& p = m_torrent.get_policy();
				for (int i = 0; i < num_peers; ++i)
				{
					tcp::endpoint adr = detail::read_v4_endpoint<tcp::endpoint>(in);
					char flags = *fin++;
					// ignore local addresses unless the peer is local to us
					if (is_local(adr.address()) && !is_local(m_pc.remote().address())) continue;
					p.add_peer(adr, pid, peer_info::pex, flags);
				} 
			}

#if TORRENT_USE_IPV6
			lazy_entry const* p6 = pex_msg.dict_find("added6");
			lazy_entry const* p6f = pex_msg.dict_find("added6.f");
			if (p6 != 0
				&& p6f != 0
				&& p6->type() == lazy_entry::string_t
				&& p6f->type() == lazy_entry::string_t
				&& p6f->string_length() == p6->string_length() / 18)
			{
				int num_peers = p6f->string_length();
				char const* in = p6->string_ptr();
				char const* fin = p6f->string_ptr();

				peer_id pid(0);
				policy& p = m_torrent.get_policy();
				for (int i = 0; i < num_peers; ++i)
				{
					tcp::endpoint adr = detail::read_v6_endpoint<tcp::endpoint>(in);
					char flags = *fin++;
					// ignore local addresses unless the peer is local to us
					if (is_local(adr.address()) && !is_local(m_pc.remote().address())) continue;
					p.add_peer(adr, pid, peer_info::pex, flags);
				} 
			}
#endif
			return true;
		}

		// the peers second tick
		// every minute we send a pex message
		virtual void tick()
		{
			if (!m_message_index) return;	// no handshake yet
			if (++m_1_minute <= 60) return;

			if (m_first_time)
			{
				send_ut_peer_list();
				m_first_time = false;
			}
			else
			{
				send_ut_peer_diff();
			}
			m_1_minute = 0;
		}

	private:

		void send_ut_peer_diff()
		{
			// if there's no change in out peer set, don't send anything
			if (m_tp.peers_in_msg() == 0) return;

			std::vector<char> const& pex_msg = m_tp.get_ut_pex_msg();

			buffer::interval i = m_pc.allocate_send_buffer(6 + pex_msg.size());

			detail::write_uint32(1 + 1 + pex_msg.size(), i.begin);
			detail::write_uint8(bt_peer_connection::msg_extended, i.begin);
			detail::write_uint8(m_message_index, i.begin);
			std::copy(pex_msg.begin(), pex_msg.end(), i.begin);
			i.begin += pex_msg.size();

			TORRENT_ASSERT(i.begin == i.end);
			m_pc.setup_send();
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
				bt_peer_connection* p = dynamic_cast<bt_peer_connection*>(peer);
				if (!p) continue;

				// no supported flags to set yet
				// 0x01 - peer supports encryption
				// 0x02 - peer is a seed
				int flags = p->is_seed() ? 2 : 0;
#ifndef TORRENT_DISABLE_ENCRYPTION
				flags |= p->supports_encryption() ? 1 : 0;
#endif
				tcp::endpoint const& remote = peer->remote();
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

			buffer::interval i = m_pc.allocate_send_buffer(6 + pex_msg.size());

			detail::write_uint32(1 + 1 + pex_msg.size(), i.begin);
			detail::write_uint8(bt_peer_connection::msg_extended, i.begin);
			detail::write_uint8(m_message_index, i.begin);
			std::copy(pex_msg.begin(), pex_msg.end(), i.begin);
			i.begin += pex_msg.size();

			TORRENT_ASSERT(i.begin == i.end);
			m_pc.setup_send();
		}

		torrent& m_torrent;
		peer_connection& m_pc;
		ut_pex_plugin& m_tp;
		int m_1_minute;
		int m_message_index;

		// this is initialized to true, and set to
		// false after the first pex message has been sent.
		// it is used to know if a diff message or a full
		// message should be sent.
		bool m_first_time;
	};

	boost::shared_ptr<peer_plugin> ut_pex_plugin::new_connection(peer_connection* pc)
	{
		bt_peer_connection* c = dynamic_cast<bt_peer_connection*>(pc);
		if (!c) return boost::shared_ptr<peer_plugin>();
		return boost::shared_ptr<peer_plugin>(new ut_pex_peer_plugin(m_torrent
			, *pc, *this));
	}
}}

namespace libtorrent
{

	boost::shared_ptr<torrent_plugin> create_ut_pex_plugin(torrent* t, void*)
	{
		if (t->torrent_file().priv())
		{
			return boost::shared_ptr<torrent_plugin>();
		}
		return boost::shared_ptr<torrent_plugin>(new ut_pex_plugin(*t));
	}

}


