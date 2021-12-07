/*

Copyright (c) 2015, 2017-2019, 2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_SIM_CREATE_TORRENT_HPP_INCLUDED
#define TORRENT_SIM_CREATE_TORRENT_HPP_INCLUDED

#include <string>
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/create_torrent.hpp"

std::string save_path(int idx);
lt::add_torrent_params create_torrent(int idx, bool seed = true
	, int num_pieces = 9, lt::create_flags_t flags = {});

#endif

