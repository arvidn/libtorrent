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
#include <stdlib.h> // for wcstombscstombs
#include "libtorrent/enum_net.hpp"
#include "libtorrent/broadcast_socket.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/assert.hpp"
#if BOOST_VERSION < 103500
#include <asio/ip/host_name.hpp>
#else
#include <boost/asio/ip/host_name.hpp>
#endif

#if TORREN_USE_IFCONF
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <string.h>
#endif

#if TORRENT_USE_SYSCTL
#include <sys/sysctl.h>
#include <net/route.h>
#include <boost/scoped_array.hpp>
#endif

#if TORRENT_USE_GETIPFORWARDTABLE || TORRENT_USE_GETADAPTERSADDRESSES
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <iphlpapi.h>
#endif

#if TORRENT_USE_NETLINK
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <asm/types.h>
#include <netinet/ether.h>
#include <netinet/in.h>
#include <net/if.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#endif

#if TORRENT_USE_IFADDRS
#include <ifaddrs.h>
#endif

namespace libtorrent { namespace
{

	address inaddr_to_address(in_addr const* ina, int len = 4)
	{
		typedef asio::ip::address_v4::bytes_type bytes_t;
		bytes_t b;
		std::memset(&b[0], 0, b.size());
		if (len > 0) std::memcpy(&b[0], ina, (std::min)(len, int(b.size())));
		return address_v4(b);
	}

#if TORRENT_USE_IPV6
	address inaddr6_to_address(in6_addr const* ina6, int len = 16)
	{
		typedef asio::ip::address_v6::bytes_type bytes_t;
		bytes_t b;
		std::memset(&b[0], 0, b.size());
		if (len > 0) std::memcpy(&b[0], ina6, (std::min)(len, int(b.size())));
		return address_v6(b);
	}
#endif

	int sockaddr_len(sockaddr const* sin)
	{
#if TORRENT_HAS_SALEN
		return sin->sa_len;
#else
		return sin->sa_family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
#endif
	}

	address sockaddr_to_address(sockaddr const* sin, int assume_family = -1)
	{
		if (sin->sa_family == AF_INET || assume_family == AF_INET)
			return inaddr_to_address(&((sockaddr_in const*)sin)->sin_addr
				, sockaddr_len(sin) - offsetof(sockaddr, sa_data));
#if TORRENT_USE_IPV6
		else if (sin->sa_family == AF_INET6 || assume_family == AF_INET6)
			return inaddr6_to_address(&((sockaddr_in6 const*)sin)->sin6_addr
				, sockaddr_len(sin) - offsetof(sockaddr, sa_data));
#endif
		return address();
	}

#if TORRENT_USE_NETLINK

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

	bool parse_route(int s, nlmsghdr* nl_hdr, ip_route* rt_info)
	{
		rtmsg* rt_msg = (rtmsg*)NLMSG_DATA(nl_hdr);

		if((rt_msg->rtm_family != AF_INET && rt_msg->rtm_family != AF_INET6) || (rt_msg->rtm_table != RT_TABLE_MAIN
			&& rt_msg->rtm_table != RT_TABLE_LOCAL))
			return false;

		int if_index = 0;
		int rt_len = RTM_PAYLOAD(nl_hdr);
		for (rtattr* rt_attr = (rtattr*)RTM_RTA(rt_msg);
			RTA_OK(rt_attr,rt_len); rt_attr = RTA_NEXT(rt_attr,rt_len))
		{
			switch(rt_attr->rta_type)
			{
				case RTA_OIF:
					if_index = *(int*)RTA_DATA(rt_attr);
					break;
				case RTA_GATEWAY:
#if TORRENT_USE_IPV6
					if (rt_msg->rtm_family == AF_INET6)
					{
						rt_info->gateway = inaddr6_to_address((in6_addr*)RTA_DATA(rt_attr));
					}
					else
#endif
					{
						rt_info->gateway = inaddr_to_address((in_addr*)RTA_DATA(rt_attr));
					}
					break;
				case RTA_DST:
#if TORRENT_USE_IPV6
					if (rt_msg->rtm_family == AF_INET6)
					{
						rt_info->destination = inaddr6_to_address((in6_addr*)RTA_DATA(rt_attr));
					}
					else
#endif
					{
						rt_info->destination = inaddr_to_address((in_addr*)RTA_DATA(rt_attr));
					}
					break;
			}
		}

		if_indextoname(if_index, rt_info->name);
		ifreq req;
		memset(&req, 0, sizeof(req));
		if_indextoname(if_index, req.ifr_name);
		ioctl(s, SIOCGIFMTU, &req);
		rt_info->mtu = req.ifr_mtu;
//		obviously this doesn't work correctly. How do you get the netmask for a route?
//		if (ioctl(s, SIOCGIFNETMASK, &req) == 0) {
//			rt_info->netmask = sockaddr_to_address(&req.ifr_addr, req.ifr_addr.sa_family);
//		}
		return true;
	}
#endif

#if TORRENT_USE_SYSCTL

	bool parse_route(int s, rt_msghdr* rtm, ip_route* rt_info)
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
		rt_info->destination = sockaddr_to_address(rti_info[RTAX_DST]);
		rt_info->netmask = sockaddr_to_address(rti_info[RTAX_NETMASK]
			, rt_info->destination.is_v4() ? AF_INET : AF_INET6);
		if_indextoname(rtm->rtm_index, rt_info->name);

		ifreq req;
		memset(&req, 0, sizeof(req));
		if_indextoname(rtm->rtm_index, req.ifr_name);
		if (ioctl(s, SIOCGIFMTU, &req) < 0) return false;
		rt_info->mtu = req.ifr_mtu;

		return true;
	}
#endif

#if TORRENT_USE_IFADDRS
	bool iface_from_ifaddrs(ifaddrs *ifa, ip_interface &rv, error_code& ec)
	{
		int family = ifa->ifa_addr->sa_family;

		if (family != AF_INET
#if TORRENT_USE_IPV6
			&& family != AF_INET6
#endif
		)
		{
			return false;
		}

		strncpy(rv.name, ifa->ifa_name, sizeof(rv.name));
		rv.name[sizeof(rv.name)-1] = 0;

		// determine address
		rv.interface_address = sockaddr_to_address(ifa->ifa_addr);
		// determine netmask
		if (ifa->ifa_netmask != NULL)
		{
			rv.netmask = sockaddr_to_address(ifa->ifa_netmask);
		}
		return true;
	}
#endif

}} // <anonymous>

namespace libtorrent
{
	
	// return (a1 & mask) == (a2 & mask)
	bool match_addr_mask(address const& a1, address const& a2, address const& mask)
	{
		// all 3 addresses needs to belong to the same family
		if (a1.is_v4() != a2.is_v4()) return false;
		if (a1.is_v4() != mask.is_v4()) return false;

#if TORRENT_USE_IPV6
		if (a1.is_v6())
		{
			address_v6::bytes_type b1;
			address_v6::bytes_type b2;
			address_v6::bytes_type m;
			b1 = a1.to_v6().to_bytes();
			b2 = a2.to_v6().to_bytes();
			m = mask.to_v6().to_bytes();
			for (int i = 0; i < int(b1.size()); ++i)
				b1[i] &= m[i];
			return memcmp(&b1[0], &b2[0], b1.size()) == 0;
		}
#endif
		return (a1.to_v4().to_ulong() & mask.to_v4().to_ulong())
			== (a2.to_v4().to_ulong() & mask.to_v4().to_ulong());
	}

	bool in_local_network(io_service& ios, address const& addr, error_code& ec)
	{
		std::vector<ip_interface> net = enum_net_interfaces(ios, ec);
		if (ec) return false;
		for (std::vector<ip_interface>::iterator i = net.begin()
			, end(net.end()); i != end; ++i)
		{
			if (match_addr_mask(addr, i->interface_address, i->netmask)) return true;
		}
		return false;
	}

#if TORRENT_USE_GETIPFORWARDTABLE
	address build_netmask(int bits, int family)
	{
		if (family == AF_INET)
		{
			typedef asio::ip::address_v4::bytes_type bytes_t;
			bytes_t b;
			std::memset(&b[0], 0xff, b.size());
			for (int i = sizeof(bytes_t)/8-1; i > 0; --i)
			{
				if (bits < 8)
				{
					b[i] <<= bits;
					break;
				}
				b[i] = 0;
				bits -= 8;
			}
			return address_v4(b);
		}
#if TORRENT_USE_IPV6
		else if (family == AF_INET6)
		{
			typedef asio::ip::address_v6::bytes_type bytes_t;
			bytes_t b;
			std::memset(&b[0], 0xff, b.size());
			for (int i = sizeof(bytes_t)/8-1; i > 0; --i)
			{
				if (bits < 8)
				{
					b[i] <<= bits;
					break;
				}
				b[i] = 0;
				bits -= 8;
			}
			return address_v6(b);
		}
#endif
		else
		{
			return address();
		}
	}
#endif
	
	std::vector<ip_interface> enum_net_interfaces(io_service& ios, error_code& ec)
	{
		std::vector<ip_interface> ret;
#if TORRENT_USE_IFADDRS
		int s = socket(AF_INET, SOCK_DGRAM, 0);
		if (s < 0)
		{
			ec = error_code(errno, asio::error::system_category);
			return ret;
		}

		ifaddrs *ifaddr;
		if (getifaddrs(&ifaddr) == -1)
		{
			ec = error_code(errno, asio::error::system_category);
			close(s);
			return ret;
		}

		for (ifaddrs* ifa = ifaddr; ifa; ifa = ifa->ifa_next)
		{
			if (ifa->ifa_addr == 0) continue;
			if ((ifa->ifa_flags & IFF_UP) == 0) continue;

			int family = ifa->ifa_addr->sa_family;
			if (family == AF_INET
#if TORRENT_USE_IPV6
				|| family == AF_INET6
#endif
				)
			{
				ip_interface iface;
				if (iface_from_ifaddrs(ifa, iface, ec))
				{
					ifreq req;
					memset(&req, 0, sizeof(req));
					// -1 to leave a null terminator
					strncpy(req.ifr_name, iface.name, IF_NAMESIZE - 1);
					if (ioctl(s, SIOCGIFMTU, &req) < 0)
					{
						continue;
					}
					iface.mtu = req.ifr_mtu;
					ret.push_back(iface);
				}
			}
		}
		close(s);
		freeifaddrs(ifaddr);
// MacOS X, BSD and solaris
#elif TORRENT_USE_IFCONF
		int s = socket(AF_INET, SOCK_DGRAM, 0);
		if (s < 0)
		{
			ec = error_code(errno, asio::error::system_category);
			return ret;
		}
		ifconf ifc;
		// make sure the buffer is aligned to hold ifreq structs
		ifreq buf[40];
		ifc.ifc_len = sizeof(buf);
		ifc.ifc_buf = (char*)buf;
		if (ioctl(s, SIOCGIFCONF, &ifc) < 0)
		{
			ec = error_code(errno, asio::error::system_category);
			close(s);
			return ret;
		}

		char *ifr = (char*)ifc.ifc_req;
		int remaining = ifc.ifc_len;

		while (remaining > 0)
		{
			ifreq const& item = *reinterpret_cast<ifreq*>(ifr);

#ifdef _SIZEOF_ADDR_IFREQ
			int current_size = _SIZEOF_ADDR_IFREQ(item);
#elif defined TORRENT_BSD
			int current_size = item.ifr_addr.sa_len + IFNAMSIZ;
#else
			int current_size = sizeof(ifreq);
#endif

			if (remaining < current_size) break;

			if (item.ifr_addr.sa_family == AF_INET
#if TORRENT_USE_IPV6
				|| item.ifr_addr.sa_family == AF_INET6
#endif
				)
			{
				ip_interface iface;
				iface.interface_address = sockaddr_to_address(&item.ifr_addr);
				strcpy(iface.name, item.ifr_name);

				ifreq req;
				memset(&req, 0, sizeof(req));
				// -1 to leave a null terminator
				strncpy(req.ifr_name, item.ifr_name, IF_NAMESIZE - 1);
				if (ioctl(s, SIOCGIFMTU, &req) < 0)
				{
					ec = error_code(errno, asio::error::system_category);
					close(s);
					return ret;
				}
				iface.mtu = req.ifr_mtu;

				memset(&req, 0, sizeof(req));
				strncpy(req.ifr_name, item.ifr_name, IF_NAMESIZE - 1);
				if (ioctl(s, SIOCGIFNETMASK, &req) < 0)
				{
#if TORRENT_USE_IPV6
					if (iface.interface_address.is_v6())
					{
						// this is expected to fail (at least on MacOS X)
						iface.netmask = address_v6::any();
					}
					else
#endif
					{
						ec = error_code(errno, asio::error::system_category);
						close(s);
						return ret;
					}
				}
				else
				{
					iface.netmask = sockaddr_to_address(&req.ifr_addr, item.ifr_addr.sa_family);
				}
				ret.push_back(iface);
			}

			ifr += current_size;
			remaining -= current_size;
		}
		close(s);

#elif TORRENT_USE_GETADAPTERSADDRESSES

#if _WIN32_WINNT >= 0x0501
		// Load Iphlpapi library
		HMODULE iphlp = LoadLibraryA("Iphlpapi.dll");
		if (iphlp)
		{
			// Get GetAdaptersAddresses() pointer
			typedef ULONG (WINAPI *GetAdaptersAddresses_t)(ULONG,ULONG,PVOID,PIP_ADAPTER_ADDRESSES,PULONG);
			GetAdaptersAddresses_t GetAdaptersAddresses = (GetAdaptersAddresses_t)GetProcAddress(
				iphlp, "GetAdaptersAddresses");

			if (GetAdaptersAddresses)
			{
				PIP_ADAPTER_ADDRESSES adapter_addresses = 0;
				ULONG out_buf_size = 0;
				if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER
					| GAA_FLAG_SKIP_ANYCAST, NULL, adapter_addresses, &out_buf_size) != ERROR_BUFFER_OVERFLOW)
				{
					FreeLibrary(iphlp);
					ec = asio::error::operation_not_supported;
					return std::vector<ip_interface>();
				}

				adapter_addresses = (IP_ADAPTER_ADDRESSES*)malloc(out_buf_size);
				if (!adapter_addresses)
				{
					FreeLibrary(iphlp);
					ec = asio::error::no_memory;
					return std::vector<ip_interface>();
				}

				if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER
					| GAA_FLAG_SKIP_ANYCAST, NULL, adapter_addresses, &out_buf_size) == NO_ERROR)
				{
					for (PIP_ADAPTER_ADDRESSES adapter = adapter_addresses;
						adapter != 0; adapter = adapter->Next)
					{
						ip_interface r;
						strncpy(r.name, adapter->AdapterName, sizeof(r.name));
						r.name[sizeof(r.name)-1] = 0;
						r.mtu = adapter->Mtu;
						IP_ADAPTER_UNICAST_ADDRESS* unicast = adapter->FirstUnicastAddress;
						while (unicast)
						{
							r.interface_address = sockaddr_to_address(unicast->Address.lpSockaddr);

							ret.push_back(r);

							unicast = unicast->Next;
						}
					}
				}

				// Free memory
				free(adapter_addresses);
				FreeLibrary(iphlp);
				return ret;
			}
			FreeLibrary(iphlp);
		}
#endif

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
			if (iface.interface_address == address_v4::any()) continue;
			iface.netmask = sockaddr_to_address(&buffer[i].iiNetmask.Address
				, iface.interface_address.is_v4() ? AF_INET : AF_INET6);
			iface.name[0] = 0;
			iface.mtu = 1500; // how to get the MTU?
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
			iface.mtu = 1500;
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
	
#if TORRENT_USE_SYSCTL
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

	int s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0)
	{
		ec = error_code(errno, asio::error::system_category);
		return std::vector<ip_route>();
	}
	rt_msghdr* rtm;
	for (char* next = buf.get(); next < end; next += rtm->rtm_msglen)
	{
		rtm = (rt_msghdr*)next;
		if (rtm->rtm_version != RTM_VERSION)
			continue;
		
		ip_route r;
		if (parse_route(s, rtm, &r)) ret.push_back(r);
	}
	close(s);
	
#elif TORRENT_USE_GETIPFORWARDTABLE
/*
	move this to enum_net_interfaces
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
*/

		// Load Iphlpapi library
		HMODULE iphlp = LoadLibraryA("Iphlpapi.dll");
		if (!iphlp)
		{
			ec = asio::error::operation_not_supported;
			return std::vector<ip_route>();
		}

		typedef DWORD (WINAPI *GetIfEntry_t)(PMIB_IFROW pIfRow);
		GetIfEntry_t GetIfEntry = (GetIfEntry_t)GetProcAddress(iphlp, "GetIfEntry");
		if (!GetIfEntry)
		{
			ec = asio::error::operation_not_supported;
			return std::vector<ip_route>();
		}

#if _WIN32_WINNT >= 0x0600
		typedef DWORD (WINAPI *GetIpForwardTable2_t)(
			ADDRESS_FAMILY, PMIB_IPFORWARD_TABLE2*);
		typedef void (WINAPI *FreeMibTable_t)(PVOID Memory);
			
		GetIpForwardTable2_t GetIpForwardTable2 = (GetIpForwardTable2_t)GetProcAddress(
			iphlp, "GetIpForwardTable2");
		FreeMibTable_t FreeMibTable = (FreeMibTable_t)GetProcAddress(
			iphlp, "FreeMibTable");
		if (GetIpForwardTable2 && FreeMibTable)
		{
			MIB_IPFORWARD_TABLE2* routes = NULL;
			int res = GetIpForwardTable2(AF_UNSPEC, &routes);
			if (res == NO_ERROR)
			{
				for (int i = 0; i < routes->NumEntries; ++i)
				{
					ip_route r;
					r.gateway = sockaddr_to_address((const sockaddr*)&routes->Table[i].NextHop);
					r.destination = sockaddr_to_address(
						(const sockaddr*)&routes->Table[i].DestinationPrefix.Prefix);
					r.netmask = build_netmask(routes->Table[i].SitePrefixLength
						, routes->Table[i].DestinationPrefix.Prefix.si_family);
					MIB_IFROW ifentry;
					ifentry.dwIndex = routes->Table[i].InterfaceIndex;
					if (GetIfEntry(&ifentry) == NO_ERROR)
					{
						wcstombs(r.name, ifentry.wszName, sizeof(r.name));
						r.mtu = ifentry.dwMtu;
						ret.push_back(r);
					}
				}
			}
			if (routes) FreeMibTable(routes);
			FreeLibrary(iphlp);
			return ret;
		}
#endif

		// Get GetIpForwardTable() pointer
		typedef DWORD (WINAPI *GetIpForwardTable_t)(PMIB_IPFORWARDTABLE pIpForwardTable,PULONG pdwSize,BOOL bOrder);
			
		GetIpForwardTable_t GetIpForwardTable = (GetIpForwardTable_t)GetProcAddress(
			iphlp, "GetIpForwardTable");
		if (!GetIpForwardTable)
		{
			FreeLibrary(iphlp);
			ec = asio::error::operation_not_supported;
			return std::vector<ip_route>();
		}

		MIB_IPFORWARDTABLE* routes = NULL;
		ULONG out_buf_size = 0;
		if (GetIpForwardTable(routes, &out_buf_size, FALSE) != ERROR_INSUFFICIENT_BUFFER)
		{
			FreeLibrary(iphlp);
			ec = asio::error::operation_not_supported;
			return std::vector<ip_route>();
		}

		routes = (MIB_IPFORWARDTABLE*)malloc(out_buf_size);
		if (!routes)
		{
			FreeLibrary(iphlp);
			ec = asio::error::no_memory;
			return std::vector<ip_route>();
		}

		if (GetIpForwardTable(routes, &out_buf_size, FALSE) == NO_ERROR)
		{
			for (int i = 0; i < routes->dwNumEntries; ++i)
			{
				ip_route r;
				r.destination = inaddr_to_address((in_addr const*)&routes->table[i].dwForwardDest);
				r.netmask = inaddr_to_address((in_addr const*)&routes->table[i].dwForwardMask);
				r.gateway = inaddr_to_address((in_addr const*)&routes->table[i].dwForwardNextHop);
				MIB_IFROW ifentry;
				ifentry.dwIndex = routes->table[i].dwForwardIfIndex;
				if (GetIfEntry(&ifentry) == NO_ERROR)
				{
					wcstombs(r.name, ifentry.wszName, sizeof(r.name));
					r.name[sizeof(r.name)-1] = 0;
					r.mtu = ifentry.dwMtu;
					ret.push_back(r);
				}
			}
		}

		// Free memory
		free(routes);
		FreeLibrary(iphlp);
#elif TORRENT_USE_NETLINK
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

		int s = socket(AF_INET, SOCK_DGRAM, 0);
		if (s < 0)
		{
			ec = error_code(errno, asio::error::system_category);
			return std::vector<ip_route>();
		}
		for (; NLMSG_OK(nl_msg, len); nl_msg = NLMSG_NEXT(nl_msg, len))
		{
			ip_route r;
			if (parse_route(s, nl_msg, &r)) ret.push_back(r);
		}
		close(s);
		close(sock);

#endif
		return ret;
	}

}


