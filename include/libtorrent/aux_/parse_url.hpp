/*

Copyright (c) 2008-2009, 2014, 2016, 2020, Arvid Norberg
Copyright (c) 2021, Alden Torres
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

namespace lt::aux {

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

	// the query string is the part of the URL immediately following "?", i.e.
	// the query string arguments. This function returns true if any of the
	// arguments are "info_hash", "port", "key", "event", "uploaded",
	// "downloaded", "left" or "corrupt".
	TORRENT_EXTRA_EXPORT bool has_tracker_query_string(string_view query_string);
}

#endif
