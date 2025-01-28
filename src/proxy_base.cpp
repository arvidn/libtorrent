/*

Copyright (c) 2010, 2014, 2016, 2019-2021, Arvid Norberg
Copyright (c) 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/proxy_base.hpp"

namespace libtorrent::aux {

	proxy_base::proxy_base(io_context& io_context)
		: m_sock(io_context)
		, m_resolver(io_context)
	{}

#if TORRENT_USE_ASSERTS
	proxy_base::~proxy_base()
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		m_magic = 0;
	}
#else
	proxy_base::~proxy_base() = default;
#endif

	static_assert(std::is_nothrow_move_constructible<proxy_base>::value
		, "should be nothrow move constructible");
}
