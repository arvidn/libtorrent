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
#include <map>

namespace libtorrent::aux {

struct torrent;
struct torrent_peer;

// v1 torrents have no per-block hashes, so when a piece fails its hash
// check there's no way to tell, from the metadata alone, which
// downloader sent the bad block. This tracks the data peers have sent us
// for pieces that failed, by re-reading the blocks back from disk and
// hashing them, and once a piece completes and passes (or a peer is
// caught sending two different hashes for the same block) bans the
// peer(s) that turned out to have sent corrupt data. v2 torrents don't
// need this; bad blocks are identified directly via merkle block hashes,
// see torrent::piece_failed and hash_picker::verify_block_hashes.
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

	torrent& m_torrent;

	// per-instance key for siphash24(), so a peer cannot craft a block
	// colliding with another peer's hash
	siphash_key m_salt = aux::random_siphash_key();

	// maps a piece_block to the peer and hash (salted with m_salt) of the
	// block data it sent
	std::map<piece_block, block_entry> m_block_hashes;
};

}

#endif // TORRENT_SMART_BAN_HPP_INCLUDED
