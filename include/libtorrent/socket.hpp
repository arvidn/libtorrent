/*

Copyright (c) 2003, Arvid Norberg
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

#ifndef TORRENT_SOCKET_HPP_INCLUDED
#define TORRENT_SOCKET_HPP_INCLUDED

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

// if building as Objective C++, asio's template
// parameters Protocol has to be renamed to avoid
// colliding with keywords

#ifdef __OBJC__
#define Protocol Protocol_
#endif

#include <asio.hpp>

#ifdef __OBJC__ 
#undef Protocol
#endif

#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace libtorrent
{
/*
	namespace asio = boost::asio;

	using boost::asio::ipv4::tcp;
	using boost::asio::ipv4::address;
	using boost::asio::stream_socket;
	using boost::asio::datagram_socket;
	using boost::asio::socket_acceptor;
	using boost::asio::demuxer;
	using boost::asio::ipv4::host_resolver;
	using boost::asio::async_write;
	using boost::asio::ipv4::host;
	using boost::asio::deadline_timer;
*/
	namespace asio = ::asio;

	using asio::ipv4::tcp;
	using asio::ipv4::udp;
	typedef asio::ipv4::tcp::socket stream_socket;
	using asio::ipv4::address;
	typedef asio::ipv4::udp::socket datagram_socket;
	typedef asio::ipv4::tcp::acceptor socket_acceptor;
	typedef asio::io_service demuxer;
	using asio::ipv4::host_resolver;
	using asio::ipv4::host;

	using asio::async_write;
	using asio::deadline_timer;
}

#endif // TORRENT_SOCKET_HPP_INCLUDED

