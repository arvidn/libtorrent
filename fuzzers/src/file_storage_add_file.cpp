/*

Copyright (c) 2019-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/file_storage.hpp"

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
	lt::file_storage fs;
	// we expect this call to fail sometimes
	try {
		fs.add_file({reinterpret_cast<char const*>(data), size}, 1);
	}
	catch (...) {}
	return 0;
}

