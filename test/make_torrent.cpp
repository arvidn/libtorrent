/*

Copyright (c) 2016, Alden Torres
Copyright (c) 2016, Andrei Kurushin
Copyright (c) 2016-2020, 2022, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <deque>

#include "make_torrent.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/load_torrent.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/aux_/posix_storage.hpp"

using namespace lt;

lt::add_torrent_params make_test_torrent(torrent_args const& args)
{
	entry e;

	entry::dictionary_type& info = e["info"].dict();
	int total_size = 0;

	if (args.m_priv)
	{
		info["priv"] = 1;
	}

	// torrent offset ranges where the pad files are
	// used when generating hashes
	std::deque<std::pair<int,int>> pad_files;

	int const piece_length = 32768;
	info["piece length"] = piece_length;

	if (args.m_files.size() == 1)
	{
		std::string const& ent = args.m_files[0];
		std::string name = "test_file-1";
		if (ent.find("name=") != std::string::npos)
		{
			std::string::size_type pos = ent.find("name=") + 5;
			name = ent.substr(pos, ent.find(',', pos));
		}
		info["name"] = name;
		int file_size = atoi(args.m_files[0].c_str());
		info["length"] = file_size;
		total_size = file_size;
	}
	else
	{
		info["name"] = args.m_name;

		entry::list_type& files = info["files"].list();
		for (int i = 0; i < int(args.m_files.size()); ++i)
		{
			auto const idx = static_cast<std::size_t>(i);
			int file_size = atoi(args.m_files[idx].c_str());

			files.push_back(entry());
			entry::dictionary_type& file_entry = files.back().dict();
			std::string const& ent = args.m_files[idx];
			if (ent.find("padfile") != std::string::npos)
			{
				file_entry["attr"].string() += "p";
				pad_files.push_back(std::make_pair(total_size, total_size + file_size));
			}
			if (ent.find("executable") != std::string::npos)
				file_entry["attr"].string() += "x";

			char filename[100];
			std::snprintf(filename, sizeof(filename), "test_file-%d", i);

			std::string name = filename;
			if (ent.find("name=") != std::string::npos)
			{
				std::string::size_type pos = ent.find("name=") + 5;
				name = ent.substr(pos, ent.find(',', pos));
			}
			file_entry["path"].list().push_back(name);
			file_entry["length"] = file_size;
			total_size += file_size;
		}
	}

	if (!args.m_url_seed.empty())
	{
		e["url-list"] = args.m_url_seed;
	}

	if (!args.m_http_seed.empty())
	{
		e["httpseeds"] = args.m_http_seed;
	}

	if (!args.m_collection.empty())
	{
		auto& l = info["collections"].list();
		l.push_back(args.m_collection);
	}

	std::string piece_hashes;

	int num_pieces = (total_size + piece_length - 1) / piece_length;
	int torrent_offset = 0;
	for (int i = 0; i < num_pieces; ++i)
	{
		hasher h;
		int const piece_size = (i < num_pieces - 1)
			? piece_length
			: total_size - (num_pieces - 1) * piece_length;

		char const data = char(i & 0xff);
		char const zero = 0;
		for (int o = 0; o < piece_size; ++o, ++torrent_offset)
		{
			while (!pad_files.empty() && torrent_offset >= pad_files.front().second)
				pad_files.pop_front();

			if (!pad_files.empty() && torrent_offset >= pad_files.front().first)
			{
				h.update(zero);
			}
			else
			{
				h.update(data);
			}
		}
		piece_hashes += h.final().to_string();
	}

	info["pieces"] = piece_hashes;

	std::vector<char> const tmp = bencode(e);

	return lt::load_torrent_buffer(tmp);
}

void generate_files(lt::torrent_info const& ti, std::string const& path
	, bool alternate_data)
{
	aux::vector<download_priority_t, file_index_t> priorities;
	renamed_files rf;
	storage_params params{
		ti.files(),
		rf,
		path,
		storage_mode_t::storage_mode_sparse,
		priorities,
		sha1_hash{},
		true, // v1-hashes
		true // v2-hashes
	};

	// default settings
	aux::session_settings sett;
	aux::posix_storage st(params);

	file_storage const& fs = ti.files();
	std::vector<char> buffer;
	for (auto const i : fs.piece_range())
	{
		int const piece_size = ti.piece_size(i);
		buffer.resize(static_cast<std::size_t>(ti.piece_length()));

		char const data = static_cast<char>((alternate_data
			? 255 - static_cast<int>(i) : static_cast<int>(i)) & 0xff);
		for (int o = 0; o < piece_size; ++o)
		{
			buffer[static_cast<std::size_t>(o)] = data;
		}

		span<char> const b = { &buffer[0], piece_size };
		storage_error ec;
		int ret = st.write(sett, b, i, 0, ec);
		if (ret != piece_size || ec)
		{
			std::printf("ERROR writing files: (%d expected %d) %s\n"
				, ret, piece_size, ec.ec.message().c_str());
		}
	}
}
