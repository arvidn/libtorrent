/*

Copyright (c) 2019-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/bdecode.hpp"

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
	lt::error_code ec;
	lt::bdecode({reinterpret_cast<char const*>(data), int(size)}, ec);
	return 0;
}

