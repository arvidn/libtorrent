/*

Copyright (c) 2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/config.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/aux_/ip_helpers.hpp"

namespace lt::aux {

	bool is_ip_address(std::string const& host)
	{
		error_code ec;
		make_address(host, ec);
		return !ec;
	}

	bool is_global(address const& a)
	{
		if (a.is_v6())
		{
			// https://www.iana.org/assignments/ipv6-address-space/ipv6-address-space.xhtml
			address_v6 const a6 = a.to_v6();
			return (a6.to_bytes()[0] & 0xe0) == 0x20;
		}
		else
		{
			address_v4 const a4 = a.to_v4();
			return !(a4.is_multicast() || a4.is_unspecified() || is_local(a));
		}
	}

	bool is_link_local(address const& a)
	{
		if (a.is_v6())
		{
			address_v6 const a6 = a.to_v6();
			return a6.is_link_local()
				|| a6.is_multicast_link_local();
		}
		address_v4 const a4 = a.to_v4();
		std::uint32_t ip = a4.to_uint();
		return (ip & 0xffff0000) == 0xa9fe0000; // 169.254.x.x
	}

	bool is_local(address const& a)
	{
		TORRENT_TRY {
			if (a.is_v6())
			{
				// NOTE: site local is deprecated but by
				// https://www.ietf.org/rfc/rfc3879.txt:
				// routers SHOULD be configured to prevent
				// routing of this prefix by default.

				address_v6 const a6 = a.to_v6();
				return a6.is_loopback()
					|| a6.is_link_local()
					|| a6.is_site_local()
					|| a6.is_multicast_link_local()
					|| a6.is_multicast_site_local()
					//  fc00::/7, unique local address
					|| (a6.to_bytes()[0] & 0xfe) == 0xfc;
			}
			address_v4 a4 = a.to_v4();
			std::uint32_t const ip = a4.to_uint();
			return ((ip & 0xff000000) == 0x0a000000 // 10.x.x.x
				|| (ip & 0xfff00000) == 0xac100000 // 172.16.x.x
				|| (ip & 0xffff0000) == 0xc0a80000 // 192.168.x.x
				|| (ip & 0xffff0000) == 0xa9fe0000 // 169.254.x.x
				|| (ip & 0xff000000) == 0x7f000000); // 127.x.x.x
		} TORRENT_CATCH(std::exception const&) { return false; }
	}

	bool is_teredo(address const& addr)
	{
		TORRENT_TRY {
			if (!addr.is_v6()) return false;
			static const std::uint8_t teredo_prefix[] = {0x20, 0x01, 0, 0};
			address_v6::bytes_type b = addr.to_v6().to_bytes();
			return std::memcmp(b.data(), teredo_prefix, 4) == 0;
		} TORRENT_CATCH(std::exception const&) { return false; }
	}

	address ensure_v6(address const& a)
	{
		return a == address_v4() ? address_v6() : a;
	}
}
