/*

Copyright (c) 2019-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/load_torrent.hpp"

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
	try
	{
		lt::add_torrent_params atp = lt::load_torrent_buffer({reinterpret_cast<char const*>(data), int(size)});
	}
	catch (lt::system_error comst)
	{
		return 0;
	}
	return 0;
}

