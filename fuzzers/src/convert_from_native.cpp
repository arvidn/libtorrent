/*

Copyright (c) 2019-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/escape_string.hpp"

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
	// on platforms with TORRENT_NATIVE_UTF8 set, convert_from_native() is an
	// inline identity function, so the result must be consumed to prevent
	// the optimizer from proving the call has no effect and eliding it.
	std::string const result = lt::convert_from_native({reinterpret_cast<char const*>(data), size});
	static volatile std::size_t sink;
	sink = result.size();
	static_cast<void>(sink);
	return 0;
}

