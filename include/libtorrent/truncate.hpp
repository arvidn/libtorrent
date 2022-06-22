/*

Copyright (c) 2022, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_TRUNCATE_HPP_INCLUDED
#define TORRENT_TRUNCATE_HPP_INCLUDED

#include "libtorrent/fwd.hpp"
#include "libtorrent/error_code.hpp"
#include <string>

namespace libtorrent {

// Truncates files larger than specified in the file_storage, saved under
// the specified save_path.
TORRENT_EXPORT void truncate_files(file_storage const& fs, std::string const& save_path, storage_error& ec);

}

#endif
