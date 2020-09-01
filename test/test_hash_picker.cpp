/*

Copyright (c) 2017, BitTorrent Inc.
Copyright (c) 2019-2020, Steven Siloti
Copyright (c) 2020, Arvid Norberg
Copyright (c) 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <iostream>

#include "libtorrent/aux_/hash_picker.hpp"
#include "libtorrent/peer_connection_interface.hpp"
#include "libtorrent/aux_/stat.hpp"
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
	aux::torrent_peer* peer_info_struct() const override { return m_torrent_peer; }
	void set_peer_info(aux::torrent_peer* pi) override { m_torrent_peer = pi; }
	bool is_outgoing() const override { return false; }
	void add_stat(std::int64_t, std::int64_t) override {}
	bool fast_reconnect() const override { return false; }
	bool is_choked() const override { return false; }
	bool failed() const override { return false; }
	aux::stat const& statistics() const override { return m_stat; }
	void get_peer_info(peer_info&) const override {}
#ifndef TORRENT_DISABLE_LOGGING
	bool should_log(peer_log_alert::direction_t) const override { return true; }
	void peer_log(peer_log_alert::direction_t
		, char const*, char const*, ...) const noexcept override TORRENT_FORMAT(4, 5) {}
#endif

	aux::torrent_peer* m_torrent_peer;
	aux::stat m_stat;
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

	aux::vector<aux::merkle_tree, file_index_t> trees;

	trees.push_back(aux::merkle_tree(merkle_num_nodes(merkle_num_leafs(4 * 512))));
	aux::from_hex("0000000000000000000000000000000000000000000000000000000000000001", trees.back()[0].data());
	trees.push_back(aux::merkle_tree(merkle_num_nodes(merkle_num_leafs(4 * 512))));
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

namespace {
sha256_hash from_hex(span<char const> str)
{
	sha256_hash ret;
	aux::from_hex(str, ret.data());
	return ret;
}
}

TORRENT_TEST(reject_piece_request)
{
	file_storage fs;
	fs.set_piece_length(16 * 1024);

	fs.add_file("test/tmp1", 4 * 512 * 16 * 1024);

	aux::vector<aux::merkle_tree, file_index_t> trees;
	auto const root = from_hex("0000000000000000000000000000000000000000000000000000000000000001");
	trees.emplace_back(4 * 512, 1, root.data());

	aux::hash_picker picker(fs, trees);

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

	aux::vector<aux::merkle_tree, file_index_t> trees;
	auto const full_tree = build_tree(4 * 512);
	sha256_hash const root = full_tree[0];
	trees.emplace_back(4 * 512, 1, root.data());

	aux::hash_picker picker(fs, trees);

	std::vector<sha256_hash> hashes;
	auto const pieces_start = full_tree.end_index() - merkle_num_leafs(4 * 512);
	for (int i = 0; i < 512; ++i) hashes.push_back(full_tree[pieces_start + i]);
	for (int i = 3; i > 0; i = merkle_get_parent(i))
	{
		hashes.push_back(full_tree[merkle_get_sibling(i)]);
	}
	aux::add_hashes_result result = picker.add_hashes(aux::hash_request(0_file, 0, 0, 512, 10)
		, hashes);
	TEST_CHECK(result.valid);

	result = picker.add_hashes(aux::hash_request(0_file, 0, 512, 512, 0)
		, span<sha256_hash const>(full_tree).last(merkle_num_leafs(4 * 512) - 512).first(512));
	TEST_CHECK(result.valid);

	hashes.clear();
	for (int i = 1024; i < 1536; ++i) hashes.push_back(full_tree[pieces_start + i]);
	for (int i = 5; i > 0; i = merkle_get_parent(i))
	{
		hashes.push_back(full_tree[merkle_get_sibling(i)]);
	}

	result = picker.add_hashes(aux::hash_request(0_file, 0, 1024, 512, 10)
		, hashes);
	TEST_CHECK(result.valid);

	result = picker.add_hashes(aux::hash_request(0_file, 0, 1536, 512, 0)
		, span<sha256_hash const>(full_tree).last(merkle_num_leafs(4 * 512) - 1536).first(512));
	TEST_CHECK(result.valid);

	TEST_CHECK(trees.front().build_vector() == full_tree);
}

TORRENT_TEST(add_piece_hashes)
{
	file_storage fs;
	fs.set_piece_length(4 * 16 * 1024);

	fs.add_file("test/tmp1", 4 * 1024 * 16 * 1024);

	aux::vector<aux::merkle_tree, file_index_t> trees;
	auto const full_tree = build_tree(4 * 1024);
	sha256_hash const root = full_tree[0];
	trees.emplace_back(4 * 1024, 4, root.data());

	aux::hash_picker picker(fs, trees);

	auto pieces_start = full_tree.begin() + merkle_num_nodes(1024) - 1024;

	std::vector<sha256_hash> hashes;
	std::copy(pieces_start, pieces_start + 512, std::back_inserter(hashes));
	hashes.push_back(full_tree[2]);
	aux::add_hashes_result result = picker.add_hashes(aux::hash_request(0_file, 2, 0, 512, 9), hashes);
	TEST_CHECK(result.valid);

	hashes.clear();
	std::copy(pieces_start + 512, pieces_start + 1024, std::back_inserter(hashes));
	result = picker.add_hashes(aux::hash_request(0_file, 2, 512, 512, 8), hashes);
	TEST_CHECK(result.valid);

	auto const cmp = trees.front().build_vector();
	TEST_CHECK(std::equal(cmp.begin(), cmp.begin() + merkle_num_nodes(1024), full_tree.begin()));
}

TORRENT_TEST(add_piece_hashes_padded)
{
	file_storage fs;
	fs.set_piece_length(4 * 16 * 1024);

	fs.add_file("test/tmp1", 4 * 1029 * 16 * 1024);

	aux::vector<aux::merkle_tree, file_index_t> trees;
	auto const full_tree = build_tree(4 * 1029);
	sha256_hash const root = full_tree[0];
	trees.emplace_back(4 * 1029, 4, root.data());

	aux::hash_picker picker(fs, trees);

	auto pieces_start = merkle_num_nodes(merkle_num_leafs(1029)) - merkle_num_leafs(1029);

	std::vector<sha256_hash> hashes;
	// 5 hashes left after 1024 rounds up to 8, 1024 + 8 = 1032
	std::copy(full_tree.begin() + pieces_start + 1024, full_tree.begin() + pieces_start + 1032
		, std::back_inserter(hashes));
	auto proof = merkle_get_parent(merkle_get_parent(merkle_get_parent(pieces_start + 1024)));
	while (proof > 0)
	{
		hashes.push_back(full_tree[merkle_get_sibling(proof)]);
		proof = merkle_get_parent(proof);
	}
	aux::add_hashes_result result = picker.add_hashes(aux::hash_request(0_file, 2, 1024, 8, 10), hashes);
	TEST_CHECK(result.valid);
}

TORRENT_TEST(add_piece_hashes_unpadded)
{
	file_storage fs;
	fs.set_piece_length(4 * 16 * 1024);

	fs.add_file("test/tmp1", 4 * 1029 * 16 * 1024);

	aux::vector<aux::merkle_tree, file_index_t> trees;
	auto const full_tree = build_tree(4 * 1029);
	sha256_hash const root = full_tree[0];
	trees.emplace_back(4 * 1029, 4, root.data());

	aux::hash_picker picker(fs, trees);

	auto pieces_start = merkle_num_nodes(merkle_num_leafs(1029)) - merkle_num_leafs(1029);

	std::vector<sha256_hash> hashes;
	std::copy(full_tree.begin() + pieces_start + 1024, full_tree.begin() + pieces_start + 1029
		, std::back_inserter(hashes));
	auto proof = merkle_get_parent(merkle_get_parent(merkle_get_parent(pieces_start + 1024)));
	while (proof > 0)
	{
		hashes.push_back(full_tree[merkle_get_sibling(proof)]);
		proof = merkle_get_parent(proof);
	}
	aux::add_hashes_result result = picker.add_hashes(aux::hash_request(0_file, 2, 1024, 5, 10), hashes);
	TEST_CHECK(result.valid);
}

TORRENT_TEST(add_bad_hashes)
{
	file_storage fs;
	fs.set_piece_length(4 * 16 * 1024);

	fs.add_file("test/tmp1", 4 * 512 * 16 * 1024);

	aux::vector<aux::merkle_tree, file_index_t> trees;
	auto const full_tree = build_tree(4 * 512);
	sha256_hash const root = full_tree[0];
	trees.emplace_back(4 * 512, 4, root.data());

	aux::hash_picker picker(fs, trees);

	// totally bogus hashes
	std::vector<sha256_hash> hashes(512);
	auto result = picker.add_hashes(aux::hash_request(0_file, 2, 0, 512, 0), hashes);
	TEST_CHECK(!result.valid);

	// bad proof hash
	hashes.clear();
	auto const pieces_start = full_tree.end_index() - 512;
	for (int i = 0; i < 512; ++i) hashes.push_back(full_tree[pieces_start + i]);
	hashes.back()[1] ^= 0xaa;
	result = picker.add_hashes(aux::hash_request(0_file, 2, 0, 512, 0), hashes);
	TEST_CHECK(!result.valid);
}

TORRENT_TEST(bad_block_hash)
{
	file_storage fs;
	fs.set_piece_length(16 * 1024);

	fs.add_file("test/tmp1", 4 * 512 * 16 * 1024);

	auto const full_tree = build_tree(4 * 512);

	aux::vector<aux::merkle_tree, file_index_t> trees;
	trees.emplace_back(4 * 512, 1, full_tree[0].data());

	sha256_hash hash;
	aux::from_hex("0000000000000000000000000000000000000000000000000000000000000001"
		, hash.data());

	trees.front().set_block(1, hash);

	aux::hash_picker picker(fs, trees);

	std::vector<sha256_hash> hashes;
	auto leafs_start = full_tree.end() - merkle_num_leafs(4 * 512);
	std::copy(leafs_start, leafs_start + 512, std::back_inserter(hashes));
	for (int i = 3; i > 0; i = merkle_get_parent(i))
	{
		hashes.push_back(full_tree[merkle_get_sibling(i)]);
	}
	aux::add_hashes_result result = picker.add_hashes(aux::hash_request(0_file, 0, 0, 512, 10)
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

	aux::vector<aux::merkle_tree, file_index_t> trees;
	auto const full_tree = build_tree(4 * 512);
	trees.emplace_back(4 * 512, 4, full_tree[0].data());
	trees.front().load_tree(full_tree);

	int const first_leaf = full_tree.end_index() - merkle_num_leafs(4 * 512);

	aux::hash_picker picker(fs, trees);
	auto result = picker.set_block_hash(1_piece, default_block_size
		, full_tree[first_leaf + 5]);
	TEST_CHECK(result.status == aux::set_block_hash_result::result::success);

	result = picker.set_block_hash(2_piece, default_block_size * 2
		, full_tree[first_leaf + 10]);
	TEST_CHECK(result.status == aux::set_block_hash_result::result::success);

	result = picker.set_block_hash(2_piece, default_block_size * 2
		, sha256_hash("01234567890123456789012345678901"));
	TEST_CHECK(result.status == aux::set_block_hash_result::result::block_hash_failed);
}

TORRENT_TEST(set_block_hash_fail)
{
	file_storage fs;
	fs.set_piece_length(4 * 16 * 1024);

	fs.add_file("test/tmp1", 4 * 512 * 16 * 1024);

	aux::vector<aux::merkle_tree, file_index_t> trees;
	auto full_tree = build_tree(4 * 512);
	trees.emplace_back(4 * 512, 4, full_tree[0].data());

	// zero out the inner nodes for a piece along with a single leaf node
	// then add a bogus hash for the leaf

	int const first_leaf = full_tree.end_index() - merkle_num_leafs(4 * 512);

	full_tree[merkle_get_parent(first_leaf + 12)].clear();
	full_tree[merkle_get_parent(first_leaf + 14)].clear();
	auto const orig_hash = full_tree[first_leaf + 13];
	full_tree[first_leaf + 13].clear();

	trees.front().load_tree(full_tree);

	aux::hash_picker picker(fs, trees);

	TEST_CHECK(picker.set_block_hash(3_piece, 0, full_tree[first_leaf + 12]).status
		== aux::set_block_hash_result::result::unknown);
	TEST_CHECK(picker.set_block_hash(3_piece, 2 * default_block_size, full_tree[first_leaf + 14]).status
		== aux::set_block_hash_result::result::unknown);
	TEST_CHECK(picker.set_block_hash(3_piece, 3 * default_block_size, full_tree[first_leaf + 15]).status
		== aux::set_block_hash_result::result::unknown);

	auto result = picker.set_block_hash(3_piece, default_block_size, sha256_hash("01234567890123456789012345678901"));
	TEST_CHECK(result.status == aux::set_block_hash_result::result::piece_hash_failed);

	TEST_CHECK(trees.front()[merkle_get_parent(first_leaf + 12)].is_all_zeros());
	TEST_CHECK(trees.front()[merkle_get_parent(first_leaf + 14)].is_all_zeros());

	result = picker.set_block_hash(3_piece, default_block_size, orig_hash);
	TEST_CHECK(result.status == aux::set_block_hash_result::result::success);
}

TORRENT_TEST(pass_piece)
{
	file_storage fs;
	fs.set_piece_length(4 * 16 * 1024);

	fs.add_file("test/tmp1", 4 * 512 * 16 * 1024);

	auto const full_tree = build_tree(4 * 512);

	aux::vector<aux::merkle_tree, file_index_t> trees;
	sha256_hash root = full_tree[0];
	trees.emplace_back(4 * 512, 4, root.data());

	aux::hash_picker picker(fs, trees);

	int const first_leaf = full_tree.end_index() - merkle_num_leafs(4 * 512);

	for (int i = 0; i < 4; ++i)
	{
		auto result = picker.set_block_hash(0_piece, default_block_size * i
			, full_tree[first_leaf + i]);
		TEST_CHECK(result.status == aux::set_block_hash_result::result::unknown);
	}

	auto const pieces_start = full_tree.begin() + merkle_num_nodes(512) - 512;

	std::vector<sha256_hash> hashes;
	std::copy(pieces_start, pieces_start + 512, std::back_inserter(hashes));
	aux::add_hashes_result result = picker.add_hashes(aux::hash_request(0_file, 2, 0, 512, 8), hashes);
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

	aux::vector<aux::merkle_tree, file_index_t> trees;
	sha256_hash root = from_hex("0000000000000000000000000000000000000000000000000000000000000001");
	trees.emplace_back(4 * 512, 1, root.data());

	aux::hash_picker picker(fs, trees);

	typed_bitfield<piece_index_t> pieces;
	pieces.resize(4 * 512);
	pieces.set_bit(512_piece);
	pieces.set_bit(1537_piece);

	std::vector<aux::hash_request> picked;
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
	using hash_request = aux::hash_request;

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

namespace {

int const num_blocks = 260;
auto const f = build_tree(num_blocks);
int const num_leafs = merkle_num_leafs(num_blocks);
int const num_nodes = merkle_num_nodes(num_leafs);
int const num_pad_leafs = num_leafs - num_blocks;

}

TORRENT_TEST(load_tree)
{
	// test with full tree and valid root
	{
		aux::merkle_tree t(260, 1, f[0].data());
		t.load_tree(f);
		for (int i = 0; i < num_nodes - num_pad_leafs; ++i)
		{
			TEST_CHECK(t.has_node(i));
			TEST_CHECK(t.compare_node(i, f[i]));
		}
		for (int i = num_nodes - num_pad_leafs; i < num_nodes; ++i)
		{
			TEST_CHECK(!t.has_node(i));
			TEST_CHECK(t.compare_node(i, f[i]));
		}
	}

	// mismatching root hash
	{
		sha256_hash const bad_root("01234567890123456789012345678901");
		aux::merkle_tree t(260, 1, bad_root.data());
		t.load_tree(f);
		TEST_CHECK(t.has_node(0));
		for (int i = 1; i < num_nodes; ++i)
			TEST_CHECK(!t.has_node(i));
	}

	// mismatching size
	{
		aux::merkle_tree t(260, 1, f[0].data());
		t.load_tree(span<sha256_hash const>(f).first(f.end_index() - 1));
		TEST_CHECK(t.has_node(0));
		for (int i = 1; i < num_nodes; ++i)
			TEST_CHECK(!t.has_node(i));
	}
}

TORRENT_TEST(load_sparse_tree)
{
	// test with full tree and valid root
	{
		std::vector<bool> mask(f.size(), true);
		aux::merkle_tree t(num_blocks, 1, f[0].data());
		t.load_sparse_tree(f, mask);
		for (int i = 0; i < num_nodes - num_pad_leafs; ++i)
		{
			TEST_CHECK(t.has_node(i));
			TEST_CHECK(t.compare_node(i, f[i]));
		}
		for (int i = num_nodes - num_pad_leafs; i < num_nodes; ++i)
		{
			TEST_CHECK(!t.has_node(i));
			TEST_CHECK(t.compare_node(i, f[i]));
		}
	}

	// mismatching root hash
	{
		sha256_hash const bad_root("01234567890123456789012345678901");
		aux::merkle_tree t(num_blocks, 1, bad_root.data());
		std::vector<bool> mask(f.size(), false);
		mask[1] = true;
		mask[2] = true;
		t.load_sparse_tree(span<sha256_hash const>(f).subspan(1, 2), mask);
		TEST_CHECK(t.has_node(0));
		for (int i = 1; i < num_nodes; ++i)
			TEST_CHECK(!t.has_node(i));
	}

	// block layer
	{
		aux::merkle_tree t(num_blocks, 1, f[0].data());
		int const first_block = merkle_first_leaf(num_leafs);
		int const end_block = first_block + num_blocks;
		std::vector<bool> mask(f.size(), false);
		for (int i = first_block; i < end_block; ++i)
			mask[std::size_t(i)] = true;
		t.load_sparse_tree(span<sha256_hash const>(f).subspan(first_block, num_blocks), mask);
		for (int i = 0; i < num_nodes - num_pad_leafs; ++i)
		{
			TEST_CHECK(t.has_node(i));
			TEST_CHECK(t.compare_node(i, f[i]));
		}
		for (int i = num_nodes - num_pad_leafs; i < num_nodes; ++i)
		{
			TEST_CHECK(!t.has_node(i));
			TEST_CHECK(t.compare_node(i, f[i]));
		}
	}

	// piece layer
	{
		int const num_pieces = (num_blocks + 1) / 2;
		int const first_piece = merkle_first_leaf(merkle_num_leafs(num_pieces));
		aux::merkle_tree t(num_blocks, 2, f[0].data());
		std::vector<bool> mask(f.size(), false);
		for (int i = first_piece, end = i + num_pieces; i < end; ++i)
			mask[std::size_t(i)] = true;
		t.load_sparse_tree(span<sha256_hash const>(f).subspan(first_piece, num_pieces), mask);
		int const end_piece_layer = first_piece + merkle_num_leafs(num_pieces);
		for (int i = 0; i < end_piece_layer; ++i)
		{
			TEST_CHECK(t.has_node(i));
			TEST_CHECK(t.compare_node(i, f[i]));
		}
		for (int i = end_piece_layer; i < num_nodes; ++i)
		{
			TEST_CHECK(!t.has_node(i));
		}
	}
}

namespace {
void test_roundtrip(aux::merkle_tree const& t
	, int const block_count
	, int const blocks_per_piece)
{
	// TODO: use structured bindings in C++17
	aux::vector<bool> mask;
	std::vector<sha256_hash> tree;
	std::tie(tree, mask) = t.build_sparse_vector();

	aux::merkle_tree t2(block_count, blocks_per_piece, f[0].data());
	t2.load_sparse_tree(tree, mask);

	TEST_CHECK(t.build_vector() == t2.build_vector());
}
}

TORRENT_TEST(roundtrip_merkle_tree)
{
	// empty tree
	{
		aux::merkle_tree t(num_blocks, 1, f[0].data());
		test_roundtrip(t, num_blocks, 1);
	}

	// full tree
	{
		aux::merkle_tree t(num_blocks, 1, f[0].data());
		t.load_tree(f);
		test_roundtrip(t, num_blocks, 1);
	}

	// piece layer tree
	{
		aux::merkle_tree t(num_blocks, 2, f[0].data());
		auto sparse_tree = f;
		for (int i = f.end_index() / 2; i < f.end_index(); ++i)
			sparse_tree[i] = lt::sha256_hash{};
		t.load_tree(sparse_tree);
		test_roundtrip(t, num_blocks, 2);
	}

	// some hashes tree
	{
		aux::merkle_tree t(num_blocks, 2, f[0].data());
		auto sparse_tree = f;
		for (int i = f.end_index() / 4; i < f.end_index(); ++i)
		{
			if ((i % 3) == 0)
				sparse_tree[i] = lt::sha256_hash{};
		}

		t.load_tree(sparse_tree);
		test_roundtrip(t, num_blocks, 2);
	}

	// some more hashes tree
	{
		aux::merkle_tree t(num_blocks, 2, f[0].data());
		auto sparse_tree = f;
		for (int i = f.end_index() / 4; i < f.end_index(); ++i)
		{
			if ((i % 4) == 0)
				sparse_tree[i] = lt::sha256_hash{};
		}

		t.load_tree(sparse_tree);
		test_roundtrip(t, num_blocks, 2);
	}

	// 1 block tree
	{
		aux::merkle_tree t(1, 256, f[0].data());
		t.load_tree(span<sha256_hash const>(f).first(1));
		test_roundtrip(t, 1, 256);
	}

	// 2 block tree
	{
		aux::merkle_tree t(2, 256, f[0].data());
		t.load_tree(span<sha256_hash const>(f).first(3));
		test_roundtrip(t, 2, 256);
	}

	// 2 block, partial tree
	{
		auto pf = f;
		pf.resize(3);
		pf[2].clear();
		aux::merkle_tree t(2, 256, f[0].data());
		t.load_tree(pf);
		test_roundtrip(t, 2, 256);
	}
}

TORRENT_TEST(small_tree)
{
	// a tree with a single block but large piece size
	aux::merkle_tree t(1, 256, f[0].data());

	TEST_CHECK(t.build_vector() == std::vector<lt::sha256_hash>{f[0]});
}

// the top 4 layers of the tree:
//                        0
//             1                     2
//       3          4            5         6
//   7     8     9    10    11    12     13   14
// 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30

TORRENT_TEST(add_proofs_left_path)
{
	aux::merkle_tree t(260, 1, f[0].data());

	std::vector<std::pair<sha256_hash, sha256_hash>> const proofs{
		{f[19], f[20]},
		{f[9], f[10]},
		{f[3], f[4]},
		{f[1], f[2]}
	};

	t.add_proofs(19, proofs);

	TEST_CHECK(t[19] == f[19]);
	TEST_CHECK(t[20] == f[20]);
	TEST_CHECK(t[9]  == f[9]);
	TEST_CHECK(t[10] == f[10]);
	TEST_CHECK(t[3]  == f[3]);
	TEST_CHECK(t[4]  == f[4]);
	TEST_CHECK(t[1]  == f[1]);
	TEST_CHECK(t[2]  == f[2]);
}

TORRENT_TEST(add_proofs_right_path)
{
	aux::merkle_tree t(260, 1, f[0].data());

	std::vector<std::pair<sha256_hash, sha256_hash>> const proofs{
		{f[19], f[20]},
		{f[9], f[10]},
		{f[3], f[4]},
		{f[1], f[2]}
	};

	// the only difference is that we say index 20 is the start index,
	// everything else is the same
	t.add_proofs(20, proofs);

	TEST_CHECK(t[19] == f[19]);
	TEST_CHECK(t[20] == f[20]);
	TEST_CHECK(t[9]  == f[9]);
	TEST_CHECK(t[10] == f[10]);
	TEST_CHECK(t[3]  == f[3]);
	TEST_CHECK(t[4]  == f[4]);
	TEST_CHECK(t[1]  == f[1]);
	TEST_CHECK(t[2]  == f[2]);
}

TORRENT_TEST(add_proofs_far_left_path)
{
	aux::merkle_tree t(260, 1, f[0].data());

	std::vector<std::pair<sha256_hash, sha256_hash>> const proofs{
		{f[15], f[16]},
		{f[7], f[8]},
		{f[3], f[4]},
		{f[1], f[2]}
	};

	t.add_proofs(15, proofs);

	TEST_CHECK(t[15] == f[15]);
	TEST_CHECK(t[16] == f[16]);
	TEST_CHECK(t[7]  == f[7]);
	TEST_CHECK(t[8]  == f[8]);
	TEST_CHECK(t[3]  == f[3]);
	TEST_CHECK(t[4]  == f[4]);
	TEST_CHECK(t[1]  == f[1]);
	TEST_CHECK(t[2]  == f[2]);
}

TORRENT_TEST(add_proofs_far_right_path)
{
	aux::merkle_tree t(260, 1, f[0].data());

	std::vector<std::pair<sha256_hash, sha256_hash>> const proofs{
		{f[29], f[30]},
		{f[13], f[14]},
		{f[5], f[6]},
		{f[1], f[2]}
	};

	t.add_proofs(29, proofs);

	TEST_CHECK(t[29] == f[29]);
	TEST_CHECK(t[30] == f[30]);
	TEST_CHECK(t[13] == f[13]);
	TEST_CHECK(t[14] == f[14]);
	TEST_CHECK(t[5]  == f[5]);
	TEST_CHECK(t[6]  == f[6]);
	TEST_CHECK(t[1]  == f[1]);
	TEST_CHECK(t[2]  == f[2]);
}

TORRENT_TEST(add_hashes_pass)
{
	std::vector<sha256_hash> const subtree{
		f[3],
		f[7], f[8],
		f[15], f[16], f[17], f[18],
	};

	aux::merkle_tree t(260, 1, f[0].data());
	auto const failed = t.add_hashes(15, subtree);
	TEST_CHECK(failed.empty());

	TEST_CHECK(t[3]  == f[3]);
	TEST_CHECK(t[7]  == f[7]);
	TEST_CHECK(t[8]  == f[8]);
	TEST_CHECK(t[15] == f[15]);
	TEST_CHECK(t[16] == f[16]);
	TEST_CHECK(t[17] == f[17]);
	TEST_CHECK(t[18] == f[18]);
}

using p = std::map<piece_index_t, std::vector<int>>;

// this is the full tree:
//                        0
//             1                     2
//       3          4            5         6
//   7     8     9    10    11    12     13   14
// 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30
TORRENT_TEST(add_hashes_fail1)
{
	std::vector<sha256_hash> const subtree{
		f[3],
		f[7], f[8],
		f[15], f[16], f[17], f[18],
	};

	aux::merkle_tree t(13, 1, f[0].data());

	// this is an invalid hash
	t.set_block(1, sha256_hash("01234567890123456789012345678901"));

	auto const failed = t.add_hashes(15, subtree);
	TEST_CHECK((failed == p{{piece_index_t{1}, {0}}}));

	TEST_CHECK(t[3]  == f[3]);
	TEST_CHECK(t[7]  == f[7]);
	TEST_CHECK(t[8]  == f[8]);
	TEST_CHECK(t[15] == f[15]);
	TEST_CHECK(t[16] == f[16]);
	TEST_CHECK(t[17] == f[17]);
	TEST_CHECK(t[18] == f[18]);
}

TORRENT_TEST(add_hashes_fail2)
{
	std::vector<sha256_hash> const subtree{
		f[3],
		f[7], f[8],
		f[15], f[16], f[17], f[18],
	};

	aux::merkle_tree t(13, 2, f[0].data());

	// this is an invalid hash
	t.set_block(1, sha256_hash("01234567890123456789012345678901"));
	t.set_block(2, sha256_hash("01234567890123456789012345678901"));
	t.set_block(3, sha256_hash("01234567890123456789012345678901"));

	auto const failed = t.add_hashes(15, subtree);
	TEST_CHECK((failed == p{{piece_index_t{0}, {1}}, {piece_index_t{1}, {0, 1}}}));

	TEST_CHECK(t[3]  == f[3]);
	TEST_CHECK(t[7]  == f[7]);
	TEST_CHECK(t[8]  == f[8]);
	TEST_CHECK(t[15] == f[15]);
	TEST_CHECK(t[16] == f[16]);
	TEST_CHECK(t[17] == f[17]);
	TEST_CHECK(t[18] == f[18]);
}

// the 4 layers of the tree:
//                        0
//             1                     2
//       3          4            5         6
//   7     8     9    10    11    12     13   14
// 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30

TORRENT_TEST(sparse_merkle_tree_block_layer)
{
	aux::merkle_tree t(260, 2, f[0].data());

	t.load_tree(span<sha256_hash const>(f).first(int(t.size())));

	for (int i = 0; i < int(t.size()); ++i)
	{
		std::cout << i << '\n';
		TEST_CHECK(t[i] == f[i]);
	}
}

TORRENT_TEST(get_piece_layer)
{
	// 8 blocks per piece.
	aux::merkle_tree t(260, 8, f[0].data());
	t.load_tree(span<sha256_hash const>(f).first(int(t.size())));

	int const num_pieces = (260 + 7) / 8;
	int const piece_layer_size = merkle_num_leafs(num_pieces);
	int const piece_layer_start = merkle_first_leaf(piece_layer_size);
	auto const piece_layer = t.get_piece_layer();

	TEST_EQUAL(num_pieces, int(piece_layer.size()));
	for (int i = 0; i < int(piece_layer.size()); ++i)
	{
		TEST_CHECK(t[piece_layer_start + i] == piece_layer[i]);
	}
}

// TODO: add test for load_piece_layer()
// TODO: add test for get_piece_layer() when tree is in piece-layer mode

namespace {
using s = span<sha256_hash const>;

span<sha256_hash const> range(std::vector<sha256_hash> const& c, int first, int count)
{
	return s(c).subspan(first, count);
}
}

TORRENT_TEST(merkle_tree_get_hashes)
{
	aux::merkle_tree t(260, 2, f[0].data());

	t.load_tree(span<sha256_hash const>(f).first(int(t.size())));

	// all nodes leaf layer
	{
		auto h = t.get_hashes(0, 0, 260, 0);
		TEST_CHECK(s(h) == range(f, 511, 260));
	}

	// all nodes leaf layer but the first
	{
		auto h = t.get_hashes(0, 1, 259, 0);
		TEST_CHECK(s(h) == range(f, 512, 259));
	}

	// all nodes leaf layer but the last
	{
		auto h = t.get_hashes(0, 0, 259, 0);
		TEST_CHECK(s(h) == range(f, 511, 259));
	}

	// one layer up
	{
		auto h = t.get_hashes(1, 0, 256, 0);
		TEST_CHECK(s(h) == range(f, 255, 256));
	}

	// one layer up + one layer proof
	{
		auto h = t.get_hashes(1, 0, 4, 2);
		TEST_CHECK(s(h).first(4) == range(f, 255, 4));

		// the proof is the sibling to the root of the tree we got back.
		// the hashes are rooted at 255 / 2 / 2 = 63
		std::vector<sha256_hash> const proofs{f[merkle_get_sibling(63)]};
		TEST_CHECK(s(h).subspan(4) == s(proofs));
	}

	// one layer up, hashes 2 - 10, 5 proof layers
	{
		auto h = t.get_hashes(1, 2, 8, 5);
		TEST_CHECK(s(h).first(8) == range(f, 255 + 2, 8));

		// the proof is the sibling to the root of the tree we got back.
		int const start_proofs = merkle_get_parent(merkle_get_parent(merkle_get_parent(257)));
		std::vector<sha256_hash> const proofs{
			f[merkle_get_sibling(start_proofs)]
			, f[merkle_get_sibling(merkle_get_parent(start_proofs))]
			, f[merkle_get_sibling(merkle_get_parent(merkle_get_parent(start_proofs)))]
			};
		TEST_CHECK(s(h).subspan(8) == s(proofs));
	}

	// full tree
	{
		auto h = t.get_hashes(0, 0, 512, 8);
		TEST_CHECK(s(h) == range(f, 511, 512));
		// there won't be any proofs, since we got the full tree
	}

	// second half of the tree
	{
		auto h = t.get_hashes(0, 256, 256, 8);
		TEST_CHECK(s(h).first(256) == range(f, 511 + 256, 256));

		// there just one proof hash
		std::vector<sha256_hash> const proofs{ f[1] };
		TEST_CHECK(s(h).subspan(256) == s(proofs));
	}

	// 3rd quarter of the tree
	{
		auto h = t.get_hashes(0, 256, 128, 8);
		TEST_CHECK(s(h).first(128) == range(f, 511 + 256, 128));

		// there just two proof hashes
		std::vector<sha256_hash> const proofs{ f[6], f[1] };
		TEST_CHECK(s(h).subspan(128) == s(proofs));
	}

	// 3rd quarter of the tree, starting one layer up
	{
		auto h = t.get_hashes(1, 128, 64, 7);
		TEST_CHECK(s(h).first(64) == range(f, 255 + 128, 64));

		// still just two proof hashes
		std::vector<sha256_hash> const proofs{ f[6], f[1] };
		TEST_CHECK(s(h).subspan(64) == s(proofs));
	}

	// 3rd quarter of the tree, starting one layer up
	// request no proof hashes
	{
		auto h = t.get_hashes(1, 128, 64, 0);
		TEST_CHECK(s(h) == range(f, 255 + 128, 64));
	}
}
