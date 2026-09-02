/*

Copyright (c) 2007, 2013, 2017, 2019-2022, Arvid Norberg
Copyright (c) 2015, Steven Siloti
Copyright (c) 2016, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_SMART_BAN_HPP_INCLUDED
#define TORRENT_SMART_BAN_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/disk_buffer_holder.hpp"
#include "libtorrent/disk_interface.hpp"
#include "libtorrent/piece_block.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/units.hpp"
#include "libtorrent/aux_/siphash.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <utility>

namespace libtorrent::aux {

struct torrent;
struct torrent_peer;

// std::less<torrent_peer*> gives a total order for unrelated pointers,
// unlike operator< (used by std::pair::operator<), which is only
// guaranteed consistent within the same array. nullptr is ordered first
// explicitly, since the standard leaves its position relative to other
// pointers under std::less unspecified; check_block_hash() relies on it
// to seek to the start of a piece's key range with lower_bound().
struct block_peer_less
{
	bool operator()(std::pair<piece_block, torrent_peer*> const& lhs,
		std::pair<piece_block, torrent_peer*> const& rhs) const
	{
		if (lhs.first != rhs.first)
			return lhs.first < rhs.first;
		if (lhs.second == nullptr || rhs.second == nullptr)
			return lhs.second == nullptr && rhs.second != nullptr;
		return std::less<torrent_peer*>{}(lhs.second, rhs.second);
	}
};

// v1 torrents have no per-block hashes, so a failed piece can't be
// attributed to a peer from the metadata alone: blocks are re-read from
// disk and hashed once the piece completes, banning any peer whose
// digest doesn't match, or that sent two different digests for the same
// block.
//
// v2 (and hybrid) torrents get an authoritative block hash directly from
// the peer hash-exchange (hash_picker::verify_block_hashes()), so
// record_block_hashes()/check_block_hash() resolve a dispute against the
// merkle-verified hash without a disk read, tracked separately in
// m_v2_block_hashes. Once a block's hash is known it's kept in
// m_known_block_hashes to catch a repeat offender immediately.
struct TORRENT_EXTRA_EXPORT smart_ban
{
	explicit smart_ban(torrent& t)
		: m_torrent(t)
	{}
	smart_ban(smart_ban const&) = delete;
	smart_ban& operator=(smart_ban const&) = delete;

	void on_piece_pass(piece_index_t p);
	void on_piece_failed(piece_index_t p);
	void on_erase_peers(span<torrent_peer* const> peers);
	void on_clear_peers();

	void record_block_hashes(piece_index_t p, span<sha256_hash const> block_hashes);
	void check_block_hash(piece_index_t piece, span<sha256_hash const> authoritative);

private:
	// this entry ties a specific block hash to a peer.
	struct block_entry
	{
		torrent_peer* peer;
		std::uint64_t digest;
	};

	void on_read_failed_block(piece_block b,
		address const& a,
		disk_buffer_holder buffer,
		int block_size,
		storage_error const& error);
	void on_read_ok_block(std::pair<piece_block, block_entry> b,
		address const& a,
		disk_buffer_holder buffer,
		int block_size,
		storage_error const& error);

	// bans and disconnects peer if it isn't banned already, logging the two
	// conflicting hashes for block b. Instantiated for sha256_hash (v2
	// merkle hash) and std::uint64_t (v1 siphash digest) in smart_ban.cpp.
	template <typename Hash>
	void ban_mismatched_peer(
		torrent_peer* peer, piece_block b, Hash const& hash1, Hash const& hash2);

	torrent& m_torrent;

	// per-instance key for siphash24(), so a peer cannot craft a block
	// colliding with another peer's hash
	siphash_key m_salt = aux::random_siphash_key();

	// maps a piece_block to the peer and hash (salted with m_salt) of the
	// block data it sent
	std::map<piece_block, block_entry> m_block_hashes;

	// v2 counterpart to m_block_hashes: populated from block hashes
	// computed while verifying a v2 piece, resolved against the
	// merkle-verified hash instead of a second disk read. Keyed on
	// (piece_block, peer), not just piece_block, so distinct peers
	// contributing the same block don't collide; piece_block sorts
	// first, so one peer's entries for a block are contiguous and found
	// with lower_bound().
	std::map<std::pair<piece_block, torrent_peer*>, sha256_hash, block_peer_less> m_v2_block_hashes;

	// authoritative hash for a disputed block, once known
	// (check_block_hash()). Lets record_block_hashes() catch a repeat
	// offender on a later re-download attempt without another
	// hash-exchange round.
	std::map<piece_block, sha256_hash> m_known_block_hashes;
};

}

#endif // TORRENT_SMART_BAN_HPP_INCLUDED
