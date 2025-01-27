/*

Copyright (c) 2025, 2019-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_I2P_PEX_EXTENSION_HPP_INCLUDED
#define TORRENT_I2P_PEX_EXTENSION_HPP_INCLUDED

#ifndef TORRENT_DISABLE_EXTENSIONS
#if TORRENT_USE_I2P

#include "libtorrent/config.hpp"

#include <memory>

namespace libtorrent {

	struct torrent_plugin;
	struct torrent_handle;
	struct client_data_t;

	// The i2p_pex extension gossips i2p peer addresses, only on i2p torrents.
	// The extension will not activate for non-i2p torrents.
	//
	// This can either be passed in the add_torrent_params::extensions field, or
	// via torrent_handle::add_extension().
	TORRENT_EXPORT std::shared_ptr<torrent_plugin> create_i2p_pex_plugin(torrent_handle const&, client_data_t);
}

#endif
#endif // TORRENT_DISABLE_EXTENSIONS

#endif // TORRENT_I2P_PEX_EXTENSION_HPP_INCLUDED
