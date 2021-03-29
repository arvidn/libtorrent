/*

Copyright (c) 2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_POOL_HPP
#define TORRENT_POOL_HPP

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/pool/pool.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace lt::aux {

struct allocator_new_delete
{
	using size_type = std::size_t;
	using difference_type = std::ptrdiff_t;

	// TODO: ensure the alignment is good here
	static char* malloc(size_type const bytes)
	{ return new char[bytes]; }
	static void free(char* const block)
	{ delete [] block; }
};

using pool = boost::pool<allocator_new_delete>;

}

#endif
