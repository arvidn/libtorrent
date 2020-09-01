/*

Copyright (c) 2019, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/torrent_info.hpp"

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
	std::string str{reinterpret_cast<char const*>(data), size};
	lt::aux::verify_encoding(str);
	return 0;
}

