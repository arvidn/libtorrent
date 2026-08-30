/*

Copyright (c) 2019-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/torrent_info.hpp"
#include "libtorrent/path_sanitize_flags.hpp"
#include "path_sanitize_rulesets.hpp"

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
	lt::string_view const element(reinterpret_cast<char const*>(data), size);

	for (lt::path_sanitize_flags_t const flags : fuzzers::path_sanitize_rulesets)
	{
		std::string out;
		lt::aux::sanitize_path_element(out, element, flags);
	}
	return 0;
}

