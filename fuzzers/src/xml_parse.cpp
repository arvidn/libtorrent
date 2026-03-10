/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <cstdint>
#include <cstddef>
#include "libtorrent/aux_/xml_parse.hpp"

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, std::size_t size)
{
	lt::aux::xml_parse({reinterpret_cast<char const*>(data), size}
		, [](int const tag, lt::string_view const str1, lt::string_view const str2) {
		TORRENT_ASSERT(tag >= lt::aux::xml_start_tag && tag <= lt::aux::xml_tag_content);
		TORRENT_ASSERT(str1.size() < 0x7fffffff);
		TORRENT_ASSERT(str2.size() < 0x7fffffff);
	});
	return 0;
}
