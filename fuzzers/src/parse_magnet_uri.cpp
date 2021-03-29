/*

Copyright (c) 2019-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/magnet_uri.hpp"
#include "libtorrent/add_torrent_params.hpp"

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
	lt::error_code ec;
	lt::add_torrent_params params;
	lt::parse_magnet_uri({reinterpret_cast<char const*>(data), size}
		, params, ec);
	return 0;
}


