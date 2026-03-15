/*

Copyright (c) 2026, Arvid Norberg
Copyright (c) 2026, The Baron Vladimir Harkonnen
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_CURL_HPP_INCLUDED
#define TORRENT_CURL_HPP_INCLUDED
#include "libtorrent/config.hpp"

#if TORRENT_USE_CURL
#include <stdexcept>
#include <string>
#include <curl/curl.h>

namespace libtorrent::aux {

enum class curl_poll_t : int {
	none   = CURL_POLL_NONE,
	in     = CURL_POLL_IN,
	out    = CURL_POLL_OUT,
	remove = CURL_POLL_REMOVE,
};
static_assert(CURL_POLL_INOUT == (CURL_POLL_IN | CURL_POLL_OUT));

enum class curl_cselect_t : int {
	none = 0, // curl allows the value to be 0, but does not define a type for it
	in   = CURL_CSELECT_IN,
	out  = CURL_CSELECT_OUT,
	err  = CURL_CSELECT_ERR,
};

class curl_easy_error: public std::runtime_error
{
	CURLcode code_;

public:
	curl_easy_error( CURLcode const ec, std::string const & prefix ):
		std::runtime_error( prefix + ": " + curl_easy_strerror(ec) ), code_( ec ) {}

	[[nodiscard]] CURLcode code() const noexcept
	{
		return code_;
	}
};

inline bool curl_version_lower_than(unsigned int version) {
	const auto ver = curl_version_info(CURLVERSION_NOW);
	return !ver || ver->version_num < version;
}
}

#endif //TORRENT_USE_CURL
#endif //TORRENT_CURL_HPP_INCLUDED
