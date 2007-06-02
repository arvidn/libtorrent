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
		// ut pex does not support IPv6
		if (!p.remote().address().is_v4()) return false;
		return true;
	}

	struct ut_pex_plugin: torrent_plugin
	{
		ut_pex_plugin(torrent& t): m_torrent(t), m_1_minute(0) {}
	
		virtual boost::shared_ptr<peer_plugin> new_connection(peer_connection* pc);

		std::vector<char>& get_ut_pex_msg()
		{
			return m_ut_pex_msg;
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

			std::set<tcp::endpoint> dropped;
			m_old_peers.swap(dropped);

			int num_added = 0;
			for (torrent::peer_iterator i = m_torrent.begin()
				, end(m_torrent.end()); i != end; ++i)
			{
				if (!send_peer(*i->second)) continue;

				m_old_peers.insert(i->first);

				std::set<tcp::endpoint>::iterator di = dropped.find(i->first);
				if (di == dropped.end())
				{
					// don't write too big of a package
					if (num_added >= max_peer_entries) continue;

					// i->first was added since the last time
					detail::write_endpoint(i->first, pla_out);
					// no supported flags to set yet
					// 0x01 - peer supports encryption
					// 0x02 - peer is a seed
					int flags = i->second->is_seed() ? 2 : 0;
					detail::write_uint8(flags, plf_out);
					++num_added;
				}
				else
				{
					// this was in the previous message
					// so, it wasn't dropped
					dropped.erase(di);
				}
			}

			for (std::set<tcp::endpoint>::const_iterator i = dropped.begin()
				, end(dropped.end());i != end; ++i)
			{	
				if (!i->address().is_v4()) continue;
				detail::write_endpoint(*i, pld_out);
			}

			m_ut_pex_msg.clear();
			bencode(std::back_inserter(m_ut_pex_msg), pex);
		}

	private:
		torrent& m_torrent;

		std::set<tcp::endpoint> m_old_peers;
		int m_1_minute;
		std::vector<char> m_ut_pex_msg;
	};


	struct ut_pex_peer_plugin : peer_plugin
	{	
		ut_pex_peer_plugin(torrent& t, peer_connection& pc, ut_pex_plugin& tp)
			: m_torrent(t)
			, m_pc(pc)
			, m_tp(tp)
			, m_1_minute(0)
			, m_message_index(0)
			, m_first_time(true)
		{}

		virtual void add_handshake(entry& h)
		{
			entry& messages = h["m"];
			messages[extension_name] = extension_index;
		}

		virtual bool on_extension_handshake(entry const& h)
		{
			entry const& messages = h["m"];

			if (entry const* index = messages.find_key(extension_name))
			{
				m_message_index = index->integer();
				return true;
			}
			else
			{
				m_message_index = 0;
				return false;
			}
		}

		virtual bool on_extended(int length, int msg, buffer::const_interval body)
			try
		{
			if (msg != extension_index) return false;
			if (m_message_index == 0) return false;

			if (length > 500 * 1024)
				throw protocol_error("ut peer exchange message larger than 500 kB");

			if (body.left() < length) return true;

			entry pex_msg = bdecode(body.begin, body.end);
			std::string const& peers = pex_msg["added"].string();
			std::string const& peer_flags = pex_msg["added.f"].string();

			int num_peers = peers.length() / 6;
			char const* in = peers.c_str();
			char const* fin = peer_flags.c_str();

			if (int(peer_flags.size()) != num_peers)
				return true;

			peer_id pid(0);
			policy& p = m_torrent.get_policy();
			for (int i = 0; i < num_peers; ++i)
			{
				tcp::endpoint adr = detail::read_v4_endpoint<tcp::endpoint>(in);
				char flags = detail::read_uint8(fin);
				p.peer_from_tracker(adr, pid, peer_info::pex, flags);
			} 
			return true;
		}
		catch (std::exception&)
		{
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
			std::vector<char> const& pex_msg = m_tp.get_ut_pex_msg();

			buffer::interval i = m_pc.allocate_send_buffer(6 + pex_msg.size());

			detail::write_uint32(1 + 1 + pex_msg.size(), i.begin);
			detail::write_uint8(bt_peer_connection::msg_extended, i.begin);
			detail::write_uint8(m_message_index, i.begin);
			std::copy(pex_msg.begin(), pex_msg.end(), i.begin);
			i.begin += pex_msg.size();

			assert(i.begin == i.end);
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

			int num_added = 0;
			for (torrent::peer_iterator i = m_torrent.begin()
				, end(m_torrent.end()); i != end; ++i)
			{
				if (!send_peer(*i->second)) continue;

				// don't write too big of a package
				if (num_added >= max_peer_entries) continue;

				// i->first was added since the last time
				detail::write_endpoint(i->first, pla_out);
				// no supported flags to set yet
				// 0x01 - peer supports encryption
				// 0x02 - peer is a seed
				int flags = i->second->is_seed() ? 2 : 0;
				detail::write_uint8(flags, plf_out);
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

			assert(i.begin == i.end);
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

	boost::shared_ptr<torrent_plugin> create_ut_pex_plugin(torrent* t)
	{
		if (t->torrent_file().priv())
		{
			return boost::shared_ptr<torrent_plugin>();
		}
		return boost::shared_ptr<torrent_plugin>(new ut_pex_plugin(*t));
	}

}


