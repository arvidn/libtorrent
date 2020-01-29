/*

Copyright (c) 2020, Arvid Norberg
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

#ifndef TORRENT_KEEP_ALIVE_HPP_INCLUDED
#define TORRENT_KEEP_ALIVE_HPP_INCLUDED

#if !defined _WIN32

#include "libtorrent/config.hpp"

#include <netinet/in.h> // for IPPROTO_TCP

namespace libtorrent {
namespace aux {

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
}

#endif // _WIN32

#endif

