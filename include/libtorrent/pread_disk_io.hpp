/*

Copyright (c) 2022, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_PREAD_DISK_IO_HPP
#define TORRENT_PREAD_DISK_IO_HPP

#include "libtorrent/config.hpp"
#include "libtorrent/disk_interface.hpp"
#include "libtorrent/io_context.hpp"

namespace libtorrent {

	struct counters;
	struct settings_interface;

	// constructs a multi-threaded file disk I/O using pread()/pwrite()
	TORRENT_EXPORT std::unique_ptr<disk_interface> pread_disk_io_constructor(
		io_context& ios, settings_interface const&, counters& cnt);

}

#endif // TORRENT_PREAD_DISK_IO_HPP
