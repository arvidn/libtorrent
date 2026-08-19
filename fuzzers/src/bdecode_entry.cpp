/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <cstdint>
#include <cstddef>

#include "libtorrent/aux_/export.hpp" // for TORRENT_ABI_VERSION

// the entry-based bdecode<InIt>() overloads in bencode.hpp (backed by
// aux::bdecode_recursive()) only exist at TORRENT_ABI_VERSION == 1. build
// this fuzzer with deprecated-functions=1 to exercise them.
#if TORRENT_ABI_VERSION == 1

#include "libtorrent/aux_/disable_deprecation_warnings_push.hpp"
#include "libtorrent/bencode.hpp"

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
	// unlike most fuzz targets, bdecode<InIt>() is a header-defined template,
	// visible to the optimizer in this TU, so the result must be consumed to
	// prevent the optimizer from proving the call has no effect and eliding it.
	char const* const begin = reinterpret_cast<char const*>(data);
	lt::entry const result = lt::bdecode(begin, begin + size);
	static volatile int sink;
	sink = static_cast<int>(result.type());
	static_cast<void>(sink);
	return 0;
}

#else

extern "C" int LLVMFuzzerTestOneInput(uint8_t const*, size_t) { return -1; }

#endif
