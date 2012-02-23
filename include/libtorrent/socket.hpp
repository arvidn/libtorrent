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

#include <boost/version.hpp>

#if BOOST_VERSION < 103500
#include <asio/ip/tcp.hpp>
#include <asio/ip/udp.hpp>
#include <asio/io_service.hpp>
#include <asio/deadline_timer.hpp>
#include <asio/write.hpp>
#include <asio/read.hpp>
#include <asio/time_traits.hpp>
#include <asio/basic_deadline_timer.hpp>
#else
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/time_traits.hpp>
#include <boost/asio/basic_deadline_timer.hpp>
#endif

#ifdef __OBJC__ 
#undef Protocol
#endif

#include "libtorrent/io.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/escape_string.hpp" // for to_string

#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace libtorrent
{

#if BOOST_VERSION < 103500
	using ::asio::ip::tcp;
	using ::asio::ip::udp;
	using ::asio::async_write;
	using ::asio::async_read;

	typedef ::asio::ip::tcp::socket stream_socket;
	typedef ::asio::ip::address address;
	typedef ::asio::ip::address_v4 address_v4;
	typedef ::asio::ip::address_v6 address_v6;
	typedef ::asio::ip::udp::socket datagram_socket;
	typedef ::asio::ip::tcp::acceptor socket_acceptor;
	typedef ::asio::io_service io_service;
	typedef ::asio::basic_deadline_timer<libtorrent::ptime> deadline_timer;
#else
	using boost::asio::ip::tcp;
	using boost::asio::ip::udp;
	using boost::asio::async_write;
	using boost::asio::async_read;

	typedef boost::asio::ip::tcp::socket stream_socket;
	typedef boost::asio::ip::address address;
	typedef boost::asio::ip::address_v4 address_v4;
	typedef boost::asio::ip::address_v6 address_v6;
	typedef boost::asio::ip::udp::socket datagram_socket;
	typedef boost::asio::ip::tcp::acceptor socket_acceptor;
	typedef boost::asio::io_service io_service;

	namespace asio = boost::asio;
	typedef boost::asio::basic_deadline_timer<libtorrent::ptime> deadline_timer;
#endif
	
	inline std::string print_address(address const& addr)
	{
		error_code ec;
		return addr.to_string(ec);
	}

	inline std::string print_endpoint(tcp::endpoint const& ep)
	{
		error_code ec;
		std::string ret;
		address const& addr = ep.address();
#if TORRENT_USE_IPV6
		if (addr.is_v6())
		{
			ret += '[';
			ret += addr.to_string(ec);
			ret += ']';
			ret += ':';
			ret += to_string(ep.port()).elems;
		}
		else
#endif
		{
			ret += addr.to_string(ec);
			ret += ':';
			ret += to_string(ep.port()).elems;
		}
		return ret;
	}

	inline std::string print_endpoint(udp::endpoint const& ep)
	{
		return print_endpoint(tcp::endpoint(ep.address(), ep.port()));
	}

	namespace detail
	{
		template<class OutIt>
		void write_address(address const& a, OutIt& out)
		{
#if TORRENT_USE_IPV6
			if (a.is_v4())
			{
#endif
				write_uint32(a.to_v4().to_ulong(), out);
#if TORRENT_USE_IPV6
			}
			else if (a.is_v6())
			{
				typedef address_v6::bytes_type bytes_t;
				bytes_t bytes = a.to_v6().to_bytes();
				for (bytes_t::iterator i = bytes.begin()
					, end(bytes.end()); i != end; ++i)
					write_uint8(*i, out);
			}
#endif
		}

		template<class InIt>
		address read_v4_address(InIt& in)
		{
			unsigned long ip = read_uint32(in);
			return address_v4(ip);
		}

#if TORRENT_USE_IPV6
		template<class InIt>
		address read_v6_address(InIt& in)
		{
			typedef address_v6::bytes_type bytes_t;
			bytes_t bytes;
			for (bytes_t::iterator i = bytes.begin()
				, end(bytes.end()); i != end; ++i)
				*i = read_uint8(in);
			return address_v6(bytes);
		}
#endif

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

#if TORRENT_USE_IPV6
		template<class Endpoint, class InIt>
		Endpoint read_v6_endpoint(InIt& in)
		{
			address addr = read_v6_address(in);
			int port = read_uint16(in);
			return Endpoint(addr, port);
		}
#endif
	}

#if TORRENT_USE_IPV6
	struct v6only
	{
		v6only(bool enable): m_value(enable) {}
		template<class Protocol>
		int level(Protocol const&) const { return IPPROTO_IPV6; }
		template<class Protocol>
		int name(Protocol const&) const { return IPV6_V6ONLY; }
		template<class Protocol>
		int const* data(Protocol const&) const { return &m_value; }
		template<class Protocol>
		size_t size(Protocol const&) const { return sizeof(m_value); }
		int m_value;
	};
#endif
	
#ifdef TORRENT_WINDOWS

#ifndef IPV6_PROTECTION_LEVEL
#define IPV6_PROTECTION_LEVEL 30
#endif
	struct v6_protection_level
	{
		v6_protection_level(int level): m_value(level) {}
		template<class Protocol>
		int level(Protocol const&) const { return IPPROTO_IPV6; }
		template<class Protocol>
		int name(Protocol const&) const { return IPV6_PROTECTION_LEVEL; }
		template<class Protocol>
		int const* data(Protocol const&) const { return &m_value; }
		template<class Protocol>
		size_t size(Protocol const&) const { return sizeof(m_value); }
		int m_value;
	};
#endif

	struct type_of_service
	{
#ifdef WIN32
		typedef DWORD tos_t;
#else
		typedef int tos_t;
#endif
		type_of_service(char val): m_value(val) {}
		template<class Protocol>
		int level(Protocol const&) const { return IPPROTO_IP; }
		template<class Protocol>
		int name(Protocol const&) const { return IP_TOS; }
		template<class Protocol>
		tos_t const* data(Protocol const&) const { return &m_value; }
		template<class Protocol>
		size_t size(Protocol const&) const { return sizeof(m_value); }
		tos_t m_value;
	};
}

#endif // TORRENT_SOCKET_HPP_INCLUDED

