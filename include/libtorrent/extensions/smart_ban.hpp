/*

Copyright (c) 2007, 2013, 2017, 2019-2020, Arvid Norberg
Copyright (c) 2015, Steven Siloti
Copyright (c) 2016, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_SMART_BAN_HPP_INCLUDED
#define TORRENT_SMART_BAN_HPP_INCLUDED

#ifndef TORRENT_DISABLE_EXTENSIONS

#include "libtorrent/config.hpp"

#include <memory>

namespace lt {

	struct torrent_plugin;
	struct torrent_handle;
	struct client_data_t;

	// constructor function for the smart ban extension. The extension keeps
	// track of the data peers have sent us for failing pieces and once the
	// piece completes and passes the hash check bans the peers that turned
	// out to have sent corrupt data.
	// This function can either be passed in the add_torrent_params::extensions
	// field, or via torrent_handle::add_extension().
	TORRENT_EXPORT std::shared_ptr<torrent_plugin> create_smart_ban_plugin(torrent_handle const&, client_data_t);
}

#endif // TORRENT_DISABLE_EXTENSIONS

#endif // TORRENT_SMART_BAN_HPP_INCLUDED
