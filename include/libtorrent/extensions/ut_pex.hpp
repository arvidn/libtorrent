/*

Copyright (c) 2006, MassaRoddel
Copyright (c) 2006, 2013, 2015-2017, 2019-2020, Arvid Norberg
Copyright (c) 2015, Steven Siloti
Copyright (c) 2016, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_UT_PEX_EXTENSION_HPP_INCLUDED
#define TORRENT_UT_PEX_EXTENSION_HPP_INCLUDED

#ifndef TORRENT_DISABLE_EXTENSIONS

#include "libtorrent/config.hpp"

#include <memory>

namespace lt {

	struct torrent_plugin;
	struct torrent_handle;
	struct client_data_t;

	// constructor function for the ut_pex extension. The ut_pex
	// extension allows peers to gossip about their connections, allowing
	// the swarm stay well connected and peers aware of more peers in the
	// swarm. This extension is enabled by default unless explicitly disabled in
	// the session constructor.
	//
	// This can either be passed in the add_torrent_params::extensions field, or
	// via torrent_handle::add_extension().
	TORRENT_EXPORT std::shared_ptr<torrent_plugin> create_ut_pex_plugin(torrent_handle const&, client_data_t);
}

#endif // TORRENT_DISABLE_EXTENSIONS

#endif // TORRENT_UT_PEX_EXTENSION_HPP_INCLUDED
