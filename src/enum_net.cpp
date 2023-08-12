/*

Copyright (c) 2007-2012, 2014-2021, Arvid Norberg
Copyright (c) 2015-2018, 2021, Alden Torres
Copyright (c) 2015-2018, 2020, Steven Siloti
Copyright (c) 2016-2017, Andrei Kurushin
Copyright (c) 2018, Alexandre Janniaux
Copyright (c) 2020, Tiger Wang
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
#include "libtorrent/assert.hpp"
#include "libtorrent/aux_/socket_type.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/aux_/ip_helpers.hpp"
#ifdef TORRENT_WINDOWS
#include "libtorrent/aux_/win_util.hpp"
#endif

#include <functional>
#include <cstdlib> // for wcstombscstombs

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/asio/ip/host_name.hpp>
#include <boost/optional.hpp>

#if TORRENT_USE_IFCONF
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <cstring>
#endif

#if TORRENT_USE_GRTTABLE
#include <net/if.h>
#endif

#if TORRENT_USE_SYSCTL
#include <sys/sysctl.h>
#ifdef __APPLE__
#include "TargetConditionals.h"
#endif

#if defined TARGET_IPHONE_SIMULATOR || defined TARGET_OS_IPHONE
// net/route.h is not included in the iphone sdk.
#include "libtorrent/aux_/route.h"
#else
#include <net/route.h>
#endif
#endif // TORRENT_USE_SYSCTL

#if TORRENT_USE_GETIPFORWARDTABLE || TORRENT_USE_GETADAPTERSADDRESSES
#include "libtorrent/aux_/windows.hpp"
#include <iphlpapi.h>
#include <ifdef.h> // for IF_OPER_STATUS
#ifdef TORRENT_WINRT
#include <netioapi.h>
#endif
#endif

#include "libtorrent/aux_/netlink_utils.hpp"

#if TORRENT_USE_NETLINK

// We really should be including <linux/if.h> here, for the IF_OPER_* flags.
// However, including this header creates conflicting definitions of <net/if.h>
// on some platforms. So, instead, we just pull those flags out and define them
// here.
//#include <linux/if.h> // for IF_OPER* flags

// RFC 2863 operational status
// these match the ones in linux/if.h, but with different names to not cause any
// conflicts
namespace if_oper {
enum : int {
	unknown,
	notpresent,
	down,
	lowerlayerdown,
	testing,
	dormant,
	up,
};
}

#include <asm/types.h>
#include <netinet/ether.h>
#include <netinet/in.h>
#include <cstdio>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/types.h>

#if defined TORRENT_ANDROID && !defined IFA_F_DADFAILED
#define IFA_F_DADFAILED 8
#endif

#endif

#if TORRENT_USE_IFADDRS
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/ioctl.h>
#endif

#if TORRENT_USE_IFADDRS || TORRENT_USE_IFCONF || TORRENT_USE_NETLINK || TORRENT_USE_SYSCTL
#ifdef TORRENT_BEOS
// TODO: in C++17, use __has_include for this. Other operating systems are
// likely to require this as well
#include <sys/sockio.h>
#endif
// capture this here where warnings are disabled (the macro generates warnings)
const unsigned long siocgifmtu = SIOCGIFMTU;
#endif

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#if defined(TORRENT_OS2) && !defined(IF_NAMESIZE)
#define IF_NAMESIZE IFNAMSIZ
#endif

namespace libtorrent {

namespace {

#if !defined TORRENT_WINDOWS && !defined TORRENT_BUILD_SIMULATOR
	struct socket_closer
	{
		socket_closer(int s) : m_socket(s) {}
		socket_closer(socket_closer const&) = delete;
		socket_closer(socket_closer &&) = delete;
		socket_closer& operator=(socket_closer const&) = delete;
		socket_closer& operator=(socket_closer &&) = delete;
		~socket_closer() { ::close(m_socket); }
	private:
		int m_socket;
	};
#endif

#if !defined TORRENT_BUILD_SIMULATOR
	address_v4 inaddr_to_address(void const* ina, int const len = 4)
	{
		boost::asio::ip::address_v4::bytes_type b = {};
		if (len > 0) std::memcpy(b.data(), ina, std::min(std::size_t(len), b.size()));
		return address_v4(b);
	}

	address_v6 inaddr6_to_address(void const* ina6, int const len = 16)
	{
		boost::asio::ip::address_v6::bytes_type b = {};
		if (len > 0) std::memcpy(b.data(), ina6, std::min(std::size_t(len), b.size()));
		return address_v6(b);
	}

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
		else if (sin->sa_family == AF_INET6 || assume_family == AF_INET6)
		{
			auto saddr = reinterpret_cast<sockaddr_in6 const*>(sin);
			auto ret = inaddr6_to_address(&saddr->sin6_addr
				, sockaddr_len(sin) - int(offsetof(sockaddr, sa_data)));
			ret.scope_id(saddr->sin6_scope_id);
			return ret;
		}
		return address();
	}
#endif

	bool valid_addr_family(int family)
	{
		return (family == AF_INET
			|| family == AF_INET6
		);
	}

#if TORRENT_USE_NETLINK || TORRENT_USE_IFADDRS || TORRENT_USE_IFCONF
	interface_flags convert_if_flags(unsigned int const f)
	{
		return ((f & IFF_UP) ? if_flags::up : interface_flags{})
			| ((f & IFF_BROADCAST) ? if_flags::broadcast : interface_flags{})
			| ((f & IFF_LOOPBACK) ? if_flags::loopback : interface_flags{})
			| ((f & IFF_POINTOPOINT) ? if_flags::pointopoint : interface_flags{})
#ifdef IFF_RUNNING
			| ((f & IFF_RUNNING) ? if_flags::running : interface_flags{})
#endif
			| ((f & IFF_NOARP) ? if_flags::noarp : interface_flags{})
			| ((f & IFF_PROMISC) ? if_flags::promisc : interface_flags{})
			| ((f & IFF_ALLMULTI) ? if_flags::allmulti : interface_flags{})
#ifdef IFF_MASTER
			| ((f & IFF_MASTER) ? if_flags::master : interface_flags{})
#endif
#ifdef IFF_SLAVE
			| ((f & IFF_SLAVE) ? if_flags::slave : interface_flags{})
#endif
			| ((f & IFF_MULTICAST) ? if_flags::multicast : interface_flags{})
#ifdef IFF_DYNAMIC
			| ((f & IFF_DYNAMIC) ? if_flags::dynamic : interface_flags{})
#endif
		;
	}
#endif

#if TORRENT_USE_NETLINK

	int read_nl_sock(int sock, std::uint32_t const seq, std::uint32_t const pid
		, std::function<void(nlmsghdr const*)> on_msg)
	{
		std::array<char, 4096> buf;
		for (;;)
		{
			int const read_len = int(recv(sock, buf.data(), buf.size(), 0));
			if (read_len < 0) return -1;

			auto const* nl_hdr = reinterpret_cast<nlmsghdr const*>(buf.data());
			int len = read_len;

			for (; len > 0 && aux::nlmsg_ok(nl_hdr, len); nl_hdr = aux::nlmsg_next(nl_hdr, len))
			{
				// TODO: if we get here, the caller still assumes the error code
				// is reported via errno
				if ((aux::nlmsg_ok(nl_hdr, read_len) == 0) || (nl_hdr->nlmsg_type == NLMSG_ERROR))
					return -1;
				// this function doesn't handle multiple requests at the same time
				// so report an error if the message does not have the expected seq and pid
				// TODO: if we get here, the caller still assumes the error code
				// is reported via errno
				if (nl_hdr->nlmsg_seq != seq || nl_hdr->nlmsg_pid != pid)
					return -1;

				if (nl_hdr->nlmsg_type == NLMSG_DONE) return 0;

				on_msg(nl_hdr);

				if ((nl_hdr->nlmsg_flags & NLM_F_MULTI) == 0) return 0;
			}
		}
//		return 0;
	}

	int nl_dump_request(int const sock, std::uint32_t const seq
		, nlmsghdr* const request_msg, std::function<void(nlmsghdr const*)> on_msg)
	{
		request_msg->nlmsg_flags = NLM_F_DUMP | NLM_F_REQUEST;
		request_msg->nlmsg_seq = seq;
		// in theory nlmsg_pid should be set to the netlink port ID (NOT the process ID)
		// of the sender, but the kernel ignores this field so it is typically set to
		// zero
		request_msg->nlmsg_pid = 0;

		if (::send(sock, request_msg, request_msg->nlmsg_len, 0) < 0)
			return -1;

		// get the socket's port ID so that we can verify it in the response
		sockaddr_nl sock_addr;
		socklen_t sock_addr_len = sizeof(sock_addr);
		if (::getsockname(sock, reinterpret_cast<sockaddr*>(&sock_addr), &sock_addr_len) < 0)
			return -1;

		return read_nl_sock(sock, seq, sock_addr.nl_pid, std::move(on_msg));
	}

	address to_address(int const address_family, void const* in)
	{
		if (address_family == AF_INET6) return inaddr6_to_address(in);
		else return inaddr_to_address(in);
	}

	struct link_info
	{
		int mtu;
		int if_idx;
		int type;
		int oper_state;
		char name[64];
		interface_flags flags;
	};

	link_info parse_nl_link(nlmsghdr const* nl_hdr)
	{
		auto const* if_msg = static_cast<ifinfomsg const*>(aux::nlmsg_data(nl_hdr));
		rtattr const* rta_ptr = aux::ifla_rta(if_msg);
		std::size_t attr_len = aux::ifla_payload(nl_hdr);

		link_info ret{};
		ret.flags = convert_if_flags(if_msg->ifi_flags);
		ret.if_idx = if_msg->ifi_index;

		for (; aux::rta_ok(rta_ptr, attr_len); rta_ptr = aux::rta_next(rta_ptr, attr_len))
		{
			auto* const ptr = aux::rta_data(rta_ptr);
			switch (rta_ptr->rta_type)
			{
				case IFLA_IFNAME:
					std::strncpy(ret.name, static_cast<char const*>(ptr), sizeof(ret.name) - 1);
					ret.name[sizeof(ret.name)-1] = '\0';
					break;
				case IFLA_MTU: std::memcpy(&ret.mtu, ptr, sizeof(int)); break;
				case IFLA_LINK: std::memcpy(&ret.type, ptr, sizeof(int)); break;
				case IFLA_OPERSTATE: std::memcpy(&ret.oper_state, ptr, sizeof(int)); break;

				// ignore these attributes
				case IFLA_ADDRESS:
				case IFLA_BROADCAST:
				case IFLA_QDISC:
				case IFLA_COST:
				case IFLA_WIRELESS:
				case IFLA_WEIGHT:
				case IFLA_LINKMODE:
				case IFLA_LINKINFO:
				default:
					break;
			}
		}
		return ret;
	}

	bool parse_route(int s, nlmsghdr const* nl_hdr, ip_route* rt_info)
	{
		// sanity check
		if (nl_hdr->nlmsg_type != RTM_NEWROUTE) return false;

		auto const* rt_msg = static_cast<rtmsg const*>(aux::nlmsg_data(nl_hdr));

		if (!valid_addr_family(rt_msg->rtm_family))
			return false;

		// make sure the defaults have the right address family
		// in case the attributes are not present
		if (rt_msg->rtm_family == AF_INET6)
		{
			rt_info->gateway = address_v6();
			rt_info->destination = address_v6();
		}

		int if_index = 0;
		std::size_t rt_len = aux::rtm_payload(nl_hdr);
		for (rtattr const* rt_attr = aux::rtm_rta(rt_msg);
			aux::rta_ok(rt_attr, rt_len); rt_attr = aux::rta_next(rt_attr, rt_len))
		{
			switch(rt_attr->rta_type)
			{
				case RTA_OIF:
					std::memcpy(&if_index, aux::rta_data(rt_attr), sizeof(int));
					break;
				case RTA_GATEWAY:
					rt_info->gateway = to_address(rt_msg->rtm_family, aux::rta_data(rt_attr));
					break;
				case RTA_DST:
					rt_info->destination = to_address(rt_msg->rtm_family, aux::rta_data(rt_attr));
					break;
				case RTA_PREFSRC:
					rt_info->source_hint = to_address(rt_msg->rtm_family, aux::rta_data(rt_attr));
					break;
			}
		}

		if (rt_info->gateway.is_v6() && rt_info->gateway.to_v6().is_link_local())
		{
			address_v6 gateway6 = rt_info->gateway.to_v6();
			gateway6.scope_id(std::uint32_t(if_index));
			rt_info->gateway = gateway6;
		}

		ifreq req = {};
		::if_indextoname(std::uint32_t(if_index), req.ifr_name);
		static_assert(sizeof(rt_info->name) >= sizeof(req.ifr_name), "ip_route::name is too small");
		std::memcpy(rt_info->name, req.ifr_name, sizeof(req.ifr_name));
		::ioctl(s, ::siocgifmtu, &req);
		rt_info->mtu = req.ifr_mtu;
		rt_info->netmask = build_netmask(rt_msg->rtm_dst_len, rt_msg->rtm_family);
		return true;
	}

	bool parse_nl_address(nlmsghdr const* nl_hdr, span<link_info const> nics
		, ip_interface* ip_info)
	{
		// sanity check
		if (nl_hdr->nlmsg_type != RTM_NEWADDR) return false;

		auto const* addr_msg = static_cast<ifaddrmsg const*>(aux::nlmsg_data(nl_hdr));

		if (!valid_addr_family(addr_msg->ifa_family))
			return false;

		auto interface = std::find_if(nics.begin(), nics.end()
			, [addr_msg](link_info const& li) { return li.if_idx == int(addr_msg->ifa_index); });
		TORRENT_ASSERT(interface != nics.end());
		if (interface == nics.end()) return false;

		ip_info->preferred = (addr_msg->ifa_flags & (IFA_F_DADFAILED | IFA_F_DEPRECATED | IFA_F_TENTATIVE)) == 0;
		ip_info->netmask = build_netmask(addr_msg->ifa_prefixlen, addr_msg->ifa_family);

		ip_info->interface_address = address();
		std::size_t rt_len = aux::ifa_payload(nl_hdr);
		for (rtattr const* rt_attr = aux::ifa_rta(addr_msg);
			aux::rta_ok(rt_attr, rt_len); rt_attr = aux::rta_next(rt_attr, rt_len))
		{
			switch(rt_attr->rta_type)
			{
			case IFA_ADDRESS:
				// if this is a point-to-point link then IFA_LOCAL holds
				// the local address while IFA_ADDRESS is the destination
				// don't overwrite the former with the latter
				if (!ip_info->interface_address.is_unspecified())
					break;
				BOOST_FALLTHROUGH;
			case IFA_LOCAL:
				if (addr_msg->ifa_family == AF_INET6)
				{
					address_v6 addr = inaddr6_to_address(aux::rta_data(rt_attr));
					if (addr_msg->ifa_scope == RT_SCOPE_LINK)
						addr.scope_id(addr_msg->ifa_index);
					ip_info->interface_address = addr;
				}
				else
				{
					ip_info->interface_address = inaddr_to_address(aux::rta_data(rt_attr));
				}
				break;
			}
		}

		static_assert(sizeof(ip_info->name) == sizeof(interface->name), "interface name field sizes differ");
		std::memcpy(ip_info->name, interface->name, sizeof(ip_info->name));
		ip_info->flags = interface->flags;

		ip_info->state
			= interface->oper_state == if_oper::up ? if_state::up
			: interface->oper_state == if_oper::dormant ? if_state::dormant
			: interface->oper_state == if_oper::lowerlayerdown ? if_state::lowerlayerdown
			: interface->oper_state == if_oper::down ? if_state::down
			: interface->oper_state == if_oper::notpresent ? if_state::notpresent
			: interface->oper_state == if_oper::testing ? if_state::testing
			: interface->oper_state == if_oper::unknown ? if_state::unknown
			: if_state::unknown;

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
		auto* sa = reinterpret_cast<sockaddr*>(rtm + 1);
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
		if (rti_info[RTAX_IFA]) rt_info->source_hint = sockaddr_to_address(rti_info[RTAX_IFA]);
		return true;
	}
#endif

#if TORRENT_USE_IFADDRS && !defined TORRENT_BUILD_SIMULATOR
	bool iface_from_ifaddrs(ifaddrs *ifa, ip_interface &rv)
	{
		// some android ifaddrs implementations are buggy
		if (ifa->ifa_addr == nullptr) return false;

		// determine address
		rv.interface_address = sockaddr_to_address(ifa->ifa_addr);
		if (rv.interface_address.is_unspecified()) return false;

		if (ifa->ifa_name != nullptr)
		{
			std::strncpy(rv.name, ifa->ifa_name, sizeof(rv.name) - 1);
			rv.name[sizeof(rv.name) - 1] = '\0';
		}

		// determine netmask
		if (ifa->ifa_netmask != nullptr)
			rv.netmask = sockaddr_to_address(ifa->ifa_netmask);

		rv.flags = convert_if_flags(ifa->ifa_flags);
		return true;
	}
#endif

	void build_netmask_impl(span<unsigned char> mask, int prefix_bits)
	{
		TORRENT_ASSERT(prefix_bits <= mask.size() * 8);
		TORRENT_ASSERT(prefix_bits >= 0);
		int i = 0;
		while (prefix_bits >= 8)
		{
			mask[i] = 0xff;
			prefix_bits -= 8;
			++i;
		}
		if (i < mask.size())
		{
			mask[i] = (0xff << (8 - prefix_bits)) & 0xff;
			++i;
			while (i < mask.size())
			{
				mask[i] = 0;
				++i;
			}
		}
	}

} // <anonymous>

	int family(address const& a) { return a.is_v4() ? AF_INET : AF_INET6; }

	address build_netmask(int prefix_bits, int const family)
	{
		if (family == AF_INET)
		{
			address_v4::bytes_type b;
			build_netmask_impl(b, prefix_bits);
			return address_v4(b);
		}
		else if (family == AF_INET6)
		{
			address_v6::bytes_type b;
			build_netmask_impl(b, prefix_bits);
			return address_v6(b);
		}
		return {};
	}

	// return (a1 & mask) == (a2 & mask)
	bool match_addr_mask(address const& a1, address const& a2, address const& mask)
	{
		// all 3 addresses needs to belong to the same family
		if (a1.is_v4() != a2.is_v4()) return false;
		if (a1.is_v4() != mask.is_v4()) return false;

		if (a1.is_v6())
		{
			if (a1.to_v6().scope_id() != a2.to_v6().scope_id()) return false;

			address_v6::bytes_type b1 = a1.to_v6().to_bytes();
			address_v6::bytes_type b2 = a2.to_v6().to_bytes();
			address_v6::bytes_type m = mask.to_v6().to_bytes();
			for (std::size_t i = 0; i < b1.size(); ++i)
			{
				b1[i] &= m[i];
				b2[i] &= m[i];
			}
			return b1 == b2;
		}
		return (a1.to_v4().to_uint() & mask.to_v4().to_uint())
			== (a2.to_v4().to_uint() & mask.to_v4().to_uint());
	}

	std::vector<ip_interface> enum_net_interfaces(io_context& ios, error_code& ec)
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
			if (ip.is_v4())
				wan.netmask = make_address_v4("255.0.0.0");
			else
				wan.netmask = make_address_v6("ffff::");
			std::strcpy(wan.name, "eth0");
			std::strcpy(wan.friendly_name, "Ethernet");
			std::strcpy(wan.description, "Simulator Ethernet Adapter");
			ret.push_back(wan);
		}
#elif TORRENT_USE_NETLINK
		int const sock = ::socket(PF_ROUTE, SOCK_DGRAM, NETLINK_ROUTE);
		if (sock < 0)
		{
			ec = error_code(errno, system_category());
			return ret;
		}
		socket_closer c1(sock);

		// netlink socket documentation:
		// https://people.redhat.com/nhorman/papers/netlink.pdf
		std::uint32_t seq = 0;

		struct
		{
			struct nlmsghdr hdr;
			struct ifinfomsg msg;
		} link_req{};

		link_req.hdr.nlmsg_len = std::uint32_t(NLMSG_LENGTH(sizeof(link_req.msg)));
		link_req.hdr.nlmsg_type = RTM_GETLINK;
		link_req.msg.ifi_family = AF_PACKET;
		link_req.msg.ifi_change = 0xFFFFFFFF;

		std::vector<link_info> nics;
		if (nl_dump_request(sock, seq++, &link_req.hdr, [&](nlmsghdr const* msg) {

				// sanity check
				if (msg->nlmsg_type != RTM_NEWLINK) return;

				nics.push_back(parse_nl_link(msg));
			}) != 0)
		{
			ec = error_code(errno, system_category());
			return ret;
		}

		struct
		{
			struct nlmsghdr hdr;
			struct ifaddrmsg msg;
		} request{};

		request.hdr.nlmsg_len = std::uint32_t(NLMSG_LENGTH(sizeof(request.msg)));
		request.hdr.nlmsg_type = RTM_GETADDR;
		request.msg.ifa_family = AF_PACKET;

		if (nl_dump_request(sock, seq++, &request.hdr, [&](nlmsghdr const* msg) {
				ip_interface iface;
				if (parse_nl_address(msg, nics, &iface)) ret.push_back(iface);
			}) != 0)
		{
			ec = error_code(errno, system_category());
			return ret;
		}
#elif TORRENT_USE_IFADDRS
		int const s = ::socket(AF_INET, SOCK_DGRAM, 0);
		if (s < 0)
		{
			ec = error_code(errno, system_category());
			return ret;
		}
		socket_closer c1(s);

		ifaddrs *ifaddr;
		if (getifaddrs(&ifaddr) == -1)
		{
			ec = error_code(errno, system_category());
			return ret;
		}

		for (ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next)
		{
			ip_interface iface;
			if (iface_from_ifaddrs(ifa, iface))
				ret.push_back(iface);
		}
		freeifaddrs(ifaddr);
// MacOS X, BSD, solaris and Haiku
#elif TORRENT_USE_IFCONF
		int const s = ::socket(AF_INET, SOCK_DGRAM, 0);
		if (s < 0)
		{
			ec = error_code(errno, system_category());
			return ret;
		}
		socket_closer c1(s);
		ifconf ifc;
		// make sure the buffer is aligned to hold ifreq structs
		ifreq buf[40];
		ifc.ifc_len = sizeof(buf);
		ifc.ifc_req = buf;
		if (ioctl(s, SIOCGIFCONF, &ifc) < 0)
		{
			ec = error_code(errno, system_category());
			return ret;
		}

		char const* ifr = reinterpret_cast<char const*>(ifc.ifc_buf);

		int current_size = 0;
		for (int remaining = ifc.ifc_len;
			remaining > 0;
			ifr += current_size, remaining -= current_size)
		{
			ifreq const& item = *reinterpret_cast<ifreq const*>(ifr);

#ifdef _SIZEOF_ADDR_IFREQ
			current_size = _SIZEOF_ADDR_IFREQ(item);
#elif defined TORRENT_BSD
			current_size = item.ifr_addr.sa_len + IFNAMSIZ;
#else
			current_size = sizeof(ifreq);
#endif

			if (remaining < current_size) break;

			if (!valid_addr_family(item.ifr_addr.sa_family))
				continue;

			ip_interface iface;
			iface.interface_address = sockaddr_to_address(&item.ifr_addr);
			std::strncpy(iface.name, item.ifr_name, sizeof(iface.name) - 1);
			iface.name[sizeof(iface.name) - 1] = '\0';

			ifreq req = {};
			std::strncpy(req.ifr_name, item.ifr_name, IF_NAMESIZE - 1);
			if (ioctl(s, SIOCGIFFLAGS, &req) == 0)
				iface.flags = convert_if_flags(req.ifr_flags);
			else
				iface.flags = if_flags::up;

			if (ioctl(s, SIOCGIFNETMASK, &req) < 0)
			{
				if (iface.interface_address.is_v6())
				{
					// this is expected to fail (at least on MacOS X)
					iface.netmask = address_v6::any();
				}
				else
				{
					ec = error_code(errno, system_category());
					return ret;
				}
			}
			else
			{
				iface.netmask = sockaddr_to_address(&req.ifr_addr, item.ifr_addr.sa_family);
			}
			ret.push_back(iface);
		}

#elif TORRENT_USE_GETADAPTERSADDRESSES

#if _WIN32_WINNT >= 0x0501
		using GetAdaptersAddresses_t = ULONG (WINAPI *)(ULONG,ULONG,PVOID,PIP_ADAPTER_ADDRESSES,PULONG);
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
				adapter != nullptr; adapter = adapter->Next)
			{
				ip_interface r;
				std::strncpy(r.name, adapter->AdapterName, sizeof(r.name) - 1);
				r.name[sizeof(r.name) - 1] = '\0';
				wcstombs(r.friendly_name, adapter->FriendlyName, sizeof(r.friendly_name));
				r.friendly_name[sizeof(r.friendly_name) - 1] = '\0';
				wcstombs(r.description, adapter->Description, sizeof(r.description));
				r.description[sizeof(r.description) - 1] = '\0';
				r.state
					= (adapter->OperStatus == IfOperStatusUp) ? if_state::up
					: (adapter->OperStatus == IfOperStatusDown) ? if_state::down
					: (adapter->OperStatus == IfOperStatusTesting) ? if_state::testing
					: (adapter->OperStatus == IfOperStatusUnknown) ? if_state::unknown
					: (adapter->OperStatus == IfOperStatusDormant) ? if_state::dormant
					: (adapter->OperStatus == IfOperStatusNotPresent) ? if_state::notpresent
					: (adapter->OperStatus == IfOperStatusLowerLayerDown) ? if_state::lowerlayerdown
					: if_state::unknown;

				r.flags = r.state != if_state::down ? if_flags::up : interface_flags{};
				if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
					r.flags |= if_flags::loopback;
				if (adapter->IfType == IF_TYPE_PPP)
					r.flags |= if_flags::pointopoint;
				if (!(adapter->Flags & IP_ADAPTER_NO_MULTICAST))
					r.flags |= if_flags::multicast;

				for (IP_ADAPTER_UNICAST_ADDRESS* unicast = adapter->FirstUnicastAddress;
					unicast; unicast = unicast->Next)
				{
					auto const family = unicast->Address.lpSockaddr->sa_family;

					if (!valid_addr_family(family))
						continue;

					if (family == AF_INET && !(adapter->Flags & IP_ADAPTER_IPV4_ENABLED))
						r.flags &= ~if_flags::up;
					else if (family == AF_INET6 && !(adapter->Flags & IP_ADAPTER_IPV6_ENABLED))
						r.flags &= ~if_flags::up;
					else
						r.flags |= if_flags::up;

					r.preferred = unicast->DadState == IpDadStatePreferred;
					r.interface_address = sockaddr_to_address(unicast->Address.lpSockaddr);
					int const max_prefix_len = family == AF_INET ? 32 : 128;

					if (unicast->Length <= offsetof(IP_ADAPTER_UNICAST_ADDRESS, OnLinkPrefixLength))
					{
						// OnLinkPrefixLength is only present on Vista and newer. If
						// we're running on XP, we don't have the netmask.
						r.netmask = (family == AF_INET)
							? address(address_v4())
							: address(address_v6());
						ret.push_back(r);
						continue;
					}

					if (family == AF_INET6
						&& unicast->OnLinkPrefixLength == 128
						&& (unicast->PrefixOrigin == IpPrefixOriginDhcp
							|| unicast->SuffixOrigin == IpSuffixOriginRandom))
					{
						// DHCPv6 does not specify a subnet mask (it should be taken from the RA)
						// but apparently MS didn't get the memo and incorrectly reports a
						// prefix length of 128 for DHCPv6 assigned addresses
						// 128 is also reported for privacy addresses despite claiming to
						// have gotten the prefix length from the RA *shrug*
						// use a 64 bit prefix in these cases since that is likely to be
						// the correct value, or at least less wrong than 128
						r.netmask = build_netmask(64, family);
					}
					else if (unicast->OnLinkPrefixLength <= max_prefix_len)
					{
						r.netmask = build_netmask(unicast->OnLinkPrefixLength, family);
					}
					else
					{
						// we don't know what the netmask is
						r.netmask = (family == AF_INET)
							? address(address_v4())
							: address(address_v6());
					}

					ret.push_back(r);
				}
			}

			return ret;
		}
#endif

		SOCKET s = ::socket(AF_INET, SOCK_DGRAM, 0);
		if (int(s) == SOCKET_ERROR)
		{
			ec = error_code(WSAGetLastError(), system_category());
			return ret;
		}

		INTERFACE_INFO buffer[30];
		DWORD size;

		if (WSAIoctl(s, SIO_GET_INTERFACE_LIST, nullptr, 0, buffer,
			sizeof(buffer), &size, nullptr, nullptr) != 0)
		{
			ec = error_code(WSAGetLastError(), system_category());
			closesocket(s);
			return ret;
		}
		closesocket(s);

		int n = size / sizeof(INTERFACE_INFO);

		for (int i = 0; i < n; ++i)
		{
			ip_interface iface;
			iface.interface_address = sockaddr_to_address(&buffer[i].iiAddress.Address);
			if (iface.interface_address == address_v4::any()) continue;
			iface.netmask = sockaddr_to_address(&buffer[i].iiNetmask.Address
				, iface.interface_address.is_v4() ? AF_INET : AF_INET6);
			ret.push_back(iface);
		}
#else

#error "Don't know how to enumerate network interfaces on this platform"

#endif
		return ret;
	}

	boost::optional<address> get_gateway(ip_interface const& iface, span<ip_route const> routes)
	{
		bool const v4 = iface.interface_address.is_v4();

		// local IPv6 addresses can never be used to reach the internet
		if (!v4 && aux::is_local(iface.interface_address)) return {};

		auto const it = std::find_if(routes.begin(), routes.end()
			, [&](ip_route const& r) -> bool
			{
				return r.destination.is_unspecified()
					&& r.destination.is_v4() == iface.interface_address.is_v4()
					&& !r.gateway.is_unspecified()
					// in case there are multiple networks on the same networking
					// device, the source hint may be the only thing telling them
					// apart
					&& (r.source_hint.is_unspecified() || r.source_hint == iface.interface_address)
					&& std::strcmp(r.name, iface.name) == 0;
			});
		if (it != routes.end()) return it->gateway;
		return {};
	}

	bool has_any_internet_route(span<ip_route const> routes)
	{
		return std::any_of(routes.begin(), routes.end()
			, [&](ip_route const& r) -> bool
			{
				// if *any* global IP can be routed to this interface, it's
				// considered able to reach the internet
				return r.destination.is_unspecified()
					|| aux::is_global(r.destination) ;
			});
	}

	bool has_internet_route(string_view device, int const fam, span<ip_route const> routes)
	{
		return std::any_of(routes.begin(), routes.end()
			, [&](ip_route const& r) -> bool
			{
				// if *any* global IP can be routed to this interface, it's
				// considered able to reach the internet
				return family(r.destination) == fam
					&& r.name == device
					&& (r.destination.is_unspecified()
						|| aux::is_global(r.destination)
					);
			});
	}

	std::vector<ip_route> enum_routes(io_context& ios, error_code& ec)
	{
		std::vector<ip_route> ret;
		TORRENT_UNUSED(ios);
		ec.clear();

#ifdef TORRENT_BUILD_SIMULATOR

		TORRENT_UNUSED(ec);

		std::vector<address> ips = ios.get_ips();

		for (auto const& ip : ips)
		{
			ip_route r;
			if (ip.is_v4())
			{
				r.destination = address_v4();
				r.netmask = make_address_v4("255.0.0.0");
				address_v4::bytes_type b = ip.to_v4().to_bytes();
				b[3] = 1;
				r.gateway = address_v4(b);
			}
			else
			{
				r.destination = address_v6();
				r.netmask = make_address_v6("ffff:ffff:ffff:ffff::0");
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

		int s = ::socket(PF_ROUTE, SOCK_RAW, AF_UNSPEC);
		if (s == -1)
		{
			ec = error_code(errno, system_category());
			return std::vector<ip_route>();
		}

		int n = write(s, &m, len);
		if (n == -1)
		{
			ec = error_code(errno, system_category());
			::close(s);
			return std::vector<ip_route>();
		}
		else if (n != len)
		{
			ec = boost::asio::error::operation_not_supported;
			::close(s);
			return std::vector<ip_route>();
		}
		bzero(&m, len);

		n = read(s, &m, len);
		if (n == -1)
		{
			ec = error_code(errno, system_category());
			::close(s);
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
		::close(s);
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

	if (needed == 0)
	{
		return std::vector<ip_route>();
	}

	std::unique_ptr<char[]> buf(new (std::nothrow) char[needed]);
	if (buf == nullptr)
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

	int const s = ::socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0)
	{
		ec = error_code(errno, system_category());
		return std::vector<ip_route>();
	}
	socket_closer c1(s);
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
#elif TORRENT_USE_GETIPFORWARDTABLE
/*
	move this to enum_net_interfaces
		// Get GetAdaptersInfo() pointer
		using GetAdaptersInfo_t = DWORD (WINAPI*)(PIP_ADAPTER_INFO, PULONG);
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
				r.destination = make_address(adapter->IpAddressList.IpAddress.String, ec);
				r.gateway = make_address(adapter->GatewayList.IpAddress.String, ec);
				r.netmask = make_address(adapter->IpAddressList.IpMask.String, ec);
				strncpy(r.name, adapter->AdapterName, sizeof(r.name) - 1);
				r.name[sizeof(r.name) - 1] = '\0';

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

		using GetIfEntry_t = DWORD (WINAPI *)(PMIB_IFROW pIfRow);
		auto GetIfEntry = aux::get_library_procedure<aux::iphlpapi, GetIfEntry_t>(
			"GetIfEntry");

		if (GetIfEntry == nullptr)
		{
			ec = boost::asio::error::operation_not_supported;
			return std::vector<ip_route>();
		}

#if _WIN32_WINNT >= 0x0600
		using GetIpForwardTable2_t = DWORD (WINAPI *)(
			ADDRESS_FAMILY, PMIB_IPFORWARD_TABLE2*);
		using FreeMibTable_t = void (WINAPI *)(PVOID Memory);

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
					// The scope_id in NextHop is always zero because that would make
					// things too easy apparently
					if (r.gateway.is_v6() && r.gateway.to_v6().is_link_local())
					{
						address_v6 gateway6 = r.gateway.to_v6();
						gateway6.scope_id(routes->Table[i].InterfaceIndex);
						r.gateway = gateway6;
					}
					r.destination = sockaddr_to_address(
						(const sockaddr*)&routes->Table[i].DestinationPrefix.Prefix);
					r.netmask = build_netmask(routes->Table[i].DestinationPrefix.PrefixLength
						, routes->Table[i].DestinationPrefix.Prefix.si_family);
					MIB_IFROW ifentry;
					ifentry.dwIndex = routes->Table[i].InterfaceIndex;
					if (GetIfEntry(&ifentry) == NO_ERROR)
					{
						WCHAR* name = ifentry.wszName;
						// strip UNC prefix to match the names returned by enum_net_interfaces
						if (wcsncmp(name, L"\\DEVICE\\TCPIP_", wcslen(L"\\DEVICE\\TCPIP_")) == 0)
						{
							name += wcslen(L"\\DEVICE\\TCPIP_");
						}
						wcstombs(r.name, name, sizeof(r.name) - 1);
						r.name[sizeof(r.name) - 1] = '\0';
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
		using GetIpForwardTable_t = DWORD (WINAPI*)(PMIB_IPFORWARDTABLE pIpForwardTable,PULONG pdwSize,BOOL bOrder);

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
				r.destination = inaddr_to_address(&routes->table[i].dwForwardDest);
				r.netmask = inaddr_to_address(&routes->table[i].dwForwardMask);
				r.gateway = inaddr_to_address(&routes->table[i].dwForwardNextHop);
				MIB_IFROW ifentry;
				ifentry.dwIndex = routes->table[i].dwForwardIfIndex;
				if (GetIfEntry(&ifentry) == NO_ERROR)
				{
					wcstombs(r.name, ifentry.wszName, sizeof(r.name) - 1);
					r.name[sizeof(r.name) - 1] = '\0';
					r.mtu = ifentry.dwMtu;
					ret.push_back(r);
				}
			}
		}

		// Free memory
		free(routes);
#elif TORRENT_USE_NETLINK
		int const sock = ::socket(PF_ROUTE, SOCK_DGRAM, NETLINK_ROUTE);
		if (sock < 0)
		{
			ec = error_code(errno, system_category());
			return std::vector<ip_route>();
		}
		socket_closer c1(sock);

		int dgram_sock = ::socket(AF_INET, SOCK_DGRAM, 0);
		if (dgram_sock < 0)
		{
			ec = error_code(errno, system_category());
			return std::vector<ip_route>();
		}
		socket_closer c2(dgram_sock);

		struct
		{
			struct nlmsghdr hdr;
			struct rtmsg msg;
		} request{};
		request.hdr.nlmsg_len = std::uint32_t(NLMSG_LENGTH(sizeof(request.msg)));
		request.hdr.nlmsg_type = RTM_GETROUTE;
		request.msg.rtm_family = AF_UNSPEC;

		std::uint32_t seq = 0;
		if (nl_dump_request(sock, seq++, &request.hdr, [&](nlmsghdr const* msg) {
				ip_route r;
				if (parse_route(dgram_sock, msg, &r)) ret.push_back(r);
			}) != 0)
		{
			ec = error_code(errno, system_category());
			return std::vector<ip_route>();
		}
#elif TORRENT_USE_GRTTABLE

		for (int fam = 0; fam < 2; ++fam)
		{
			int const s = ::socket(fam == 0 ? AF_INET : AF_INET6, SOCK_DGRAM, 0);
			if (s < 0)
			{
				ec = error_code(errno, system_category());
				return ret;
			}
			socket_closer c1(s);
			ifconf ifc;
			ifc.ifc_len = sizeof(ifc.ifc_value);
			if (ioctl(s, SIOCGRTSIZE, &ifc, sizeof(ifc)) < 0)
			{
				ec = error_code(errno, system_category());
				return ret;
			}

			std::uint32_t const sz(ifc.ifc_value);

			std::vector<char> buf(sz);
			ifc.ifc_len = sz;
			ifc.ifc_buf = static_cast<void*>(buf.data());
			if (ioctl(s, SIOCGRTTABLE, &ifc, sizeof(ifc)) < 0)
			{
				ec = error_code(errno, system_category());
				return ret;
			}

			ifreq const* i = reinterpret_cast<ifreq const*>(ifc.ifc_buf);

			int bytes_left = static_cast<int>(ifc.ifc_len);
			while (bytes_left > 0)
			{
				route_entry const& route = i->ifr_route;

				ip_route ipr{};
				int skip = IF_NAMESIZE + sizeof(route_entry);
				if (route.destination != nullptr)
				{
					ipr.destination = sockaddr_to_address(route.destination);
					skip += route.destination->sa_len;
				}
				if (route.mask != nullptr)
				{
					ipr.netmask = sockaddr_to_address(route.mask);
					skip += route.mask->sa_len;
				}
				if (route.gateway)
				{
					ipr.gateway = sockaddr_to_address(route.gateway);
					skip += route.gateway->sa_len;
				}
				if (route.source != nullptr)
				{
					ipr.source_hint = sockaddr_to_address(route.source);
					skip += route.source->sa_len;
				}
				ipr.mtu = route.mtu;
				std::strncpy(ipr.name, i->ifr_name, sizeof(ipr.name) - 1);
				ipr.name[sizeof(ipr.name) - 1] = '\0';

				ret.push_back(ipr);
				bytes_left -= skip;
				i = reinterpret_cast<ifreq const*>(reinterpret_cast<char const*>(i) + skip);
			}
		}
#elif defined TORRENT_ANDROID && __ANDROID_API__ >= 24
		ec = boost::asio::error::operation_not_supported;
#else
#error "don't know how to enumerate network routes on this platform"
#endif
		return ret;
	}

	// returns the device name whose local address is ``addr``. If
	// no such device is found, an empty string is returned.
	std::string device_for_address(address addr, io_context& ios, error_code& ec)
	{
		std::vector<ip_interface> ifs = enum_net_interfaces(ios, ec);
		if (ec) return {};

		auto const iter = std::find_if(ifs.begin(), ifs.end()
			, [&addr](ip_interface const& iface)
			{ return iface.interface_address == addr; });
		return (iter == ifs.end()) ? std::string() : iter->name;
	}
}
