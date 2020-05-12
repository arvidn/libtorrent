/*

Copyright (c) 2017, Arvid Norberg
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

#include <cstdint>

#include "libtorrent/bdecode.hpp"
#include "libtorrent/write_resume_data.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/socket_io.hpp" // for write_*_endpoint()
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"
#include "libtorrent/torrent.hpp" // for default_piece_priority
#include "libtorrent/aux_/numeric_cast.hpp" // for clamp

namespace libtorrent {

	entry write_resume_data(add_torrent_params const& atp)
	{
		entry ret;

		using namespace libtorrent::detail; // for write_*_endpoint()
		ret["file-format"] = "libtorrent resume file";
		ret["file-version"] = 1;
		ret["libtorrent-version"] = LIBTORRENT_VERSION;
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

#if TORRENT_ABI_VERSION == 1
		// deprecated in 1.2
		if (!atp.url.empty()) ret["url"] = atp.url;
		if (!atp.uuid.empty()) ret["uuid"] = atp.uuid;
#endif

		ret["info-hash"] = atp.info_hash;

		if (atp.ti)
		{
			auto const info = atp.ti->metadata();
			int const size = atp.ti->metadata_size();
			ret["info"].preformatted().assign(&info[0], &info[0] + size);
			if (!atp.ti->comment().empty())
				ret["comment"] = atp.ti->comment();
			if (atp.ti->creation_date() != 0)
				ret["creation date"] = atp.ti->creation_date();
			if (!atp.ti->creator().empty())
				ret["created by"] = atp.ti->creator();
		}

		if (!atp.merkle_tree.empty())
		{
			// we need to save the whole merkle hash tree
			// in order to resume
			std::string& tree_str = ret["merkle tree"].string();
			auto const& tree = atp.merkle_tree;
			tree_str.resize(tree.size() * 20);
			std::memcpy(&tree_str[0], &tree[0], tree.size() * 20);
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
					tier = aux::clamp(std::size_t(*tier_it++), std::size_t{0}, std::size_t{1024});

				if (tr_list.size() <= tier)
					tr_list.resize(tier + 1);

				tr_list[tier].list().emplace_back(tr);
			}
		}

		// save web seeds
		entry::list_type& url_list = ret["url-list"].list();
		std::copy(atp.url_seeds.begin(), atp.url_seeds.end(), std::back_inserter(url_list));

		entry::list_type& httpseeds_list = ret["httpseeds"].list();
		std::copy(atp.http_seeds.begin(), atp.http_seeds.end(), std::back_inserter(httpseeds_list));

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
				std::size_t const idx(static_cast<std::size_t>(static_cast<int>(ent.first)));
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
		ret["max_uploads"] = atp.upload_limit;

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
