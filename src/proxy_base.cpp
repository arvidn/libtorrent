/*

Copyright (c) 2010, 2014, 2016, 2019-2020, Arvid Norberg
Copyright (c) 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/proxy_base.hpp"

namespace lt::aux {

	proxy_base::proxy_base(io_context& io_context)
		: m_sock(io_context)
		, m_port(0)
		, m_resolver(io_context)
	{}

	proxy_base::~proxy_base() = default;

	static_assert(std::is_nothrow_move_constructible<proxy_base>::value
		, "should be nothrow move constructible");
}
