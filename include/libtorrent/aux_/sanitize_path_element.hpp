/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_SANITIZE_PATH_ELEMENT_HPP_INCLUDED
#define TORRENT_SANITIZE_PATH_ELEMENT_HPP_INCLUDED

#include <string>

#include "libtorrent/aux_/export.hpp"
#include "libtorrent/path_sanitize_flags.hpp"
#include "libtorrent/string_view.hpp"

namespace libtorrent::aux {

// Sanitizes a single path element. Returns true if "element" needed no
// sanitization, in which case "path" is left untouched and the caller can
// use "element" itself (borrowed, no copy). Returns false if "path" holds
// the sanitized result instead.
TORRENT_EXTRA_EXPORT bool sanitize_path_element(std::string& path,
	string_view element,
	path_sanitize_flags_t sanitize_flags,
	bool force_element = false);

}

#endif
