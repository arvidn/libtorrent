/*

Copyright (c) 2007-2012, 2014-2022, Arvid Norberg
Copyright (c) 2015, Steven Siloti
Copyright (c) 2016, 2018, 2020-2021, Alden Torres
Copyright (c) 2017, Andrei Kurushin
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/smart_ban.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/aux_/torrent.hpp"
#include "libtorrent/aux_/peer_connection.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/operations.hpp" // for operation_t enum

#ifndef TORRENT_DISABLE_EXTENSIONS
#include "libtorrent/extensions/smart_ban.hpp"
#include "libtorrent/extensions.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/client_data.hpp"
#endif

#include <algorithm>
#include <utility>
#include <vector>

#ifndef TORRENT_DISABLE_LOGGING
#include "libtorrent/aux_/socket_io.hpp"
#include "libtorrent/hex.hpp" // to_hex
#endif

namespace libtorrent::aux {

void smart_ban::on_piece_pass(piece_index_t const p)
{
	// has this piece failed earlier? If it has, go through the
	// CRCs from the time it failed and ban the peers that
	// sent bad blocks
	auto i = m_block_hashes.lower_bound(piece_block(p, 0));
	if (i == m_block_hashes.end() || i->first.piece_index != p)
		return;

#ifndef TORRENT_DISABLE_LOGGING
	if (m_torrent.should_log())
		m_torrent.debug_log("PIECE PASS [ p: %d | block_hash_size: %d ]",
			static_cast<int>(p),
			int(m_block_hashes.size()));
#endif

	auto const self = m_torrent.shared_from_this();
	int const block_size = m_torrent.block_size();
	int size = m_torrent.torrent_file().piece_size(p);
	peer_request r = {p, 0, std::min(block_size, size)};
	piece_block pb(p, 0);
	while (size > 0)
	{
		if (i->first.block_index == pb.block_index)
		{
			m_torrent.session().disk_thread().async_read(m_torrent.storage(),
				r,
				[this, self, entry = *i, addr = i->second.peer->address(), len = r.length](
					disk_buffer_holder buffer, storage_error const& error) {
					on_read_ok_block(entry, addr, std::move(buffer), len, error);
				});
			i = m_block_hashes.erase(i);
		}
		else
		{
			TORRENT_ASSERT(i->first.block_index > pb.block_index);
		}

		if (i == m_block_hashes.end() || i->first.piece_index != p)
			break;

		r.start += block_size;
		size -= block_size;
		r.length = std::min(block_size, size);
		++pb.block_index;
	}

#if TORRENT_USE_ASSERTS
	// make sure we actually removed all the entries for piece 'p'
	i = m_block_hashes.lower_bound(piece_block(p, 0));
	TORRENT_ASSERT(i == m_block_hashes.end() || i->first.piece_index != p);
#endif

	if (m_torrent.is_seed())
	{
		std::map<piece_block, block_entry>().swap(m_block_hashes);
	}
}

void smart_ban::on_piece_failed(piece_index_t const p)
{
	// The piece failed the hash check. Record
	// the CRC and origin peer of every block

	// if the torrent is aborted, no point in starting
	// a bunch of read operations on it
	if (m_torrent.is_aborted())
		return;

	std::vector<torrent_peer*> const downloaders = m_torrent.picker().get_downloaders(p);

	auto const self = m_torrent.shared_from_this();
	int const block_size = m_torrent.block_size();
	int size = m_torrent.torrent_file().piece_size(p);
	peer_request r = {p, 0, std::min(block_size, size)};
	piece_block pb(p, 0);
	for (auto const& i : downloaders)
	{
		if (i != nullptr)
		{
			// for very sad and involved reasons, this read need to force a copy out of the cache
			// since the piece has failed, this block is very likely to be replaced with a newly
			// downloaded one very soon, and to get a block by reference would fail, since the
			// block read will have been deleted by the time it gets back to the network thread
			m_torrent.session().disk_thread().async_read(
				m_torrent.storage(),
				r,
				[this, self, pb, addr = i->address(), len = r.length](
					disk_buffer_holder buffer, storage_error const& error) {
					on_read_failed_block(pb, addr, std::move(buffer), len, error);
				},
				disk_interface::force_copy);
		}

		r.start += block_size;
		size -= block_size;
		r.length = std::min(block_size, size);
		++pb.block_index;
	}
	TORRENT_ASSERT(size <= 0);
}

void smart_ban::on_erase_peers(span<torrent_peer* const> peers)
{
	if (m_block_hashes.empty() || peers.empty())
		return;

	std::vector<torrent_peer*> erased(peers.begin(), peers.end());
	std::sort(erased.begin(), erased.end());
	std::erase_if(m_block_hashes, [&erased](auto const& entry) {
		return std::binary_search(erased.begin(), erased.end(), entry.second.peer);
	});
}

void smart_ban::on_read_failed_block(piece_block const b,
	address const& a,
	disk_buffer_holder buffer,
	int const block_size,
	storage_error const& error)
{
	TORRENT_ASSERT(m_torrent.session().is_single_thread());

	// ignore read errors
	if (error)
		return;

	hasher h;
	h.update({buffer.data(), block_size});

	auto const range = m_torrent.find_peers(a);

	// there is no peer with this address anymore
	if (range.first == range.second)
		return;

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
									" | hash1: %s | hash2: %s | ip: %s ]",
					static_cast<int>(b.piece_index),
					b.block_index,
					client,
					aux::to_hex(i->second.digest).c_str(),
					aux::to_hex(e.digest).c_str(),
					aux::print_endpoint(p->ip()).c_str());
			}
#endif
			m_torrent.ban_peer(p);
			if (p->connection)
				p->connection->disconnect(errors::peer_banned, operation_t::bittorrent);
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
							" | digest: %s | ip: %s ]",
			static_cast<int>(b.piece_index),
			b.block_index,
			client,
			aux::to_hex(e.digest).c_str(),
			aux::print_address(p->ip().address()).c_str());
	}
#endif
}

void smart_ban::on_read_ok_block(std::pair<piece_block, block_entry> const b,
	address const& a,
	disk_buffer_holder buffer,
	int const block_size,
	storage_error const& error)
{
	TORRENT_ASSERT(m_torrent.session().is_single_thread());

	// ignore read errors
	if (error)
		return;

	hasher h;
	h.update({buffer.data(), block_size});
	sha1_hash const ok_digest = h.final();

	if (b.second.digest == ok_digest)
		return;

	// find the peer
	auto range = m_torrent.find_peers(a);
	if (range.first == range.second)
		return;
	torrent_peer* p = nullptr;
	for (; range.first != range.second; ++range.first)
	{
		if (b.second.peer != *range.first)
			continue;
		p = *range.first;
	}
	if (p == nullptr)
		return;

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
							" | ok_digest: %s | bad_digest: %s | ip: %s ]",
			static_cast<int>(b.first.piece_index),
			b.first.block_index,
			client,
			aux::to_hex(ok_digest).c_str(),
			aux::to_hex(b.second.digest).c_str(),
			aux::print_address(p->ip().address()).c_str());
	}
#endif
	m_torrent.ban_peer(p);
	if (p->connection)
		p->connection->disconnect(errors::peer_banned, operation_t::bittorrent);
}
}

#ifndef TORRENT_DISABLE_EXTENSIONS
#if TORRENT_ABI_VERSION < 5
namespace libtorrent {

// deprecated stub, constructs nothing. Corrupt-data banning is
// controlled by settings_pack::enable_smart_ban.
std::shared_ptr<torrent_plugin> create_smart_ban_plugin(torrent_handle const&, client_data_t)
{
	return {};
}
}
#endif
#endif
