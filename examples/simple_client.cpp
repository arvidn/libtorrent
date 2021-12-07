/*

Copyright (c) 2003, 2005, 2009, 2015-2017, 2020-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <cstdlib>
#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/torrent_info.hpp"

#include <iostream>

int main(int argc, char* argv[]) try
{
	if (argc != 2) {
		std::cerr << "usage: ./simple_client torrent-file\n"
			"to stop the client, press return.\n";
		return 1;
	}

	lt::session s;
	lt::add_torrent_params p;
	p.save_path = ".";
	p.ti = std::make_shared<lt::torrent_info>(argv[1]);
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
