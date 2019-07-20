/*

Copyright (c) 2009-2018, Arvid Norberg
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

#ifndef TORRENT_IO_SERVICE_FWD_HPP_INCLUDED
#define TORRENT_IO_SERVICE_FWD_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include <boost/version.hpp>

#if defined TORRENT_BUILD_SIMULATOR
namespace sim { namespace asio {

	struct io_service;
}}
#else
namespace boost { namespace asio {
#if BOOST_VERSION < 106600
	class io_service;
#else
	class io_context;
	typedef io_context io_service;
#endif
}}
#endif

namespace libtorrent {

#if defined TORRENT_BUILD_SIMULATOR
	using io_service = sim::asio::io_service;
#else
	using io_service = boost::asio::io_service;
#endif

#if BOOST_VERSION >= 107000 && !defined TORRENT_BUILD_SIMULATOR
template <typename T>
io_service& get_io_service(T& o) { return static_cast<io_service&>(o.get_executor().context()); }
#else
template <typename T>
io_service& get_io_service(T& o) { return o.get_io_service(); }
#endif

}

#endif
