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

#include <asio/ip/tcp.hpp>
#include <asio/ip/udp.hpp>
#include <asio/io_service.hpp>
#include <asio/deadline_timer.hpp>
#include <asio/write.hpp>
#include <asio/strand.hpp>

#ifdef __OBJC__ 
#undef Protocol
#endif

#include "libtorrent/io.hpp"

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
	using boost::asio::io_service;
	using boost::asio::ipv4::host_resolver;
	using boost::asio::async_write;
	using boost::asio::ipv4::host;
	using boost::asio::deadline_timer;
*/
//	namespace asio = ::asio;

	using asio::ip::tcp;
	using asio::ip::udp;
	typedef asio::ip::tcp::socket stream_socket;
	typedef asio::ip::address address;
	typedef asio::ip::address_v4 address_v4;
	typedef asio::ip::address_v6 address_v6;
	typedef asio::ip::udp::socket datagram_socket;
	typedef asio::ip::tcp::acceptor socket_acceptor;
	typedef asio::io_service io_service;

	using asio::async_write;
	using asio::deadline_timer;
	
	namespace detail
	{
		template<class OutIt>
		void write_address(address const& a, OutIt& out)
		{
			if (a.is_v4())
			{
				write_uint32(a.to_v4().to_ulong(), out);
			}
			else if (a.is_v6())
			{
				asio::ip::address_v6::bytes_type bytes
					= a.to_v6().to_bytes();
				std::copy(bytes.begin(), bytes.end(), out);
			}
		}

		template<class InIt>
		address read_v4_address(InIt& in)
		{
			unsigned long ip = read_uint32(in);
			return asio::ip::address_v4(ip);
		}

		template<class InIt>
		address read_v6_address(InIt& in)
		{
			typedef asio::ip::address_v6::bytes_type bytes_t;
			bytes_t bytes;
			for (bytes_t::iterator i = bytes.begin()
				, end(bytes.end()); i != end; ++i)
				*i = read_uint8(in);
			return asio::ip::address_v6(bytes);
		}

		template<class Endpoint, class OutIt>
		void write_endpoint(Endpoint const& e, OutIt& out)
		{
			write_address(e.address(), out);
			write_uint16(e.port(), out);
		}

		template<class Endpoint, class InIt>
		Endpoint read_v4_endpoint(InIt& in)
		{
			address addr = read_v4_address(in);
			int port = read_uint16(in);
			return Endpoint(addr, port);
		}

		template<class Endpoint, class InIt>
		Endpoint read_v6_endpoint(InIt& in)
		{
			address addr = read_v6_address(in);
			int port = read_uint16(in);
			return Endpoint(addr, port);
		}
	}
}

#endif // TORRENT_SOCKET_HPP_INCLUDED

