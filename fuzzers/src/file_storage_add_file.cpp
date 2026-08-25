/*

Copyright (c) 2019-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/file_storage.hpp"

// add_file_borrow() is deprecated, but this fuzzer exists specifically to
// exercise its path-string parsing (resolve_owned_directory() and friends),
// which has no non-deprecated string-based equivalent.
#include "libtorrent/aux_/disable_deprecation_warnings_push.hpp"

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
	lt::file_storage fs;
	lt::error_code ec;
	// we expect this call to fail sometimes
	fs.add_file_borrow(ec, {}, {reinterpret_cast<char const*>(data), size}, 1);
	return 0;
}

