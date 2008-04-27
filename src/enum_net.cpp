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

#include "libtorrent/config.hpp"

#if defined TORRENT_BSD
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <net/route.h>
#elif defined TORRENT_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <iphlpapi.h>
#elif defined TORRENT_LINUX
#include <asm/types.h>
#include <netinet/ether.h>
#include <netinet/in.h>
#include <net/if.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

struct route_info
{
	u_int dst_addr;
	u_int src_addr;
	u_int gateway;
	char if_name[IF_NAMESIZE];
};

int read_nl_sock(int sock, char *buf, int bufsize, int seq, int pid)
{
	nlmsghdr* nl_hdr;

	int msg_len = 0;

	do
	{
		int read_len = recv(sock, buf, bufsize - msg_len, 0);
		if (read_len < 0) return -1;

		nl_hdr = (nlmsghdr*)buf;

		if ((NLMSG_OK(nl_hdr, read_len) == 0) || (nl_hdr->nlmsg_type == NLMSG_ERROR))
			return -1;

		if (nl_hdr->nlmsg_type == NLMSG_DONE) break;

		buf += read_len;
		msg_len += read_len;

		if ((nl_hdr->nlmsg_flags & NLM_F_MULTI) == 0) break;

	} while((nl_hdr->nlmsg_seq != seq) || (nl_hdr->nlmsg_pid != pid));
	return msg_len;
}

// For parsing the route info returned
void parse_route(nlmsghdr *nl_hdr, route_info *rt_info)
{
	rtmsg* rt_msg = (rtmsg*)NLMSG_DATA(nl_hdr);

	if((rt_msg->rtm_family != AF_INET) || (rt_msg->rtm_table != RT_TABLE_MAIN))
		return;

	int rt_len = RTM_PAYLOAD(nl_hdr);
	for (rtattr* rt_attr = (rtattr*)RTM_RTA(rt_msg);
		RTA_OK(rt_attr,rt_len); rt_attr = RTA_NEXT(rt_attr,rt_len))
	{
		switch(rt_attr->rta_type)
		{
		case RTA_OIF:
			if_indextoname(*(int*)RTA_DATA(rt_attr), rt_info->if_name);
			break;
		case RTA_GATEWAY:
			rt_info->gateway = *(u_int*)RTA_DATA(rt_attr);
			break;
		case RTA_PREFSRC:
			rt_info->src_addr = *(u_int*)RTA_DATA(rt_attr);
			break;
		case RTA_DST:
			rt_info->dst_addr = *(u_int*)RTA_DATA(rt_attr);
			break;
		}
	}
	return;
}
#endif

#include "libtorrent/enum_net.hpp"
#include "libtorrent/broadcast_socket.hpp"
#include <asio/ip/host_name.hpp>

namespace libtorrent
{
	namespace
	{
		address inaddr_to_address(in_addr const* ina)
		{
			typedef asio::ip::address_v4::bytes_type bytes_t;
			bytes_t b;
			memcpy(&b[0], ina, b.size());
			return address_v4(b);
		}

		address inaddr6_to_address(in6_addr const* ina6)
		{
			typedef asio::ip::address_v6::bytes_type bytes_t;
			bytes_t b;
			memcpy(&b[0], ina6, b.size());
			return address_v6(b);
		}

		address sockaddr_to_address(sockaddr const* sin)
		{
			if (sin->sa_family == AF_INET)
				return inaddr_to_address(&((sockaddr_in const*)sin)->sin_addr);
			else if (sin->sa_family == AF_INET6)
				return inaddr6_to_address(&((sockaddr_in6 const*)sin)->sin6_addr);
			return address();
		}

#ifdef TORRENT_BSD
		bool verify_sockaddr(sockaddr_in* sin)
		{
			return (sin->sin_len == sizeof(sockaddr_in)
				&& sin->sin_family == AF_INET)
				|| (sin->sin_len == sizeof(sockaddr_in6)
				&& sin->sin_family == AF_INET6);
		}
#endif

	}
	
	bool in_subnet(address const& addr, ip_interface const& iface)
	{
		if (addr.is_v4() != iface.interface_address.is_v4()) return false;
		// since netmasks seems unreliable for IPv6 interfaces
		// (MacOS X returns AF_INET addresses as bitmasks) assume
		// that any IPv6 address belongs to the subnet of any
		// interface with an IPv6 address
		if (addr.is_v6()) return true;

		return (addr.to_v4().to_ulong() & iface.netmask.to_v4().to_ulong())
			== (iface.interface_address.to_v4().to_ulong() & iface.netmask.to_v4().to_ulong());
	}

	bool in_local_network(asio::io_service& ios, address const& addr, asio::error_code& ec)
	{
		std::vector<ip_interface> const& net = enum_net_interfaces(ios, ec);
		if (ec) return false;
		for (std::vector<ip_interface>::const_iterator i = net.begin()
			, end(net.end()); i != end; ++i)
		{
			if (in_subnet(addr, *i)) return true;
		}
		return false;
	}
	
	std::vector<ip_interface> enum_net_interfaces(asio::io_service& ios, asio::error_code& ec)
	{
		std::vector<ip_interface> ret;
// covers linux, MacOS X and BSD distributions
#if defined TORRENT_LINUX || defined TORRENT_BSD
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
			ec = asio::error_code(errno, asio::error::system_category);
			close(s);
			return ret;
		}

		char *ifr = (char*)ifc.ifc_req;
		int remaining = ifc.ifc_len;

		while (remaining)
		{
			ifreq const& item = *reinterpret_cast<ifreq*>(ifr);

			if (item.ifr_addr.sa_family == AF_INET
				|| item.ifr_addr.sa_family == AF_INET6)
			{
				ip_interface iface;
				iface.interface_address = sockaddr_to_address(&item.ifr_addr);

				ifreq netmask = item;
				if (ioctl(s, SIOCGIFNETMASK, &netmask) < 0)
				{
					if (iface.interface_address.is_v6())
					{
						// this is expected to fail (at least on MacOS X)
						iface.netmask = address_v6::any();
					}
					else
					{
						ec = asio::error_code(errno, asio::error::system_category);
						close(s);
						return ret;
					}
				}
				else
				{
					iface.netmask = sockaddr_to_address(&netmask.ifr_addr);
				}
				ret.push_back(iface);
			}

#if defined TORRENT_BSD
			int current_size = item.ifr_addr.sa_len + IFNAMSIZ;
#elif defined TORRENT_LINUX
			int current_size = sizeof(ifreq);
#endif
			ifr += current_size;
			remaining -= current_size;
		}
		close(s);

#elif defined TORRENT_WINDOWS

		SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
		if (s == SOCKET_ERROR)
		{
			ec = asio::error_code(WSAGetLastError(), asio::error::system_category);
			return ret;
		}

		INTERFACE_INFO buffer[30];
		DWORD size;
	
		if (WSAIoctl(s, SIO_GET_INTERFACE_LIST, 0, 0, buffer,
			sizeof(buffer), &size, 0, 0) != 0)
		{
			ec = asio::error_code(WSAGetLastError(), asio::error::system_category);
			closesocket(s);
			return ret;
		}
		closesocket(s);

		int n = size / sizeof(INTERFACE_INFO);

		ip_interface iface;
		for (int i = 0; i < n; ++i)
		{
			iface.interface_address = sockaddr_to_address(&buffer[i].iiAddress.Address);
			iface.netmask = sockaddr_to_address(&buffer[i].iiNetmask.Address);
			if (iface.interface_address == address_v4::any()) continue;
			ret.push_back(iface);
		}

#else
#warning THIS OS IS NOT RECOGNIZED, enum_net_interfaces WILL PROBABLY NOT WORK
		// make a best guess of the interface we're using and its IP
		udp::resolver r(ios);
		udp::resolver::iterator i = r.resolve(udp::resolver::query(asio::ip::host_name(ec), "0"), ec);
		if (ec) return ret;
		ip_interface iface;
		for (;i != udp::resolver_iterator(); ++i)
		{
			iface.interface_address = i->endpoint().address();
			if (iface.interface_address.is_v4())
				iface.netmask = address_v4::netmask(iface.interface_address.to_v4());
			ret.push_back(iface);
		}
#endif
		return ret;
	}

	address get_default_gateway(asio::io_service& ios, address const& interface, asio::error_code& ec)
	{
	
#if defined TORRENT_BSD

		struct rt_msg
		{
			rt_msghdr m_rtm;
			char buf[512];
		};

		rt_msg m;
		int len = sizeof(rt_msg);
		bzero(&m, len);
		m.m_rtm.rtm_type = RTM_GET;
		m.m_rtm.rtm_flags = RTF_UP | RTF_GATEWAY;
		m.m_rtm.rtm_version = RTM_VERSION;
		m.m_rtm.rtm_addrs = RTA_DST | RTA_GATEWAY;
		m.m_rtm.rtm_seq = 0;
		m.m_rtm.rtm_msglen = len;

		int s = socket(PF_ROUTE, SOCK_RAW, AF_INET);
		if (s == -1)
		{
			ec = asio::error_code(errno, asio::error::system_category);
			return address_v4::any();
		}

		int n = write(s, &m, len);
		if (n == -1)
		{
			ec = asio::error_code(errno, asio::error::system_category);
			close(s);
			return address_v4::any();
		}
		else if (n != len)
		{
			ec = asio::error::operation_not_supported;
			close(s);
			return address_v4::any();
		}
		bzero(&m, len);

		n = read(s, &m, len);
		if (n == -1)
		{
			ec = asio::error_code(errno, asio::error::system_category);
			close(s);
			return address_v4::any();
		}
		close(s);

		TORRENT_ASSERT(m.m_rtm.rtm_seq == 0);
		TORRENT_ASSERT(m.m_rtm.rtm_pid == getpid());
		if (m.m_rtm.rtm_errno)
		{
			ec = asio::error_code(m.m_rtm.rtm_errno, asio::error::system_category);
			return address_v4::any();
		}
		if (m.m_rtm.rtm_flags & RTF_UP == 0
			|| m.m_rtm.rtm_flags & RTF_GATEWAY == 0)
		{
			ec = asio::error::operation_not_supported;
			return address_v4::any();
		}
		if (m.m_rtm.rtm_addrs & RTA_DST == 0
			|| m.m_rtm.rtm_addrs & RTA_GATEWAY == 0)
		{
			ec = asio::error::operation_not_supported;
			return address_v4::any();
		}
		if (m.m_rtm.rtm_msglen > len)
		{
			ec = asio::error::operation_not_supported;
			return address_v4::any();
		}
		int min_len = sizeof(rt_msghdr) + 2 * sizeof(sockaddr_in);
		if (m.m_rtm.rtm_msglen < min_len)
		{
			ec = asio::error::operation_not_supported;
			return address_v4::any();
		}

		// default route
		char* p = m.buf;
		sockaddr_in* sin = (sockaddr_in*)p;
		if (!verify_sockaddr(sin))
		{
			ec = asio::error::operation_not_supported;
			return address_v4::any();
		}

		// default gateway
		p += sin->sin_len;
		sin = (sockaddr_in*)p;
		if (!verify_sockaddr(sin))
		{
			ec = asio::error::operation_not_supported;
			return address_v4::any();
		}

		return sockaddr_to_address((sockaddr*)sin);

#elif defined TORRENT_WINDOWS

		// Load Iphlpapi library
		HMODULE iphlp = LoadLibraryA("Iphlpapi.dll");
		if (!iphlp)
		{
			ec = asio::error::operation_not_supported;
			return address_v4::any();
		}

		// Get GetAdaptersInfo() pointer
		typedef DWORD (WINAPI *GetAdaptersInfo_t)(PIP_ADAPTER_INFO, PULONG);
		GetAdaptersInfo_t GetAdaptersInfo = (GetAdaptersInfo_t)GetProcAddress(iphlp, "GetAdaptersInfo");
		if (!GetAdaptersInfo)
		{
			FreeLibrary(iphlp);
			ec = asio::error::operation_not_supported;
			return address_v4::any();
		}

		PIP_ADAPTER_INFO adapter_info = 0;
		ULONG out_buf_size = 0;
		if (GetAdaptersInfo(adapter_info, &out_buf_size) != ERROR_BUFFER_OVERFLOW)
		{
			FreeLibrary(iphlp);
			ec = asio::error::operation_not_supported;
			return address_v4::any();
		}

		adapter_info = (IP_ADAPTER_INFO*)malloc(out_buf_size);
		if (!adapter_info)
		{
			FreeLibrary(iphlp);
			ec = asio::error::no_memory;
			return address_v4::any();
		}

		address ret;
		if (GetAdaptersInfo(adapter_info, &out_buf_size) == NO_ERROR)
		{
			for (PIP_ADAPTER_INFO adapter = adapter_info;
				adapter != 0; adapter = adapter->Next)
			{
				address iface = address::from_string(adapter->IpAddressList.IpAddress.String, ec);
				if (ec)
				{
					ec = asio::error_code();
					continue;
				}
				if (is_loopback(iface) || is_any(iface)) continue;
				if (interface == address() || interface == iface)
				{
					ret = address::from_string(adapter->GatewayList.IpAddress.String, ec);
					break;
				}
			}
		}
   
		// Free memory
		free(adapter_info);
		FreeLibrary(iphlp);

		return ret;

#elif defined TORRENT_LINUX

		enum { BUFSIZE = 8192 };

		int sock = socket(PF_ROUTE, SOCK_DGRAM, NETLINK_ROUTE);
		if (sock < 0)
		{
			ec = asio::error_code(errno, asio::error::system_category);
			return address_v4::any();
		}

		int seq = 0;

		char msg[BUFSIZE];
		memset(msg, 0, BUFSIZE);
		nlmsghdr* nl_msg = (nlmsghdr*)msg;

		nl_msg->nlmsg_len = NLMSG_LENGTH(sizeof(rtmsg));
		nl_msg->nlmsg_type = RTM_GETROUTE;
		nl_msg->nlmsg_flags = NLM_F_DUMP | NLM_F_REQUEST;
		nl_msg->nlmsg_seq = seq++;
		nl_msg->nlmsg_pid = getpid();

		if (send(sock, nl_msg, nl_msg->nlmsg_len, 0) < 0)
		{
			ec = asio::error_code(errno, asio::error::system_category);
			close(sock);
			return address_v4::any();
		}

		int len = read_nl_sock(sock, msg, BUFSIZE, seq, getpid());
		if (len < 0)
		{
			ec = asio::error_code(errno, asio::error::system_category);
			close(sock);
			return address_v4::any();
		}

		route_info rt_info;
		for (; NLMSG_OK(nl_msg, len); nl_msg = NLMSG_NEXT(nl_msg, len))
		{
			memset(&rt_info, 0, sizeof(route_info));
			parse_route(nl_msg, &rt_info);

			if (rt_info.dst_addr == 0)
			{
				in_addr addr;
				addr.s_addr = rt_info.gateway;
				close(sock);
				return inaddr_to_address(&addr);
			}
		}
		close(sock);
		return address_v4::any();

#else
		if (!interface.is_v4())
		{
			ec = asio::error::operation_not_supported;
			return address_v4::any();
		}
		return address_v4((interface.to_v4().to_ulong() & 0xffffff00) | 1);
#endif
	}

}


