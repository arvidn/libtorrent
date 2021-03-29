/*

Copyright (c) 2016, 2019-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_STRLESS_HPP_INCLUDED
#define TORRENT_STRLESS_HPP_INCLUDED

#include <type_traits>

namespace lt::aux {
	// this enables us to compare a string_view against the std::string that's
	// held by the std::map
	struct strview_less
	{
		using is_transparent = std::true_type;
		template <typename T1, typename T2>
		bool operator()(T1 const& rhs, T2 const& lhs) const
		{ return rhs < lhs; }
	};
}

#endif
