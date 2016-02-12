/*

Copyright (c) 2015, Arvid Norberg
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

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/cstdint.hpp>
#include <boost/bind.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/bdecode.hpp"
#include "libtorrent/read_resume_data.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/socket_io.hpp" // for read_*_endpoint()

namespace libtorrent
{
	namespace
	{
		void apply_flag(boost::uint64_t& current_flags
			, bdecode_node const& n
			, char const* name
			, boost::uint64_t const flag)
		{
			if (n.dict_find_int_value(name, -1) == -1)
			{
				current_flags &= ~flag;
			}
			else
			{
				current_flags |= flag;
			}
		}
	}

	add_torrent_params read_resume_data(bdecode_node const& rd, error_code& ec)
	{
		add_torrent_params ret;

		if (rd.dict_find_string_value("file-format")
			!= "libtorrent resume file")
		{
			ec = error_code(errors::invalid_file_tag, get_libtorrent_category());
			return ret;
		}

		std::string info_hash = rd.dict_find_string_value("info-hash");
		if (info_hash.empty())
		{
			ec = error_code(errors::missing_info_hash, get_libtorrent_category());
			return ret;
		}

#error we need to verify the info-hash from the resume data \
		matches the torrent_info object or the magnet link in the URL field. This \
		can only be done reliably on the libtorrent side as the torrent is being \
		added. i.e. the info_hash needs to be saved

		ret.total_uploaded = rd.dict_find_int_value("total_uploaded");
		ret.total_downloaded = rd.dict_find_int_value("total_downloaded");
		ret.active_time = rd.dict_find_int_value("active_time");
		ret.finished_time = rd.dict_find_int_value("finished_time");
		ret.seeding_time = rd.dict_find_int_value("seeding_time");

		ret.last_seen_complete = rd.dict_find_int_value("last_seen_complete");

		// scrape data cache
		ret.num_complete = rd.dict_find_int_value("num_complete", -1);
		ret.num_incomplete = rd.dict_find_int_value("num_incomplete", -1);
		ret.num_downloaded = rd.dict_find_int_value("num_downloaded", -1);

		// torrent settings
		ret.max_uploads = rd.dict_find_int_value("max_uploads", -1);
		ret.max_connections = rd.dict_find_int_value("max_connections", -1);
		ret.upload_limit = rd.dict_find_int_value("upload_rate_limit", -1);
		ret.download_limit = rd.dict_find_int_value("download_rate_limit", -1);

		// torrent state
		apply_flag(ret.flags, rd, "seed_mode", add_torrent_params::flag_seed_mode);
		apply_flag(ret.flags, rd, "super_seeding", add_torrent_params::flag_super_seeding);
		apply_flag(ret.flags, rd, "auto_managed", add_torrent_params::flag_auto_managed);
		apply_flag(ret.flags, rd, "sequential_download", add_torrent_params::flag_sequential_download);
		apply_flag(ret.flags, rd, "paused", add_torrent_params::flag_paused);

		ret.save_path = rd.dict_find_string_value("save_path");

		ret.url = rd.dict_find_string_value("url");
		ret.uuid = rd.dict_find_string_value("uuid");
		ret.source_feed_url = rd.dict_find_string_value("feed");

#error add a field for this. The mapping has to happen in the torrent \
		constructor probably, and passed on to the storage. possibly, the \
		mapped_storage should be passed directly in when the storage is constructed

		bdecode_node mapped_files = rd.dict_find_list("mapped_files");
		if (mapped_files && mapped_files.list_size() == m_torrent_file->num_files())
		{
			for (int i = 0; i < m_torrent_file->num_files(); ++i)
			{
				std::string new_filename = mapped_files.list_string_value_at(i);
				if (new_filename.empty()) continue;
				m_torrent_file->rename_file(i, new_filename);
			}
		}

		ret.added_time = rd.dict_find_int_value("added_time", 0);
		ret.completed_time = rd.dict_find_int_value("completed_time", 0);

		// load file priorities except if the add_torrent_param file was set to
		// override resume data
		bdecode_node file_priority = rd.dict_find_list("file_priority");
		if (file_priority)
		{
			const int num_files = file_priority.list_size();
			ret.file_priorities.resize(num_files, 4);
			for (int i = 0; i < num_files; ++i)
			{
				ret.file_priorities[i] = file_priority.list_int_value_at(i, 1);
				// this is suspicious, leave seed mode
				if (ret.file_priorities[i] == 0)
				{
					ret.flags &= ~add_torrent_params::flag_seed_mode;
				}
			}
		}

		bdecode_node trackers = rd.dict_find_list("trackers");
		if (trackers)
		{
			// it's possible to delete the trackers from a torrent and then save
			// resume data with an empty trackers list. Since we found a trackers
			// list here, these should replace whatever we find in the .torrent
			// file.
			ret.flags |= add_torrent_params::flag_override_trackers;

			int tier = 0;
			for (int i = 0; i < trackers.list_size(); ++i)
			{
				bdecode_node tier_list = trackers.list_at(i);
				if (!tier_list || tier_list.type() != bdecode_node::list_t)
					continue;

				for (int j = 0; j < tier_list.list_size(); ++j)
				{
					ret.trackers.push_back(tier_list.list_string_value_at(j));
					ret.tracker_tiers.push_back(tier);
				}
				++tier;
			}
		}

		// if merge resume http seeds is not set, we need to clear whatever web
		// seeds we loaded from the .torrent file, because we want whatever's in
		// the resume file to take precedence. If there aren't even any fields in
		// the resume data though, keep the ones from the torrent
		bdecode_node url_list = rd.dict_find_list("url-list");
		bdecode_node httpseeds = rd.dict_find_list("httpseeds");
		if (url_list || httpseeds)
		{
			// since we found http seeds in the resume data, they should replace
			// whatever web seeds are specified in the .torrent, by default
			ret.flags |= add_torrent_params::flag_override_web_seeds;
		}

		if (url_list)
		{
			for (int i = 0; i < url_list.list_size(); ++i)
			{
				std::string url = url_list.list_string_value_at(i);
				if (url.empty()) continue;
				ret.url_seeds.push_back(url);
			}
		}

		if (httpseeds)
		{
			for (int i = 0; i < httpseeds.list_size(); ++i)
			{
				std::string url = httpseeds.list_string_value_at(i);
				if (url.empty()) continue;
				ret.http_seeds.push_back(url);
			}
		}

		bdecode_node mt = rd.dict_find_string("merkle tree");
		if (mt)
		{
#error add field for this
			std::vector<sha1_hash> tree;
			tree.resize(m_torrent_file->merkle_tree().size());
			std::memcpy(&tree[0], mt.string_ptr()
				, (std::min)(mt.string_length(), int(tree.size()) * 20));
			if (mt.string_length() < int(tree.size()) * 20)
				std::memset(&tree[0] + mt.string_length() / 20, 0
					, tree.size() - mt.string_length() / 20);
			m_torrent_file->set_merkle_tree(tree);
		}


#error this is the case where the torrent is a merkle torrent but the resume \
		data does not contain the merkle tree, we need some kind of check in the \
		torrent constructor and error reporting
		{
			// TODO: 0 if this is a merkle torrent and we can't
			// restore the tree, we need to wipe all the
			// bits in the have array, but not necessarily
			// we might want to do a full check to see if we have
			// all the pieces. This is low priority since almost
			// no one uses merkle torrents
			TORRENT_ASSERT(false);
		}

#error add fields to add_torrent_params for these
		// some sanity checking. Maybe we shouldn't be in seed mode anymore
		bdecode_node pieces = rd.dict_find("pieces");
		if (pieces && pieces.type() == bdecode_node::string_t
			&& int(pieces.string_length()) == m_torrent_file->num_pieces())
		{
			char const* pieces_str = pieces.string_ptr();
			for (int i = 0, end(pieces.string_length()); i < end; ++i)
			{
				// being in seed mode and missing a piece is not compatible.
				// Leave seed mode if that happens
				if ((pieces_str[i] & 1)) continue;
				m_seed_mode = false;
				break;
			}
		}

		bdecode_node piece_priority = rd.dict_find_string("piece_priority");
		if (piece_priority && piece_priority.string_length()
			== m_torrent_file->num_pieces())
		{
			char const* p = piece_priority.string_ptr();
			for (int i = 0; i < piece_priority.string_length(); ++i)
			{
				if (p[i] > 0) continue;
				m_seed_mode = false;
				break;
			}
		}

		using namespace libtorrent::detail; // for read_*_endpoint()
		if (bdecode_node peers_entry = rd.dict_find_string("peers"))
		{
			char const* ptr = peers_entry.string_ptr();
			for (int i = 0; i < peers_entry.string_length(); i += 6)
				ret.peers.push_back(read_v4_endpoint<tcp::endpoint>(ptr));
		}

		if (bdecode_node peers_entry = rd.dict_find_string("peers6"))
		{
			char const* ptr = peers_entry.string_ptr();
			for (int i = 0; i < peers_entry.string_length(); i += 18)
				ret.peers.push_back(read_v6_endpoint<tcp::endpoint>(ptr));
		}

		if (bdecode_node peers_entry = rd.dict_find_string("banned_peers"))
		{
			char const* ptr = peers_entry.string_ptr();
			for (int i = 0; i < peers_entry.string_length(); i += 6)
				ret.banned_peers.push_back(read_v4_endpoint<tcp::endpoint>(ptr));
		}

		if (bdecode_node peers_entry = rd.dict_find_string("banned_peers6"))
		{
			char const* ptr = peers_entry.string_ptr();
			for (int i = 0; i < peers_entry.string_length(); i += 18)
				ret.banned_peers.push_back(read_v6_endpoint<tcp::endpoint>(ptr));
		}

#error read "unfinished" pieces

		return ret;
	}

	add_torrent_params read_resume_data(char const* buffer, int size, error_code& ec)
	{
		bdecode_node rd;
		bdecode(buffer, buffer + size, rd, ec);
		if (ec) return add_torrent_params();

		return read_resume_data(rd, ec);
	}
}

