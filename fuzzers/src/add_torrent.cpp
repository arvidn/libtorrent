/*

Copyright (c) 2020-2021, Alden Torres
Copyright (c) 2020-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <memory>
#include <iostream>
#include <optional>

#include "libtorrent/session.hpp"
#include "libtorrent/session_params.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/disk_interface.hpp" // for default_block_size
#include "libtorrent/aux_/merkle.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/disabled_disk_io.hpp"
#include "read_bits.hpp"

using namespace lt;

lt::session_params g_params;
io_context g_ioc;
std::shared_ptr<lt::torrent_info> g_torrent;
std::vector<sha256_hash> g_tree;

int const piece_size = 1024 * 1024;
int const blocks_per_piece = piece_size / lt::default_block_size;
int const num_pieces = 10;
int const num_leafs = merkle_num_leafs(num_pieces * blocks_per_piece);
int const num_nodes = merkle_num_nodes(num_leafs);
int const first_leaf = merkle_first_leaf(num_leafs);

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	lt::settings_pack& pack = g_params.settings;
	// set up settings pack we'll be using
	pack.set_int(settings_pack::tick_interval, 1);
	pack.set_int(settings_pack::alert_mask, 0);
	pack.set_int(settings_pack::out_enc_policy, settings_pack::pe_disabled);
	pack.set_int(settings_pack::in_enc_policy, settings_pack::pe_disabled);
	pack.set_int(settings_pack::aio_threads, 0);

	// don't waste time making outbound connections
	pack.set_bool(settings_pack::enable_outgoing_tcp, false);
	pack.set_bool(settings_pack::enable_outgoing_utp, false);
	pack.set_bool(settings_pack::enable_upnp, false);
	pack.set_bool(settings_pack::enable_natpmp, false);
	pack.set_bool(settings_pack::enable_dht, false);
	pack.set_bool(settings_pack::enable_lsd, false);
	pack.set_bool(settings_pack::enable_ip_notifier, false);
	pack.set_str(settings_pack::listen_interfaces, "127.0.0.1:0");

	g_params.disk_io_constructor = lt::disabled_disk_io_constructor;

	// create a torrent
	std::int64_t const total_size = std::int64_t(piece_size) * num_pieces;

	g_tree.resize(num_nodes);

	std::vector<lt::create_file_entry> fs;
	fs.emplace_back("test_file", total_size);
	create_torrent t(std::move(fs), piece_size);

	std::vector<char> piece(piece_size, 0);
	lt::span<char const> piece_span(piece);
	std::vector<sha256_hash> piece_tree(merkle_num_nodes(blocks_per_piece));
	for (piece_index_t i : t.piece_range())
	{
		std::memset(piece.data(), char(static_cast<int>(i) & 0xff), piece.size());
		t.set_hash(piece_index_t(i), lt::hasher(piece).final());

		for (int k = 0; k < blocks_per_piece; ++k)
		{
			auto const h = lt::hasher256(piece_span.subspan(
				k * lt::default_block_size, lt::default_block_size)).final();
			piece_tree[std::size_t(k)] = h;
			g_tree[std::size_t(first_leaf + static_cast<int>(i) * blocks_per_piece + k)] = h;
		}

		auto const r = merkle_root(piece_tree);
		t.set_hash2(file_index_t{0}, i - piece_index_t{0}, r);
	}

	merkle_fill_tree(g_tree, num_leafs);

	std::vector<char> buf;
	bencode(std::back_inserter(buf), t.generate());
	g_torrent = std::make_shared<torrent_info>(buf, from_span);

	return 0;
}

lt::add_torrent_params generate_atp(std::uint8_t const* data, size_t size)
{
	read_bits bits(data, size);
	lt::add_torrent_params ret;
	ret.ti = g_torrent;
	ret.info_hashes = g_torrent->info_hashes();
	ret.save_path = ".";
	ret.file_priorities.resize(bits.read(2));
	for (auto& p : ret.file_priorities)
		p = lt::download_priority_t(bits.read(3));
	ret.flags = lt::torrent_flags_t(bits.read(24));
	int const num_unfinished = bits.read(4);
	for (int i = 0; i < num_unfinished; ++i)
	{
		auto& mask = ret.unfinished_pieces[piece_index_t(bits.read(32))];
		mask.resize(bits.read(5));
		for (int i = 0; i < mask.size(); ++i)
			if (bits.read(1)) mask.set_bit(i); else mask.set_bit(i);
	}
	ret.have_pieces.resize(bits.read(6));
	for (auto const i : ret.have_pieces.range())
		if (bits.read(1)) ret.have_pieces.set_bit(i); else ret.have_pieces.set_bit(i);

	ret.verified_pieces.resize(bits.read(6));
	for (auto const i : ret.verified_pieces.range())
		if (bits.read(1)) ret.verified_pieces.set_bit(i); else ret.verified_pieces.set_bit(i);

	ret.piece_priorities.resize(bits.read(6));
	for (auto& p : ret.piece_priorities)
		p = lt::download_priority_t(bits.read(1));

	// if we read a 1 here, initialize the merkle tree fields correctly
	if (bits.read(1))
	{
		ret.merkle_trees.resize(1);
		ret.merkle_tree_mask.resize(1);
		ret.verified_leaf_hashes.resize(1);
		ret.verified_leaf_hashes[file_index_t{0}].resize(num_leafs, true);

		auto& t = ret.merkle_trees[file_index_t{0}];
		auto& mask = ret.merkle_tree_mask[file_index_t{0}];
		mask.resize(num_nodes, false);
		int idx = -1;
		for (auto const& h : g_tree)
		{
			++idx;
			if (h.is_all_zeros()) continue;
			mask[std::size_t(idx)] = true;
			t.push_back(g_tree[std::size_t(idx)]);
		}
	}
	else
	{
		ret.merkle_trees.resize(bits.read(2));
		for (auto& t : ret.merkle_trees)
		{
			std::size_t block = 0;
			t.resize(bits.read(13));
			for (auto& h : t)
			{
				h = g_tree[block++];
				if (block >= g_tree.size()) block = 0;
			}
		}
		ret.merkle_tree_mask.resize(bits.read(2));
		for (auto& m : ret.merkle_tree_mask)
		{
			m.resize(bits.read(13));
			for (std::size_t i = 0; i < m.size(); ++i)
				m[i] = bits.read(1);
		}
		ret.verified_leaf_hashes.resize(bits.read(2));
		for (auto& m : ret.verified_leaf_hashes)
		{
			m.resize(bits.read(4));
			for (std::size_t i = 0; i < m.size(); ++i)
				m[i] = bits.read(1);
		}
	}

	ret.max_uploads = bits.read(32);
	ret.max_connections = bits.read(32);
	ret.upload_limit = bits.read(32);
	ret.download_limit = bits.read(32);
	ret.active_time = bits.read(32);
	ret.finished_time = bits.read(32);
	ret.seeding_time = bits.read(32);
	ret.last_seen_complete = bits.read(32);
	ret.num_complete = bits.read(32);
	ret.num_incomplete = bits.read(32);
	ret.num_downloaded = bits.read(32);

	return ret;
}

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
	g_ioc.restart();
	std::optional<lt::session> ses(lt::session{g_params, g_ioc});

	lt::add_torrent_params atp = generate_atp(data, size);

	ses->async_add_torrent(atp);
	auto proxy = ses->abort();
	post(g_ioc, [&]{ ses.reset(); });

	g_ioc.run_for(seconds(2));

#if defined TORRENT_ASIO_DEBUGGING
	lt::aux::log_async();
	lt::aux::_async_ops.clear();
	lt::aux::_async_ops_nthreads = 0;
	lt::aux::_wakeups.clear();
#endif

	return 0;
}
