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
#include "libtorrent/aux_/siphash.hpp"
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
#include <cinttypes>
#include <type_traits>
#include <utility>
#include <vector>

#ifndef TORRENT_DISABLE_LOGGING
#include "libtorrent/aux_/socket_io.hpp"
#endif

namespace libtorrent::aux {

void smart_ban::on_piece_pass(piece_index_t const p)
{
	// any dispute over this piece's blocks is moot now it's done; runs
	// unconditionally since the v1 path below never populates these
	// v2-only tables
	std::erase_if(
		m_known_block_hashes, [p](auto const& entry) { return entry.first.piece_index == p; });
	std::erase_if(
		m_v2_block_hashes, [p](auto const& entry) { return entry.first.first.piece_index == p; });

	// has this piece failed earlier? If it has, go through the
	// hashes from the time it failed and ban the peers that
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
	// the hash and origin peer of every block

	// if the torrent is aborted, no point in starting
	// a bunch of read operations on it
	if (m_torrent.is_aborted())
		return;

	// v2 (or hybrid) torrents get attribution for free via
	// record_block_hashes()/check_block_hash(); only fall back to
	// re-reading blocks from disk without v2 metadata
	if (m_torrent.torrent_file().info_hashes().has_v2())
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
	if ((m_block_hashes.empty() && m_v2_block_hashes.empty()) || peers.empty())
		return;

	std::vector<torrent_peer*> erased(peers.begin(), peers.end());
	std::sort(erased.begin(), erased.end());
	std::erase_if(m_block_hashes, [&erased](auto const& entry) {
		return std::binary_search(erased.begin(), erased.end(), entry.second.peer);
	});
	std::erase_if(m_v2_block_hashes, [&erased](auto const& entry) {
		return std::binary_search(erased.begin(), erased.end(), entry.first.second);
	});
}

void smart_ban::on_clear_peers()
{
	m_block_hashes.clear();
	// m_v2_block_hashes holds a torrent_peer* per entry too, same
	// dangling-pointer risk; m_known_block_hashes is keyed on piece_block
	// alone, no clearing needed
	m_v2_block_hashes.clear();
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

	std::uint64_t const digest = aux::siphash24(m_salt, {buffer.data(), block_size});

	auto const range = m_torrent.find_peers(a);

	// there is no peer with this address anymore
	if (range.first == range.second)
		return;

	torrent_peer* p = (*range.first);
	block_entry e = {p, digest};

	auto i = m_block_hashes.lower_bound(b);

	if (i != m_block_hashes.end() && i->first == b && i->second.peer == p)
	{
		// this peer has sent us this block before. If the digest differs
		// from the first time it sent it, at least one of them must be bad
		if (i->second.digest != e.digest)
			ban_mismatched_peer(p, b, i->second.digest, e.digest);
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
		m_torrent.debug_log("STORE BLOCK HASH [ p: %d | b: %d | c: %s"
							" | digest: %016" PRIx64 " | ip: %s ]",
			static_cast<int>(b.piece_index),
			b.block_index,
			client,
			e.digest,
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

	std::uint64_t const ok_digest = aux::siphash24(m_salt, {buffer.data(), block_size});

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

	ban_mismatched_peer(p, b.first, ok_digest, b.second.digest);
}

template <typename Hash>
void smart_ban::ban_mismatched_peer(
	torrent_peer* const peer, piece_block const b, Hash const& hash1, Hash const& hash2)
{
	TORRENT_UNUSED(b);
	TORRENT_UNUSED(hash1);
	TORRENT_UNUSED(hash2);

	if (peer->banned)
		return;

#ifndef TORRENT_DISABLE_LOGGING
	if (m_torrent.should_log())
	{
		char const* client = "-";
		peer_info info;
		if (peer->connection)
		{
			peer->connection->get_peer_info(info);
			client = info.client.c_str();
		}
		if constexpr (std::is_same_v<Hash, sha256_hash>)
		{
			m_torrent.debug_log("BANNING PEER [ p: %d | b: %d | c: %s"
								" | hash1: %s | hash2: %s | ip: %s ]",
				static_cast<int>(b.piece_index),
				b.block_index,
				client,
				aux::to_hex(hash1).c_str(),
				aux::to_hex(hash2).c_str(),
				aux::print_endpoint(peer->ip()).c_str());
		}
		else
		{
			m_torrent.debug_log("BANNING PEER [ p: %d | b: %d | c: %s"
								" | hash1: %016" PRIx64 " | hash2: %016" PRIx64 " | ip: %s ]",
				static_cast<int>(b.piece_index),
				b.block_index,
				client,
				hash1,
				hash2,
				aux::print_endpoint(peer->ip()).c_str());
		}
	}
#endif
	m_torrent.ban_peer(peer);
	if (peer->connection)
		peer->connection->disconnect(errors::peer_banned, operation_t::bittorrent);
}

// explicit instantiations for the two hash types used
template void smart_ban::ban_mismatched_peer(
	torrent_peer*, piece_block, sha256_hash const&, sha256_hash const&);
template void smart_ban::ban_mismatched_peer(
	torrent_peer*, piece_block, std::uint64_t const&, std::uint64_t const&);

void smart_ban::record_block_hashes(
	piece_index_t const p, span<sha256_hash const> const block_hashes)
{
	// if the torrent is aborted, no point in bothering with this
	if (m_torrent.is_aborted())
		return;

	// only reached for hashes discovered downloading from a peer, where
	// the picker's download record can attribute them
	TORRENT_ASSERT(m_torrent.has_picker());

	std::vector<torrent_peer*> const downloaders = m_torrent.picker().get_downloaders(p);

	std::size_t const n =
		std::min(static_cast<std::size_t>(block_hashes.size()), downloaders.size());
	for (std::size_t i = 0; i < n; ++i)
	{
		torrent_peer* const peer = downloaders[i];
		if (peer == nullptr)
			continue;

		piece_block const b(p, static_cast<int>(i));
		sha256_hash const& hash = block_hashes[b.block_index];

		auto const known = m_known_block_hashes.find(b);
		if (known != m_known_block_hashes.end())
		{
			// authoritative hash for this block already known, catch a
			// repeat offender without another hash-exchange round
			if (known->second != hash)
				ban_mismatched_peer(peer, b, known->second, hash);
			continue;
		}

		auto const key = std::make_pair(b, peer);
		auto const j = m_v2_block_hashes.find(key);

		if (j != m_v2_block_hashes.end())
		{
			// this peer has sent us this block before. If the digest
			// differs from the first time it sent it, at least one of
			// them must be bad
			if (j->second != hash)
				ban_mismatched_peer(peer, b, j->second, hash);
			continue;
		}

		// the key includes the peer, so another peer's unresolved entry
		// for the same block doesn't collide with this new entry
		m_v2_block_hashes.emplace(key, hash);
	}
}

void smart_ban::check_block_hash(
	piece_index_t const piece, span<sha256_hash const> const authoritative)
{
	// a block-layer request always covers one whole, piece-aligned piece,
	// so its blocks form one contiguous run of keys in m_v2_block_hashes
	// (piece_block sorts ahead of the peer): one seek and a merge-walk,
	// rather than a lookup per block
	auto i = m_v2_block_hashes.lower_bound(
		std::make_pair(piece_block(piece, 0), static_cast<torrent_peer*>(nullptr)));

	// block/expected/actual hashes are only used for logging in
	// ban_mismatched_peer(), so skip storing them when logging is disabled
#ifndef TORRENT_DISABLE_LOGGING
	struct mismatch
	{
		torrent_peer* peer;
		piece_block block;
		sha256_hash expected;
		sha256_hash actual;
	};
	std::vector<mismatch> to_ban;
#else
	std::vector<torrent_peer*> to_ban;
#endif

	for (int block = 0; block < int(authoritative.size()); ++block)
	{
		piece_block const b(piece, block);

		// remember the now-known hash so a later resubmission can be
		// checked without another hash-exchange round, regardless of
		// whether a peer has a pending dispute for this block yet
		sha256_hash const& expected = authoritative[block];
		m_known_block_hashes[b] = expected;

		if (i == m_v2_block_hashes.end() || i->first.first != b)
			continue;

		do
		{
			if (i->second != expected)
			{
#ifndef TORRENT_DISABLE_LOGGING
				to_ban.push_back({i->first.second, b, expected, i->second});
#else
				to_ban.push_back(i->first.second);
#endif
			}

			// resolved either way: matched, or queued to be banned below
			i = m_v2_block_hashes.erase(i);
		}
		while (i != m_v2_block_hashes.end() && i->first.first == b);
	}

	// ban_mismatched_peer() can disconnect a peer and reenter here via
	// on_erase_peers(), mutating m_v2_block_hashes; do this only after
	// the walk above is done with its iterators
#ifndef TORRENT_DISABLE_LOGGING
	for (auto const& m : to_ban)
		ban_mismatched_peer(m.peer, m.block, m.expected, m.actual);
#else
	for (torrent_peer* const p : to_ban)
		ban_mismatched_peer(p, piece_block(), sha256_hash(), sha256_hash());
#endif
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
