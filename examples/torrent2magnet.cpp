/*

Copyright (c) 2019-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <iostream>

#include "libtorrent/torrent_info.hpp"
#include "libtorrent/magnet_uri.hpp"
#include "libtorrent/span.hpp"

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

	lt::load_torrent_limits cfg;
	lt::torrent_info t(filename, cfg);

	using namespace lt::literals;

	while (!args.empty())
	{
		if (args[0] == "--no-trackers"_sv)
		{
			t.clear_trackers();
		}
		else if (args[0] == "--no-web-seeds"_sv)
		{
			t.set_web_seeds({});
		}
		else
		{
			std::cerr << "unknown option: " << args[0] << "\n";
			print_usage();
		}
		args = args.subspan(1);
	}

	std::cout << lt::make_magnet_uri(t) << '\n';
	return 0;
}
catch (std::exception const& e)
{
	std::cerr << "ERROR: " << e.what() << "\n";
}
