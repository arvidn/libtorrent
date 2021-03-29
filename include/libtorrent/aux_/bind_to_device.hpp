/*

Copyright (c) 2016, 2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_BIND_TO_DEVICE_HPP_INCLUDED
#define TORRENT_BIND_TO_DEVICE_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/error_code.hpp"

#if TORRENT_USE_IFCONF || TORRENT_USE_NETLINK || TORRENT_USE_SYSCTL
#include <sys/socket.h> // for SO_BINDTODEVICE
#include <netinet/in.h>
#endif

namespace lt::aux {

#if defined SO_BINDTODEVICE

	struct bind_to_device
	{
		explicit bind_to_device(char const* device): m_value(device) {}
		template<class Protocol>
		int level(Protocol const&) const { return SOL_SOCKET; }
		template<class Protocol>
		int name(Protocol const&) const { return SO_BINDTODEVICE; }
		template<class Protocol>
		char const* data(Protocol const&) const { return m_value; }
		template<class Protocol>
		size_t size(Protocol const&) const { return strlen(m_value) + 1; }
	private:
		char const* m_value;
	};

	template <typename T>
	void bind_device(T& sock, char const* device, error_code& ec)
	{
		sock.set_option(bind_to_device(device), ec);
	}

#define TORRENT_HAS_BINDTODEVICE 1

#elif defined IP_BOUND_IF

	struct bind_to_device
	{
		explicit bind_to_device(unsigned int idx): m_value(idx) {}
		template<class Protocol>
		int level(Protocol const&) const { return IPPROTO_IP; }
		template<class Protocol>
		int name(Protocol const&) const { return IP_BOUND_IF; }
		template<class Protocol>
		char const* data(Protocol const&) const { return reinterpret_cast<char const*>(&m_value); }
		template<class Protocol>
		size_t size(Protocol const&) const { return sizeof(m_value); }
	private:
		unsigned int m_value;
	};

	template <typename T>
	void bind_device(T& sock, char const* device, error_code& ec)
	{
		unsigned int const if_idx = if_nametoindex(device);
		if (if_idx == 0)
		{
			ec.assign(errno, system_category());
			return;
		}
		sock.set_option(bind_to_device(if_idx), ec);
	}

#define TORRENT_HAS_BINDTODEVICE 1

#elif defined IP_FORCE_OUT_IFP

	struct bind_to_device
	{
		explicit bind_to_device(char const* device): m_value(device) {}
		template<class Protocol>
		int level(Protocol const&) const { return SOL_SOCKET; }
		template<class Protocol>
		int name(Protocol const&) const { return IP_FORCE_OUT_IFP; }
		template<class Protocol>
		char const* data(Protocol const&) const { return m_value; }
		template<class Protocol>
		size_t size(Protocol const&) const { return strlen(m_value) + 1; }
	private:
		char const* m_value;
	};

	template <typename T>
	void bind_device(T& sock, char const* device, error_code& ec)
	{
		sock.set_option(bind_to_device(device), ec);
	}

#define TORRENT_HAS_BINDTODEVICE 1

#else

#define TORRENT_HAS_BINDTODEVICE 0

#endif

}

#endif

