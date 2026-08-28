/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

// Fuzzes aux::merkle_tree::add_hashes() directly with fuzzer-controlled
// dest_start_idx and hash-span sizes, bypassing the hash_picker/wire layer
// above it.
//
// merkle_validate_and_insert_proofs() accepts any non-zero hash into an
// empty tree slot as speculative and unverified, so a single fixed canary
// hash reaches deep into add_hashes() without spending cycles hashing fuzz
// input.
//
// A fresh tree is constructed per input since add_hashes() mutates it in
// place; the fixture's shape and root are otherwise fixed.

#include <cstdint>
#include <vector>

#include "libtorrent/aux_/merkle.hpp" // for merkle_root
#include "libtorrent/aux_/merkle_tree.hpp"
#include "libtorrent/disk_interface.hpp" // for default_block_size
#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/units.hpp"

using namespace lt;

namespace {

std::uint32_t read_uint32(std::uint8_t const* p)
{
	return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 | std::uint32_t(p[2]) << 8
		| std::uint32_t(p[3]);
}

int const piece_size = 4 * default_block_size;
int const num_pieces = 4 * 512;
int const num_blocks = num_pieces * (piece_size / default_block_size);
int const blocks_per_piece = piece_size / default_block_size;

// a merkle_tree's root is never all-zero in production: torrent_info.cpp
// rejects an all-zero pieces root at parse time. Give the fixture a real
// one too, computed once, instead of the unreachable all-zero state.
sha256_hash g_root;

} // anonymous namespace

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
	std::vector<sha256_hash> const leaves(std::size_t(num_blocks), sha256_hash{});
	g_root = merkle_root(leaves);
	return 0;
}

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, size_t size)
{
	if (size < 7)
		return 0;

	aux::merkle_tree tree(num_blocks, blocks_per_piece, g_root.data());

	// mirror the bound hash_picker::add_hashes() enforces on dest_start_idx
	// before calling into here (never the root, always within the tree).
	int const dest_start_idx = 1 + int(read_uint32(data) % (std::uint32_t(tree.end_index()) - 1));

	// mirror validate_hash_request()'s real bounds: count in [1, 8192]
	// (hashes.size() == 0 violates merkle_num_leafs()'s precondition), and
	// a proof depth within merkle.cpp's walk-length bound of 32.
	int const num_hashes = 1 + int((std::uint32_t(data[4]) << 8 | data[5]) % 8192);
	int const num_uncle_hashes = int(data[6] % 33);

	sha256_hash const canary = sha256_hash::max();
	std::vector<sha256_hash> const hashes(std::size_t(num_hashes), canary);
	std::vector<sha256_hash> const uncle_hashes(std::size_t(num_uncle_hashes), canary);

	tree.add_hashes(dest_start_idx, piece_index_t::diff_type{0}, hashes, uncle_hashes);

	return 0;
}
