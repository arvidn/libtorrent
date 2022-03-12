/*

Copyright (c) 2015-2017, 2019, 2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "setup_transfer.hpp" // for ::create_torrent
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/create_torrent.hpp"
#include <fstream>


std::string save_path(int idx)
{
	int const swarm_id = unit_test::test_counter();
	char path[200];
	std::snprintf(path, sizeof(path), "swarm-%04d-peer-%02d"
		, swarm_id, idx);
	return path;
}

lt::add_torrent_params create_torrent(int const idx, bool const seed
	, int const num_pieces, lt::create_flags_t const flags)
{
	// TODO: if we want non-seeding torrents, that could be a bit cheaper to
	// create
	lt::add_torrent_params params;
	int swarm_id = unit_test::test_counter();
	char name[200];
	std::snprintf(name, sizeof(name), "temp-%02d", swarm_id);
	std::string path = save_path(idx);
	lt::error_code ec;
	lt::create_directory(path, ec);
	if (ec) std::printf("failed to create directory: \"%s\": %s\n"
		, path.c_str(), ec.message().c_str());
	std::ofstream file(lt::combine_path(path, name).c_str());
	params.ti = ::create_torrent(&file, name, 0x4000, num_pieces + idx, false, flags);
	file.close();

	// by setting the save path to a dummy path, it won't be seeding
	params.save_path = seed ? path : "dummy";
	return params;
}


