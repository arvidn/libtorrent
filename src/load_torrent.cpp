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
#include "libtorrent/aux_/file_pointer.hpp"
#include "libtorrent/aux_/path.hpp"

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
				if (fs.file_size(f) <= fs.piece_length()) continue;
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

	void load_file(std::string const& filename, std::vector<char>& v
		, error_code& ec, int const max_buffer_size)
	{
		ec.clear();
#ifdef TORRENT_WINDOWS
		aux::file_pointer f(::_wfopen(convert_to_native_path_string(filename).c_str(), L"rb"));
#else
		aux::file_pointer f(std::fopen(filename.c_str(), "rb"));
#endif
		if (f.file() == nullptr)
		{
			ec.assign(errno, generic_category());
			return;
		}

		if (std::fseek(f.file(), 0, SEEK_END) < 0)
		{
			ec.assign(errno, generic_category());
			return;
		}
		std::int64_t const s = std::ftell(f.file());
		if (s < 0)
		{
			ec.assign(errno, generic_category());
			return;
		}
		if (s > max_buffer_size)
		{
			ec = errors::metadata_too_large;
			return;
		}
		if (std::fseek(f.file(), 0, SEEK_SET) < 0)
		{
			ec.assign(errno, generic_category());
			return;
		}
		v.resize(std::size_t(s));
		if (s == 0) return;
		std::size_t const read = std::fread(v.data(), 1, v.size(), f.file());
		if (read != std::size_t(s))
		{
			if (std::feof(f.file()))
			{
				v.resize(read);
				return;
			}
			ec.assign(errno, generic_category());
		}
	}
}

	add_torrent_params load_torrent_file(std::string const& filename)
	{ return load_torrent_file(filename, load_torrent_limits{}); }
	add_torrent_params load_torrent_buffer(span<char const> buffer)
	{ return load_torrent_buffer(buffer, load_torrent_limits{}); }
	add_torrent_params load_torrent_parsed(bdecode_node const& torrent_file)
	{ return load_torrent_parsed(torrent_file, load_torrent_limits{}); }

	add_torrent_params load_torrent_file(std::string const& filename, load_torrent_limits const& cfg)
	{
		std::vector<char> buf;
		error_code ec;
		load_file(filename, buf, ec, cfg.max_buffer_size);
		if (ec) aux::throw_ex<system_error>(ec);
		return load_torrent_buffer(buf, cfg);
	}

	add_torrent_params load_torrent_buffer(span<char const> buffer, load_torrent_limits const& cfg)
	{
		error_code ec;
		bdecode_node e = bdecode(buffer, ec, nullptr, cfg.max_decode_depth
			, cfg.max_decode_tokens);
		if (ec) aux::throw_ex<system_error>(ec);
		return load_torrent_parsed(e, cfg);
	}

	add_torrent_params load_torrent_parsed(bdecode_node const& torrent_file, load_torrent_limits const& cfg)
	{
		auto ti = std::make_shared<torrent_info>(info_hash_t{});
		// TODO: move load_torrent_file logic into here
		error_code ec;
		ti->parse_torrent_file(torrent_file, ec, cfg);
		if (ec) aux::throw_ex<system_error>(ec);
		add_torrent_params ret;
		update_atp(std::move(ti), ret);
		return ret;
	}

}

