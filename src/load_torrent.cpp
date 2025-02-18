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
#include "libtorrent/aux_/escape_string.hpp" // maybe_url_encode
#include "libtorrent/magnet_uri.hpp"
#include "libtorrent/aux_/string_util.hpp" // for is_i2p_url, ltrim, ensure_trailing_slash

namespace libtorrent {

namespace {

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

	void parse_piece_layers(bdecode_node const& e, file_storage const& fs, error_code& ec, add_torrent_params& out)
	{
		std::map<sha256_hash, string_view> piece_layers;

		if (e.type() != bdecode_node::dict_t)
		{
			ec = errors::torrent_missing_piece_layer;
			return;
		}

		std::map<sha256_hash, file_index_t> all_file_roots;
		for (file_index_t i : fs.file_range())
		{
			if (fs.file_size(i) <= fs.piece_length())
				continue;
			all_file_roots.insert(std::make_pair(fs.root(i), i));
		}

		out.merkle_trees.resize(fs.num_files());
		out.merkle_tree_mask.resize(fs.num_files());
		out.verified_leaf_hashes.resize(fs.num_files());

		for (int i = 0; i < e.dict_size(); ++i)
		{
			auto const f = e.dict_at(i);
			if (f.first.size() != static_cast<std::size_t>(sha256_hash::size())
				|| f.second.type() != bdecode_node::string_t
				|| f.second.string_length() % sha256_hash::size() != 0)
			{
				ec = errors::torrent_invalid_piece_layer;
				return;
			}

			sha256_hash const root(f.first);
			auto file_index = all_file_roots.find(root);
			if (file_index == all_file_roots.end())
			{
				// This piece layer doesn't refer to any file in this torrent
				ec = errors::torrent_invalid_piece_layer;
				return;
			}

			file_index_t const file = file_index->second;
			string_view const piece_layer = f.second.string_value();
			int const num_pieces = fs.file_num_pieces(file);
			if (ptrdiff_t(piece_layer.size()) != num_pieces * sha256_hash::size())
			{
				ec = errors::torrent_invalid_piece_layer;
				return;
			}

			aux::merkle_tree tree(fs.file_num_blocks(file), fs.blocks_per_piece(), fs.root_ptr(file));
			if (!tree.load_piece_layer(piece_layer))
			{
				ec = errors::torrent_invalid_piece_layer;
				return;
			}

			auto [sparse_tree, mask] = tree.build_sparse_vector();
			out.merkle_trees[file] = std::move(sparse_tree);
			out.merkle_tree_mask[file] = std::move(mask);
			out.verified_leaf_hashes[file] = tree.verified_leafs();

			// make sure we don't have duplicate piece layers
			all_file_roots.erase(file_index);
		}
	}
}

namespace aux
{
	std::shared_ptr<torrent_info> parse_torrent_file(bdecode_node const& torrent_file
		, error_code& ec, load_torrent_limits const& cfg, add_torrent_params& out)
	{
		if (torrent_file.type() != bdecode_node::dict_t)
		{
			ec = errors::torrent_is_no_dict;
			return {};
		}

		bdecode_node const info = torrent_file.dict_find_dict("info");
		if (!info)
		{
			bdecode_node const uri = torrent_file.dict_find_string("magnet-uri");
			if (uri)
			{
				parse_magnet_uri(uri.string_value(), out, ec);
				return {};
			}

			ec = errors::torrent_missing_info;
			return {};
		}

		auto ti = std::make_shared<torrent_info>(info, ec, cfg, from_info_section);
		if (ec) return {};

		if (ti->v2())
		{
			// allow torrent files without piece layers, just like we allow magnet
			// links. However, if there are piece layers, make sure they're
			// valid
			bdecode_node const& e = torrent_file.dict_find_dict("piece layers");
			if (e)
			{
				parse_piece_layers(e, ti->orig_files(), ec, out);
				if (ec) return{};
			}
		}

#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS
		bdecode_node const similar = torrent_file.dict_find_list("similar");
		if (similar)
		{
			std::vector<sha1_hash> similar_torrents;
			for (int i = 0; i < similar.list_size(); ++i)
			{
				if (similar.list_at(i).type() != bdecode_node::string_t)
					continue;

				if (similar.list_at(i).string_length() != 20)
					continue;

				similar_torrents.emplace_back(
					similar.list_at(i).string_ptr());
			}
			ti->internal_set_similar(std::move(similar_torrents));
		}

		bdecode_node const collections = torrent_file.dict_find_list("collections");
		if (collections)
		{
			std::vector<std::string> owned_collections;
			for (int i = 0; i < collections.list_size(); ++i)
			{
				bdecode_node const str = collections.list_at(i);

				if (str.type() != bdecode_node::string_t) continue;

				owned_collections.emplace_back(str.string_ptr()
					, aux::numeric_cast<std::size_t>(str.string_length()));
			}
			ti->internal_set_collections(owned_collections);
		}
#endif // TORRENT_DISABLE_MUTABLE_TORRENTS

		// extract the url of the tracker
		bdecode_node const announce_node = torrent_file.dict_find_list("announce-list");
		if (announce_node)
		{
			out.trackers.reserve(std::size_t(announce_node.list_size()));
			out.tracker_tiers.reserve(std::size_t(announce_node.list_size()));

			for (int j = 0, end(announce_node.list_size()); j < end; ++j)
			{
				bdecode_node const tier = announce_node.list_at(j);
				if (tier.type() != bdecode_node::list_t) continue;
				for (int k = 0, end2(tier.list_size()); k < end2; ++k)
				{
					string_view const url = tier.list_string_value_at(k);
					if (url.empty()) continue;
#if TORRENT_USE_I2P
					if (aux::is_i2p_url(url)) out.flags |= torrent_flags::i2p_torrent;
#endif
					std::string u(url);
					aux::ltrim(u);
					out.trackers.push_back(std::move(u));
					out.tracker_tiers.push_back(j);
				}
			}
		}

		if (out.trackers.empty())
		{
			string_view const url = torrent_file.dict_find_string_value("announce");
#if TORRENT_USE_I2P
			if (aux::is_i2p_url(url)) out.flags |= torrent_flags::i2p_torrent;
#endif
			if (!url.empty())
			{
				std::string u(url);
				aux::ltrim(u);
				out.trackers.push_back(std::move(u));
				out.tracker_tiers.push_back(0);
			}
		}

		bdecode_node const nodes = torrent_file.dict_find_list("nodes");
		if (nodes)
		{
			for (int i = 0, end(nodes.list_size()); i < end; ++i)
			{
				bdecode_node const n = nodes.list_at(i);
				if (n.type() != bdecode_node::list_t
					|| n.list_size() < 2
					|| n.list_at(0).type() != bdecode_node::string_t
					|| n.list_at(1).type() != bdecode_node::int_t)
					continue;
				out.dht_nodes.emplace_back(
					n.list_at(0).string_value()
					, int(n.list_at(1).int_value()));
			}
		}

		// extract creation date
		std::int64_t const cd = torrent_file.dict_find_int_value("creation date", -1);
		if (cd >= 0)
		{
			out.creation_date = std::time_t(cd);
		}

		// if there are any url-seeds, extract them
		bdecode_node const url_seeds = torrent_file.dict_find("url-list");
		if (url_seeds && url_seeds.type() == bdecode_node::string_t
			&& url_seeds.string_length() > 0)
		{
			std::string url = maybe_url_encode(url_seeds.string_value());
			if (ti->num_files() > 1)
				aux::ensure_trailing_slash(url);
			out.url_seeds.push_back(std::move(url));
		}
		else if (url_seeds && url_seeds.type() == bdecode_node::list_t)
		{
			// only add a URL once
			std::set<string_view> unique;
			for (int i = 0, end(url_seeds.list_size()); i < end; ++i)
			{
				string_view const url = url_seeds.list_string_value_at(i, ""_sv);
				if (url.empty()) continue;
				if (!unique.insert(url).second) continue;
				std::string new_url = maybe_url_encode(url);
				if (ti->num_files() > 1)
					aux::ensure_trailing_slash(new_url);
				out.url_seeds.push_back(new_url);
			}
		}

		out.comment = torrent_file.dict_find_string_value("comment.utf-8");
		if (out.comment.empty()) out.comment = torrent_file.dict_find_string_value("comment");
		aux::verify_encoding(out.comment);

		out.created_by = torrent_file.dict_find_string_value("created by.utf-8");
		if (out.created_by.empty()) out.created_by = torrent_file.dict_find_string_value("created by");
		aux::verify_encoding(out.created_by);

		out.info_hashes = ti->info_hashes();

#if TORRENT_ABI_VERSION < 4
		ti->internal_set_creation_date(out.creation_date);
#endif

		return ti;
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
		error_code ec;
		add_torrent_params ret;
		std::shared_ptr<torrent_info> ti = aux::parse_torrent_file(torrent_file, ec, cfg, ret);
		if (ec) aux::throw_ex<system_error>(ec);
		ret.ti = std::move(ti);
		return ret;
	}

}

