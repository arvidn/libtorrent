/*

Copyright (c) 2019-2020, 2022, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <iostream>

#include "libtorrent/load_torrent.hpp"
#include "libtorrent/magnet_uri.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/load_torrent.hpp"

namespace {

[[noreturn]] void print_usage()
{
	std::cerr << R"(usage: torrent2magnet torrent-file [options]
    OPTIONS:
    --no-trackers    do not include trackers in the magnet link
    --no-web-seeds   do not include web seeds in the magnet link
)";
	std::exit(1);
}

} // anonymous namespace

int main(int argc, char const* argv[]) try
{
	lt::span<char const*> args(argv, argc);

	// strip executable name
	args = args.subspan(1);

	if (args.empty()) print_usage();

	char const* filename = args[0];
	args = args.subspan(1);

	lt::add_torrent_params atp = lt::load_torrent_file(filename);

	using namespace lt::literals;

	while (!args.empty())
	{
		if (args[0] == "--no-trackers"_sv)
		{
			atp.trackers.clear();
		}
		else if (args[0] == "--no-web-seeds"_sv)
		{
			atp.url_seeds.clear();
		}
		else
		{
			std::cerr << "unknown option: " << args[0] << "\n";
			print_usage();
		}
		args = args.subspan(1);
	}

	std::cout << lt::make_magnet_uri(atp) << '\n';
	return 0;
}
catch (std::exception const& e)
{
	std::cerr << "ERROR: " << e.what() << "\n";
}
