/*

Copyright (c) 2007, Arvid Norberg
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
#include <boost/enable_shared_from_this.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <vector>
#include <map>
#include <utility>
#include <numeric>
#include <cstdio>

#include "libtorrent/peer_connection.hpp"
#include "libtorrent/bt_peer_connection.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/extensions.hpp"
#include "libtorrent/extensions/smart_ban.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/disk_io_thread.hpp"
#include "libtorrent/aux_/session_impl.hpp"

namespace libtorrent { namespace
{

	struct smart_ban_plugin : torrent_plugin, boost::enable_shared_from_this<smart_ban_plugin>
	{
		smart_ban_plugin(torrent& t)
			: m_torrent(t)
			, m_salt(rand())
		{
		}

		void on_piece_pass(int p)
		{
#ifdef TORRENT_LOGGING
			(*m_torrent.session().m_logger) << time_now_string() << " PIECE PASS [ p: " << p
				<< " | block_crc_size: " << m_block_crc.size() << " ]\n";
#endif
			// has this piece failed earlier? If it has, go through the
			// CRCs from the time it failed and ban the peers that
			// sent bad blocks
			std::map<piece_block, block_entry>::iterator i = m_block_crc.lower_bound(piece_block(p, 0));
			if (i == m_block_crc.end() || i->first.piece_index != p) return;

			int size = m_torrent.torrent_file().piece_size(p);
			peer_request r = {p, 0, (std::min)(16*1024, size)};
			piece_block pb(p, 0);
			while (size > 0)
			{
				if (i->first.block_index == pb.block_index)
				{
					m_torrent.filesystem().async_read(r, bind(&smart_ban_plugin::on_read_ok_block
						, shared_from_this(), *i, _1, _2));
					m_block_crc.erase(i++);
				}
				else
				{
					TORRENT_ASSERT(i->first.block_index > pb.block_index);
				}

				if (i == m_block_crc.end() || i->first.piece_index != p)
					break;

				r.start += 16*1024;
				size -= 16*1024;
				r.length = (std::min)(16*1024, size);
				++pb.block_index;
			}

#ifndef NDEBUG
			// make sure we actually removed all the entries for piece 'p'
			i = m_block_crc.lower_bound(piece_block(p, 0));
			TORRENT_ASSERT(i == m_block_crc.end() || i->first.piece_index != p);
#endif

			if (m_torrent.is_seed())
			{
				std::map<piece_block, block_entry>().swap(m_block_crc);
				return;
			}
		}

		void on_piece_failed(int p)
		{
			// The piece failed the hash check. Record
			// the CRC and origin peer of every block

			// if the torrent is aborted, no point in starting
			// a bunch of read operations on it
			if (m_torrent.is_aborted()) return;

			std::vector<void*> downloaders;
			m_torrent.picker().get_downloaders(downloaders, p);

			int size = m_torrent.torrent_file().piece_size(p);
			peer_request r = {p, 0, (std::min)(16*1024, size)};
			piece_block pb(p, 0);
			for (std::vector<void*>::iterator i = downloaders.begin()
				, end(downloaders.end()); i != end; ++i)
			{
				if (*i != 0)
				{
					m_torrent.filesystem().async_read(r, bind(&smart_ban_plugin::on_read_failed_block
						, shared_from_this(), pb, (policy::peer*)*i, _1, _2));
				}

				r.start += 16*1024;
				size -= 16*1024;
				r.length = (std::min)(16*1024, size);
				++pb.block_index;
			}
			TORRENT_ASSERT(size <= 0);
		}

	private:

		// this entry ties a specific block CRC to
		// a peer.
		struct block_entry
		{
			policy::peer* peer;
			unsigned long crc;
		};

		void on_read_failed_block(piece_block b, policy::peer* p, int ret, disk_io_job const& j)
		{
			TORRENT_ASSERT(p);
			// ignore read errors
			if (ret != j.buffer_size) return;

			adler32_crc crc;
			crc.update(j.buffer, j.buffer_size);
			crc.update((char const*)&m_salt, sizeof(m_salt));

			block_entry e = {p, crc.final()};

			// since this callback is called directory from the disk io
			// thread, the session mutex is not locked when we get here
			aux::session_impl::mutex_t::scoped_lock l(m_torrent.session().m_mutex);
			
			std::map<piece_block, block_entry>::iterator i = m_block_crc.lower_bound(b);
			if (i != m_block_crc.end() && i->first == b && i->second.peer == p)
			{
				// this peer has sent us this block before
				if (i->second.crc != e.crc)
				{
					// this time the crc of the block is different
					// from the first time it sent it
					// at least one of them must be bad

					if (p == 0) return;
					if (!m_torrent.get_policy().has_peer(p)) return;

#ifdef TORRENT_LOGGING
					char const* client = "-";
					peer_info info;
					if (p->connection)
					{
						p->connection->get_peer_info(info);
						client = info.client.c_str();
					}
					(*m_torrent.session().m_logger) << time_now_string() << " BANNING PEER [ p: " << b.piece_index
						<< " | b: " << b.block_index
						<< " | c: " << client
						<< " | crc1: " << i->second.crc
						<< " | crc2: " << e.crc
						<< " | ip: " << p->ip() << " ]\n";
#endif
					p->banned = true;
					if (p->connection) p->connection->disconnect("banning peer for sending bad data");
				}
				// we already have this exact entry in the map
				// we don't have to insert it
				return;
			}
			
			m_block_crc.insert(i, std::make_pair(b, e));

#ifdef TORRENT_LOGGING
			char const* client = "-";
			peer_info info;
			if (p->connection)
			{
				p->connection->get_peer_info(info);
				client = info.client.c_str();
			}
			(*m_torrent.session().m_logger) << time_now_string() << " STORE BLOCK CRC [ p: " << b.piece_index
				<< " | b: " << b.block_index
				<< " | c: " << client
				<< " | crc: " << e.crc
				<< " | ip: " << p->ip() << " ]\n";
#endif
		}
		
		void on_read_ok_block(std::pair<piece_block, block_entry> b, int ret, disk_io_job const& j)
		{
			// since this callback is called directory from the disk io
			// thread, the session mutex is not locked when we get here
			aux::session_impl::mutex_t::scoped_lock l(m_torrent.session().m_mutex);

			// ignore read errors
			if (ret != j.buffer_size) return;

			adler32_crc crc;
			crc.update(j.buffer, j.buffer_size);
			crc.update((char const*)&m_salt, sizeof(m_salt));
			unsigned long ok_crc = crc.final();

			if (b.second.crc == ok_crc) return;

			policy::peer* p = b.second.peer;

			if (p == 0) return;
			if (!m_torrent.get_policy().has_peer(p)) return;

#ifdef TORRENT_LOGGING
			char const* client = "-";
			peer_info info;
			if (p->connection)
			{
				p->connection->get_peer_info(info);
				client = info.client.c_str();
			}
			(*m_torrent.session().m_logger) << time_now_string() << " BANNING PEER [ p: " << b.first.piece_index
				<< " | b: " << b.first.block_index
				<< " | c: " << client
				<< " | ok_crc: " << ok_crc
				<< " | bad_crc: " << b.second.crc
				<< " | ip: " << p->ip() << " ]\n";
#endif
			p->banned = true;
			if (p->connection) p->connection->disconnect("banning peer for sending bad data");
		}
		
		torrent& m_torrent;

		// This table maps a piece_block (piece and block index
		// pair) to a peer and the block CRC. The CRC is calculated
		// from the data in the block + the salt
		std::map<piece_block, block_entry> m_block_crc;

		// This salt is a random value used to calculate the block CRCs
		// Since the CRC function that is used is not a one way function
		// the salt is required to avoid attacks where bad data is sent
		// that is forged to match the CRC of the good data.
		int m_salt;
	};

} }

namespace libtorrent
{

	boost::shared_ptr<torrent_plugin> create_smart_ban_plugin(torrent* t, void*)
	{
		return boost::shared_ptr<torrent_plugin>(new smart_ban_plugin(*t));
	}

}


