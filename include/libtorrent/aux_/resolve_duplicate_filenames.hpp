/*

Copyright (c) 2003-2011, 2013-2022, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_RESOLVE_DUPLICATE_FILENAMES_HPP_INCLUDED
#define TORRENT_RESOLVE_DUPLICATE_FILENAMES_HPP_INCLUDED

#include <string>
#include <map>
#include "libtorrent/units.hpp"
#include "libtorrent/fwd.hpp"
#include "libtorrent/error_code.hpp"

namespace libtorrent::aux {
	std::map<file_index_t, std::string> resolve_duplicate_filenames(file_storage const& fs, int max_duplicate_filenames, error_code& ec);
}

#endif
