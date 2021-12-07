/*

Copyright (c) 2019-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/torrent_info.hpp"

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
	lt::error_code ec;
	lt::torrent_info ti({reinterpret_cast<char const*>(data), int(size)}, ec, lt::from_span);
	return 0;
}

