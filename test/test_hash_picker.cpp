/*

Copyright (c) 2017, BitTorrent Inc.
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

struct mock_peer_connection : peer_connection_interface
{
	virtual tcp::endpoint const& remote() const { return m_remote; }
	virtual tcp::endpoint local_endpoint() const { return {}; }
	virtual void disconnect(error_code const& ec
		, operation_t op, disconnect_severity_t error = peer_connection_interface::normal) {}
	virtual peer_id const& pid() const { return m_pid; }
	virtual peer_id our_pid() const { return m_pid; }
	virtual void set_holepunch_mode() {}
	virtual torrent_peer* peer_info_struct() const { return m_torrent_peer; }
	virtual void set_peer_info(torrent_peer* pi) { m_torrent_peer = pi; }
	virtual bool is_outgoing() const { return false; }
	virtual void add_stat(std::int64_t downloaded, std::int64_t uploaded) {}
	virtual bool fast_reconnect() const { return false; }
	virtual bool is_choked() const { return false; }
	virtual bool failed() const { return false; }
	virtual lt::stat const& statistics() const { return m_stat; }
	virtual void get_peer_info(peer_info& p) const {}
#ifndef TORRENT_DISABLE_LOGGING
	virtual bool should_log(peer_log_alert::direction_t direction) const { return true; }
	virtual void peer_log(peer_log_alert::direction_t direction
		, char const* event, char const* fmt = "", ...) const noexcept TORRENT_FORMAT(4, 5) {}
#endif

	torrent_peer* m_torrent_peer;
	lt::stat m_stat;
	tcp::endpoint m_remote;
	peer_id m_pid;
};

TORRENT_TEST(pick_piece_layer)
{
	file_storage fs;
	fs.set_piece_length(16 * 1024);

	fs.add_file("test/tmp1", 4 * 512 * 16 * 1024);
	fs.add_file("test/tmp2", 4 * 512 * 16 * 1024);

	aux::vector<std::vector<sha256_hash>, file_index_t> trees;

	trees.push_back(std::vector<sha256_hash>(merkle_num_nodes(merkle_num_leafs(4 * 512))));
	aux::from_hex("0000000000000000000000000000000000000000000000000000000000000001", trees.back()[0].data());
	trees.push_back(std::vector<sha256_hash>(merkle_num_nodes(merkle_num_leafs(4 * 512))));
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
	TEST_EQUAL(picked[0].file, 0);
	TEST_EQUAL(picked[0].base, 0);
	TEST_EQUAL(picked[0].count, 512);
	TEST_EQUAL(picked[0].index, 0);
	TEST_EQUAL(picked[0].proof_layers, 10);
	TEST_EQUAL(picked[1].file, 0);
	TEST_EQUAL(picked[1].base, 0);
	TEST_EQUAL(picked[1].count, 512);
	TEST_EQUAL(picked[1].index, 512);
	TEST_EQUAL(picked[1].proof_layers, 10);

	picked = picker.pick_hashes(pieces, 3, &mock_peer2);
	TEST_EQUAL(int(picked.size()), 3);
	TEST_EQUAL(picked[0].file, 0);
	TEST_EQUAL(picked[0].base, 0);
	TEST_EQUAL(picked[0].count, 512);
	TEST_EQUAL(picked[0].index, 1024);
	TEST_EQUAL(picked[0].proof_layers, 10);
	TEST_EQUAL(picked[1].file, 0);
	TEST_EQUAL(picked[1].base, 0);
	TEST_EQUAL(picked[1].count, 512);
	TEST_EQUAL(picked[1].index, 1536);
	TEST_EQUAL(picked[1].proof_layers, 10);
	TEST_EQUAL(picked[2].file, 1);
	TEST_EQUAL(picked[2].base, 0);
	TEST_EQUAL(picked[2].count, 512);
	TEST_EQUAL(picked[2].index, 0);
	TEST_EQUAL(picked[2].proof_layers, 10);

	picked = picker.pick_hashes(pieces, 4, &mock_peer1);
	TEST_EQUAL(int(picked.size()), 3);
	TEST_EQUAL(picked[0].file, 1);
	TEST_EQUAL(picked[0].base, 0);
	TEST_EQUAL(picked[0].count, 512);
	TEST_EQUAL(picked[0].index, 512);
	TEST_EQUAL(picked[0].proof_layers, 10);
	TEST_EQUAL(picked[1].file, 1);
	TEST_EQUAL(picked[1].base, 0);
	TEST_EQUAL(picked[1].count, 512);
	TEST_EQUAL(picked[1].index, 1024);
	TEST_EQUAL(picked[1].proof_layers, 10);
	TEST_EQUAL(picked[2].file, 1);
	TEST_EQUAL(picked[2].base, 0);
	TEST_EQUAL(picked[2].count, 512);
	TEST_EQUAL(picked[2].index, 1536);
	TEST_EQUAL(picked[2].proof_layers, 10);
}

TORRENT_TEST(reject_piece_request)
{
	file_storage fs;
	fs.set_piece_length(16 * 1024);

	fs.add_file("test/tmp1", 4 * 512 * 16 * 1024);

	aux::vector<std::vector<sha256_hash>, file_index_t> trees;
	trees.push_back(std::vector<sha256_hash>(merkle_num_nodes(merkle_num_leafs(4 * 512))));
	aux::from_hex("0000000000000000000000000000000000000000000000000000000000000001", trees.back()[0].data());

	hash_picker picker(fs, trees);

	typed_bitfield<piece_index_t> pieces;
	pieces.resize(4 * 512);
	pieces.set_all();

	mock_peer_connection mock_peer1;
	mock_peer1.m_torrent_peer = (torrent_peer*)0x1;

	auto picked = picker.pick_hashes(pieces, 2, &mock_peer1);
	for (auto const& req : picked)
	{
		picker.hashes_rejected(&mock_peer1, req);
	}

	auto picked2 = picker.pick_hashes(pieces, 2, &mock_peer1);
	TEST_CHECK(picked == picked2);
}

TORRENT_TEST(add_leaf_hashes)
{
	file_storage fs;
	fs.set_piece_length(16 * 1024);

	fs.add_file("test/tmp1", 4 * 512 * 16 * 1024);

	aux::vector<std::vector<sha256_hash>, file_index_t> trees;
	trees.push_back(std::vector<sha256_hash>(merkle_num_nodes(merkle_num_leafs(4 * 512))));

	std::vector<sha256_hash> full_tree(trees[0].size());

	for (int i = 0; i < 4 * 512; i++)
	{
		std::uint32_t hash[32 / 4];
		std::fill(std::begin(hash), std::end(hash), i + 1);
		full_tree[full_tree.size() - merkle_num_leafs(4 * 512) + i] = sha256_hash((char*)hash);
	}

	merkle_fill_tree(full_tree, merkle_num_leafs(4 * 512));
	trees[0][0] = full_tree[0];

	hash_picker picker(fs, trees);

	std::vector<sha256_hash> hashes;
	auto leafs_start = full_tree.end() - merkle_num_leafs(4 * 512);
	std::copy(leafs_start, leafs_start + 512, std::back_inserter(hashes));
	for (int i = 3; i > 0; i = merkle_get_parent(i))
	{
		hashes.push_back(full_tree[merkle_get_sibling(i)]);
	}
	add_hashes_result result = picker.add_hashes(hash_request(0, 0, 0, 512, 10)
		, hashes);
	TEST_CHECK(result.valid);

	result = picker.add_hashes(hash_request(0, 0, 512, 512, 0)
		, span<sha256_hash>(full_tree).last(merkle_num_leafs(4 * 512) - 512).first(512));
	TEST_CHECK(result.valid);

	hashes.clear();
	std::copy(leafs_start + 1024, leafs_start + 1536, std::back_inserter(hashes));
	for (int i = 5; i > 0; i = merkle_get_parent(i))
	{
		hashes.push_back(full_tree[merkle_get_sibling(i)]);
	}

	result = picker.add_hashes(hash_request(0, 0, 1024, 512, 10)
		, hashes);
	TEST_CHECK(result.valid);

	result = picker.add_hashes(hash_request(0, 0, 1536, 512, 0)
		, span<sha256_hash>(full_tree).last(merkle_num_leafs(4 * 512) - 1536).first(512));
	TEST_CHECK(result.valid);

	TEST_CHECK(trees[0] == full_tree);
}

TORRENT_TEST(add_piece_hashes)
{
	file_storage fs;
	fs.set_piece_length(4 * 16 * 1024);

	fs.add_file("test/tmp1", 4 * 1024 * 16 * 1024);

	aux::vector<std::vector<sha256_hash>, file_index_t> trees;
	trees.push_back(std::vector<sha256_hash>(merkle_num_nodes(merkle_num_leafs(4 * 1024))));

	std::vector<sha256_hash> full_tree(trees[0].size());

	for (int i = 0; i < 4 * 1024; i++)
	{
		std::uint32_t hash[32 / 4];
		std::fill(std::begin(hash), std::end(hash), i + 1);
		full_tree[full_tree.size() - merkle_num_leafs(4 * 1024) + i] = sha256_hash((char*)hash);
	}

	merkle_fill_tree(full_tree, merkle_num_leafs(4 * 1024));
	trees[0][0] = full_tree[0];

	hash_picker picker(fs, trees);

	auto pieces_start = full_tree.begin() + merkle_num_nodes(1024) - 1024;

	std::vector<sha256_hash> hashes;
	std::copy(pieces_start, pieces_start + 512, std::back_inserter(hashes));
	hashes.push_back(full_tree[2]);
	add_hashes_result result = picker.add_hashes(hash_request(0, 2, 0, 512, 9), hashes);
	TEST_CHECK(result.valid);

	hashes.clear();
	std::copy(pieces_start + 512, pieces_start + 1024, std::back_inserter(hashes));
	result = picker.add_hashes(hash_request(0, 2, 512, 512, 8), hashes);
	TEST_CHECK(result.valid);

	TEST_CHECK(std::equal(trees[0].begin(), trees[0].begin() + merkle_num_nodes(1024), full_tree.begin()));
}

TORRENT_TEST(add_bad_hashes)
{
	file_storage fs;
	fs.set_piece_length(4 * 16 * 1024);

	fs.add_file("test/tmp1", 4 * 512 * 16 * 1024);

	aux::vector<std::vector<sha256_hash>, file_index_t> trees;
	trees.push_back(std::vector<sha256_hash>(merkle_num_nodes(merkle_num_leafs(4 * 512))));

	std::vector<sha256_hash> full_tree(trees[0].size());

	for (int i = 0; i < 4 * 512; i++)
	{
		std::uint32_t hash[32 / 4];
		std::fill(std::begin(hash), std::end(hash), i + 1);
		full_tree[full_tree.size() - merkle_num_leafs(4 * 512) + i] = sha256_hash((char*)hash);
	}

	merkle_fill_tree(full_tree, merkle_num_leafs(4 * 512));
	trees[0][0] = full_tree[0];

	hash_picker picker(fs, trees);

	std::vector<sha256_hash> hashes(2);
	// hash count mis-match
	auto result = picker.add_hashes(hash_request(0, 0, 0, 2, 1), hashes);
	TEST_CHECK(!result.valid);
	result = picker.add_hashes(hash_request(0, 0, 0, 4, 0), hashes);
	TEST_CHECK(!result.valid);

	// wrong piece hash count
	hashes.resize(256);
	result = picker.add_hashes(hash_request(0, 2, 0, 256, 0), hashes);
	TEST_CHECK(!result.valid);

	// wrong base layer
	hashes.resize(512);
	result = picker.add_hashes(hash_request(0, 1, 0, 512, 0), hashes);
	TEST_CHECK(!result.valid);

	// index out of range
	hashes.resize(512);
	result = picker.add_hashes(hash_request(0, 2, 512, 512, 0), hashes);
	TEST_CHECK(!result.valid);

	// totally bogus hashes
	hashes.resize(512);
	result = picker.add_hashes(hash_request(0, 2, 0, 512, 0), hashes);
	TEST_CHECK(!result.valid);

	// bad proof hash
	hashes.clear();
	auto pieces_start = full_tree.begin() + merkle_num_nodes(512) - 512;
	std::copy(pieces_start, pieces_start + 512, std::back_inserter(hashes));
	hashes.back()[1] ^= 0xaa;
	result = picker.add_hashes(hash_request(0, 2, 0, 512, 0), hashes);
	TEST_CHECK(!result.valid);
}

TORRENT_TEST(bad_chunk_hash)
{
	file_storage fs;
	fs.set_piece_length(16 * 1024);

	fs.add_file("test/tmp1", 4 * 512 * 16 * 1024);

	aux::vector<std::vector<sha256_hash>, file_index_t> trees;
	trees.push_back(std::vector<sha256_hash>(merkle_num_nodes(merkle_num_leafs(4 * 512))));

	std::vector<sha256_hash> full_tree(trees[0].size());

	for (int i = 0; i < 4 * 512; i++)
	{
		std::uint32_t hash[32 / 4];
		std::fill(std::begin(hash), std::end(hash), i + 1);
		full_tree[full_tree.size() - merkle_num_leafs(4 * 512) + i] = sha256_hash((char*)hash);
	}

	merkle_fill_tree(full_tree, merkle_num_leafs(4 * 512));
	trees[0][0] = full_tree[0];

	aux::from_hex("0000000000000000000000000000000000000000000000000000000000000001"
		, trees[0][trees[0].size() - merkle_num_leafs(4 * 512) + 1].data());

	hash_picker picker(fs, trees);

	std::vector<sha256_hash> hashes;
	auto leafs_start = full_tree.end() - merkle_num_leafs(4 * 512);
	std::copy(leafs_start, leafs_start + 512, std::back_inserter(hashes));
	for (int i = 3; i > 0; i = merkle_get_parent(i))
	{
		hashes.push_back(full_tree[merkle_get_sibling(i)]);
	}
	add_hashes_result result = picker.add_hashes(hash_request(0, 0, 0, 512, 10)
		, hashes);
	TEST_CHECK(result.valid);
	TEST_CHECK(result.hash_failed.count(1) == 1);
	if (result.hash_failed.count(1) == 1)
	{
		TEST_CHECK(result.hash_failed[1].size() == 1);
		if (result.hash_failed[1].size() == 1)
			TEST_CHECK(result.hash_failed[1][0] == 0);
	}
}

TORRENT_TEST(set_chunk_hash)
{
	file_storage fs;
	fs.set_piece_length(4 * 16 * 1024);

	fs.add_file("test/tmp1", 4 * 512 * 16 * 1024);

	aux::vector<std::vector<sha256_hash>, file_index_t> trees;
	trees.push_back(std::vector<sha256_hash>(merkle_num_nodes(merkle_num_leafs(4 * 512))));

	std::vector<sha256_hash> full_tree(trees[0].size());

	for (int i = 0; i < 4 * 512; i++)
	{
		std::uint32_t hash[32 / 4];
		std::fill(std::begin(hash), std::end(hash), i + 1);
		full_tree[full_tree.size() - merkle_num_leafs(4 * 512) + i] = sha256_hash((char*)hash);
	}

	merkle_fill_tree(full_tree, merkle_num_leafs(4 * 512));
	trees[0] = full_tree;

	int const first_leaf = full_tree.size() - merkle_num_leafs(4 * 512);

	hash_picker picker(fs, trees);
	auto result = picker.set_chunk_hash(1, default_block_size
		, full_tree[first_leaf + 5]);
	TEST_CHECK(result.status == set_chunk_hash_result::success);

	result = picker.set_chunk_hash(2, default_block_size * 2
		, full_tree[first_leaf + 10]);
	TEST_CHECK(result.status == set_chunk_hash_result::success);

	result = picker.set_chunk_hash(2, default_block_size * 2
		, sha256_hash("01234567890123456789"));
	TEST_CHECK(result.status == set_chunk_hash_result::chunk_hash_failed);

	// zero out the inner nodes for a piece along with a single leaf node
	// then add a bogus hash for the leaf
	trees[0][merkle_get_parent(first_leaf + 12)].clear();
	trees[0][merkle_get_parent(first_leaf + 14)].clear();
	trees[0][first_leaf + 13].clear();

	result = picker.set_chunk_hash(3, default_block_size, sha256_hash("01234567890123456789"));
	TEST_CHECK(result.status == set_chunk_hash_result::piece_hash_failed);

	TEST_CHECK(trees[0][merkle_get_parent(first_leaf + 12)].is_all_zeros());
	TEST_CHECK(trees[0][merkle_get_parent(first_leaf + 14)].is_all_zeros());
}

TORRENT_TEST(pass_piece)
{
	file_storage fs;
	fs.set_piece_length(4 * 16 * 1024);

	fs.add_file("test/tmp1", 4 * 512 * 16 * 1024);

	aux::vector<std::vector<sha256_hash>, file_index_t> trees;
	trees.push_back(std::vector<sha256_hash>(merkle_num_nodes(merkle_num_leafs(4 * 512))));

	std::vector<sha256_hash> full_tree(trees[0].size());

	for (int i = 0; i < 4 * 512; i++)
	{
		std::uint32_t hash[32 / 4];
		std::fill(std::begin(hash), std::end(hash), i + 1);
		full_tree[full_tree.size() - merkle_num_leafs(4 * 512) + i] = sha256_hash((char*)hash);
	}

	merkle_fill_tree(full_tree, merkle_num_leafs(4 * 512));
	trees[0][0] = full_tree[0];

	hash_picker picker(fs, trees);

	int const first_leaf = full_tree.size() - merkle_num_leafs(4 * 512);

	for (int i = 0; i < 4; ++i)
	{
		auto result = picker.set_chunk_hash(0, default_block_size * i
			, full_tree[first_leaf + i]);
		TEST_CHECK(result.status == set_chunk_hash_result::unknown);
	}

	auto pieces_start = full_tree.begin() + merkle_num_nodes(512) - 512;

	std::vector<sha256_hash> hashes;
	std::copy(pieces_start, pieces_start + 512, std::back_inserter(hashes));
	add_hashes_result result = picker.add_hashes(hash_request(0, 2, 0, 512, 8), hashes);
	TEST_CHECK(result.valid);
	TEST_EQUAL(result.hash_passed.size(), 1);
	if (result.hash_passed.size() == 1)
	{
		TEST_EQUAL(result.hash_passed[0], 0);
	}
}

TORRENT_TEST(disconnect_peer)
{
	file_storage fs;
	fs.set_piece_length(16 * 1024);

	fs.add_file("test/tmp1", 4 * 512 * 16 * 1024);

	aux::vector<std::vector<sha256_hash>, file_index_t> trees;
	trees.push_back(std::vector<sha256_hash>(merkle_num_nodes(merkle_num_leafs(4 * 512))));
	aux::from_hex("0000000000000000000000000000000000000000000000000000000000000001", trees.back()[0].data());

	hash_picker picker(fs, trees);

	typed_bitfield<piece_index_t> pieces;
	pieces.resize(4 * 512);
	pieces.set_all();

	mock_peer_connection mock_peer;
	mock_peer.m_torrent_peer = (torrent_peer*)0x1;

	auto picked = picker.pick_hashes(pieces, 2, &mock_peer);
	picker.peer_disconnected(&mock_peer);
	auto picked2 = picker.pick_hashes(pieces, 2, &mock_peer);
	TEST_CHECK(picked == picked2);
}

TORRENT_TEST(only_pick_have_pieces)
{
	file_storage fs;
	fs.set_piece_length(16 * 1024);

	fs.add_file("test/tmp1", 4 * 512 * 16 * 1024);

	aux::vector<std::vector<sha256_hash>, file_index_t> trees;
	trees.push_back(std::vector<sha256_hash>(merkle_num_nodes(merkle_num_leafs(4 * 512))));
	aux::from_hex("0000000000000000000000000000000000000000000000000000000000000001", trees.back()[0].data());

	hash_picker picker(fs, trees);

	typed_bitfield<piece_index_t> pieces;
	pieces.resize(4 * 512);
	pieces.set_bit(512);
	pieces.set_bit(1537);

	mock_peer_connection mock_peer;
	mock_peer.m_torrent_peer = (torrent_peer*)0x1;

	auto picked = picker.pick_hashes(pieces, 3, &mock_peer);
	TEST_EQUAL(int(picked.size()), 2);
	TEST_EQUAL(picked[0].file, 0);
	TEST_EQUAL(picked[0].base, 0);
	TEST_EQUAL(picked[0].count, 512);
	TEST_EQUAL(picked[0].index, 512);
	TEST_EQUAL(picked[0].proof_layers, 10);
	TEST_EQUAL(picked[1].file, 0);
	TEST_EQUAL(picked[1].base, 0);
	TEST_EQUAL(picked[1].count, 512);
	TEST_EQUAL(picked[1].index, 1536);
	TEST_EQUAL(picked[1].proof_layers, 10);
}
