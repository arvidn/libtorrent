/*

Copyright (c) 2007, 2013, 2017, 2019-2021, Arvid Norberg
Copyright (c) 2015, Steven Siloti
Copyright (c) 2016, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_SMART_BAN_EXTENSION_HPP_INCLUDED
#define TORRENT_SMART_BAN_EXTENSION_HPP_INCLUDED

#ifndef TORRENT_DISABLE_EXTENSIONS

#include "libtorrent/config.hpp"

#include <memory>

namespace libtorrent {

	struct torrent_plugin;
	struct torrent_handle;
	struct client_data_t;

#if TORRENT_ABI_VERSION < 5
	// deprecated stub kept for callers of
	// add_extension(&create_smart_ban_plugin); constructs nothing and
	// always returns a null pointer. Corrupt-data banning is controlled by
	// settings_pack::enable_smart_ban.
	TORRENT_DEPRECATED_EXPORT
	std::shared_ptr<torrent_plugin> create_smart_ban_plugin(torrent_handle const&, client_data_t);
#endif
}

#endif // TORRENT_DISABLE_EXTENSIONS

#endif // TORRENT_SMART_BAN_EXTENSION_HPP_INCLUDED
