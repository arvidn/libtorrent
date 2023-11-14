/*

Copyright (c) 2023, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_WEB_SEED_ENTRY_HPP_INCLUDED
#define TORRENT_WEB_SEED_ENTRY_HPP_INCLUDED

#include <vector>
#include <string>
#include "libtorrent/config.hpp"

#if TORRENT_ABI_VERSION < 4
#include <cstdint>
#endif

namespace libtorrent {

// the web_seed_entry holds information about a web seed (also known
// as URL seed or HTTP seed). It is essentially a URL with some state
// associated with it. For more information, see `BEP 17`_ and `BEP 19`_.
struct TORRENT_EXPORT web_seed_entry
{
#if TORRENT_ABI_VERSION < 4
	// http seeds are different from url seeds in the
	// protocol they use. http seeds follows the original
	// http seed spec. by John Hoffman
	enum TORRENT_DEPRECATED type_t { url_seed, http_seed };
#endif

	using headers_t = std::vector<std::pair<std::string, std::string>>;

	// hidden
	explicit web_seed_entry(std::string url_
		, std::string auth_ = std::string()
		, headers_t extra_headers_ = headers_t());

	web_seed_entry(web_seed_entry const&);
	web_seed_entry(web_seed_entry&&);
	web_seed_entry& operator=(web_seed_entry const&);
	web_seed_entry& operator=(web_seed_entry&&);

	// URL and type comparison
	bool operator==(web_seed_entry const& e) const
	{ return url == e.url; }

	// URL and type less-than comparison
	bool operator<(web_seed_entry const& e) const
	{ return url < e.url; }

	// The URL of the web seed
	std::string url;

	// Optional authentication. If this is set, it's passed
	// in as HTTP basic auth to the web seed. The format is:
	// username:password.
	std::string auth;

	// Any extra HTTP headers that need to be passed to the web seed
	headers_t extra_headers;

#if TORRENT_ABI_VERSION < 4
	// The type of web seed (see type_t)
	TORRENT_DEPRECATED
	std::uint8_t type = 0;
#endif
};

}

#endif
