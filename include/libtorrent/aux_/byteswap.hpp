/*

Copyright (c) 2013-2015, Arvid Norberg
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

#ifndef TORRENT_BYTESWAP_HPP_INCLUDED
#define TORRENT_BYTESWAP_HPP_INCLUDED

// this header makes sure htonl(), nothl(), htons() and ntohs()
// are available

#include "libtorrent/config.hpp"

#include <cstdint>

#include "libtorrent/aux_/disable_warnings_push.hpp"

#ifdef TORRENT_WINDOWS
#include <winsock2.h>
#else
// posix header
// for ntohl and htonl
#include <arpa/inet.h>
#endif

namespace libtorrent { namespace aux {
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

}
}

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#endif // TORRENT_BYTESWAP_HPP_INCLUDED

