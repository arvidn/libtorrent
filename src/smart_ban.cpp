/*

Copyright (c) 2007-2018, Arvid Norberg
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

#include <vector>
#include <map>
#include <utility>
#include <numeric>
#include <cstdio>
#include <functional>

#include "libtorrent/hasher.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/extensions.hpp"
#include "libtorrent/extensions/smart_ban.hpp"
#include "libtorrent/disk_io_thread.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/peer_connection.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/operations.hpp" // for operation_t enum

#ifndef TORRENT_DISABLE_LOGGING
#include "libtorrent/socket_io.hpp"
#include "libtorrent/hex.hpp" // to_hex
#endif

using namespace std::placeholders;

namespace libtorrent {

class torrent;

namespace {


	struct smart_ban_plugin final
		: torrent_plugin
		, std::enable_shared_from_this<smart_ban_plugin>
	{
		explicit smart_ban_plugin(torrent& t)
			: m_torrent(t)
			, m_salt(random(0xffffffff))
		{}

		void on_piece_pass(piece_index_t const p) override
		{
			// has this piece failed earlier? If it has, go through the
			// CRCs from the time it failed and ban the peers that
			// sent bad blocks
			auto i = m_block_hashes.lower_bound(piece_block(p, 0));
			if (i == m_block_hashes.end() || i->first.piece_index != p) return;

#ifndef TORRENT_DISABLE_LOGGING
			if (m_torrent.should_log())
				m_torrent.debug_log("PIECE PASS [ p: %d | block_hash_size: %d ]"
					, static_cast<int>(p), int(m_block_hashes.size()));
#endif

			int size = m_torrent.torrent_file().piece_size(p);
			peer_request r = {p, 0, std::min(16 * 1024, size)};
			piece_block pb(p, 0);
			while (size > 0)
			{
				if (i->first.block_index == pb.block_index)
				{
					m_torrent.session().disk_thread().async_read(m_torrent.storage()
						, r, std::bind(&smart_ban_plugin::on_read_ok_block
						, shared_from_this(), *i, i->second.peer->address(), _1, r.length, _2, _3));
					i = m_block_hashes.erase(i);
				}
				else
				{
					TORRENT_ASSERT(i->first.block_index > pb.block_index);
				}

				if (i == m_block_hashes.end() || i->first.piece_index != p)
					break;

				r.start += 16 * 1024;
				size -= 16 * 1024;
				r.length = std::min(16 * 1024, size);
				++pb.block_index;
			}

#ifndef NDEBUG
			// make sure we actually removed all the entries for piece 'p'
			i = m_block_hashes.lower_bound(piece_block(p, 0));
			TORRENT_ASSERT(i == m_block_hashes.end() || i->first.piece_index != p);
#endif

			if (m_torrent.is_seed())
			{
				std::map<piece_block, block_entry>().swap(m_block_hashes);
				return;
			}
		}

		void on_piece_failed(piece_index_t const p) override
		{
			// The piece failed the hash check. Record
			// the CRC and origin peer of every block

			// if the torrent is aborted, no point in starting
			// a bunch of read operations on it
			if (m_torrent.is_aborted()) return;

			std::vector<torrent_peer*> downloaders;
			m_torrent.picker().get_downloaders(downloaders, p);

			int size = m_torrent.torrent_file().piece_size(p);
			peer_request r = {p, 0, std::min(16*1024, size)};
			piece_block pb(p, 0);
			for (auto const& i : downloaders)
			{
				if (i != nullptr)
				{
					// for very sad and involved reasons, this read need to force a copy out of the cache
					// since the piece has failed, this block is very likely to be replaced with a newly
					// downloaded one very soon, and to get a block by reference would fail, since the
					// block read will have been deleted by the time it gets back to the network thread
					m_torrent.session().disk_thread().async_read(m_torrent.storage(), r
						, std::bind(&smart_ban_plugin::on_read_failed_block
						, shared_from_this(), pb, i->address(), _1, r.length, _2, _3)
						, disk_interface::force_copy);
				}

				r.start += 16*1024;
				size -= 16*1024;
				r.length = std::min(16*1024, size);
				++pb.block_index;
			}
			TORRENT_ASSERT(size <= 0);
		}

	private:

		// this entry ties a specific block CRC to
		// a peer.
		struct block_entry
		{
			torrent_peer* peer;
			sha1_hash digest;
		};

		void on_read_failed_block(piece_block const b, address const a
			, disk_buffer_holder buffer, int const block_size, disk_job_flags_t
			, storage_error const& error)
		{
			TORRENT_ASSERT(m_torrent.session().is_single_thread());

			// ignore read errors
			if (error) return;

			hasher h;
			h.update({buffer.get(), block_size});
			h.update(reinterpret_cast<char const*>(&m_salt), sizeof(m_salt));

			auto const range = m_torrent.find_peers(a);

			// there is no peer with this address anymore
			if (range.first == range.second) return;

			torrent_peer* p = (*range.first);
			block_entry e = {p, h.final()};

			auto i = m_block_hashes.lower_bound(b);

			if (i != m_block_hashes.end() && i->first == b && i->second.peer == p)
			{
				// this peer has sent us this block before
				// if the peer is already banned, it doesn't matter if it sent
				// good or bad data. Nothings going to change it
				if (!p->banned && i->second.digest != e.digest)
				{
					// this time the digest of the block is different
					// from the first time it sent it
					// at least one of them must be bad
#ifndef TORRENT_DISABLE_LOGGING
					if (m_torrent.should_log())
					{
						char const* client = "-";
						peer_info info;
						if (p->connection)
						{
							p->connection->get_peer_info(info);
							client = info.client.c_str();
						}
						m_torrent.debug_log("BANNING PEER [ p: %d | b: %d | c: %s"
							" | hash1: %s | hash2: %s | ip: %s ]"
							, static_cast<int>(b.piece_index), b.block_index, client
							, aux::to_hex(i->second.digest).c_str()
							, aux::to_hex(e.digest).c_str()
							, print_endpoint(p->ip()).c_str());
					}
#endif
					m_torrent.ban_peer(p);
					if (p->connection) p->connection->disconnect(
						errors::peer_banned, operation_t::bittorrent);
				}
				// we already have this exact entry in the map
				// we don't have to insert it
				return;
			}

			m_block_hashes.insert(i, std::pair<const piece_block, block_entry>(b, e));

#ifndef TORRENT_DISABLE_LOGGING
			if (m_torrent.should_log())
			{
				char const* client = "-";
				peer_info info;
				if (p->connection)
				{
					p->connection->get_peer_info(info);
					client = info.client.c_str();
				}
				m_torrent.debug_log("STORE BLOCK CRC [ p: %d | b: %d | c: %s"
					" | digest: %s | ip: %s ]"
					, static_cast<int>(b.piece_index), b.block_index, client
					, aux::to_hex(e.digest).c_str()
					, print_address(p->ip().address()).c_str());
			}
#endif
		}

		void on_read_ok_block(std::pair<piece_block, block_entry> const b
			, address const& a, disk_buffer_holder buffer, int const block_size
			, disk_job_flags_t, storage_error const& error)
		{
			TORRENT_ASSERT(m_torrent.session().is_single_thread());

			// ignore read errors
			if (error) return;

			hasher h;
			h.update({buffer.get(), block_size});
			h.update(reinterpret_cast<char const*>(&m_salt), sizeof(m_salt));
			sha1_hash const ok_digest = h.final();

			if (b.second.digest == ok_digest) return;

			// find the peer
			auto range = m_torrent.find_peers(a);
			if (range.first == range.second) return;
			torrent_peer* p = nullptr;
			for (; range.first != range.second; ++range.first)
			{
				if (b.second.peer != *range.first) continue;
				p = *range.first;
			}
			if (p == nullptr) return;

#ifndef TORRENT_DISABLE_LOGGING
			if (m_torrent.should_log())
			{
				char const* client = "-";
				peer_info info;
				if (p->connection)
				{
					p->connection->get_peer_info(info);
					client = info.client.c_str();
				}
				m_torrent.debug_log("BANNING PEER [ p: %d | b: %d | c: %s"
					" | ok_digest: %s | bad_digest: %s | ip: %s ]"
					, static_cast<int>(b.first.piece_index), b.first.block_index, client
					, aux::to_hex(ok_digest).c_str()
					, aux::to_hex(b.second.digest).c_str()
					, print_address(p->ip().address()).c_str());
			}
#endif
			m_torrent.ban_peer(p);
			if (p->connection) p->connection->disconnect(
				errors::peer_banned, operation_t::bittorrent);
		}

		torrent& m_torrent;

		// This table maps a piece_block (piece and block index
		// pair) to a peer and the block CRC. The CRC is calculated
		// from the data in the block + the salt
		std::map<piece_block, block_entry> m_block_hashes;

		// This salt is a random value used to calculate the block CRCs
		// Since the CRC function that is used is not a one way function
		// the salt is required to avoid attacks where bad data is sent
		// that is forged to match the CRC of the good data.
		std::uint32_t const m_salt;

		// explicitly disallow assignment, to silence msvc warning
		smart_ban_plugin& operator=(smart_ban_plugin const&) = delete;
	};

} }

namespace libtorrent {

	std::shared_ptr<torrent_plugin> create_smart_ban_plugin(torrent_handle const& th, void*)
	{
		torrent* t = th.native_handle().get();
		return std::make_shared<smart_ban_plugin>(*t);
	}
}

#endif
