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
#include <boost/bind.hpp>
#include <vector>
#include "libtorrent/enum_net.hpp"
#include "libtorrent/broadcast_socket.hpp"
#if BOOST_VERSION < 103500
#include <asio/ip/host_name.hpp>
#else
#include <boost/asio/ip/host_name.hpp>
#endif

#if defined TORRENT_BSD || defined TORRENT_SOLARIS
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <net/route.h>
#include <string.h>
#include <boost/scoped_array.hpp>
#endif

#if defined TORRENT_BSD
#include <sys/sysctl.h>
#endif

#if defined TORRENT_WINDOWS || defined TORRENT_MINGW
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <iphlpapi.h>
#endif

#if defined TORRENT_LINUX
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
#endif

namespace libtorrent { namespace
{

	address inaddr_to_address(in_addr const* ina)
	{
		typedef asio::ip::address_v4::bytes_type bytes_t;
		bytes_t b;
		std::memcpy(&b[0], ina, b.size());
		return address_v4(b);
	}

#if TORRENT_USE_IPV6
	address inaddr6_to_address(in6_addr const* ina6)
	{
		typedef asio::ip::address_v6::bytes_type bytes_t;
		bytes_t b;
		std::memcpy(&b[0], ina6, b.size());
		return address_v6(b);
	}
#endif

	address sockaddr_to_address(sockaddr const* sin)
	{
		if (sin->sa_family == AF_INET)
			return inaddr_to_address(&((sockaddr_in const*)sin)->sin_addr);
#if TORRENT_USE_IPV6
		else if (sin->sa_family == AF_INET6)
			return inaddr6_to_address(&((sockaddr_in6 const*)sin)->sin6_addr);
#endif
		return address();
	}

#if defined TORRENT_LINUX

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

	bool parse_route(nlmsghdr* nl_hdr, ip_route* rt_info)
	{
		rtmsg* rt_msg = (rtmsg*)NLMSG_DATA(nl_hdr);

		if((rt_msg->rtm_family != AF_INET) || (rt_msg->rtm_table != RT_TABLE_MAIN))
			return false;

		int rt_len = RTM_PAYLOAD(nl_hdr);
		for (rtattr* rt_attr = (rtattr*)RTM_RTA(rt_msg);
			RTA_OK(rt_attr,rt_len); rt_attr = RTA_NEXT(rt_attr,rt_len))
		{
			switch(rt_attr->rta_type)
			{
				case RTA_OIF:
					if_indextoname(*(int*)RTA_DATA(rt_attr), rt_info->name);
					break;
				case RTA_GATEWAY:
					rt_info->gateway = address_v4(ntohl(*(u_int*)RTA_DATA(rt_attr)));
					break;
				case RTA_DST:
					rt_info->destination = address_v4(ntohl(*(u_int*)RTA_DATA(rt_attr)));
					break;
			}
		}
		return true;
	}
#endif

#if defined TORRENT_BSD

	bool parse_route(rt_msghdr* rtm, ip_route* rt_info)
	{
		sockaddr* rti_info[RTAX_MAX];
		sockaddr* sa = (sockaddr*)(rtm + 1);
		for (int i = 0; i < RTAX_MAX; ++i)
		{
			if ((rtm->rtm_addrs & (1 << i)) == 0)
			{
				rti_info[i] = 0;
				continue;
			}
			rti_info[i] = sa;

#define ROUNDUP(a) \
	((a) > 0 ? (1 + (((a) - 1) | (sizeof(long) - 1))) : sizeof(long))

			sa = (sockaddr*)((char*)(sa) + ROUNDUP(sa->sa_len));

#undef ROUNDUP
		}

		sa = rti_info[RTAX_GATEWAY];
		if (sa == 0
			|| rti_info[RTAX_DST] == 0
			|| rti_info[RTAX_NETMASK] == 0
			|| (sa->sa_family != AF_INET
#if TORRENT_USE_IPV6
				&& sa->sa_family != AF_INET6
#endif
				))
			return false;

		rt_info->gateway = sockaddr_to_address(rti_info[RTAX_GATEWAY]);
		rt_info->netmask = sockaddr_to_address(rti_info[RTAX_NETMASK]);
		rt_info->destination = sockaddr_to_address(rti_info[RTAX_DST]);
		if_indextoname(rtm->rtm_index, rt_info->name);
		return true;
	}
#endif


#ifdef TORRENT_BSD
	bool verify_sockaddr(sockaddr_in* sin)
	{
		return (sin->sin_len == sizeof(sockaddr_in)
			&& sin->sin_family == AF_INET)
#if TORRENT_USE_IPV6
			|| (sin->sin_len == sizeof(sockaddr_in6)
				&& sin->sin_family == AF_INET6)
#endif
			;
	}
#endif

}} // <anonymous>

namespace libtorrent
{
	
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

	bool in_local_network(io_service& ios, address const& addr, error_code& ec)
	{
		std::vector<ip_interface> net = enum_net_interfaces(ios, ec);
		if (ec) return false;
		for (std::vector<ip_interface>::iterator i = net.begin()
			, end(net.end()); i != end; ++i)
		{
			if (in_subnet(addr, *i)) return true;
		}
		return false;
	}
	
	std::vector<ip_interface> enum_net_interfaces(io_service& ios, error_code& ec)
	{
		std::vector<ip_interface> ret;
// covers linux, MacOS X and BSD distributions
#if defined TORRENT_LINUX || defined TORRENT_BSD || defined TORRENT_SOLARIS
		int s = socket(AF_INET, SOCK_DGRAM, 0);
		if (s < 0)
		{
			ec = error_code(errno, asio::error::system_category);
			return ret;
		}
		ifconf ifc;
		char buf[1024];
		ifc.ifc_len = sizeof(buf);
		ifc.ifc_buf = buf;
		if (ioctl(s, SIOCGIFCONF, &ifc) < 0)
		{
			ec = error_code(errno, asio::error::system_category);
			close(s);
			return ret;
		}

		char *ifr = (char*)ifc.ifc_req;
		int remaining = ifc.ifc_len;

		while (remaining)
		{
			ifreq const& item = *reinterpret_cast<ifreq*>(ifr);

			if (item.ifr_addr.sa_family == AF_INET
#if TORRENT_USE_IPV6
				|| item.ifr_addr.sa_family == AF_INET6
#endif
				)
			{
				ip_interface iface;
				iface.interface_address = sockaddr_to_address(&item.ifr_addr);
				strcpy(iface.name, item.ifr_name);

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
						ec = error_code(errno, asio::error::system_category);
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
#elif defined TORRENT_LINUX || defined TORRENT_SOLARIS
			int current_size = sizeof(ifreq);
#endif
			ifr += current_size;
			remaining -= current_size;
		}
		close(s);

#elif defined TORRENT_WINDOWS || defined TORRENT_MINGW

		SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
		if (s == SOCKET_ERROR)
		{
			ec = error_code(WSAGetLastError(), asio::error::system_category);
			return ret;
		}

		INTERFACE_INFO buffer[30];
		DWORD size;
	
		if (WSAIoctl(s, SIO_GET_INTERFACE_LIST, 0, 0, buffer,
			sizeof(buffer), &size, 0, 0) != 0)
		{
			ec = error_code(WSAGetLastError(), asio::error::system_category);
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
			iface.name[0] = 0;
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

	address get_default_gateway(io_service& ios, error_code& ec)
	{
		std::vector<ip_route> ret = enum_routes(ios, ec);
#if defined TORRENT_WINDOWS || defined TORRENT_MINGW
		std::vector<ip_route>::iterator i = std::find_if(ret.begin(), ret.end()
			, boost::bind(&is_loopback, boost::bind(&ip_route::destination, _1)));
#else
		std::vector<ip_route>::iterator i = std::find_if(ret.begin(), ret.end()
			, boost::bind(&ip_route::destination, _1) == address());
#endif
		if (i == ret.end()) return address();
		return i->gateway;
	}

	std::vector<ip_route> enum_routes(io_service& ios, error_code& ec)
	{
		std::vector<ip_route> ret;
	
#if defined TORRENT_BSD
/*
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
		m.m_rtm.rtm_addrs = RTA_DST | RTA_GATEWAY | RTA_NETMASK;
		m.m_rtm.rtm_seq = 0;
		m.m_rtm.rtm_msglen = len;

		int s = socket(PF_ROUTE, SOCK_RAW, AF_UNSPEC);
		if (s == -1)
		{
			ec = error_code(errno, asio::error::system_category);
			return std::vector<ip_route>();
		}

		int n = write(s, &m, len);
		if (n == -1)
		{
			ec = error_code(errno, asio::error::system_category);
			close(s);
			return std::vector<ip_route>();
		}
		else if (n != len)
		{
			ec = asio::error::operation_not_supported;
			close(s);
			return std::vector<ip_route>();
		}
		bzero(&m, len);

		n = read(s, &m, len);
		if (n == -1)
		{
			ec = error_code(errno, asio::error::system_category);
			close(s);
			return std::vector<ip_route>();
		}

		for (rt_msghdr* ptr = &m.m_rtm; (char*)ptr < ((char*)&m.m_rtm) + n; ptr = (rt_msghdr*)(((char*)ptr) + ptr->rtm_msglen))
		{
			std::cout << " rtm_msglen: " << ptr->rtm_msglen << std::endl;
			std::cout << " rtm_type: " << ptr->rtm_type << std::endl;
			if (ptr->rtm_errno)
			{
				ec = error_code(ptr->rtm_errno, asio::error::system_category);
				return std::vector<ip_route>();
			}
			if (m.m_rtm.rtm_flags & RTF_UP == 0
				|| m.m_rtm.rtm_flags & RTF_GATEWAY == 0)
			{
				ec = asio::error::operation_not_supported;
				return address_v4::any();
			}
			if (ptr->rtm_addrs & RTA_DST == 0
				|| ptr->rtm_addrs & RTA_GATEWAY == 0
				|| ptr->rtm_addrs & RTA_NETMASK == 0)
			{
				ec = asio::error::operation_not_supported;
				return std::vector<ip_route>();
			}
			if (ptr->rtm_msglen > len - ((char*)ptr - ((char*)&m.m_rtm)))
			{
				ec = asio::error::operation_not_supported;
				return std::vector<ip_route>();
			}
			int min_len = sizeof(rt_msghdr) + 2 * sizeof(sockaddr_in);
			if (m.m_rtm.rtm_msglen < min_len)
			{
				ec = asio::error::operation_not_supported;
				return std::vector<ip_route>();
			}

			ip_route r;
			// destination
			char* p = m.buf;
			sockaddr_in* sin = (sockaddr_in*)p;
			r.destination = sockaddr_to_address((sockaddr*)p);

			// gateway
			p += sin->sin_len;
			sin = (sockaddr_in*)p;
			r.gateway = sockaddr_to_address((sockaddr*)p);

			// netmask
			p += sin->sin_len;
			sin = (sockaddr_in*)p;
			r.netmask = sockaddr_to_address((sockaddr*)p);
			ret.push_back(r);
		}
		close(s);
*/
	int mib[6] = { CTL_NET, PF_ROUTE, 0, AF_UNSPEC, NET_RT_DUMP, 0};

	size_t needed = 0;
	if (sysctl(mib, 6, 0, &needed, 0, 0) < 0)
	{
		ec = error_code(errno, asio::error::system_category);
		return std::vector<ip_route>();
	}

	if (needed <= 0)
	{
		return std::vector<ip_route>();
	}

	boost::scoped_array<char> buf(new (std::nothrow) char[needed]);
	if (buf.get() == 0)
	{
		ec = asio::error::no_memory;
		return std::vector<ip_route>();
	}

	if (sysctl(mib, 6, buf.get(), &needed, 0, 0) < 0)
	{
		ec = error_code(errno, asio::error::system_category);
		return std::vector<ip_route>();
	}

	char* end = buf.get() + needed;

	rt_msghdr* rtm;
	for (char* next = buf.get(); next < end; next += rtm->rtm_msglen)
	{
		rtm = (rt_msghdr*)next;
		if (rtm->rtm_version != RTM_VERSION)
			continue;
		
		ip_route r;
		if (parse_route(rtm, &r)) ret.push_back(r);
	}
	
#elif defined TORRENT_WINDOWS || defined TORRENT_MINGW

		// Load Iphlpapi library
		HMODULE iphlp = LoadLibraryA("Iphlpapi.dll");
		if (!iphlp)
		{
			ec = asio::error::operation_not_supported;
			return std::vector<ip_route>();
		}

		// Get GetAdaptersInfo() pointer
		typedef DWORD (WINAPI *GetAdaptersInfo_t)(PIP_ADAPTER_INFO, PULONG);
		GetAdaptersInfo_t GetAdaptersInfo = (GetAdaptersInfo_t)GetProcAddress(iphlp, "GetAdaptersInfo");
		if (!GetAdaptersInfo)
		{
			FreeLibrary(iphlp);
			ec = asio::error::operation_not_supported;
			return std::vector<ip_route>();
		}

		PIP_ADAPTER_INFO adapter_info = 0;
		ULONG out_buf_size = 0;
		if (GetAdaptersInfo(adapter_info, &out_buf_size) != ERROR_BUFFER_OVERFLOW)
		{
			FreeLibrary(iphlp);
			ec = asio::error::operation_not_supported;
			return std::vector<ip_route>();
		}

		adapter_info = (IP_ADAPTER_INFO*)malloc(out_buf_size);
		if (!adapter_info)
		{
			FreeLibrary(iphlp);
			ec = asio::error::no_memory;
			return std::vector<ip_route>();
		}

		if (GetAdaptersInfo(adapter_info, &out_buf_size) == NO_ERROR)
		{
			for (PIP_ADAPTER_INFO adapter = adapter_info;
				adapter != 0; adapter = adapter->Next)
			{

				ip_route r;
				r.destination = address::from_string(adapter->IpAddressList.IpAddress.String, ec);
				r.gateway = address::from_string(adapter->GatewayList.IpAddress.String, ec);
				r.netmask = address::from_string(adapter->IpAddressList.IpMask.String, ec);
				strncpy(r.name, adapter->AdapterName, sizeof(r.name));

				if (ec)
				{
					ec = error_code();
					continue;
				}
				ret.push_back(r);
			}
		}
   
		// Free memory
		free(adapter_info);
		FreeLibrary(iphlp);

#elif defined TORRENT_LINUX

		enum { BUFSIZE = 8192 };

		int sock = socket(PF_ROUTE, SOCK_DGRAM, NETLINK_ROUTE);
		if (sock < 0)
		{
			ec = error_code(errno, asio::error::system_category);
			return std::vector<ip_route>();
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
			ec = error_code(errno, asio::error::system_category);
			close(sock);
			return std::vector<ip_route>();
		}

		int len = read_nl_sock(sock, msg, BUFSIZE, seq, getpid());
		if (len < 0)
		{
			ec = error_code(errno, asio::error::system_category);
			close(sock);
			return std::vector<ip_route>();
		}

		for (; NLMSG_OK(nl_msg, len); nl_msg = NLMSG_NEXT(nl_msg, len))
		{
			ip_route r;
			if (parse_route(nl_msg, &r)) ret.push_back(r);
		}
		close(sock);

#endif
		return ret;
	}

}


