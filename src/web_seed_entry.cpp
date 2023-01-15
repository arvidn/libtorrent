/*

Copyright (c) 2023, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/web_seed_entry.hpp"

namespace libtorrent {

web_seed_entry::web_seed_entry(std::string url_, std::string auth_, headers_t extra_headers_)
	: url(std::move(url_))
	, auth(std::move(auth_))
	, extra_headers(std::move(extra_headers_))
{
}

web_seed_entry::web_seed_entry(web_seed_entry const&) = default;
web_seed_entry::web_seed_entry(web_seed_entry&&) = default;
web_seed_entry& web_seed_entry::operator=(web_seed_entry const&) = default;
web_seed_entry& web_seed_entry::operator=(web_seed_entry&&) = default;

}
