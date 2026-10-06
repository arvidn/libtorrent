/*

Copyright (c) 2016, 2020, Arvid Norberg
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

#ifndef TORRENT_BIND_TO_DEVICE_HPP_INCLUDED
#define TORRENT_BIND_TO_DEVICE_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/error_code.hpp"

#if TORRENT_USE_IFCONF || TORRENT_USE_NETLINK || TORRENT_USE_SYSCTL
#include <sys/socket.h> // for SO_BINDTODEVICE
#include <netinet/in.h>
#endif

#ifdef TORRENT_WINDOWS
#include "libtorrent/aux_/windows.hpp"
#include "libtorrent/aux_/win_util.hpp"
#include "libtorrent/string_view.hpp"
#include <ws2tcpip.h>
// NET_LUID and NET_IFINDEX live in ifdef.h. Avoid <iphlpapi.h> for these,
// since it drags in iprtrmib.h -> mprapi.h -> wincrypt.h, which #defines
// X509_NAME to a CryptoAPI encoding-type constant and clobbers OpenSSL's
// X509_NAME type in any translation unit that also includes <openssl/x509.h>.
#include <ifdef.h>
#include <rpc.h>
#include <rpcdce.h>
#include <array>
#include <cstring>
#endif

namespace libtorrent { namespace aux {

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

#elif defined TORRENT_WINDOWS

// ConvertInterfaceGuidToLuid(), ConvertInterfaceLuidToIndex() and NET_LUID /
// NET_IFINDEX are Vista-and-later additions, not declared by the SDK headers
// when targeting earlier versions of windows
#if _WIN32_WINNT >= 0x0600

	// binding the source address alone does not constrain outbound
	// routing on multihomed Windows hosts; IP_UNICAST_IF / IPV6_UNICAST_IF
	// select the actual outgoing interface
	struct bind_to_device
	{
		// IP_UNICAST_IF takes the interface index in network byte order,
		// IPV6_UNICAST_IF takes it in host byte order, not a typo:
		// https://learn.microsoft.com/en-us/windows/win32/winsock/ipproto-ip-socket-options
		// https://learn.microsoft.com/en-us/windows/win32/winsock/ipproto-ipv6-socket-options
		// m_if_index_v4 is pre-swapped here rather than at every data() call
		bind_to_device(unsigned long if_index_v4, unsigned long if_index_v6)
			: m_if_index_v4(htonl(if_index_v4)), m_if_index_v6(if_index_v6) {}
		template<class Protocol>
		int level(Protocol const& p) const
		{ return is_v6(p) ? IPPROTO_IPV6 : IPPROTO_IP; }
		template<class Protocol>
		int name(Protocol const& p) const
		{ return is_v6(p) ? IPV6_UNICAST_IF : IP_UNICAST_IF; }
		template<class Protocol>
		char const* data(Protocol const& p) const
		{
			return is_v6(p)
				? reinterpret_cast<char const*>(&m_if_index_v6)
				: reinterpret_cast<char const*>(&m_if_index_v4);
		}
		template<class Protocol>
		size_t size(Protocol const&) const { return sizeof(m_if_index_v4); }
	private:
		template <class Protocol>
		static bool is_v6(Protocol const& p) { return p.family() == AF_INET6; }
		unsigned long m_if_index_v4;
		unsigned long m_if_index_v6;
	};

	template <typename T>
	void bind_device(T& sock, char const* device, error_code& ec)
	{
		using UuidFromStringA_t = RPC_STATUS (RPC_ENTRY *)(RPC_CSTR, UUID*);
		auto UuidFromString = get_library_procedure<rpcrt4, UuidFromStringA_t>("UuidFromStringA");
		using ConvertInterfaceGuidToLuid_t = DWORD (WINAPI *)(GUID const*, NET_LUID*);
		auto ConvertInterfaceGuidToLuid = get_library_procedure<iphlpapi, ConvertInterfaceGuidToLuid_t>(
			"ConvertInterfaceGuidToLuid");
		using ConvertInterfaceLuidToIndex_t = DWORD (WINAPI *)(NET_LUID const*, NET_IFINDEX*);
		auto ConvertInterfaceLuidToIndex = get_library_procedure<iphlpapi, ConvertInterfaceLuidToIndex_t>(
			"ConvertInterfaceLuidToIndex");

		if (UuidFromString == nullptr || ConvertInterfaceGuidToLuid == nullptr
			|| ConvertInterfaceLuidToIndex == nullptr)
		{
			ec = error_code(boost::system::errc::not_supported, generic_category());
			return;
		}

		// device names on windows are the adapter's interface GUID, formatted
		// as "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}"
		string_view name = device;
		if (name.size() == 38 && name.front() == '{' && name.back() == '}')
			name = name.substr(1, 36);

		if (name.size() != 36)
		{
			ec = error_code(boost::system::errc::no_such_device, generic_category());
			return;
		}

		std::array<char, 37> guid_str;
		std::memcpy(guid_str.data(), name.data(), name.size());
		guid_str[36] = '\0';

		UUID guid;
		// RPC_S_OK is always 0; compared as a literal since RPC_S_OK itself
		// (declared in winerror.h) isn't reliably visible with
		// WIN32_LEAN_AND_MEAN defined on all SDKs
		if (UuidFromString(reinterpret_cast<RPC_CSTR>(guid_str.data()), &guid) != 0)
		{
			ec = error_code(boost::system::errc::no_such_device, generic_category());
			return;
		}

		NET_LUID luid;
		if (ConvertInterfaceGuidToLuid(&guid, &luid) != NO_ERROR)
		{
			ec = error_code(boost::system::errc::no_such_device, generic_category());
			return;
		}

		NET_IFINDEX if_index;
		if (ConvertInterfaceLuidToIndex(&luid, &if_index) != NO_ERROR)
		{
			ec = error_code(boost::system::errc::no_such_device, generic_category());
			return;
		}

		sock.set_option(bind_to_device(if_index, if_index), ec);
	}

#define TORRENT_HAS_BINDTODEVICE 1

#else // _WIN32_WINNT >= 0x0600

#define TORRENT_HAS_BINDTODEVICE 0

#endif // _WIN32_WINNT >= 0x0600

#else

#define TORRENT_HAS_BINDTODEVICE 0

#endif

} }

#endif

