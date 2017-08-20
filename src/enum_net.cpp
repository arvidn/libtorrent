/*

Copyright (c) 2007-2016, Arvid Norberg
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
#include "libtorrent/enum_net.hpp"
#include "libtorrent/broadcast_socket.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/socket_type.hpp"
#ifdef TORRENT_WINDOWS
#include "libtorrent/aux_/win_util.hpp"
#endif

#include <functional>
#include <cstdlib> // for wcstombscstombs

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/asio/ip/host_name.hpp>

#if TORRENT_USE_IFCONF
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <cstring>
#endif

#if TORRENT_USE_SYSCTL
#include <sys/sysctl.h>
#include <net/route.h>
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

#if TORRENT_USE_IFADDRS || TORRENT_USE_IFCONF || TORRENT_USE_NETLINK || TORRENT_USE_SYSCTL
// capture this here where warnings are disabled (the macro generates warnings)
const unsigned long siocgifmtu = SIOCGIFMTU;
#endif

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#if defined(TORRENT_OS2) && !defined(IF_NAMESIZE)
#define IF_NAMESIZE IFNAMSIZ
#endif

namespace libtorrent {
namespace {


#if !defined TORRENT_BUILD_SIMULATOR
	address_v4 inaddr_to_address(in_addr const* ina, int const len = 4)
	{
		boost::asio::ip::address_v4::bytes_type b = {};
		if (len > 0) std::memcpy(b.data(), ina, std::min(std::size_t(len), b.size()));
		return address_v4(b);
	}

#if TORRENT_USE_IPV6
	address_v6 inaddr6_to_address(in6_addr const* ina6, int const len = 16)
	{
		boost::asio::ip::address_v6::bytes_type b = {};
		if (len > 0) std::memcpy(b.data(), ina6, std::min(std::size_t(len), b.size()));
		return address_v6(b);
	}
#endif

#if !TORRENT_USE_NETLINK
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
			return inaddr_to_address(&reinterpret_cast<sockaddr_in const*>(sin)->sin_addr
				, sockaddr_len(sin) - int(offsetof(sockaddr, sa_data)));
#if TORRENT_USE_IPV6
		else if (sin->sa_family == AF_INET6 || assume_family == AF_INET6)
		{
			auto saddr = reinterpret_cast<sockaddr_in6 const*>(sin);
			auto ret = inaddr6_to_address(&saddr->sin6_addr
				, sockaddr_len(sin) - int(offsetof(sockaddr, sa_data)));
			ret.scope_id(saddr->sin6_scope_id);
			return ret;
		}
#endif
		return address();
	}
#endif

	bool valid_addr_family(int family)
	{
		return (family == AF_INET
#if TORRENT_USE_IPV6
			|| family == AF_INET6
#endif
		);
	}

#if TORRENT_USE_NETLINK

	int read_nl_sock(int sock, span<char> buf, std::uint32_t const seq, std::uint32_t const pid)
	{
		nlmsghdr* nl_hdr;

		int msg_len = 0;

		for (;;)
		{
			auto next_msg = buf.subspan(std::size_t(msg_len));
			int read_len = int(recv(sock, next_msg.data(), next_msg.size(), 0));
			if (read_len < 0) return -1;

			nl_hdr = reinterpret_cast<nlmsghdr*>(next_msg.data());

#ifdef __clang__
#pragma clang diagnostic push
// NLMSG_OK uses signed/unsigned compare in the same expression
#pragma clang diagnostic ignored "-Wsign-compare"
#endif
			if ((NLMSG_OK(nl_hdr, read_len) == 0) || (nl_hdr->nlmsg_type == NLMSG_ERROR))
				return -1;
#ifdef __clang__
#pragma clang diagnostic pop
#endif

			// this function doesn't handle multiple requests at the same time
			// so report an error if the message does not have the expected seq and pid
			if (nl_hdr->nlmsg_seq != seq || nl_hdr->nlmsg_pid != pid)
				return -1;

			if (nl_hdr->nlmsg_type == NLMSG_DONE) break;

			msg_len += read_len;

			if ((nl_hdr->nlmsg_flags & NLM_F_MULTI) == 0) break;
		}
		return msg_len;
	}

	constexpr int NL_BUFSIZE = 8192;

	int nl_dump_request(int sock, std::uint16_t type, std::uint32_t seq, char family, span<char> msg, std::size_t msg_len)
	{
		nlmsghdr* nl_msg = reinterpret_cast<nlmsghdr*>(msg.data());
		nl_msg->nlmsg_len = std::uint32_t(NLMSG_LENGTH(msg_len));
		nl_msg->nlmsg_type = type;
		nl_msg->nlmsg_flags = NLM_F_DUMP | NLM_F_REQUEST;
		nl_msg->nlmsg_seq = seq;
		// in theory nlmsg_pid should be set to the netlink port ID (NOT the process ID)
		// of the sender, but the kernel ignores this field so it is typically set to
		// zero
		nl_msg->nlmsg_pid = 0;
		// first byte of routing messages is always the family
		msg[sizeof(nlmsghdr)] = family;

		if (send(sock, nl_msg, nl_msg->nlmsg_len, 0) < 0)
		{
			return -1;
		}

		// get the socket's port ID so that we can verify it in the repsonse
		sockaddr_nl sock_addr;
		socklen_t sock_addr_len = sizeof(sock_addr);
		if (getsockname(sock, reinterpret_cast<sockaddr*>(&sock_addr), &sock_addr_len) < 0)
		{
			return -1;
		}

		return read_nl_sock(sock, msg, seq, sock_addr.nl_pid);
	}

	bool parse_route(int s, nlmsghdr* nl_hdr, ip_route* rt_info)
	{
		rtmsg* rt_msg = reinterpret_cast<rtmsg*>(NLMSG_DATA(nl_hdr));

		if (!valid_addr_family(rt_msg->rtm_family) || (rt_msg->rtm_table != RT_TABLE_MAIN
			&& rt_msg->rtm_table != RT_TABLE_LOCAL))
			return false;

		int if_index = 0;
		int rt_len = int(RTM_PAYLOAD(nl_hdr));
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-align"
#endif
		for (rtattr* rt_attr = reinterpret_cast<rtattr*>(RTM_RTA(rt_msg));
			RTA_OK(rt_attr, rt_len); rt_attr = RTA_NEXT(rt_attr, rt_len))
		{
			switch(rt_attr->rta_type)
			{
				case RTA_OIF:
					if_index = *reinterpret_cast<int*>(RTA_DATA(rt_attr));
					break;
				case RTA_GATEWAY:
#if TORRENT_USE_IPV6
					if (rt_msg->rtm_family == AF_INET6)
					{
						rt_info->gateway = inaddr6_to_address(reinterpret_cast<in6_addr*>(RTA_DATA(rt_attr)));
					}
					else
#endif
					{
						rt_info->gateway = inaddr_to_address(reinterpret_cast<in_addr*>(RTA_DATA(rt_attr)));
					}
					break;
				case RTA_DST:
#if TORRENT_USE_IPV6
					if (rt_msg->rtm_family == AF_INET6)
					{
						rt_info->destination = inaddr6_to_address(reinterpret_cast<in6_addr*>(RTA_DATA(rt_attr)));
					}
					else
#endif
					{
						rt_info->destination = inaddr_to_address(reinterpret_cast<in_addr*>(RTA_DATA(rt_attr)));
					}
					break;
			}
		}
#ifdef __clang__
#pragma clang diagnostic pop
#endif

		ifreq req = {};
		if_indextoname(std::uint32_t(if_index), req.ifr_name);
		static_assert(sizeof(rt_info->name) >= sizeof(req.ifr_name), "ip_route::name is too small");
		std::memcpy(rt_info->name, req.ifr_name, sizeof(req.ifr_name));
		ioctl(s, siocgifmtu, &req);
		rt_info->mtu = req.ifr_mtu;
//		obviously this doesn't work correctly. How do you get the netmask for a route?
//		if (ioctl(s, SIOCGIFNETMASK, &req) == 0) {
//			rt_info->netmask = sockaddr_to_address(&req.ifr_addr, req.ifr_addr.sa_family);
//		}
		return true;
	}

	bool parse_nl_address(nlmsghdr* nl_hdr, ip_interface* ip_info)
	{
		ifaddrmsg* addr_msg = reinterpret_cast<ifaddrmsg*>(NLMSG_DATA(nl_hdr));

		if (!valid_addr_family(addr_msg->ifa_family))
			return false;

		ip_info->preferred = (addr_msg->ifa_flags & (IFA_F_DADFAILED | IFA_F_DEPRECATED | IFA_F_TENTATIVE)) == 0;

#if TORRENT_USE_IPV6
		if (addr_msg->ifa_family == AF_INET6)
		{
			TORRENT_ASSERT(addr_msg->ifa_prefixlen <= 128);
			if (addr_msg->ifa_prefixlen > 0)
			{
				address_v6::bytes_type mask = {};
				auto it = mask.begin();
				if (addr_msg->ifa_prefixlen > 64)
				{
					detail::write_uint64(0xffffffffffffffffULL, it);
					addr_msg->ifa_prefixlen -= 64;
				}
				if (addr_msg->ifa_prefixlen > 0)
				{
					std::uint64_t const m = ~((1ULL << (64 - addr_msg->ifa_prefixlen)) - 1);
					detail::write_uint64(m, it);
				}
				ip_info->netmask = address_v6(mask);
			}
		}
		else
#endif
		{
			TORRENT_ASSERT(addr_msg->ifa_prefixlen <= 32);
			if (addr_msg->ifa_prefixlen != 0)
			{
				std::uint32_t const m = ~((1U << (32 - addr_msg->ifa_prefixlen)) - 1);
				ip_info->netmask = address_v4(m);
			}
		}

		int rt_len = int(IFA_PAYLOAD(nl_hdr));
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-align"
#endif
		for (rtattr* rt_attr = reinterpret_cast<rtattr*>(IFA_RTA(addr_msg));
			RTA_OK(rt_attr, rt_len); rt_attr = RTA_NEXT(rt_attr, rt_len))
		{
			switch(rt_attr->rta_type)
			{
			case IFA_ADDRESS:
#if TORRENT_USE_IPV6
				if (addr_msg->ifa_family == AF_INET6)
				{
					address_v6 addr = inaddr6_to_address(reinterpret_cast<in6_addr*>(RTA_DATA(rt_attr)));
					if (addr_msg->ifa_scope == RT_SCOPE_LINK)
						addr.scope_id(addr_msg->ifa_index);
					ip_info->interface_address = addr;
				}
				else
#endif
				{
					ip_info->interface_address = inaddr_to_address(reinterpret_cast<in_addr*>(RTA_DATA(rt_attr)));
				}
				break;
			}
		}
#ifdef __clang__
#pragma clang diagnostic pop
#endif

		static_assert(sizeof(ip_info->name) >= IF_NAMESIZE, "not enough space in ip_interface::name");
		if_indextoname(addr_msg->ifa_index, ip_info->name);

		return true;
	}
#endif // TORRENT_USE_NETLINK
#endif // !BUILD_SIMULATOR

#if TORRENT_USE_SYSCTL && !defined TORRENT_BUILD_SIMULATOR
#ifdef TORRENT_OS2
int _System __libsocket_sysctl(int* mib, u_int namelen, void *oldp, size_t *oldlenp, void *newp, size_t newlen);
#endif

	bool parse_route(int, rt_msghdr* rtm, ip_route* rt_info)
	{
		sockaddr* rti_info[RTAX_MAX];
		sockaddr* sa = reinterpret_cast<sockaddr*>(rtm + 1);
		for (int i = 0; i < RTAX_MAX; ++i)
		{
			if ((rtm->rtm_addrs & (1 << i)) == 0)
			{
				rti_info[i] = nullptr;
				continue;
			}
			rti_info[i] = sa;

#define ROUNDUP(a) \
	((a) > 0 ? (1 + (((a) - 1) | (sizeof(long) - 1))) : sizeof(long))

			sa = reinterpret_cast<sockaddr*>(reinterpret_cast<char*>(sa) + ROUNDUP(sa->sa_len));

#undef ROUNDUP
		}

		sa = rti_info[RTAX_GATEWAY];
		if (sa == nullptr
			|| rti_info[RTAX_DST] == nullptr
			|| rti_info[RTAX_NETMASK] == nullptr
			|| !valid_addr_family(sa->sa_family))
			return false;

		rt_info->gateway = sockaddr_to_address(rti_info[RTAX_GATEWAY]);
		rt_info->destination = sockaddr_to_address(rti_info[RTAX_DST]);
		rt_info->netmask = sockaddr_to_address(rti_info[RTAX_NETMASK]
			, rt_info->destination.is_v4() ? AF_INET : AF_INET6);
		if_indextoname(rtm->rtm_index, rt_info->name);
		return true;
	}
#endif

#if TORRENT_USE_IFADDRS && !defined TORRENT_BUILD_SIMULATOR
	bool iface_from_ifaddrs(ifaddrs *ifa, ip_interface &rv)
	{
		if (!valid_addr_family(ifa->ifa_addr->sa_family))
		{
			return false;
		}

		std::strncpy(rv.name, ifa->ifa_name, sizeof(rv.name));
		rv.name[sizeof(rv.name) - 1] = 0;

		// determine address
		rv.interface_address = sockaddr_to_address(ifa->ifa_addr);
		// determine netmask
		if (ifa->ifa_netmask != nullptr)
		{
			rv.netmask = sockaddr_to_address(ifa->ifa_netmask);
		}
		return true;
	}
#endif

} // <anonymous>

	// return (a1 & mask) == (a2 & mask)
	bool match_addr_mask(address const& a1, address const& a2, address const& mask)
	{
		// all 3 addresses needs to belong to the same family
		if (a1.is_v4() != a2.is_v4()) return false;
		if (a1.is_v4() != mask.is_v4()) return false;

#if TORRENT_USE_IPV6
		if (a1.is_v6())
		{
			address_v6::bytes_type b1 = a1.to_v6().to_bytes();
			address_v6::bytes_type b2 = a2.to_v6().to_bytes();
			address_v6::bytes_type m = mask.to_v6().to_bytes();
			for (std::size_t i = 0; i < b1.size(); ++i)
			{
				b1[i] &= m[i];
				b2[i] &= m[i];
			}
			return std::memcmp(b1.data(), b2.data(), b1.size()) == 0;
		}
#endif
		return (a1.to_v4().to_ulong() & mask.to_v4().to_ulong())
			== (a2.to_v4().to_ulong() & mask.to_v4().to_ulong());
	}

	bool in_local_network(io_service& ios, address const& addr, error_code& ec)
	{
		std::vector<ip_interface> net = enum_net_interfaces(ios, ec);
		if (ec) return false;
		return in_local_network(net, addr);
	}

	bool in_local_network(std::vector<ip_interface> const& net, address const& addr)
	{
		for (auto const& i : net)
		{
			if (match_addr_mask(addr, i.interface_address, i.netmask))
				return true;
		}
		return false;
	}

#if TORRENT_USE_GETIPFORWARDTABLE
	address build_netmask(int bits, int family)
	{
		if (family == AF_INET)
		{
			typedef boost::asio::ip::address_v4::bytes_type bytes_t;
			bytes_t b;
			std::memset(&b[0], 0xff, b.size());
			for (int i = int(sizeof(bytes_t)) / 8 - 1; i > 0; --i)
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
			typedef boost::asio::ip::address_v6::bytes_type bytes_t;
			bytes_t b;
			std::memset(&b[0], 0xff, b.size());
			for (int i = int(sizeof(bytes_t)) / 8 - 1; i > 0; --i)
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
		TORRENT_UNUSED(ios); // this may be unused depending on configuration
		std::vector<ip_interface> ret;
		ec.clear();
#if defined TORRENT_BUILD_SIMULATOR

		std::vector<address> ips = ios.get_ips();

		for (auto const& ip : ips)
		{
			ip_interface wan;
			wan.interface_address = ip;
			wan.netmask = address_v4::from_string("255.255.255.255");
			std::strcpy(wan.name, "eth0");
			ret.push_back(wan);
		}
#elif TORRENT_USE_NETLINK
		int sock = socket(PF_ROUTE, SOCK_DGRAM, NETLINK_ROUTE);
		if (sock < 0)
		{
			ec = error_code(errno, system_category());
			return ret;
		}

		char msg[NL_BUFSIZE] = {};
		nlmsghdr* nl_msg = reinterpret_cast<nlmsghdr*>(msg);
		int len = nl_dump_request(sock, RTM_GETADDR, 0, AF_PACKET, msg, sizeof(ifaddrmsg));
		if (len < 0)
		{
			ec = error_code(errno, system_category());
			close(sock);
			return ret;
		}

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-align"
			// NLMSG_OK uses signed/unsigned compare in the same expression
#pragma clang diagnostic ignored "-Wsign-compare"
#endif
		for (; NLMSG_OK(nl_msg, len); nl_msg = NLMSG_NEXT(nl_msg, len))
		{
			ip_interface iface;
			if (parse_nl_address(nl_msg, &iface)) ret.push_back(iface);
		}
#ifdef __clang__
#pragma clang diagnostic pop
#endif

		close(sock);
#elif TORRENT_USE_IFADDRS
		int s = socket(AF_INET, SOCK_DGRAM, 0);
		if (s < 0)
		{
			ec = error_code(errno, system_category());
			return ret;
		}

		ifaddrs *ifaddr;
		if (getifaddrs(&ifaddr) == -1)
		{
			ec = error_code(errno, system_category());
			close(s);
			return ret;
		}

		for (ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next)
		{
			if (ifa->ifa_addr == nullptr) continue;
			if ((ifa->ifa_flags & IFF_UP) == 0) continue;

			if (valid_addr_family(ifa->ifa_addr->sa_family))
			{
				ip_interface iface;
				if (iface_from_ifaddrs(ifa, iface))
					ret.push_back(iface);
			}
		}
		close(s);
		freeifaddrs(ifaddr);
// MacOS X, BSD and solaris
#elif TORRENT_USE_IFCONF
		int s = socket(AF_INET, SOCK_DGRAM, 0);
		if (s < 0)
		{
			ec = error_code(errno, system_category());
			return ret;
		}
		ifconf ifc;
		// make sure the buffer is aligned to hold ifreq structs
		ifreq buf[40];
		ifc.ifc_len = sizeof(buf);
		ifc.ifc_buf = reinterpret_cast<char*>(buf);
		if (ioctl(s, SIOCGIFCONF, &ifc) < 0)
		{
			ec = error_code(errno, system_category());
			close(s);
			return ret;
		}

		char *ifr = reinterpret_cast<char*>(ifc.ifc_req);
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

			if (valid_addr_family(item.ifr_addr.sa_family))
			{
				ip_interface iface;
				iface.interface_address = sockaddr_to_address(&item.ifr_addr);
				std::strcpy(iface.name, item.ifr_name);

				ifreq req = {};
				std::strncpy(req.ifr_name, item.ifr_name, IF_NAMESIZE - 1);
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
						ec = error_code(errno, system_category());
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
		typedef ULONG (WINAPI *GetAdaptersAddresses_t)(ULONG,ULONG,PVOID,PIP_ADAPTER_ADDRESSES,PULONG);
		// Get GetAdaptersAddresses() pointer
		auto GetAdaptersAddresses =
			aux::get_library_procedure<aux::iphlpapi, GetAdaptersAddresses_t>("GetAdaptersAddresses");

		if (GetAdaptersAddresses != nullptr)
		{
			ULONG buf_size = 10000;
			std::vector<char> buffer(buf_size);
			PIP_ADAPTER_ADDRESSES adapter_addresses
				= reinterpret_cast<IP_ADAPTER_ADDRESSES*>(&buffer[0]);

			DWORD res = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER
				| GAA_FLAG_SKIP_ANYCAST, nullptr, adapter_addresses, &buf_size);
			if (res == ERROR_BUFFER_OVERFLOW)
			{
				buffer.resize(buf_size);
				adapter_addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(&buffer[0]);
				res = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER
					| GAA_FLAG_SKIP_ANYCAST, nullptr, adapter_addresses, &buf_size);
			}
			if (res != NO_ERROR)
			{
				ec = error_code(WSAGetLastError(), system_category());
				return std::vector<ip_interface>();
			}

			for (PIP_ADAPTER_ADDRESSES adapter = adapter_addresses;
				adapter != 0; adapter = adapter->Next)
			{
				ip_interface r;
				std::strncpy(r.name, adapter->AdapterName, sizeof(r.name));
				r.name[sizeof(r.name)-1] = 0;
				for (IP_ADAPTER_UNICAST_ADDRESS* unicast = adapter->FirstUnicastAddress;
					unicast; unicast = unicast->Next)
				{
					if (!valid_addr_family(unicast->Address.lpSockaddr->sa_family))
						continue;
					r.preferred = unicast->DadState == IpDadStatePreferred;
					r.interface_address = sockaddr_to_address(unicast->Address.lpSockaddr);
					ret.push_back(r);
				}
			}

			return ret;
		}
#endif

		SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
		if (int(s) == SOCKET_ERROR)
		{
			ec = error_code(WSAGetLastError(), system_category());
			return ret;
		}

		INTERFACE_INFO buffer[30];
		DWORD size;

		if (WSAIoctl(s, SIO_GET_INTERFACE_LIST, 0, 0, buffer,
			sizeof(buffer), &size, 0, 0) != 0)
		{
			ec = error_code(WSAGetLastError(), system_category());
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
			ret.push_back(iface);
		}

#else

#ifdef _MSC_VER
#pragma message ( "THIS OS IS NOT RECOGNIZED, enum_net_interfaces WILL PROBABLY NOT WORK" )
#else
#warning "THIS OS IS NOT RECOGNIZED, enum_net_interfaces WILL PROBABLY NOT WORK"
#endif

		// make a best guess of the interface we're using and its IP
		udp::resolver r(ios);
		udp::resolver::iterator i = r.resolve(udp::resolver::query(boost::asio::ip::host_name(ec), "0"), ec);
		if (ec) return ret;
		ip_interface iface;
		for (;i != udp::resolver_iterator(); ++i)
		{
			iface.interface_address = i->endpoint().address();
			iface.name[0] = '\0';
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
		auto const i = std::find_if(ret.begin(), ret.end()
			, [](ip_route const& r) { return r.destination == address(); });
		if (i == ret.end()) return address();
		return i->gateway;
	}

	std::vector<ip_route> enum_routes(io_service& ios, error_code& ec)
	{
		std::vector<ip_route> ret;
		TORRENT_UNUSED(ios);

#ifdef TORRENT_BUILD_SIMULATOR

		TORRENT_UNUSED(ec);

		std::vector<address> ips = ios.get_ips();

		for (auto const& ip : ips)
		{
			ip_route r;
			if (ip.is_v4())
			{
				r.destination = address_v4();
				r.netmask = address_v4::from_string("255.255.255.0");
				address_v4::bytes_type b = ip.to_v4().to_bytes();
				b[3] = 1;
				r.gateway = address_v4(b);
			}
			else
			{
				r.destination = address_v6();
				r.netmask = address_v6::from_string("FFFF:FFFF:FFFF:FFFF::0");
				address_v6::bytes_type b = ip.to_v6().to_bytes();
				b[14] = 1;
				r.gateway = address_v6(b);
			}
			std::strcpy(r.name, "eth0");
			r.mtu = ios.sim().config().path_mtu(ip, ip);
			ret.push_back(r);
		}

#elif TORRENT_USE_SYSCTL
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
			ec = error_code(errno, system_category());
			return std::vector<ip_route>();
		}

		int n = write(s, &m, len);
		if (n == -1)
		{
			ec = error_code(errno, system_category());
			close(s);
			return std::vector<ip_route>();
		}
		else if (n != len)
		{
			ec = boost::asio::error::operation_not_supported;
			close(s);
			return std::vector<ip_route>();
		}
		bzero(&m, len);

		n = read(s, &m, len);
		if (n == -1)
		{
			ec = error_code(errno, system_category());
			close(s);
			return std::vector<ip_route>();
		}

		for (rt_msghdr* ptr = &m.m_rtm; (char*)ptr < ((char*)&m.m_rtm) + n; ptr = (rt_msghdr*)(((char*)ptr) + ptr->rtm_msglen))
		{
			std::cout << " rtm_msglen: " << ptr->rtm_msglen << std::endl;
			std::cout << " rtm_type: " << ptr->rtm_type << std::endl;
			if (ptr->rtm_errno)
			{
				ec = error_code(ptr->rtm_errno, system_category());
				return std::vector<ip_route>();
			}
			if (m.m_rtm.rtm_flags & RTF_UP == 0
				|| m.m_rtm.rtm_flags & RTF_GATEWAY == 0)
			{
				ec = boost::asio::error::operation_not_supported;
				return address_v4::any();
			}
			if (ptr->rtm_addrs & RTA_DST == 0
				|| ptr->rtm_addrs & RTA_GATEWAY == 0
				|| ptr->rtm_addrs & RTA_NETMASK == 0)
			{
				ec = boost::asio::error::operation_not_supported;
				return std::vector<ip_route>();
			}
			if (ptr->rtm_msglen > len - ((char*)ptr - ((char*)&m.m_rtm)))
			{
				ec = boost::asio::error::operation_not_supported;
				return std::vector<ip_route>();
			}
			int min_len = sizeof(rt_msghdr) + 2 * sizeof(sockaddr_in);
			if (m.m_rtm.rtm_msglen < min_len)
			{
				ec = boost::asio::error::operation_not_supported;
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
	int mib[6] = {CTL_NET, PF_ROUTE, 0, AF_UNSPEC, NET_RT_DUMP, 0};

	std::size_t needed = 0;
#ifdef TORRENT_OS2
	if (__libsocket_sysctl(mib, 6, 0, &needed, 0, 0) < 0)
#else
	if (sysctl(mib, 6, nullptr, &needed, nullptr, 0) < 0)
#endif
	{
		ec = error_code(errno, system_category());
		return std::vector<ip_route>();
	}

	if (needed <= 0)
	{
		return std::vector<ip_route>();
	}

	std::unique_ptr<char[]> buf(new (std::nothrow) char[needed]);
	if (buf.get() == nullptr)
	{
		ec = boost::asio::error::no_memory;
		return std::vector<ip_route>();
	}

#ifdef TORRENT_OS2
	if (__libsocket_sysctl(mib, 6, buf.get(), &needed, 0, 0) < 0)
#else
	if (sysctl(mib, 6, buf.get(), &needed, nullptr, 0) < 0)
#endif
	{
		ec = error_code(errno, system_category());
		return std::vector<ip_route>();
	}

	char* end = buf.get() + needed;

	int s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0)
	{
		ec = error_code(errno, system_category());
		return std::vector<ip_route>();
	}
	rt_msghdr* rtm;
	for (char* next = buf.get(); next < end; next += rtm->rtm_msglen)
	{
		rtm = reinterpret_cast<rt_msghdr*>(next);
		if (rtm->rtm_version != RTM_VERSION
			|| (rtm->rtm_type != RTM_ADD
			&& rtm->rtm_type != RTM_GET))
		{
			continue;
		}

		ip_route r;
		if (parse_route(s, rtm, &r)) ret.push_back(r);
	}
	close(s);

#elif TORRENT_USE_GETIPFORWARDTABLE
/*
	move this to enum_net_interfaces
		// Get GetAdaptersInfo() pointer
		typedef DWORD (WINAPI *GetAdaptersInfo_t)(PIP_ADAPTER_INFO, PULONG);
		GetAdaptersInfo_t GetAdaptersInfo = get_library_procedure<iphlpapi, GetAdaptersInfo_t>("GetAdaptersInfo");
		if (GetAdaptersInfo == nullptr)
		{
			ec = boost::asio::error::operation_not_supported;
			return std::vector<ip_route>();
		}

		PIP_ADAPTER_INFO adapter_info = 0;
		ULONG out_buf_size = 0;
		if (GetAdaptersInfo(adapter_info, &out_buf_size) != ERROR_BUFFER_OVERFLOW)
		{
			ec = boost::asio::error::operation_not_supported;
			return std::vector<ip_route>();
		}

		adapter_info = (IP_ADAPTER_INFO*)malloc(out_buf_size);
		if (!adapter_info)
		{
			ec = boost::asio::error::no_memory;
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
*/

		typedef DWORD (WINAPI *GetIfEntry_t)(PMIB_IFROW pIfRow);
		auto GetIfEntry = aux::get_library_procedure<aux::iphlpapi, GetIfEntry_t>("GetIfEntry");

		if (GetIfEntry == nullptr)
		{
			ec = boost::asio::error::operation_not_supported;
			return std::vector<ip_route>();
		}

#if _WIN32_WINNT >= 0x0600
		typedef DWORD (WINAPI *GetIpForwardTable2_t)(
			ADDRESS_FAMILY, PMIB_IPFORWARD_TABLE2*);
		typedef void (WINAPI *FreeMibTable_t)(PVOID Memory);

		auto GetIpForwardTable2 = aux::get_library_procedure<aux::iphlpapi, GetIpForwardTable2_t>("GetIpForwardTable2");
		auto FreeMibTable = aux::get_library_procedure<aux::iphlpapi, FreeMibTable_t>("FreeMibTable");
		if (GetIpForwardTable2 != nullptr && FreeMibTable != nullptr)
		{
			MIB_IPFORWARD_TABLE2* routes = nullptr;
			int res = GetIpForwardTable2(AF_UNSPEC, &routes);
			if (res == NO_ERROR)
			{
				for (int i = 0; i < int(routes->NumEntries); ++i)
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
			return ret;
		}
#endif

		// Get GetIpForwardTable() pointer
		typedef DWORD (WINAPI *GetIpForwardTable_t)(PMIB_IPFORWARDTABLE pIpForwardTable,PULONG pdwSize,BOOL bOrder);

		auto GetIpForwardTable = aux::get_library_procedure<aux::iphlpapi, GetIpForwardTable_t>("GetIpForwardTable");
		if (GetIpForwardTable == nullptr)
		{
			ec = boost::asio::error::operation_not_supported;
			return std::vector<ip_route>();
		}

		MIB_IPFORWARDTABLE* routes = nullptr;
		ULONG out_buf_size = 0;
		if (GetIpForwardTable(routes, &out_buf_size, FALSE) != ERROR_INSUFFICIENT_BUFFER)
		{
			ec = boost::asio::error::operation_not_supported;
			return std::vector<ip_route>();
		}

		routes = (MIB_IPFORWARDTABLE*)malloc(out_buf_size);
		if (!routes)
		{
			ec = boost::asio::error::no_memory;
			return std::vector<ip_route>();
		}

		if (GetIpForwardTable(routes, &out_buf_size, FALSE) == NO_ERROR)
		{
			for (int i = 0; i < int(routes->dwNumEntries); ++i)
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
#elif TORRENT_USE_NETLINK
		int sock = socket(PF_ROUTE, SOCK_DGRAM, NETLINK_ROUTE);
		if (sock < 0)
		{
			ec = error_code(errno, system_category());
			return std::vector<ip_route>();
		}

		std::uint32_t seq = 0;

		char msg[NL_BUFSIZE] = {};
		nlmsghdr* nl_msg = reinterpret_cast<nlmsghdr*>(msg);
		int len = nl_dump_request(sock, RTM_GETROUTE, seq++, AF_UNSPEC, msg, sizeof(rtmsg));
		if (len < 0)
		{
			ec = error_code(errno, system_category());
			close(sock);
			return std::vector<ip_route>();
		}

		int s = socket(AF_INET, SOCK_DGRAM, 0);
		if (s < 0)
		{
			ec = error_code(errno, system_category());
			return std::vector<ip_route>();
		}
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-align"
// NLMSG_OK uses signed/unsigned compare in the same expression
#pragma clang diagnostic ignored "-Wsign-compare"
#endif
		for (; NLMSG_OK(nl_msg, len); nl_msg = NLMSG_NEXT(nl_msg, len))
		{
			ip_route r;
			if (parse_route(s, nl_msg, &r)) ret.push_back(r);
		}
#ifdef __clang__
#pragma clang diagnostic pop
#endif
		close(s);
		close(sock);

#endif
		return ret;
	}

	// returns the device name whose local address is ``addr``. If
	// no such device is found, an empty string is returned.
	std::string device_for_address(address addr, io_service& ios, error_code& ec)
	{
		std::vector<ip_interface> ifs = enum_net_interfaces(ios, ec);
		if (ec) return std::string();

		for (auto const& iface : ifs)
			if (iface.interface_address == addr) return iface.name;
		return std::string();
	}
}
