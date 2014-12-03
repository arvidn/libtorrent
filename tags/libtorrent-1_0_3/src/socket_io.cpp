/*

Copyright (c) 2009-2014, Arvid Norberg
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

#include <string>

#include "libtorrent/escape_string.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/socket_io.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/io.hpp" // for write_uint16
#include "libtorrent/hasher.hpp" // for hasher

namespace libtorrent
{

	std::string print_address(address const& addr)
	{
		error_code ec;
		return addr.to_string(ec);
	}

	std::string address_to_bytes(address const& a)
	{
		std::string ret;
		std::back_insert_iterator<std::string> out(ret);
		detail::write_address(a, out);
		return ret;
	}

	std::string endpoint_to_bytes(udp::endpoint const& ep)
	{
		std::string ret;
		std::back_insert_iterator<std::string> out(ret);
		detail::write_endpoint(ep, out);
		return ret;
	}

	std::string print_endpoint(tcp::endpoint const& ep)
	{
		error_code ec;
		char buf[200];
		address const& addr = ep.address();
#if TORRENT_USE_IPV6
		if (addr.is_v6())
			snprintf(buf, sizeof(buf), "[%s]:%d", addr.to_string(ec).c_str(), ep.port());
		else
#endif
			snprintf(buf, sizeof(buf), "%s:%d", addr.to_string(ec).c_str(), ep.port());
		return buf;
	}

	std::string print_endpoint(udp::endpoint const& ep)
	{
		return print_endpoint(tcp::endpoint(ep.address(), ep.port()));
	}

	void hash_address(address const& ip, sha1_hash& h)
	{
#if TORRENT_USE_IPV6
		if (ip.is_v6())
		{
			address_v6::bytes_type b = ip.to_v6().to_bytes();
			h = hasher((char*)&b[0], b.size()).final();
		}
		else
#endif
		{
			address_v4::bytes_type b = ip.to_v4().to_bytes();
			h = hasher((char*)&b[0], b.size()).final();
		}
	}

}

