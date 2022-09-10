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

#ifndef TORRENT_NETLINK_UTILS_HPP_INCLUDED
#define TORRENT_NETLINK_UTILS_HPP_INCLUDED

#include "libtorrent/config.hpp"

#if TORRENT_USE_NETLINK

#include <linux/netlink.h>
#include <linux/rtnetlink.h>

namespace libtorrent {
namespace aux {

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wcast-qual"
#pragma clang diagnostic ignored "-Wcast-align"
#endif
	// these are here to concentrate all the shady casts these macros expand to,
	// to disable the warnings for them all
	inline bool nlmsg_ok(nlmsghdr const* hdr, int const len)
	{ return NLMSG_OK(hdr, len); }

	inline nlmsghdr const* nlmsg_next(nlmsghdr const* hdr, int& len)
	{ return NLMSG_NEXT(hdr, len); }

	inline void const* nlmsg_data(nlmsghdr const* hdr)
	{ return NLMSG_DATA(hdr); }

	inline rtattr const* rtm_rta(rtmsg const* hdr)
	{ return static_cast<rtattr const*>(RTM_RTA(hdr)); }

	inline std::size_t rtm_payload(nlmsghdr const* hdr)
	{ return RTM_PAYLOAD(hdr); }

	inline bool rta_ok(rtattr const* rt, std::size_t const len)
	{ return RTA_OK(rt, len); }

	inline void const* rta_data(rtattr const* rt)
	{ return RTA_DATA(rt); }

	inline rtattr const* rta_next(rtattr const* rt, std::size_t& len)
	{ return RTA_NEXT(rt, len); }

	inline rtattr const* ifa_rta(ifaddrmsg const* ifa)
	{ return static_cast<rtattr const*>(IFA_RTA(ifa)); }

	inline std::size_t ifa_payload(nlmsghdr const* hdr)
	{ return IFA_PAYLOAD(hdr); }

	inline rtattr const* ifla_rta(ifinfomsg const* ifinfo)
	{ return static_cast<rtattr const*>(IFLA_RTA(ifinfo)); }

	inline std::size_t ifla_payload(nlmsghdr const* hdr)
	{ return IFLA_PAYLOAD(hdr); }
#ifdef __clang__
#pragma clang diagnostic pop
#endif

}
}

#endif // TORRENT_USE_NETLINK

#endif

