/*

Copyright (c) 2017-2018, 2020-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_WRITE_RESUME_DATA_HPP_INCLUDE
#define TORRENT_WRITE_RESUME_DATA_HPP_INCLUDE

#include <vector>

#include "libtorrent/fwd.hpp"
#include "libtorrent/aux_/export.hpp"
#include "libtorrent/bencode.hpp"

namespace libtorrent {

	// this function turns the resume data in an ``add_torrent_params`` object
	// into a bencoded structure
	TORRENT_EXPORT entry write_resume_data(add_torrent_params const& atp);
	TORRENT_EXPORT std::vector<char> write_resume_data_buf(add_torrent_params const& atp);

	// writes only the fields to create a .torrent file. This function may fail
	// with a ``std::system_error`` exception if:
	//
	// * The add_torrent_params object passed to this function does not contain the
	//   info dictionary (the ``ti`` field)
	// * The piece layers are not complete for all files that need them
	TORRENT_EXPORT entry write_torrent_file(add_torrent_params const& atp);
}

#endif
