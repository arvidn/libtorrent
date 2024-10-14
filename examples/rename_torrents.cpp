/*

Copyright (c) 2024, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <cstdio> // for snprintf
#include <cinttypes> // for PRId64 et.al.
#include <fstream>
#include <iostream>
#include <filesystem>

#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/load_torrent.hpp"
#include "libtorrent/bdecode.hpp"
#include "libtorrent/magnet_uri.hpp"
#include "libtorrent/span.hpp"

namespace {

[[noreturn]] void print_usage()
{
	std::cerr << R"(usage: rename_torrents input-dir output-dir

This tool traversese the input directory, copying any torrent file into the
output directory under the name of the info-hash of the torrent. This can be
useful for organizing a fuzzing corpus.
)";
	std::exit(1);
}

}

int main(int argc, char const* argv[]) try
{
	namespace fs = std::filesystem;

	lt::span<char const*> args(argv, argc);

	// strip executable name
	args = args.subspan(1);

	lt::load_torrent_limits cfg;

	if (args.empty() || args.size() != 2) print_usage();

	char const* in_dir = args[0];
	auto out_dir = fs::path(args[1]);

	using namespace lt::literals;

	for(auto& p: fs::recursive_directory_iterator(in_dir))
	{
		if (!p.is_regular_file()) continue;
		if (p.path().extension() != ".torrent") continue;

		try
		{
			lt::add_torrent_params const atp = lt::load_torrent_file(p.path().string(), cfg);
			std::stringstream new_name;
			if (atp.info_hashes.has_v1())
				new_name << atp.info_hashes.v1;
			if (atp.info_hashes.has_v2())
			{
				if (atp.info_hashes.has_v1())
					new_name << "-";
				new_name << atp.info_hashes.v2;
			}
			new_name << ".torrent";
			try
			{
				fs::copy(p.path(), out_dir / fs::path(new_name.str()));
				std::cout << "\33[2K\r" << p.path();
				std::cout.flush();
			}
			catch (std::system_error const& err)
			{
				if (err.code() != std::errc::file_exists)
				{
					std::cerr << "\33[2K\rfailed to copy: " << p.path()
						<< " -> " << (out_dir / fs::path(new_name.str()))
						<< ": " << err.code().message() << "\n\n";
				}
			}
		}
		catch (std::system_error const& err)
		{
			std::cerr << "\33[2K\rfailed to load " << p.path()
				<< ": " << err.code().message() << "\n\n";
		}
	}
	return 0;
}
catch (std::exception const& e)
{
	std::cerr << "ERROR: " << e.what() << "\n";
}

