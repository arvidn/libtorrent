/*

Copyright (c) 2007, 2013, 2016-2017, 2019-2020, Arvid Norberg
Copyright (c) 2015, Steven Siloti
Copyright (c) 2016, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_UT_METADATA_HPP_INCLUDED
#define TORRENT_UT_METADATA_HPP_INCLUDED

#ifndef TORRENT_DISABLE_EXTENSIONS

#include "libtorrent/config.hpp"

#include <memory>

namespace lt {

	struct torrent_plugin;
	struct torrent_handle;
	struct client_data_t;

	// constructor function for the ut_metadata extension. The ut_metadata
	// extension allows peers to request the .torrent file (or more
	// specifically the info-dictionary of the .torrent file) from each
	// other. This is the main building block in making magnet links work.
	// This extension is enabled by default unless explicitly disabled in
	// the session constructor.
	//
	// This can either be passed in the add_torrent_params::extensions field, or
	// via torrent_handle::add_extension().
	TORRENT_EXPORT std::shared_ptr<torrent_plugin> create_ut_metadata_plugin(torrent_handle const&, client_data_t);
}

#endif // TORRENT_DISABLE_EXTENSIONS
#endif // TORRENT_UT_METADATA_HPP_INCLUDED
