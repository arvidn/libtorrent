/*

Copyright (c) 2005, 2008-2009, 2013, 2016-2020, Arvid Norberg
Copyright (c) 2020, Paul-Louis Ageneau
Copyright (c) 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_UTF8_HPP_INCLUDED
#define TORRENT_UTF8_HPP_INCLUDED

#include "libtorrent/aux_/export.hpp"

#include <cstdint>
#include <string>
#include "libtorrent/string_view.hpp"
#include "libtorrent/span.hpp"

namespace lt::aux {

	TORRENT_EXTRA_EXPORT std::pair<std::int32_t, int>
		parse_utf8_codepoint(string_view str);

	TORRENT_EXTRA_EXPORT void append_utf8_codepoint(std::string&, std::int32_t);

	TORRENT_EXTRA_EXPORT std::string latin1_utf8(span<char const> s);
	TORRENT_EXTRA_EXPORT std::string utf8_latin1(std::string_view sv);

} // namespace lt

#endif
