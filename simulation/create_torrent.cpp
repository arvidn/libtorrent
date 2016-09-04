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

#include "test.hpp"
#include "setup_transfer.hpp" // for ::create_torrent
#include "libtorrent/add_torrent_params.hpp"
#include <fstream>

namespace lt = libtorrent;

std::string save_path(int idx)
{
	int const swarm_id = test_counter();
	char path[200];
	std::snprintf(path, sizeof(path), "swarm-%04d-peer-%02d"
		, swarm_id, idx);
	return path;
}

lt::add_torrent_params create_torrent(int const idx, bool const seed
	, int const num_pieces)
{
	// TODO: if we want non-seeding torrents, that could be a bit cheaper to
	// create
	lt::add_torrent_params params;
	int swarm_id = test_counter();
	char name[200];
	std::snprintf(name, sizeof(name), "temp-%02d", swarm_id);
	std::string path = save_path(idx);
	lt::error_code ec;
	lt::create_directory(path, ec);
	if (ec) std::fprintf(stderr, "failed to create directory: \"%s\": %s\n"
		, path.c_str(), ec.message().c_str());
	std::ofstream file(lt::combine_path(path, name).c_str());
	params.ti = ::create_torrent(&file, name, 0x4000, num_pieces + idx, false);
	file.close();

	// by setting the save path to a dummy path, it won't be seeding
	params.save_path = seed ? path : "dummy";
	return params;
}


