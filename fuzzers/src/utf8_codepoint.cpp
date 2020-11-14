/*

Copyright (c) 2019-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/utf8.hpp"

#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, size_t size)
{
	if (size == 0) return 0;
	lt::parse_utf8_codepoint({reinterpret_cast<char const*>(data), size});
	return 0;
}

