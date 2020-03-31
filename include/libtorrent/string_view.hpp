/*

Copyright (c) 2016-2018, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#ifndef TORRENT_STRING_VIEW_HPP_INCLUDED
#define TORRENT_STRING_VIEW_HPP_INCLUDED

#include <boost/version.hpp>

#include "libtorrent/aux_/disable_warnings_push.hpp"

// TODO: replace this by the standard string_view in C++17

#if BOOST_VERSION < 106100
#include <boost/utility/string_ref.hpp>
#include <cstring> // for strchr
namespace libtorrent {

using string_view = boost::string_ref;
using wstring_view = boost::wstring_ref;

// internal
inline string_view::size_type find_first_of(string_view const v, char const c
	, string_view::size_type pos)
{
	while (pos < v.size())
	{
		if (v[pos] == c) return pos;
		++pos;
	}
	return string_view::npos;
}

// internal
inline string_view::size_type find_first_of(string_view const v, char const* c
	, string_view::size_type pos)
{
	while (pos < v.size())
	{
		if (std::strchr(c, v[pos]) != nullptr) return pos;
		++pos;
	}
	return string_view::npos;
}
}
#else
#include <boost/utility/string_view.hpp>
namespace libtorrent {

using string_view = boost::string_view;
using wstring_view = boost::wstring_view;

// internal
inline string_view::size_type find_first_of(string_view const v, char const c
	, string_view::size_type pos)
{
	return v.find_first_of(c, pos);
}

// internal
inline string_view::size_type find_first_of(string_view const v, char const* c
	, string_view::size_type pos)
{
	return v.find_first_of(c, pos);
}
}
#endif

namespace libtorrent {

inline namespace literals {

	constexpr string_view operator "" _sv(char const* str, std::size_t len)
	{ return string_view(str, len); }
}
}


#include "libtorrent/aux_/disable_warnings_pop.hpp"

#endif
