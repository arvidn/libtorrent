/*

Copyright (c) 2003-2016, Arvid Norberg
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

#if defined TORRENT_WINDOWS || defined TORRENT_CYGWIN
// asio assumes that the windows error codes are defined already
#include <winsock2.h>
#endif

#include <boost/version.hpp>

#if BOOST_VERSION < 103500
#include <asio/ip/tcp.hpp>
#include <asio/ip/udp.hpp>
#include <asio/write.hpp>
#include <asio/read.hpp>
#else
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>
#endif

#ifdef __OBJC__ 
#undef Protocol
#endif

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
	typedef ::asio::ip::udp::socket datagram_socket;
	typedef ::asio::ip::tcp::acceptor socket_acceptor;
#else
	using boost::asio::ip::tcp;
	using boost::asio::ip::udp;
	using boost::asio::async_write;
	using boost::asio::async_read;

	typedef boost::asio::ip::tcp::socket stream_socket;
	typedef boost::asio::ip::udp::socket datagram_socket;
	typedef boost::asio::ip::tcp::acceptor socket_acceptor;

	namespace asio = boost::asio;
#endif

#ifdef TORRENT_WINDOWS

#ifndef PROTECTION_LEVEL_UNRESTRICTED
#define PROTECTION_LEVEL_UNRESTRICTED 10
#endif

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

	struct exclusive_address_use
	{
		exclusive_address_use(int enable): m_value(enable) {}
		template<class Protocol>
		int level(Protocol const&) const { return SOL_SOCKET; }
		template<class Protocol>
		int name(Protocol const&) const { return SO_EXCLUSIVEADDRUSE; }
		template<class Protocol>
		int const* data(Protocol const&) const { return &m_value; }
		template<class Protocol>
		size_t size(Protocol const&) const { return sizeof(m_value); }
		int m_value;
	};
#endif // TORRENT_WINDOWS

#ifdef IPV6_TCLASS
	struct traffic_class
	{
		traffic_class(char val): m_value(val) {}
		template<class Protocol>
		int level(Protocol const&) const { return IPPROTO_IPV6; }
		template<class Protocol>
		int name(Protocol const&) const { return IPV6_TCLASS; }
		template<class Protocol>
		int const* data(Protocol const&) const { return &m_value; }
		template<class Protocol>
		size_t size(Protocol const&) const { return sizeof(m_value); }
		int m_value;
	};
#endif

	struct type_of_service
	{
#ifdef _WIN32
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

#if defined IP_DONTFRAG || defined IP_MTU_DISCOVER || defined IP_DONTFRAGMENT
#define TORRENT_HAS_DONT_FRAGMENT
#endif

#ifdef TORRENT_HAS_DONT_FRAGMENT
	struct dont_fragment
	{
		dont_fragment(bool val)
#ifdef IP_PMTUDISCOVER_DO
			: m_value(val ? IP_PMTUDISC_DO : IP_PMTUDISC_DONT) {}
#else
			: m_value(val) {}
#endif
		template<class Protocol>
		int level(Protocol const&) const { return IPPROTO_IP; }
		template<class Protocol>
		int name(Protocol const&) const
#if defined IP_DONTFRAG
		{ return IP_DONTFRAG; }
#elif defined IP_MTU_DISCOVER
		{ return IP_MTU_DISCOVER; }
#elif defined IP_DONTFRAGMENT
		{ return IP_DONTFRAGMENT; }
#else
		{}
#endif
		template<class Protocol>
		int const* data(Protocol const&) const { return &m_value; }
		template<class Protocol>
		size_t size(Protocol const&) const { return sizeof(m_value); }
		int m_value;
	};
#endif // TORRENT_HAS_DONT_FRAGMENT
}

#endif // TORRENT_SOCKET_HPP_INCLUDED

