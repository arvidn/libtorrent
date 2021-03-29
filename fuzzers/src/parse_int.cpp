/*

Copyright (c) 2019-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/
#include "libtorrent/bdecode.hpp"

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
	lt::bdecode_errors::error_code_enum ec;
	std::int64_t val = 0;
	lt::parse_int(reinterpret_cast<char const*>(data), reinterpret_cast<char const*>(data) + size, ':', val, ec);
	return 0;
}

