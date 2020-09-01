/*

Copyright (c) 2019, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/torrent_info.hpp"

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
	std::string out;
	lt::aux::sanitize_append_path_element(out, {reinterpret_cast<char const*>(data), size});
	return 0;
}

