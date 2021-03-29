/*

Copyright (c) 2010, 2014-2017, 2020, Arvid Norberg
Copyright (c) 2015, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_BYTESWAP_HPP_INCLUDED
#define TORRENT_BYTESWAP_HPP_INCLUDED

// this header makes sure htonl(), nothl(), htons() and ntohs()
// are available

#include "libtorrent/config.hpp"

#include <cstdint>

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/predef/other/endian.h>

#ifdef TORRENT_WINDOWS
#include <winsock2.h>
#else
// posix header
// for ntohl and htonl
#include <arpa/inet.h>
#endif

namespace lt::aux {
// these need to be within the disabled warnings because on OSX
// the htonl and ntohl macros cause lots of old-style case warnings
inline std::uint32_t host_to_network(std::uint32_t x)
{ return htonl(x); }

inline std::uint32_t network_to_host(std::uint32_t x)
{ return ntohl(x); }

inline std::uint16_t host_to_network(std::uint16_t x)
{ return htons(x); }

inline std::uint16_t network_to_host(std::uint16_t x)
{ return ntohs(x); }

#include "libtorrent/aux_/disable_warnings_pop.hpp"

inline std::uint32_t swap_byteorder(std::uint32_t const x)
{
#ifdef __GNUC__
	return __builtin_bswap32(x);
#else
	return (x & 0xff000000) >> 24
		| (x & 0x00ff0000) >> 8
		| (x & 0x0000ff00) << 8
		| (x & 0x000000ff) << 24;
#endif
}

inline std::uint32_t little_endian_to_host(std::uint32_t x)
{
#if BOOST_ENDIAN_BIG_BYTE
	return swap_byteorder(x);
#elif BOOST_ENDIAN_LITTLE_BYTE
	return x;
#else
#error "unknown endian"
#endif
}

}

#endif // TORRENT_BYTESWAP_HPP_INCLUDED

