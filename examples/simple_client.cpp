/*

Copyright (c) 2003, 2005, 2009, 2015-2017, 2020, Arvid Norberg
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

#include <cstdlib>
#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/magnet_uri.hpp"

#include <iostream>

int main(int argc, char* argv[]) try
{
	if (argc != 2) {
		std::cerr << "usage: ./simple_client torrent-file|magnet-link\n"
			"to stop the client, press return.\n";
		return 1;
	}

	lt::session s;
	lt::error_code ec;
	lt::add_torrent_params p;

	// based on client_test.cpp
	lt::string_view torrent = argv[1];
	if (torrent.substr(0, 7) == "magnet:") {
		// add_magnet(s, torrent);
		p = lt::parse_magnet_uri(torrent.to_string(), ec);
		if (ec)
		{
			std::printf("invalid magnet link \"%s\": %s\n"
				, torrent.to_string().c_str(), ec.message().c_str());
			return 1;
		}
		// std::printf("adding magnet: %s\n", torrent.to_string().c_str());
	}
	else {
		p.ti = std::make_shared<lt::torrent_info>(torrent);
	}
	p.save_path = ".";
	s.add_torrent(p);

	// wait for the user to end
	char a;
	int ret = std::scanf("%c\n", &a);
	(void)ret; // ignore
	return 0;
}
catch (std::exception const& e) {
	std::cerr << "ERROR: " << e.what() << "\n";
}
