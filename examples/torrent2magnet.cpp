/*

Copyright (c) 2019, Arvid Norberg
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

#include <iostream>

#include "libtorrent/torrent_info.hpp"
#include "libtorrent/magnet_uri.hpp"
#include "libtorrent/span.hpp"

void print_usage()
{
	std::cerr << R"(usage: torrent2magnet torrent-file [options]
    OPTIONS:
    --no-trackers    do not include trackers in the magnet link
    --no-web-seeds   do not include web seeds in the magnet link
)";
	std::exit(1);
}

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
