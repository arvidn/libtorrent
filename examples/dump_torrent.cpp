/*

Copyright (c) 2018, Steven Siloti
Copyright (c) 2003-2004, 2008-2010, 2013, 2015-2020, Arvid Norberg
Copyright (c) 2018, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <cstdio> // for snprintf
#include <cinttypes> // for PRId64 et.al.
#include <fstream>
#include <iostream>

#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/bdecode.hpp"
#include "libtorrent/magnet_uri.hpp"
#include "libtorrent/span.hpp"

namespace {

[[noreturn]] void print_usage()
{
	std::cerr << R"(usage: dump_torrent torrent-file [options]
    OPTIONS:
    --items-limit <count>    set the upper limit of the number of bencode items
                             in the torrent file.
    --depth-limit <count>    set the recursion limit in the bdecoder
    --show-padfiles          show pad files in file list
    --max-pieces <count>     set the upper limit on the number of pieces to
                             load in the torrent.
    --max-size <size in MiB> reject files larger than this size limit
)";
	std::exit(1);
}

}

int main(int argc, char const* argv[]) try
{
	lt::span<char const*> args(argv, argc);

	// strip executable name
	args = args.subspan(1);

	lt::load_torrent_limits cfg;
	bool show_pad = false;

	if (args.empty()) print_usage();

	char const* filename = args[0];
	args = args.subspan(1);

	using namespace lt::literals;

	while (!args.empty())
	{
		if (args[0] == "--items-limit"_sv && args.size() > 1)
		{
			cfg.max_decode_tokens = atoi(args[1]);
			args = args.subspan(2);
		}
		else if (args[0] == "--depth-limit"_sv && args.size() > 1)
		{
			cfg.max_decode_depth = atoi(args[1]);
			args = args.subspan(2);
		}
		else if (args[0] == "--max-pieces"_sv && args.size() > 1)
		{
			cfg.max_pieces = atoi(args[1]);
			args = args.subspan(2);
		}
		else if (args[0] == "--max-size"_sv && args.size() > 1)
		{
			cfg.max_buffer_size = atoi(args[1]) * 1024 * 1024;
			args = args.subspan(2);
		}
		else if (args[0] == "--show-padfiles"_sv)
		{
			show_pad = true;
			args = args.subspan(1);
		}
		else
		{
			std::cerr << "unknown option: " << args[0] << "\n";
			print_usage();
		}
	}

	lt::torrent_info const t(filename, cfg);

	// print info about torrent
	if (!t.nodes().empty())
	{
		std::printf("nodes:\n");
		for (auto const& i : t.nodes())
			std::printf("%s: %d\n", i.first.c_str(), i.second);
	}

	if (!t.trackers().empty())
	{
		puts("trackers:\n");
		for (auto const& i : t.trackers())
			std::printf("%2d: %s\n", i.tier, i.url.c_str());
	}

	std::stringstream ih;
	ih << t.info_hashes().v1;
	if (t.info_hashes().has_v2())
		ih << ", " << t.info_hashes().v2;
	std::printf("number of pieces: %d\n"
		"piece length: %d\n"
		"info hash: %s\n"
		"comment: %s\n"
		"created by: %s\n"
		"magnet link: %s\n"
		"name: %s\n"
		"number of files: %d\n"
		"files:\n"
		, t.num_pieces()
		, t.piece_length()
		, ih.str().c_str()
		, t.comment().c_str()
		, t.creator().c_str()
		, make_magnet_uri(t).c_str()
		, t.name().c_str()
		, t.num_files());
	lt::file_storage const& st = t.files();
	for (auto const i : st.file_range())
	{
		auto const first = st.map_file(i, 0, 0).piece;
		auto const last = st.map_file(i, std::max(std::int64_t(st.file_size(i)) - 1, std::int64_t(0)), 0).piece;
		auto const flags = st.file_flags(i);
		if ((flags & lt::file_storage::flag_pad_file) && !show_pad) continue;
		std::stringstream file_root;
		if (!st.root(i).is_all_zeros())
			file_root << st.root(i);
		std::printf(" %8" PRIx64 " %11" PRId64 " %c%c%c%c [ %5d, %5d ] %7u %s %s %s%s\n"
			, st.file_offset(i)
			, st.file_size(i)
			, ((flags & lt::file_storage::flag_pad_file)?'p':'-')
			, ((flags & lt::file_storage::flag_executable)?'x':'-')
			, ((flags & lt::file_storage::flag_hidden)?'h':'-')
			, ((flags & lt::file_storage::flag_symlink)?'l':'-')
			, static_cast<int>(first)
			, static_cast<int>(last)
			, std::uint32_t(st.mtime(i))
			, file_root.str().c_str()
			, st.file_path(i).c_str()
			, (flags & lt::file_storage::flag_symlink) ? "-> " : ""
			, (flags & lt::file_storage::flag_symlink) ? st.symlink(i).c_str() : "");
	}
	std::printf("web seeds:\n");
	for (auto const& ws : t.web_seeds())
	{
		std::printf("%s %s\n"
			, ws.type == lt::web_seed_entry::url_seed ? "BEP19" : "BEP17"
			, ws.url.c_str());
	}

	return 0;
}
catch (std::exception const& e)
{
	std::cerr << "ERROR: " << e.what() << "\n";
}
