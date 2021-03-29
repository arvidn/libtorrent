/*

Copyright (c) 2016-2017, 2019-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_POSIX_DISK_IO
#define TORRENT_POSIX_DISK_IO

#include "libtorrent/config.hpp"
#include "libtorrent/io_context.hpp"

#include <memory>

namespace lt {

	struct counters;
	struct disk_interface;
	struct settings_interface;

	// this is a simple posix disk I/O back-end, used for systems that don't
	// have a 64 bit virtual address space or don't support memory mapped files.
	// It's implemented using portable C file functions and is single-threaded.
	TORRENT_EXPORT std::unique_ptr<disk_interface> posix_disk_io_constructor(
		io_context& ios, settings_interface const&, counters& cnt);
}

#endif

