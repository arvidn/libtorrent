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
#include "libtorrent/aux_/throw.hpp"

namespace libtorrent {

namespace {
	void update_atp(std::shared_ptr<torrent_info> ti, add_torrent_params& atp)
	{
		// This is a temporary measure until all non info-dict content is parsed
		// here, rather than the torrent_info constructor
		auto drained_state = ti->_internal_drain();
		for (auto const& ae : drained_state.urls)
		{
			atp.trackers.push_back(std::move(ae.url));
			atp.tracker_tiers.push_back(ae.tier);
		}

		if (ti->is_i2p())
			atp.flags |= torrent_flags::i2p_torrent;

		for (auto const& ws : drained_state.web_seeds)
		{
#if TORRENT_ABI_VERSION < 4
			if (ws.type == web_seed_entry::url_seed)
#endif
				atp.url_seeds.push_back(std::move(ws.url));
#if TORRENT_ABI_VERSION < 4
			else if (ws.type == web_seed_entry::http_seed)
				atp.http_seeds.push_back(std::move(ws.url));
#endif
		}

		atp.dht_nodes = std::move(drained_state.nodes);

		if (ti->v2_piece_hashes_verified())
		{
			int const blocks_per_piece = ti->files().blocks_per_piece();
			std::vector<sha256_hash> scratch;
			sha256_hash const pad = merkle_pad(blocks_per_piece, 1);
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

				int const full_size = merkle_num_nodes(
					merkle_num_leafs(fs.file_num_blocks(f)));
				int const num_pieces = fs.file_num_pieces(f);
				int const piece_layer_size = merkle_num_leafs(num_pieces);

				if (!layer.empty())
				{
					sha256_hash const computed_root = merkle_root_scratch(layer
						, piece_layer_size
						, pad
						, scratch);
					if (computed_root != fs.root(f))
						aux::throw_ex<system_error>(errors::torrent_invalid_piece_layer);
				}

				auto& mask = atp.merkle_tree_mask[f];
				mask.resize(full_size, false);
				for (int i = merkle_first_leaf(piece_layer_size)
					, end = i + num_pieces; i < end; ++i)
				{
					mask.set_bit(i);
				}
			}
			ti->free_piece_layers();
		}

		atp.comment = ti->comment();
		atp.created_by = ti->creator();
		atp.creation_date = ti->creation_date();
		atp.info_hashes = ti->info_hashes();
		atp.ti = std::move(ti);
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
		auto ti = std::make_shared<torrent_info>(filename, cfg);
		add_torrent_params ret;
		update_atp(std::move(ti), ret);
		return ret;
	}

	add_torrent_params load_torrent_buffer(span<char const> buffer, load_torrent_limits const& cfg)
	{
		auto ti = std::make_shared<torrent_info>(buffer, cfg, from_span);
		add_torrent_params ret;
		update_atp(std::move(ti), ret);
		return ret;
	}

	add_torrent_params load_torrent_parsed(bdecode_node const& torrent_file, load_torrent_limits const& cfg)
	{
		auto ti = std::make_shared<torrent_info>(torrent_file, cfg);
		add_torrent_params ret;
		update_atp(std::move(ti), ret);
		return ret;
	}

}

