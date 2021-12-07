/*

Copyright (c) 2019-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <cstdint>
#include "libtorrent/session_params.hpp"

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
	try {
	auto ret = lt::read_session_params({reinterpret_cast<char const*>(data), int(size)});
	} catch (...) {}
	return 0;
}


