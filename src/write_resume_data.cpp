/*

Copyright (c) 2017, Steven Siloti
Copyright (c) 2017-2020, Arvid Norberg
Copyright (c) 2017-2018, 2020-2021, Alden Torres
Copyright (c) 2021, Vladimir Golovnev (glassez)
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <cstdint>
#include <algorithm>

#include "libtorrent/bdecode.hpp"
#include "libtorrent/write_resume_data.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/aux_/socket_io.hpp" // for write_*_endpoint()
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"
#include "libtorrent/aux_/ip_helpers.hpp"

namespace lt {

	entry write_resume_data(add_torrent_params const& atp)
	{
		entry ret;

		using namespace lt::aux; // for write_*_endpoint()
		ret["file-format"] = "libtorrent resume file";
		ret["file-version"] = 1;
		ret["libtorrent-version"] = lt::version_str;
		ret["allocation"] = atp.storage_mode == storage_mode_allocate
			? "allocate" : "sparse";

		ret["total_uploaded"] = atp.total_uploaded;
		ret["total_downloaded"] = atp.total_downloaded;

		// cast to seconds in case that internal values doesn't have ratio<1>
		ret["active_time"] = atp.active_time;
		ret["finished_time"] = atp.finished_time;
		ret["seeding_time"] = atp.seeding_time;
		ret["last_seen_complete"] = atp.last_seen_complete;
		ret["last_download"] = atp.last_download;
		ret["last_upload"] = atp.last_upload;

		ret["num_complete"] = atp.num_complete;
		ret["num_incomplete"] = atp.num_incomplete;
		ret["num_downloaded"] = atp.num_downloaded;

		ret["seed_mode"] = bool(atp.flags & torrent_flags::seed_mode);
		ret["upload_mode"] = bool(atp.flags & torrent_flags::upload_mode);
#ifndef TORRENT_DISABLE_SHARE_MODE
		ret["share_mode"] = bool(atp.flags & torrent_flags::share_mode);
#endif
		ret["apply_ip_filter"] = bool(atp.flags & torrent_flags::apply_ip_filter);
		ret["paused"] = bool(atp.flags & torrent_flags::paused);
		ret["auto_managed"] = bool(atp.flags & torrent_flags::auto_managed);
#ifndef TORRENT_DISABLE_SUPERSEEDING
		ret["super_seeding"] = bool(atp.flags & torrent_flags::super_seeding);
#endif
		ret["sequential_download"] = bool(atp.flags & torrent_flags::sequential_download);
		ret["stop_when_ready"] = bool(atp.flags & torrent_flags::stop_when_ready);
		ret["disable_dht"] = bool(atp.flags & torrent_flags::disable_dht);
		ret["disable_lsd"] = bool(atp.flags & torrent_flags::disable_lsd);
		ret["disable_pex"] = bool(atp.flags & torrent_flags::disable_pex);

		ret["added_time"] = atp.added_time;
		ret["completed_time"] = atp.completed_time;

		ret["save_path"] = atp.save_path;

		if (!atp.name.empty()) ret["name"] = atp.name;

#if TORRENT_ABI_VERSION == 1
		// deprecated in 1.2
		if (!atp.url.empty()) ret["url"] = atp.url;
#endif

		ret["info-hash"] = atp.info_hashes.v1;
		ret["info-hash2"] = atp.info_hashes.v2;

		if (atp.ti)
		{
			auto const info = atp.ti->info_section();
			ret["info"].preformatted().assign(info.data(), info.data() + info.size());
			if (!atp.ti->comment().empty())
				ret["comment"] = atp.ti->comment();
			if (atp.ti->creation_date() != 0)
				ret["creation date"] = atp.ti->creation_date();
			if (!atp.ti->creator().empty())
				ret["created by"] = atp.ti->creator();
		}

		if (!atp.merkle_trees.empty())
		{
			auto const& trees = atp.merkle_trees;
			auto& ret_trees = ret["trees"].list();
			ret_trees.reserve(atp.merkle_trees.size());
			for (file_index_t f(0); f < file_index_t{int(atp.merkle_trees.size())}; ++f)
			{
				auto const& tree = trees[f];
				ret_trees.emplace_back(entry::dictionary_t);
				auto& ret_dict = ret_trees.back().dict();
				auto& ret_tree = ret_dict["hashes"].string();

				ret_tree.reserve(tree.size() * 32);
				for (auto const& n : tree)
					ret_tree.append(n.data(), n.size());

				if (f < atp.verified_leaf_hashes.end_index())
				{
					auto const& verified = atp.verified_leaf_hashes[f];
					if (!verified.empty())
					{
						auto& ret_verified = ret_dict["verified"].string();
						ret_verified.reserve(verified.size());
						for (auto const bit : verified)
							ret_verified.push_back(bit ? '1' : '0');
					}
				}

				if (f < atp.merkle_tree_mask.end_index())
				{
					auto const& mask = atp.merkle_tree_mask[f];
					if (!mask.empty())
					{
						auto& ret_mask = ret_dict["mask"].string();
						ret_mask.reserve(mask.size());
						for (auto const bit : mask)
							ret_mask.push_back(bit ? '1' : '0');
					}
				}
			}
		}

		if (!atp.unfinished_pieces.empty())
		{
			entry::list_type& up = ret["unfinished"].list();
			up.reserve(atp.unfinished_pieces.size());

			// info for each unfinished piece
			for (auto const& p : atp.unfinished_pieces)
			{
				entry piece_struct(entry::dictionary_t);

				// the unfinished piece's index
				piece_struct["piece"] = static_cast<int>(p.first);
				piece_struct["bitmask"] = std::string(p.second.data(), std::size_t(p.second.size() + 7) / 8);
				// push the struct onto the unfinished-piece list
				up.push_back(std::move(piece_struct));
			}
		}

		// save trackers
		entry::list_type& tr_list = ret["trackers"].list();
		if (!atp.trackers.empty())
		{
			tr_list.emplace_back(entry::list_type());
			std::size_t tier = 0;
			auto tier_it = atp.tracker_tiers.begin();
			for (std::string const& tr : atp.trackers)
			{
				if (tier_it != atp.tracker_tiers.end())
					tier = std::clamp(std::size_t(*tier_it++), std::size_t{0}, std::size_t{1024});

				if (tr_list.size() <= tier)
					tr_list.resize(tier + 1);

				tr_list[tier].list().emplace_back(tr);
			}
		}

		// save web seeds
		entry::list_type& url_list = ret["url-list"].list();
		std::copy(atp.url_seeds.begin(), atp.url_seeds.end(), std::back_inserter(url_list));

		// write have bitmask
		entry::string_type& pieces = ret["pieces"].string();
		pieces.resize(aux::numeric_cast<std::size_t>(std::max(
			atp.have_pieces.size(), atp.verified_pieces.size())));

		std::size_t piece(0);
		for (auto const bit : atp.have_pieces)
		{
			pieces[piece] = bit ? 1 : 0;
			++piece;
		}

		piece = 0;
		for (auto const bit : atp.verified_pieces)
		{
			pieces[piece] |= bit ? 2 : 0;
			++piece;
		}

		// write renamed files
		if (!atp.renamed_files.empty())
		{
			entry::list_type& fl = ret["mapped_files"].list();
			for (auto const& ent : atp.renamed_files)
			{
				auto const idx = static_cast<std::size_t>(static_cast<int>(ent.first));
				if (idx >= fl.size()) fl.resize(idx + 1);
				fl[idx] = ent.second;
			}
		}

		// write local peers
		if (!atp.peers.empty())
		{
			std::back_insert_iterator<entry::string_type> ptr(ret["peers"].string());
			std::back_insert_iterator<entry::string_type> ptr6(ret["peers6"].string());
			for (auto const& p : atp.peers)
			{
				if (is_v6(p))
					write_endpoint(p, ptr6);
				else
					write_endpoint(p, ptr);
			}
		}

		if (!atp.banned_peers.empty())
		{
			std::back_insert_iterator<entry::string_type> ptr(ret["banned_peers"].string());
			std::back_insert_iterator<entry::string_type> ptr6(ret["banned_peers6"].string());
			for (auto const& p : atp.banned_peers)
			{
				if (is_v6(p))
					write_endpoint(p, ptr6);
				else
					write_endpoint(p, ptr);
			}
		}

		ret["upload_rate_limit"] = atp.upload_limit;
		ret["download_rate_limit"] = atp.download_limit;
		ret["max_connections"] = atp.max_connections;
		ret["max_uploads"] = atp.max_uploads;

		if (!atp.file_priorities.empty())
		{
			// write file priorities
			entry::list_type& prio = ret["file_priority"].list();
			prio.reserve(atp.file_priorities.size());
			for (auto const p : atp.file_priorities)
				prio.emplace_back(static_cast<std::uint8_t>(p));
		}

		if (!atp.piece_priorities.empty())
		{
			// write piece priorities
			entry::string_type& prio = ret["piece_priority"].string();
			prio.reserve(atp.piece_priorities.size());
			for (auto const p : atp.piece_priorities)
				prio.push_back(static_cast<char>(static_cast<std::uint8_t>(p)));
		}

		return ret;
	}

	std::vector<char> write_resume_data_buf(add_torrent_params const& atp)
	{
		std::vector<char> ret;
		entry rd = write_resume_data(atp);
		bencode(std::back_inserter(ret), rd);
		return ret;
	}
}
