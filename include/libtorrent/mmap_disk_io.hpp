/*

Copyright (c) 2007-2018, Steven Siloti
Copyright (c) 2007, 2013-2016, 2019-2020, Arvid Norberg
Copyright (c) 2016, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_DISK_IO_THREAD
#define TORRENT_DISK_IO_THREAD

#include "libtorrent/config.hpp"
#include "libtorrent/disk_interface.hpp"
#include "libtorrent/io_context.hpp"

namespace lt {

#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE

	struct counters;
	struct settings_interface;

	// constructs a memory mapped file disk I/O object.
	TORRENT_EXPORT std::unique_ptr<disk_interface> mmap_disk_io_constructor(
		io_context& ios, settings_interface const&, counters& cnt);

#endif // HAVE_MMAP || HAVE_MAP_VIEW_OF_FILE

}

#endif // TORRENT_DISK_IO_THREAD
