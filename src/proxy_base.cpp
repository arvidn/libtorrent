/*

Copyright (c) 2010, 2014, 2016-2017, 2019, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/proxy_base.hpp"

namespace libtorrent::aux {

	proxy_base::proxy_base(io_context& io_context)
		: m_sock(io_context)
		, m_port(0)
		, m_resolver(io_context)
	{}

	proxy_base::~proxy_base() = default;

	static_assert(std::is_nothrow_move_constructible<proxy_base>::value
		, "should be nothrow move constructible");
}
