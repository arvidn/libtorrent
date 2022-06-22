/*

Copyright (c) 2003-2004, 2006-2010, 2012, 2014-2022, Arvid Norberg
Copyright (c) 2017, Alden Torres
Copyright (c) 2018, Alexandre Janniaux
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

#include "libtorrent/config.hpp"
#include "libtorrent/aux_/noexcept_movable.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

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

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>

#ifdef __OBJC__
#undef Protocol
#endif

#if defined TORRENT_BUILD_SIMULATOR
#include "simulator/simulator.hpp"
#endif

#if TORRENT_USE_NETLINK
#include <linux/netlink.h>
#ifndef SOL_NETLINK
#define SOL_NETLINK 270
#endif

// NETLINK_NO_ENOBUFS exists at least since android 2.3, but is not exposed
#if defined TORRENT_ANDROID && !defined NETLINK_NO_ENOBUFS
#define NETLINK_NO_ENOBUFS 5
#endif
#endif

#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent {

#if defined TORRENT_BUILD_SIMULATOR
struct tcp : sim::asio::ip::tcp {
	tcp(sim::asio::ip::tcp const& p) : sim::asio::ip::tcp(p) {} // NOLINT
	using socket = aux::noexcept_move_only<sim::asio::ip::tcp::socket>;
};
struct udp : sim::asio::ip::udp {
	udp(sim::asio::ip::udp const& p) : sim::asio::ip::udp(p) {} // NOLINT
	using socket = aux::noexcept_move_only<sim::asio::ip::udp::socket>;
};
	using sim::asio::async_write;
	using sim::asio::async_read;
	using true_tcp_socket = sim::asio::ip::tcp::socket;
#else
struct tcp : boost::asio::ip::tcp {
	tcp(boost::asio::ip::tcp const& p) : boost::asio::ip::tcp(p) {} // NOLINT
	using socket = aux::noexcept_move_only<boost::asio::ip::tcp::socket>;
};
struct udp : boost::asio::ip::udp {
	udp(boost::asio::ip::udp const& p) : boost::asio::ip::udp(p) {} // NOLINT
	using socket = aux::noexcept_move_only<boost::asio::ip::udp::socket>;
};
	using boost::asio::async_write;
	using boost::asio::async_read;
	using true_tcp_socket = boost::asio::ip::tcp::socket;
#endif

	// internal
	inline udp::endpoint make_udp(tcp::endpoint const ep)
	{ return {ep.address(), ep.port()}; }

	// internal
	inline tcp::endpoint make_tcp(udp::endpoint const ep)
	{ return {ep.address(), ep.port()}; }

#ifdef TORRENT_WINDOWS

#ifndef PROTECTION_LEVEL_UNRESTRICTED
#define PROTECTION_LEVEL_UNRESTRICTED 10
#endif

#ifndef IPV6_PROTECTION_LEVEL
#define IPV6_PROTECTION_LEVEL 30
#endif

	struct v6_protection_level
	{
		explicit v6_protection_level(int level): m_value(level) {}
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
		explicit exclusive_address_use(int enable): m_value(enable) {}
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
		explicit traffic_class(int val): m_value(val) {}
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
		using tos_t = DWORD;
#else
		using tos_t = int;
#endif
		explicit type_of_service(tos_t const val) : m_value(tos_t(val)) {}
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

#ifdef IP_DSCP_TRAFFIC_TYPE
	struct dscp_traffic_type
	{
		explicit dscp_traffic_type(DWORD val) : m_value(val) {}
		template<class Protocol>
		int level(Protocol const&) const { return IP_DSCP_TRAFFIC_TYPE; }
		template<class Protocol>
		int name(Protocol const&) const { return DSCP_TRAFFIC_TYPE; }
		template<class Protocol>
		DWORD const* data(Protocol const&) const { return &m_value; }
		template<class Protocol>
		size_t size(Protocol const&) const { return sizeof(m_value); }
		DWORD m_value;
	};
#endif

#if defined IP_DONTFRAG || defined IP_MTU_DISCOVER || defined IP_DONTFRAGMENT
#define TORRENT_HAS_DONT_FRAGMENT
#endif

#ifdef TORRENT_HAS_DONT_FRAGMENT

	// the order of these preprocessor tests matters. Windows defines both
	// IP_DONTFRAGMENT and IP_MTU_DISCOVER, but the latter is not supported
	// in general, the simple option of just setting the DF bit is preferred, if
	// it's available
#if defined IP_DONTFRAG || defined IP_DONTFRAGMENT

	struct dont_fragment
	{
		explicit dont_fragment(bool val) : m_value(val) {}
		template<class Protocol>
		int level(Protocol const&) const { return IPPROTO_IP; }
		template<class Protocol>
		int name(Protocol const&) const
#if defined IP_DONTFRAG
		{ return IP_DONTFRAG; }
#else // defined IP_DONTFRAGMENT
		{ return IP_DONTFRAGMENT; }
#endif
		template<class Protocol>
		int const* data(Protocol const&) const { return &m_value; }
		template<class Protocol>
		size_t size(Protocol const&) const { return sizeof(m_value); }
		int m_value;
	};

#else

	// this is the fallback mechanism using the IP_MTU_DISCOVER option, which
	// does a little bit more than we want, it makes the kernel track an estimate
	// of the MTU and rejects packets immediately if they are believed to exceed
	// it.
	struct dont_fragment
	{
		explicit dont_fragment(bool val)
			: m_value(val ? IP_PMTUDISC_PROBE : IP_PMTUDISC_DONT) {}
		template<class Protocol>
		int level(Protocol const&) const { return IPPROTO_IP; }
		template<class Protocol>
		int name(Protocol const&) const { return IP_MTU_DISCOVER; }
		template<class Protocol>
		int const* data(Protocol const&) const { return &m_value; }
		template<class Protocol>
		size_t size(Protocol const&) const { return sizeof(m_value); }
		int m_value;
	};

#endif // IP_DONTFRAG vs. IP_MTU_DISCOVER

#endif // TORRENT_HAS_DONT_FRAGMENT

#if TORRENT_USE_NETLINK
	struct no_enobufs
	{
		explicit no_enobufs(bool val) : m_value(val) {}
		template<class Protocol>
		int level(Protocol const&) const { return SOL_NETLINK; }
		template<class Protocol>
		int name(Protocol const&) const { return NETLINK_NO_ENOBUFS; }
		template<class Protocol>
		int const* data(Protocol const&) const { return &m_value; }
		template<class Protocol>
		std::size_t size(Protocol const&) const { return sizeof(m_value); }
		int m_value;
	};
#endif // TORRENT_USE_NETLINK

#ifdef TCP_NOTSENT_LOWAT
	struct tcp_notsent_lowat
	{
		explicit tcp_notsent_lowat(int val) : m_value(val) {}
		template<class Protocol>
		int level(Protocol const&) const { return IPPROTO_TCP; }
		template<class Protocol>
		int name(Protocol const&) const { return TCP_NOTSENT_LOWAT; }
		template<class Protocol>
		int const* data(Protocol const&) const { return &m_value; }
		template<class Protocol>
		std::size_t size(Protocol const&) const { return sizeof(m_value); }
		int m_value;
	};
#endif
}

#endif // TORRENT_SOCKET_HPP_INCLUDED
