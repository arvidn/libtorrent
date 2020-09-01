/*

Copyright (c) 2019, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/utf8.hpp"

#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, size_t size)
{
	lt::parse_utf8_codepoint({reinterpret_cast<char const*>(data), size});
	return 0;
}

