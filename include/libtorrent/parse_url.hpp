/*

Copyright (c) 2008-2009, 2014, 2016-2017, 2019-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_PARSE_URL_HPP_INCLUDED
#define TORRENT_PARSE_URL_HPP_INCLUDED

#include "libtorrent/config.hpp"

#include <tuple>
#include <string>

#include "libtorrent/string_view.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/string_view.hpp"

namespace libtorrent {

	// returns protocol, auth, hostname, port, path
	TORRENT_EXTRA_EXPORT std::tuple<std::string, std::string
		, std::string, int, std::string>
		parse_url_components(string_view url, error_code& ec);

	// split a URL in its base and path parts
	TORRENT_EXTRA_EXPORT std::tuple<std::string, std::string>
		split_url(std::string url, error_code& ec);

	// returns true if the hostname contains any IDNA (internationalized domain
	// name) labels.
	TORRENT_EXTRA_EXPORT bool is_idna(string_view hostname);
}

#endif
