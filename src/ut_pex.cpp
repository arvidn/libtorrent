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
			std::list<tcp::endpoint> cs;
			for (torrent::peer_iterator i = m_torrent.begin()
				, end(m_torrent.end()); i != end; ++i)
			{	
				// don't send out peers that we haven't connected to
				// (that have connected to us)
				if (!i->second->is_local()) continue;
				// don't send out peers that we haven't successfully connected to
				if (i->second->is_connecting()) continue;
				cs.push_back(i->first);
			}
			std::list<tcp::endpoint> added_peers, dropped_peers;

			std::set_difference(cs.begin(), cs.end(), m_old_peers.begin()
				, m_old_peers.end(), std::back_inserter(added_peers));
			std::set_difference(m_old_peers.begin(), m_old_peers.end()
				, cs.begin(), cs.end(), std::back_inserter(dropped_peers));
			m_old_peers = cs;

			unsigned int num_peers = max_peer_entries;

			std::string pla, pld, plf;
			std::back_insert_iterator<std::string> pla_out(pla);
			std::back_insert_iterator<std::string> pld_out(pld);
			std::back_insert_iterator<std::string> plf_out(plf);

			// TODO: use random selection in case added_peers.size() > num_peers
			for (std::list<tcp::endpoint>::const_iterator i = added_peers.begin()
				, end(added_peers.end());i != end; ++i)
			{	
				if (!i->address().is_v4()) continue;
				detail::write_endpoint(*i, pla_out);
				// no supported flags to set yet
				// 0x01 - peer supports encryption
				detail::write_uint8(0, plf_out);

				if (--num_peers == 0) break;
			}

			num_peers = max_peer_entries;
			// TODO: use random selection in case dropped_peers.size() > num_peers
			for (std::list<tcp::endpoint>::const_iterator i = dropped_peers.begin()
				, end(dropped_peers.end());i != end; ++i)
			{	
				if (!i->address().is_v4()) continue;
				detail::write_endpoint(*i, pld_out);

				if (--num_peers == 0) break;
			}

			entry pex(entry::dictionary_t);
			pex["added"] = pla;
			pex["dropped"] = pld;
			pex["added.f"] = plf;

			m_ut_pex_msg.clear();
			bencode(std::back_inserter(m_ut_pex_msg), pex);
		}

	private:
		torrent& m_torrent;

		std::list<tcp::endpoint> m_old_peers;
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
		{
			if (msg != extension_index) return false;
			if (m_message_index == 0) return false;

			if (length > 500 * 1024)
				throw protocol_error("ut peer exchange message larger than 500 kB");

			if (body.left() < length) return true;

			// in case we are a seed we do not use the peers
			// from the pex message to prevent us from 
			// overloading ourself
			if (m_torrent.is_seed()) return true;
			
			entry Pex = bdecode(body.begin, body.end);
			entry* PeerList = Pex.find_key("added");

			if (!PeerList) return true;
			std::string const& peers = PeerList->string();
			int num_peers = peers.length() / 6;
			char const* in = peers.c_str();

			peer_id pid;
			pid.clear();
			policy& p = m_torrent.get_policy();
			for (int i = 0; i < num_peers; ++i)
			{
				tcp::endpoint adr = detail::read_v4_endpoint<tcp::endpoint>(in);
				if (!m_torrent.connection_for(adr)) p.peer_from_tracker(adr, pid);
			} 
			return true;
		}

		// the peers second tick
		// every minute we send a pex message
		virtual void tick()
		{
			if (!m_message_index) return;	// no handshake yet
			if (++m_1_minute <= 60) return;

			send_ut_peer_list();
			m_1_minute = 0;
		}

	private:

		void send_ut_peer_list()
		{
			std::vector<char>& pex_msg = m_tp.get_ut_pex_msg();

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
	};

	boost::shared_ptr<peer_plugin> ut_pex_plugin::new_connection(peer_connection* pc)
	{
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


