/*

Copyright (c) 2017, 2019, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/


#ifndef TORRENT_OPTIONAL_HPP_INCLUDED
#define TORRENT_OPTIONAL_HPP_INCLUDED

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/optional.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent {

	template <typename T, typename U>
	T value_or(boost::optional<T> opt, U def)
	{
		return opt ? *opt : T(def);
	}
}

#endif

