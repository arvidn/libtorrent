/*

Copyright (c) 2009-2016, Arvid Norberg
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

#include "libtorrent/config.hpp"

#if defined TORRENT_BUILD_SIMULATOR
#include "simulator/simulator.hpp"
#else
#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/asio/ip/address.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"
#endif // SIMULATOR

namespace libtorrent {

#if defined TORRENT_BUILD_SIMULATOR
	using address = sim::asio::ip::address;
	using address_v4 = sim::asio::ip::address_v4;
#if TORRENT_USE_IPV6
	using address_v6 = sim::asio::ip::address_v6;
#endif
#else
	using address = boost::asio::ip::address;
	using address_v4 = boost::asio::ip::address_v4;
#if TORRENT_USE_IPV6
	using address_v6 = boost::asio::ip::address_v6;
#endif
#endif // SIMULATOR
}

#endif
