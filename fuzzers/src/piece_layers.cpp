/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

// Fuzzer for parse_piece_layers() (src/load_torrent.cpp).
// It validates each piece-layer dict entry: key must be a 32-byte file root
// hash, value must be exactly num_pieces * 32 bytes, and the hashes must form
// a valid merkle tree against the stored root.
//
// A stable file_storage (with correct root hashes) is built once at startup.
// Each fuzz input is bdecoded and passed directly to parse_piece_layers so
// the fuzzer focuses on that function rather than the surrounding
// load_torrent_buffer plumbing (which has its own fuzzer).

#include <cstdint>
#include <memory>
#include <vector>

#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/aux_/merkle.hpp"
#include "libtorrent/bdecode.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/disk_interface.hpp" // for default_block_size
#include "libtorrent/error_code.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/load_torrent.hpp"
#include "libtorrent/torrent_info.hpp"

namespace {

	std::shared_ptr<lt::torrent_info const> g_torrent_info;

} // namespace

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
	int const piece_size = 256 * 1024;
	int const num_pieces = 4;
	std::int64_t const file_size = std::int64_t(piece_size) * num_pieces;
	int const blocks_per_piece = piece_size / lt::default_block_size;

	std::vector<lt::create_file_entry> files;
	files.emplace_back("test_file", file_size);
	lt::create_torrent t(std::move(files), piece_size);

	std::vector<char> piece(piece_size);
	std::vector<lt::sha256_hash> piece_tree(lt::merkle_num_nodes(blocks_per_piece));

	for (lt::piece_index_t i : t.piece_range())
	{
		std::fill(piece.begin(), piece.end(), static_cast<char>(static_cast<int>(i)));
		t.set_hash(i, lt::hasher(piece).final());

		lt::span<char const> const piece_span(piece);
		for (int k = 0; k < blocks_per_piece; ++k)
		{
			piece_tree[std::size_t(k)] =
				lt::hasher256(piece_span.subspan(k * lt::default_block_size, lt::default_block_size)
				)
					.final();
		}
		t.set_hash2(lt::file_index_t{0}, i - lt::piece_index_t{0}, lt::merkle_root(piece_tree));
	}

	lt::entry const tor = t.generate();

	std::vector<char> buf;
	lt::bencode(std::back_inserter(buf), tor);
	lt::add_torrent_params atp = lt::load_torrent_buffer({buf.data(), int(buf.size())});
	g_torrent_info = atp.ti;
	return 0;
}

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, std::size_t size)
{
	lt::error_code ec;
	lt::bdecode_node const e = lt::bdecode({reinterpret_cast<char const*>(data), int(size)}, ec);
	if (ec) return 0;

	lt::add_torrent_params out;
	lt::aux::parse_piece_layers(e, g_torrent_info->layout(), ec, out);
	return 0;
}
