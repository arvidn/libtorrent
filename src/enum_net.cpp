/*

Copyright (c) 2007, Arvid Norberg
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

#if defined __linux__ || defined __MACH__
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#endif

#include "libtorrent/enum_net.hpp"

namespace libtorrent
{
	std::vector<address> enum_net_interfaces(asio::io_service& ios, asio::error_code& ec)
	{
		std::vector<address> ret;

#if defined __linux__ || defined __MACH__ || defined(__FreeBSD__)
		int s = socket(AF_INET, SOCK_DGRAM, 0);
		if (s < 0)
		{
			ec = asio::error::fault;
			return ret;
		}
		ifconf ifc;
		char buf[1024];
		ifc.ifc_len = sizeof(buf);
		ifc.ifc_buf = buf;
		if (ioctl(s, SIOCGIFCONF, &ifc) < 0)
		{
			close(s);
			ec = asio::error::fault;
			return ret;
		}
		close(s);

		char *ifr = (char*)ifc.ifc_req;
		int remaining = ifc.ifc_len;

		while (remaining)
		{
			ifreq const& item = *reinterpret_cast<ifreq*>(ifr);
			if (item.ifr_addr.sa_family == AF_INET)
			{
				typedef asio::ip::address_v4::bytes_type bytes_t;
				bytes_t b;
				memcpy(&b[0], &((sockaddr_in const*)&item.ifr_addr)->sin_addr, b.size());
				ret.push_back(address_v4(b));
			}
			else if (item.ifr_addr.sa_family == AF_INET6)
			{
				typedef asio::ip::address_v6::bytes_type bytes_t;
				bytes_t b;
				memcpy(&b[0], &((sockaddr_in6 const*)&item.ifr_addr)->sin6_addr, b.size());
				ret.push_back(address_v6(b));
			}

#if defined __MACH__ || defined(__FreeBSD__)
			int current_size = item.ifr_addr.sa_len + IFNAMSIZ;
#elif defined __linux__
			int current_size = sizeof(ifreq);
#endif
			ifr += current_size;
			remaining -= current_size;
		}

#elif defined WIN32

		SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
		if (s == SOCKET_ERROR)
		{
			ec = asio::error::fault;
			return ret;
		}

		INTERFACE_INFO buffer[30];
		DWORD size;
	
		if (WSAIoctl(s, SIO_GET_INTERFACE_LIST, 0, 0, buffer,
			sizeof(buffer), &size, 0, 0) != 0)
		{
			closesocket(s);
			ec = asio::error::fault;
			return ret;
		}
		closesocket(s);

		int n = size / sizeof(INTERFACE_INFO);

		for (int i = 0; i < n; ++i)
		{
			sockaddr_in *sockaddr = (sockaddr_in*)&buffer[i].iiAddress;
			address a(address::from_string(inet_ntoa(sockaddr->sin_addr)));
			if (a == address_v4::any()) continue;
			ret.push_back(a);
		}

#else
		// make a best guess of the interface we're using and its IP
		udp::resolver r(ios);
		udp::resolver::iterator i = r.resolve(udp::resolver::query(asio::ip::host_name(), "0"));
		for (;i != udp::resolver_iterator(); ++i)
		{
			ret.push_back(i->endpoint().address());
		}
#endif
		return ret;
	}

}


