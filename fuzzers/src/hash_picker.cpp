/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

// hash_picker::hashes_rejected() is fed a hash_request that comes straight off
// the wire (bt_peer_connection::on_hash_reject() only matches it against an
// outstanding request by field equality, it never revalidates base/index).
// hashes_rejected() itself only documents its "base is the piece layer, index
// is a multiple of 512" invariant with a TORRENT_ASSERT, which is compiled out
// in release builds, unlike the equivalent check in add_hashes(). This target
// drives that call directly with fuzzer-controlled fields to look for the
// resulting out-of-bounds access into m_piece_hash_requested.
//
// The file_storage / merkle_tree / hash_picker fixture is fixed and doesn't
// depend on the fuzz input, so it's built once at startup rather than on
// every call.

#include <cstdint>
#include <memory>

#include "libtorrent/aux_/hash_picker.hpp"
#include "libtorrent/aux_/merkle_tree.hpp"
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/disk_interface.hpp" // for default_block_size
#include "libtorrent/file_storage.hpp"
#include "libtorrent/units.hpp"

using namespace lt;

namespace {

	std::uint32_t read_uint32(std::uint8_t const* p)
	{
		return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 | std::uint32_t(p[2]) << 8
			| std::uint32_t(p[3]);
	}

	file_storage g_fs;
	aux::vector<aux::merkle_tree, file_index_t> g_trees;
	std::unique_ptr<aux::hash_picker> g_picker;

} // anonymous namespace

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
	int const piece_size = 4 * default_block_size;
	int const num_pieces = 4 * 512;

	g_fs.set_piece_length(piece_size);
	g_fs.add_file("test/tmp1", std::int64_t(num_pieces) * piece_size);

	char const root[32] = {};
	g_trees.emplace_back(
		num_pieces * (piece_size / default_block_size), piece_size / default_block_size, root);

	g_picker = std::make_unique<aux::hash_picker>(g_fs, g_trees);
	return 0;
}

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, size_t size)
{
	if (size < 13)
		return 0;

	// keep index/count within a range that can't overflow when added together,
	// while still comfortably exceeding the small number of real buckets
	// m_piece_hash_requested ends up with for this torrent (4).
	int const base = int(data[0]);
	int const index = int(read_uint32(data + 1) % (1 << 20));
	int const count = int(read_uint32(data + 5) % (1 << 20));
	int const proof_layers = int(read_uint32(data + 9));

	aux::hash_request const req(file_index_t{0}, base, index, count, proof_layers);

	g_picker->hashes_rejected(req);

	return 0;
}
