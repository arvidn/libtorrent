/*

Copyright (c) 2022, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/load_torrent.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/aux_/merkle.hpp"

namespace libtorrent {

namespace {
	void update_atp(add_torrent_params& atp)
	{
		auto const& ti = atp.ti;
		for (auto const& ae : ti->trackers())
		{
			atp.trackers.push_back(ae.url);
			atp.tracker_tiers.push_back(ae.tier);
		}
		ti->clear_trackers();

		for (auto const& ws : ti->web_seeds())
		{
#if TORRENT_ABI_VERSION < 4
			if (ws.type == web_seed_entry::url_seed)
#endif
				atp.url_seeds.push_back(ws.url);
#if TORRENT_ABI_VERSION < 4
			else if (ws.type == web_seed_entry::http_seed)
				atp.http_seeds.push_back(ws.url);
#endif
		}
		ti->clear_web_seeds();

		atp.dht_nodes = ti->nodes();

		if (ti->v2_piece_hashes_verified())
		{
			file_storage const& fs = ti->files();
			atp.merkle_trees.resize(fs.num_files());
			atp.merkle_tree_mask.resize(fs.num_files());
			for (auto const f : fs.file_range())
			{
				if (fs.pad_file_at(f)) continue;
				if (fs.file_size(f) == 0) continue;
				auto const bytes = ti->piece_layer(f);
				auto& layer = atp.merkle_trees[f];
				layer.reserve(std::size_t(bytes.size() / sha256_hash::size()));
				for (int i = 0; i < bytes.size(); i += int(sha256_hash::size()))
					layer.emplace_back(bytes.data() + i);
				auto& mask = atp.merkle_tree_mask[f];

				int const full_size = merkle_num_nodes(
					merkle_num_leafs(fs.file_num_blocks(f)));
				int const num_pieces = fs.file_num_pieces(f);
				int const piece_layer_size = merkle_num_leafs(num_pieces);
				mask.resize(std::size_t(full_size), false);
				for (int i = merkle_first_leaf(piece_layer_size)
					, end = i + num_pieces; i < end; ++i)
				{
					mask[std::size_t(i)] = true;
				}
			}
			ti->free_piece_layers();
		}

		atp.comment = atp.ti->comment();
		atp.created_by = atp.ti->creator();
		atp.creation_date = atp.ti->creation_date();
		atp.info_hashes = atp.ti->info_hashes();
	}
}

	add_torrent_params load_torrent_file(std::string const& filename)
	{ return load_torrent_file(filename, load_torrent_limits{}); }
	add_torrent_params load_torrent_buffer(span<char const> buffer)
	{ return load_torrent_buffer(buffer, load_torrent_limits{}); }
	add_torrent_params load_torrent_parsed(bdecode_node const& torrent_file)
	{ return load_torrent_parsed(torrent_file, load_torrent_limits{}); }

	// TODO: move the loading logic from torrent_info constructor into here
	add_torrent_params load_torrent_file(std::string const& filename, load_torrent_limits const& cfg)
	{
		add_torrent_params ret;
		ret.ti = std::make_shared<torrent_info>(filename, cfg);
		update_atp(ret);
		return ret;
	}

	add_torrent_params load_torrent_buffer(span<char const> buffer, load_torrent_limits const& cfg)
	{
		add_torrent_params ret;
		ret.ti = std::make_shared<torrent_info>(buffer, cfg, from_span);
		update_atp(ret);
		return ret;
	}

	add_torrent_params load_torrent_parsed(bdecode_node const& torrent_file, load_torrent_limits const& cfg)
	{
		add_torrent_params ret;
		ret.ti = std::make_shared<torrent_info>(torrent_file, cfg);
		update_atp(ret);
		return ret;
	}

}

