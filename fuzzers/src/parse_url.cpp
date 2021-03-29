/*

Copyright (c) 2020, Arvid Norberg
Copyright (c) 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/parse_url.hpp"

#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, size_t size)
{
	lt::error_code ec;
	lt::aux::parse_url_components(std::string(reinterpret_cast<char const*>(data), size), ec);
	return 0;
}
