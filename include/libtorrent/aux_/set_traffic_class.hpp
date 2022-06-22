/*

Copyright (c) 2022, Alden Torres
Copyright (c) 2022, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_SET_TRAFFIC_CLASS_HPP
#define TORRENT_SET_TRAFFIC_CLASS_HPP

#include "libtorrent/error_code.hpp"
#include "libtorrent/socket.hpp"

namespace libtorrent::aux {

	template <typename Socket>
	void set_traffic_class(Socket& s, int v, error_code& ec)
	{
#ifdef IP_DSCP_TRAFFIC_TYPE
		s.set_option(dscp_traffic_type((v & 0xff) >> 2), ec);
		if (!ec) return;
		ec.clear();
#endif
#if defined IPV6_TCLASS
		if (is_v6(s.local_endpoint(ec)))
			s.set_option(traffic_class(v & 0xfc), ec);
		else if (!ec)
#endif
			s.set_option(type_of_service(v & 0xfc), ec);
	}

}

#endif
