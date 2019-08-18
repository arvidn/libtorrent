/*

Copyright (c) 2017, BitTorrent Inc.
Copyright (c) 2019, Steven Siloti
Copyright (c) 2019, Arvid Norberg
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

#include "libtorrent/hash_picker.hpp"
#include "libtorrent/peer_connection_interface.hpp"
#include "libtorrent/stat.hpp"
#include "libtorrent/aux_/merkle.hpp"
#include "libtorrent/hex.hpp"
#include "libtorrent/disk_interface.hpp" // for default_block_size

#include "test.hpp"

using namespace lt;

constexpr lt::file_index_t operator""_file (unsigned long long int const v)
{ return lt::file_index_t{static_cast<int>(v)}; }

constexpr lt::piece_index_t operator""_piece (unsigned long long int const v)
{ return lt::piece_index_t{static_cast<int>(v)}; }

struct mock_peer_connection final : peer_connection_interface
{
	tcp::endpoint const& remote() const override { return m_remote; }
	tcp::endpoint local_endpoint() const override { return {}; }
	void disconnect(error_code const&, operation_t, disconnect_severity_t) override {}
	peer_id const& pid() const override { return m_pid; }
	peer_id our_pid() const override { return m_pid; }
	void set_holepunch_mode() override {}
	torrent_peer* peer_info_struct() const override { return m_torrent_peer; }
	void set_peer_info(torrent_peer* pi) override { m_torrent_peer = pi; }
	bool is_outgoing() const override { return false; }
	void add_stat(std::int64_t, std::int64_t) override {}
	bool fast_reconnect() const override { return false; }
	bool is_choked() const override { return false; }
	bool failed() const override { return false; }
	lt::stat const& statistics() const override { return m_stat; }
	void get_peer_info(peer_info&) const override {}
#ifndef TORRENT_DISABLE_LOGGING
	bool should_log(peer_log_alert::direction_t) const override { return true; }
	void peer_log(peer_log_alert::direction_t
		, char const*, char const*, ...) const noexcept override TORRENT_FORMAT(4, 5) {}
#endif

	torrent_peer* m_torrent_peer;
	lt::stat m_stat;
	tcp::endpoint m_remote;
	peer_id m_pid;
};

namespace {

aux::vector<sha256_hash> build_tree(int const size)
{
	int const num_leafs = merkle_num_leafs(size);
	aux::vector<sha256_hash> full_tree(merkle_num_nodes(num_leafs));

	for (int i = 0; i < size; i++)
	{
		std::uint32_t hash[32 / 4];
		std::fill(std::begin(hash), std::end(hash), i + 1);
		full_tree[full_tree.end_index() - num_leafs + i] = sha256_hash(reinterpret_cast<char*>(hash));
	}

	merkle_fill_tree(full_tree, num_leafs);
	return full_tree;
}

}

#if 0
TORRENT_TEST(pick_piece_layer)
{
	file_storage fs;
	fs.set_piece_length(16 * 1024);

	fs.add_file("test/tmp1", 4 * 512 * 16 * 1024);
	fs.add_file("test/tmp2", 4 * 512 * 16 * 1024);

	aux::vector<aux::vector<sha256_hash>, file_index_t> trees;

	trees.push_back(aux::vector<sha256_hash>(merkle_num_nodes(merkle_num_leafs(4 * 512))));
	aux::from_hex("0000000000000000000000000000000000000000000000000000000000000001", trees.back()[0].data());
	trees.push_back(aux::vector<sha256_hash>(merkle_num_nodes(merkle_num_leafs(4 * 512))));
	aux::from_hex("0000000000000000000000000000000000000000000000000000000000000001", trees.back()[0].data());

	hash_picker picker(fs, trees);

	typed_bitfield<piece_index_t> pieces;
	pieces.resize(8 * 512);
	pieces.set_all();

	mock_peer_connection mock_peer1, mock_peer2;
	mock_peer1.m_torrent_peer = (torrent_peer*)0x1;
	mock_peer2.m_torrent_peer = (torrent_peer*)0x2;

	auto picked = picker.pick_hashes(pieces, 2, &mock_peer1);
	TEST_EQUAL(int(picked.size()), 2);
	TEST_EQUAL(picked[0].file, 0_file);
	TEST_EQUAL(picked[0].base, 0);
	TEST_EQUAL(picked[0].count, 512);
	TEST_EQUAL(picked[0].index, 0);
	TEST_EQUAL(picked[0].proof_layers, 10);
	TEST_EQUAL(picked[1].file, 0_file);
	TEST_EQUAL(picked[1].base, 0);
	TEST_EQUAL(picked[1].count, 512);
	TEST_EQUAL(picked[1].index, 512);
	TEST_EQUAL(picked[1].proof_layers, 10);

	picked = picker.pick_hashes(pieces, 3, &mock_peer2);
	TEST_EQUAL(int(picked.size()), 3);
	TEST_EQUAL(picked[0].file, 0_file);
	TEST_EQUAL(picked[0].base, 0);
	TEST_EQUAL(picked[0].count, 512);
	TEST_EQUAL(picked[0].index, 1024);
	TEST_EQUAL(picked[0].proof_layers, 10);
	TEST_EQUAL(picked[1].file, 0_file);
	TEST_EQUAL(picked[1].base, 0);
	TEST_EQUAL(picked[1].count, 512);
	TEST_EQUAL(picked[1].index, 1536);
	TEST_EQUAL(picked[1].proof_layers, 10);
	TEST_EQUAL(picked[2].file, 1_file);
	TEST_EQUAL(picked[2].base, 0);
	TEST_EQUAL(picked[2].count, 512);
	TEST_EQUAL(picked[2].index, 0);
	TEST_EQUAL(picked[2].proof_layers, 10);

	picked = picker.pick_hashes(pieces, 4, &mock_peer1);
	TEST_EQUAL(int(picked.size()), 3);
	TEST_EQUAL(picked[0].file, 1_file);
	TEST_EQUAL(picked[0].base, 0);
	TEST_EQUAL(picked[0].count, 512);
	TEST_EQUAL(picked[0].index, 512);
	TEST_EQUAL(picked[0].proof_layers, 10);
	TEST_EQUAL(picked[1].file, 1_file);
	TEST_EQUAL(picked[1].base, 0);
	TEST_EQUAL(picked[1].count, 512);
	TEST_EQUAL(picked[1].index, 1024);
	TEST_EQUAL(picked[1].proof_layers, 10);
	TEST_EQUAL(picked[2].file, 1_file);
	TEST_EQUAL(picked[2].base, 0);
	TEST_EQUAL(picked[2].count, 512);
	TEST_EQUAL(picked[2].index, 1536);
	TEST_EQUAL(picked[2].proof_layers, 10);
}
#endif

TORRENT_TEST(reject_piece_request)
{
	file_storage fs;
	fs.set_piece_length(16 * 1024);

	fs.add_file("test/tmp1", 4 * 512 * 16 * 1024);

	aux::vector<aux::vector<sha256_hash>, file_index_t> trees;
	trees.push_back(aux::vector<sha256_hash>(merkle_num_nodes(merkle_num_leafs(4 * 512))));
	aux::from_hex("0000000000000000000000000000000000000000000000000000000000000001", trees.back()[0].data());

	hash_picker picker(fs, trees);

	typed_bitfield<piece_index_t> const pieces(4 * 512, true);

	auto const picked = picker.pick_hashes(pieces);
	picker.hashes_rejected(picked);

	auto const picked2 = picker.pick_hashes(pieces);
	TEST_CHECK(picked == picked2);
}

TORRENT_TEST(add_leaf_hashes)
{
	file_storage fs;
	fs.set_piece_length(16 * 1024);

	fs.add_file("test/tmp1", 4 * 512 * 16 * 1024);

	aux::vector<aux::vector<sha256_hash>, file_index_t> trees;
	trees.push_back(aux::vector<sha256_hash>(merkle_num_nodes(merkle_num_leafs(4 * 512))));

	aux::vector<sha256_hash> const full_tree = build_tree(4 * 512);
	trees.front()[0] = full_tree[0];

	hash_picker picker(fs, trees);

	std::vector<sha256_hash> hashes;
	auto leafs_start = full_tree.end() - merkle_num_leafs(4 * 512);
	std::copy(leafs_start, leafs_start + 512, std::back_inserter(hashes));
	for (int i = 3; i > 0; i = merkle_get_parent(i))
	{
		hashes.push_back(full_tree[merkle_get_sibling(i)]);
	}
	add_hashes_result result = picker.add_hashes(hash_request(0_file, 0, 0, 512, 10)
		, hashes);
	TEST_CHECK(result.valid);

	result = picker.add_hashes(hash_request(0_file, 0, 512, 512, 0)
		, span<sha256_hash const>(full_tree).last(merkle_num_leafs(4 * 512) - 512).first(512));
	TEST_CHECK(result.valid);

	hashes.clear();
	std::copy(leafs_start + 1024, leafs_start + 1536, std::back_inserter(hashes));
	for (int i = 5; i > 0; i = merkle_get_parent(i))
	{
		hashes.push_back(full_tree[merkle_get_sibling(i)]);
	}

	result = picker.add_hashes(hash_request(0_file, 0, 1024, 512, 10)
		, hashes);
	TEST_CHECK(result.valid);

	result = picker.add_hashes(hash_request(0_file, 0, 1536, 512, 0)
		, span<sha256_hash const>(full_tree).last(merkle_num_leafs(4 * 512) - 1536).first(512));
	TEST_CHECK(result.valid);

	TEST_CHECK(trees.front() == full_tree);
}

TORRENT_TEST(add_piece_hashes)
{
	file_storage fs;
	fs.set_piece_length(4 * 16 * 1024);

	fs.add_file("test/tmp1", 4 * 1024 * 16 * 1024);

	aux::vector<aux::vector<sha256_hash>, file_index_t> trees;
	trees.push_back(aux::vector<sha256_hash>(merkle_num_nodes(merkle_num_leafs(4 * 1024))));

	aux::vector<sha256_hash> const full_tree = build_tree(4 * 1024);
	trees.front()[0] = full_tree[0];

	hash_picker picker(fs, trees);

	auto pieces_start = full_tree.begin() + merkle_num_nodes(1024) - 1024;

	std::vector<sha256_hash> hashes;
	std::copy(pieces_start, pieces_start + 512, std::back_inserter(hashes));
	hashes.push_back(full_tree[2]);
	add_hashes_result result = picker.add_hashes(hash_request(0_file, 2, 0, 512, 9), hashes);
	TEST_CHECK(result.valid);

	hashes.clear();
	std::copy(pieces_start + 512, pieces_start + 1024, std::back_inserter(hashes));
	result = picker.add_hashes(hash_request(0_file, 2, 512, 512, 8), hashes);
	TEST_CHECK(result.valid);

	TEST_CHECK(std::equal(trees.front().begin(), trees.front().begin() + merkle_num_nodes(1024), full_tree.begin()));
}

TORRENT_TEST(add_bad_hashes)
{
	file_storage fs;
	fs.set_piece_length(4 * 16 * 1024);

	fs.add_file("test/tmp1", 4 * 512 * 16 * 1024);

	aux::vector<aux::vector<sha256_hash>, file_index_t> trees;
	trees.push_back(aux::vector<sha256_hash>(merkle_num_nodes(merkle_num_leafs(4 * 512))));

	aux::vector<sha256_hash> const full_tree = build_tree(4 * 512);
	trees.front()[0] = full_tree[0];

	hash_picker picker(fs, trees);

	std::vector<sha256_hash> hashes(2);
	// hash count mis-match
	auto result = picker.add_hashes(hash_request(0_file, 0, 0, 2, 1), hashes);
	TEST_CHECK(!result.valid);
	result = picker.add_hashes(hash_request(0_file, 0, 0, 4, 0), hashes);
	TEST_CHECK(!result.valid);

	// wrong piece hash count
	hashes.resize(256);
	result = picker.add_hashes(hash_request(0_file, 2, 0, 256, 0), hashes);
	TEST_CHECK(!result.valid);

	// wrong base layer
	hashes.resize(512);
	result = picker.add_hashes(hash_request(0_file, 1, 0, 512, 0), hashes);
	TEST_CHECK(!result.valid);

	// index out of range
	hashes.resize(512);
	result = picker.add_hashes(hash_request(0_file, 2, 512, 512, 0), hashes);
	TEST_CHECK(!result.valid);

	// totally bogus hashes
	hashes.resize(512);
	result = picker.add_hashes(hash_request(0_file, 2, 0, 512, 0), hashes);
	TEST_CHECK(!result.valid);

	// bad proof hash
	hashes.clear();
	auto pieces_start = full_tree.begin() + merkle_num_nodes(512) - 512;
	std::copy(pieces_start, pieces_start + 512, std::back_inserter(hashes));
	hashes.back()[1] ^= 0xaa;
	result = picker.add_hashes(hash_request(0_file, 2, 0, 512, 0), hashes);
	TEST_CHECK(!result.valid);
}

TORRENT_TEST(bad_block_hash)
{
	file_storage fs;
	fs.set_piece_length(16 * 1024);

	fs.add_file("test/tmp1", 4 * 512 * 16 * 1024);

	aux::vector<aux::vector<sha256_hash>, file_index_t> trees;
	trees.push_back(aux::vector<sha256_hash>(merkle_num_nodes(merkle_num_leafs(4 * 512))));

	aux::vector<sha256_hash> const full_tree = build_tree(4 * 512);
	trees.front()[0] = full_tree[0];

	aux::from_hex("0000000000000000000000000000000000000000000000000000000000000001"
		, trees.front()[trees.front().end_index() - merkle_num_leafs(4 * 512) + 1].data());

	hash_picker picker(fs, trees);

	std::vector<sha256_hash> hashes;
	auto leafs_start = full_tree.end() - merkle_num_leafs(4 * 512);
	std::copy(leafs_start, leafs_start + 512, std::back_inserter(hashes));
	for (int i = 3; i > 0; i = merkle_get_parent(i))
	{
		hashes.push_back(full_tree[merkle_get_sibling(i)]);
	}
	add_hashes_result result = picker.add_hashes(hash_request(0_file, 0, 0, 512, 10)
		, hashes);
	TEST_CHECK(result.valid);
	TEST_CHECK(result.hash_failed.count(1_piece) == 1);
	if (result.hash_failed.count(1_piece) == 1)
	{
		TEST_CHECK(result.hash_failed[1_piece].size() == 1);
		if (result.hash_failed[1_piece].size() == 1)
			TEST_CHECK(result.hash_failed[1_piece][0] == 0);
	}
}

TORRENT_TEST(set_block_hash)
{
	file_storage fs;
	fs.set_piece_length(4 * 16 * 1024);

	fs.add_file("test/tmp1", 4 * 512 * 16 * 1024);

	aux::vector<aux::vector<sha256_hash>, file_index_t> trees;
	trees.push_back(aux::vector<sha256_hash>(merkle_num_nodes(merkle_num_leafs(4 * 512))));

	aux::vector<sha256_hash> const full_tree = build_tree(4 * 512);
	trees.front() = full_tree;

	int const first_leaf = full_tree.end_index() - merkle_num_leafs(4 * 512);

	hash_picker picker(fs, trees);
	auto result = picker.set_block_hash(1_piece, default_block_size
		, full_tree[first_leaf + 5]);
	TEST_CHECK(result.status == set_block_hash_result::result::success);

	result = picker.set_block_hash(2_piece, default_block_size * 2
		, full_tree[first_leaf + 10]);
	TEST_CHECK(result.status == set_block_hash_result::result::success);

	result = picker.set_block_hash(2_piece, default_block_size * 2
		, sha256_hash("01234567890123456789012345678901"));
	TEST_CHECK(result.status == set_block_hash_result::result::block_hash_failed);

	// zero out the inner nodes for a piece along with a single leaf node
	// then add a bogus hash for the leaf
	trees.front()[merkle_get_parent(first_leaf + 12)].clear();
	trees.front()[merkle_get_parent(first_leaf + 14)].clear();
	trees.front()[first_leaf + 13].clear();

	result = picker.set_block_hash(3_piece, default_block_size, sha256_hash("01234567890123456789012345678901"));
	TEST_CHECK(result.status == set_block_hash_result::result::piece_hash_failed);

	TEST_CHECK(trees.front()[merkle_get_parent(first_leaf + 12)].is_all_zeros());
	TEST_CHECK(trees.front()[merkle_get_parent(first_leaf + 14)].is_all_zeros());
}

TORRENT_TEST(pass_piece)
{
	file_storage fs;
	fs.set_piece_length(4 * 16 * 1024);

	fs.add_file("test/tmp1", 4 * 512 * 16 * 1024);

	aux::vector<aux::vector<sha256_hash>, file_index_t> trees;
	trees.push_back(aux::vector<sha256_hash>(merkle_num_nodes(merkle_num_leafs(4 * 512))));

	aux::vector<sha256_hash> const full_tree = build_tree(4 * 512);
	trees.front()[0] = full_tree[0];

	hash_picker picker(fs, trees);

	int const first_leaf = full_tree.end_index() - merkle_num_leafs(4 * 512);

	for (int i = 0; i < 4; ++i)
	{
		auto result = picker.set_block_hash(0_piece, default_block_size * i
			, full_tree[first_leaf + i]);
		TEST_CHECK(result.status == set_block_hash_result::result::unknown);
	}

	auto pieces_start = full_tree.begin() + merkle_num_nodes(512) - 512;

	std::vector<sha256_hash> hashes;
	std::copy(pieces_start, pieces_start + 512, std::back_inserter(hashes));
	add_hashes_result result = picker.add_hashes(hash_request(0_file, 2, 0, 512, 8), hashes);
	TEST_CHECK(result.valid);
	TEST_EQUAL(result.hash_passed.size(), 1);
	if (result.hash_passed.size() == 1)
	{
		TEST_EQUAL(result.hash_passed[0], 0_piece);
	}
}

TORRENT_TEST(only_pick_have_pieces)
{
	file_storage fs;
	fs.set_piece_length(16 * 1024);

	fs.add_file("test/tmp1", 4 * 512 * 16 * 1024);

	aux::vector<aux::vector<sha256_hash>, file_index_t> trees;
	trees.push_back(aux::vector<sha256_hash>(merkle_num_nodes(merkle_num_leafs(4 * 512))));
	aux::from_hex("0000000000000000000000000000000000000000000000000000000000000001", trees.back()[0].data());

	hash_picker picker(fs, trees);

	typed_bitfield<piece_index_t> pieces;
	pieces.resize(4 * 512);
	pieces.set_bit(512_piece);
	pieces.set_bit(1537_piece);

	std::vector <hash_request> picked;
	for (int i = 0; i < 3; ++i)
		picked.push_back(picker.pick_hashes(pieces));
	TEST_EQUAL(picked[0].file, 0_file);
	TEST_EQUAL(picked[0].base, 0);
	TEST_EQUAL(picked[0].count, 512);
	TEST_EQUAL(picked[0].index, 512);
	TEST_EQUAL(picked[0].proof_layers, 10);
	TEST_EQUAL(picked[1].file, 0_file);
	TEST_EQUAL(picked[1].base, 0);
	TEST_EQUAL(picked[1].count, 512);
	TEST_EQUAL(picked[1].index, 1536);
	TEST_EQUAL(picked[1].proof_layers, 10);
	TEST_EQUAL(picked[2].count, 0);
}

TORRENT_TEST(validate_hash_request)
{
	file_storage fs;
	fs.set_piece_length(16 * 1024);
	fs.add_file("test/tmp1", 2048 * 16 * 1024);

	// the merkle tree for this file has 2048 blocks
	int const num_leaves = merkle_num_leafs(2048);
	int const num_layers = merkle_num_layers(num_leaves);

	int const max = std::numeric_limits<int>::max();
	int const min = std::numeric_limits<int>::min();

	// hash_request make function
	// (file_index_t const f, int const b, int const i, int const c, int const p)

	TEST_CHECK(validate_hash_request(hash_request(file_index_t{0}, 0, 0, 1, 0), fs));

	// file index out-of-range
	TEST_CHECK(!validate_hash_request(hash_request(file_index_t{1}, 0, 0, 1, 0), fs));
	TEST_CHECK(!validate_hash_request(hash_request(file_index_t{-1}, 0, 0, 1, 0), fs));
	TEST_CHECK(!validate_hash_request(hash_request(file_index_t{max}, 0, 0, 1, 0), fs));
	TEST_CHECK(!validate_hash_request(hash_request(file_index_t{min}, 0, 0, 1, 0), fs));

	// base out-of-range
	TEST_CHECK(!validate_hash_request(hash_request(file_index_t{0}, -1, 0, 1, 0), fs));
	TEST_CHECK(!validate_hash_request(hash_request(file_index_t{0}, num_layers, 0, 1, 0), fs));
	TEST_CHECK(!validate_hash_request(hash_request(file_index_t{0}, max, 0, 1, 0), fs));
	TEST_CHECK(!validate_hash_request(hash_request(file_index_t{0}, min, 0, 1, 0), fs));

	// base in-range
	TEST_CHECK(validate_hash_request(hash_request(file_index_t{0}, 0, 0, 1, 0), fs));
	TEST_CHECK(validate_hash_request(hash_request(file_index_t{0}, num_layers-1, 0, 1, 0), fs));

	// count out-of-range
	// the upper limit of count depends on base and index
	TEST_CHECK(!validate_hash_request(hash_request(file_index_t{0}, 0, 0, 0, 0), fs));
	TEST_CHECK(!validate_hash_request(hash_request(file_index_t{0}, 0, 0, num_leaves + 1, 0), fs));
	TEST_CHECK(!validate_hash_request(hash_request(file_index_t{0}, 0, 100, num_leaves - 100 + 1, 0), fs));
	TEST_CHECK(!validate_hash_request(hash_request(file_index_t{0}, 0, 0, 8193, 0), fs));
	TEST_CHECK(!validate_hash_request(hash_request(file_index_t{0}, 0, 0, min, 0), fs));
	TEST_CHECK(!validate_hash_request(hash_request(file_index_t{0}, 0, 0, max, 0), fs));

	TEST_CHECK(!validate_hash_request(hash_request(file_index_t{0}, 1, 0, num_leaves / 2 + 1, 0), fs));
	TEST_CHECK(!validate_hash_request(hash_request(file_index_t{0}, 2, 0, num_leaves / 4 + 1, 0), fs));
	TEST_CHECK(!validate_hash_request(hash_request(file_index_t{0}, 3, 0, num_leaves / 8 + 1, 0), fs));
	TEST_CHECK(!validate_hash_request(hash_request(file_index_t{0}, 3, 5, num_leaves / 8 - 5 + 1, 0), fs));

	// count in-range
	TEST_CHECK(validate_hash_request(hash_request(file_index_t{0}, 0, 100, num_leaves - 100, 0), fs));
	TEST_CHECK(validate_hash_request(hash_request(file_index_t{0}, 0, 0, 1, 0), fs));
	TEST_CHECK(validate_hash_request(hash_request(file_index_t{0}, 0, 0, num_leaves, 0), fs));

	TEST_CHECK(validate_hash_request(hash_request(file_index_t{0}, 1, 0, num_leaves / 2, 0), fs));
	TEST_CHECK(validate_hash_request(hash_request(file_index_t{0}, 2, 0, num_leaves / 4, 0), fs));
	TEST_CHECK(validate_hash_request(hash_request(file_index_t{0}, 3, 0, num_leaves / 8, 0), fs));
	TEST_CHECK(validate_hash_request(hash_request(file_index_t{0}, 3, 5, num_leaves / 8 - 5, 0), fs));

	// proof_layers out-of-range
	TEST_CHECK(!validate_hash_request(hash_request(file_index_t{0}, 0, 0, 1, num_layers), fs));
	TEST_CHECK(!validate_hash_request(hash_request(file_index_t{0}, 1, 0, 1, num_layers), fs));
	TEST_CHECK(!validate_hash_request(hash_request(file_index_t{0}, 1, 0, 1, min), fs));
	TEST_CHECK(!validate_hash_request(hash_request(file_index_t{0}, 1, 0, 1, max), fs));
	TEST_CHECK(!validate_hash_request(hash_request(file_index_t{0}, 1, 0, 1, -1), fs));

	// proof_layers in-range
	TEST_CHECK(validate_hash_request(hash_request(file_index_t{0}, 0, 0, 1, num_layers - 1), fs));
	TEST_CHECK(validate_hash_request(hash_request(file_index_t{0}, 1, 0, 1, num_layers - 2), fs));
}

