/*

Copyright (c) 2016-2017, 2019-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_STRING_VIEW_HPP_INCLUDED
#define TORRENT_STRING_VIEW_HPP_INCLUDED

#include <string_view>

namespace lt {

using std::string_view;
using std::wstring_view;

inline namespace literals {

	constexpr string_view operator "" _sv(char const* str, std::size_t len)
	{ return string_view(str, len); }
}
}

#endif
