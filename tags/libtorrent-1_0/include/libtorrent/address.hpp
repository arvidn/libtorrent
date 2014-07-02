/*

Copyright (c) 2009-2014, Arvid Norberg
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

#ifndef TORRENT_ADDRESS_HPP_INCLUDED
#define TORRENT_ADDRESS_HPP_INCLUDED

#include <boost/version.hpp>
#include "libtorrent/config.hpp"

#ifdef __OBJC__
#define Protocol Protocol_
#endif

#if defined TORRENT_WINDOWS || defined TORRENT_CYGWIN
// asio assumes that the windows error codes are defined already
#include <winsock2.h>
#endif

#if BOOST_VERSION < 103500
#include <asio/ip/address.hpp>
#else
#include <boost/asio/ip/address.hpp>
#endif

#ifdef __OBJC__ 
#undef Protocol
#endif

namespace libtorrent
{

#if BOOST_VERSION < 103500
	typedef ::asio::ip::address address;
	typedef ::asio::ip::address_v4 address_v4;
#if TORRENT_USE_IPV6
	typedef ::asio::ip::address_v6 address_v6;
#endif
#else
	typedef boost::asio::ip::address address;
	typedef boost::asio::ip::address_v4 address_v4;
#if TORRENT_USE_IPV6
	typedef boost::asio::ip::address_v6 address_v6;
#endif
#endif
}

#endif

