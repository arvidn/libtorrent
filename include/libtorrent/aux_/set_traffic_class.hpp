/*

Copyright (c) 2022, Arvid Norberg
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

#ifndef TORRENT_SET_TRAFFIC_CLASS_HPP
#define TORRENT_SET_TRAFFIC_CLASS_HPP

#include "libtorrent/error_code.hpp"
#include "libtorrent/socket.hpp"

namespace libtorrent {
namespace aux {

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

}}

#endif
