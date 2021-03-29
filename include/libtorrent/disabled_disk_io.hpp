/*

Copyright (c) 2016, 2019-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_DISABLED_DISK_IO
#define TORRENT_DISABLED_DISK_IO

#include "libtorrent/config.hpp"
#include "libtorrent/io_context.hpp"

#include <vector>

namespace lt {

	struct counters;
	struct disk_interface;
	struct settings_interface;

	// creates a disk io object that discards all data written to it, and only
	// returns zero-buffers when read from. May be useful for testing and
	// benchmarking.
	TORRENT_EXPORT std::unique_ptr<disk_interface> disabled_disk_io_constructor(
		io_context& ios, settings_interface const&, counters& cnt);
}

#endif

