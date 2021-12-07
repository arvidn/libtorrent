/*

Copyright (c) 2019-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/escape_string.hpp"

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
	lt::base32decode({reinterpret_cast<char const*>(data), size});
	return 0;
}

