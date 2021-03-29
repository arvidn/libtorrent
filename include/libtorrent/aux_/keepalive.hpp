/*

Copyright (c) 2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_KEEP_ALIVE_HPP_INCLUDED
#define TORRENT_KEEP_ALIVE_HPP_INCLUDED

#if !defined _WIN32

#include "libtorrent/config.hpp"

#include <netinet/in.h> // for IPPROTO_TCP

namespace lt::aux {

#if defined TCP_KEEPIDLE
#define TORRENT_HAS_KEEPALIVE_IDLE
	struct tcp_keepalive_idle
	{
		explicit tcp_keepalive_idle(int seconds): m_value(seconds) {}
		template<class Protocol>
		int level(Protocol const&) const { return IPPROTO_TCP; }
		template<class Protocol>
		int name(Protocol const&) const { return TCP_KEEPIDLE; }
		template<class Protocol>
		char const* data(Protocol const&) const { return reinterpret_cast<char const*>(&m_value); }
		template<class Protocol>
		size_t size(Protocol const&) const { return sizeof(m_value); }
	private:
		int m_value;
	};
#elif defined TCP_KEEPALIVE
#define TORRENT_HAS_KEEPALIVE_IDLE
	struct tcp_keepalive_idle
	{
		explicit tcp_keepalive_idle(int seconds): m_value(seconds) {}
		template<class Protocol>
		int level(Protocol const&) const { return IPPROTO_TCP; }
		template<class Protocol>
		int name(Protocol const&) const { return TCP_KEEPALIVE; }
		template<class Protocol>
		char const* data(Protocol const&) const { return reinterpret_cast<char const*>(&m_value); }
		template<class Protocol>
		size_t size(Protocol const&) const { return sizeof(m_value); }
	private:
		int m_value;
	};
#endif

#ifdef TCP_KEEPINTVL
#define TORRENT_HAS_KEEPALIVE_INTERVAL
	struct tcp_keepalive_interval
	{
		explicit tcp_keepalive_interval(int seconds): m_value(seconds) {}
		template<class Protocol>
		int level(Protocol const&) const { return IPPROTO_TCP; }
		template<class Protocol>
		int name(Protocol const&) const { return TCP_KEEPINTVL; }
		template<class Protocol>
		char const* data(Protocol const&) const { return reinterpret_cast<char const*>(&m_value); }
		template<class Protocol>
		size_t size(Protocol const&) const { return sizeof(m_value); }
	private:
		int m_value;
	};
#endif
}

#endif // _WIN32

#endif

