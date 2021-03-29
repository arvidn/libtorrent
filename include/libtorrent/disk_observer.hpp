/*

Copyright (c) 2010, 2013-2015, 2017, 2020, Arvid Norberg
Copyright (c) 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_DISK_OBSERVER_HPP
#define TORRENT_DISK_OBSERVER_HPP

#include "libtorrent/config.hpp"

namespace lt {

	struct TORRENT_EXPORT disk_observer
	{
		// called when the disk cache size has dropped
		// below the low watermark again and we can
		// resume downloading from peers
		virtual void on_disk() = 0;
	protected:
		~disk_observer() {}
	};
}

#endif
