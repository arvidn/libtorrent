/*

Copyright (c) 2007, 2009-2010, 2013-2020, Arvid Norberg
Copyright (c) 2016, Alden Torres
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

#ifndef TORRENT_SOCKS5_STREAM_HPP_INCLUDED
#define TORRENT_SOCKS5_STREAM_HPP_INCLUDED

#include <functional>

#include "libtorrent/proxy_base.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/aux_/ip_helpers.hpp" // for is_ip_address
#include "libtorrent/assert.hpp"
#include "libtorrent/debug.hpp"
#include "libtorrent/string_util.hpp" // for to_string
#include "libtorrent/socket_io.hpp"

namespace libtorrent {

namespace socks_error {

	// SOCKS5 error values. If an error_code has the
	// socks error category (get_socks_category()), these
	// are the error values.
	enum socks_error_code
	{
		no_error = 0,
		unsupported_version,
		unsupported_authentication_method,
		unsupported_authentication_version,
		authentication_error,
		username_required,
		general_failure,
		command_not_supported,
		no_identd,
		identd_error,

		num_errors
	};

	// internal
	TORRENT_EXPORT boost::system::error_code make_error_code(socks_error_code e);

} // namespace socks_error
}

namespace boost {
namespace system {

	template<> struct is_error_code_enum<libtorrent::socks_error::socks_error_code>
	{ static const bool value = true; };

}
}

namespace libtorrent {

// returns the error_category for SOCKS5 errors
TORRENT_EXPORT boost::system::error_category& socks_category();

#if TORRENT_ABI_VERSION == 1
TORRENT_DEPRECATED
inline boost::system::error_category& get_socks_category()
{ return socks_category(); }
#endif

class socks5_stream : public proxy_base
{
public:

	// commands
	enum {
		socks5_connect = 1,
		socks5_udp_associate = 3
	};

	socks5_stream(socks5_stream&&) = default;
	explicit socks5_stream(io_context& io_context)
		: proxy_base(io_context)
		, m_version(5)
		, m_command(socks5_connect)
	{}

	void set_version(int v) { m_version = v; }

	void set_command(int c)
	{
		TORRENT_ASSERT(c == socks5_connect || c == socks5_udp_associate);
		m_command = c;
	}

	void set_username(std::string const& user
		, std::string const& password)
	{
		m_user = user;
		m_password = password;
	}

	void set_dst_name(std::string const& host)
	{
		// if this assert trips, set_dst_name() is called wth an IP address rather
		// than a hostname. Instead, resolve the IP into an address and pass it to
		// async_connect instead
		TORRENT_ASSERT(!aux::is_ip_address(host));
		m_dst_name = host;
		if (m_dst_name.size() > 255)
			m_dst_name.resize(255);
	}

	void close(error_code& ec)
	{
		m_dst_name.clear();
		proxy_base::close(ec);
	}

#ifndef BOOST_NO_EXCEPTIONS
	void close()
	{
		m_dst_name.clear();
		proxy_base::close();
	}
#endif

	// TODO: 2 add async_connect() that takes a hostname and port as well
	template <class Handler>
	void async_connect(endpoint_type const& endpoint, Handler handler)
	{
		// make sure we don't try to connect to INADDR_ANY. binding is fine,
		// and using a hostname is fine on SOCKS version 5.
		TORRENT_ASSERT(endpoint.address() != address()
			|| (!m_dst_name.empty() && m_version == 5));

		m_remote_endpoint = endpoint;

		// the connect is split up in the following steps:
		// 1. resolve name of proxy server
		// 2. connect to proxy server
		// 3. if version == 5:
		//   3.1 send SOCKS5 authentication method message
		//   3.2 read SOCKS5 authentication response
		//   3.3 send username+password
		// 4. send SOCKS command message

		ADD_OUTSTANDING_ASYNC("socks5_stream::name_lookup");
		m_resolver.async_resolve(m_hostname, to_string(m_port).data(), wrap_allocator(
			[this](error_code const& ec, tcp::resolver::results_type ips, Handler hn) {
				name_lookup(ec, std::move(ips), std::move(hn));
			}, std::move(handler)));
	}

private:

	template <typename Handler>
	void name_lookup(error_code const& e, tcp::resolver::results_type ips
		, Handler h)
	{
		COMPLETE_ASYNC("socks5_stream::name_lookup");
		if (handle_error(e, std::move(h))) return;

		auto i = ips.begin();
		if (!m_sock.is_open())
		{
			error_code ec;
			m_sock.open(i->endpoint().protocol(), ec);
			if (handle_error(ec, std::move(h))) return;
		}

		// TODO: we could bind the socket here, since we know what the
		// target endpoint is of the proxy
		ADD_OUTSTANDING_ASYNC("socks5_stream::connected");
		m_sock.async_connect(i->endpoint(), wrap_allocator(
			[this](error_code const& ec, Handler hn)
			{ connected(ec, std::move(hn)); }, std::move(h)));
	}

	template <typename Handler>
	void connected(error_code const& e, Handler h)
	{
		COMPLETE_ASYNC("socks5_stream::connected");
		if (handle_error(e, std::move(h))) return;

		using namespace libtorrent::aux;
		if (m_version == 5)
		{
			// send SOCKS5 authentication methods
			m_buffer.resize(m_user.empty()?3:4);
			char* p = &m_buffer[0];
			write_uint8(5, p); // SOCKS VERSION 5
			if (m_user.empty())
			{
				write_uint8(1, p); // 1 authentication method (no auth)
				write_uint8(0, p); // no authentication
			}
			else
			{
				write_uint8(2, p); // 2 authentication methods
				write_uint8(0, p); // no authentication
				write_uint8(2, p); // username/password
			}
			ADD_OUTSTANDING_ASYNC("socks5_stream::handshake1");
			async_write(m_sock, boost::asio::buffer(m_buffer), wrap_allocator(
				[this](error_code const& ec, std::size_t, Handler hn) {
					handshake1(ec, std::move(hn));
				}, std::move(h)));
		}
		else if (m_version == 4)
		{
			socks_connect(std::move(h));
		}
		else
		{
			std::move(h)(error_code(socks_error::unsupported_version));
		}
	}

	template <typename Handler>
	void handshake1(error_code const& e, Handler h)
	{
		COMPLETE_ASYNC("socks5_stream::handshake1");
		if (handle_error(e, std::move(h))) return;

		ADD_OUTSTANDING_ASYNC("socks5_stream::handshake2");
		m_buffer.resize(2);
		async_read(m_sock, boost::asio::buffer(m_buffer), wrap_allocator(
			[this](error_code const& ec, std::size_t, Handler hn) {
				handshake2(ec, std::move(hn));
			}, std::move(h)));
	}

	template <typename Handler>
	void handshake2(error_code const& e, Handler h)
	{
		COMPLETE_ASYNC("socks5_stream::handshake2");
		if (handle_error(e, std::move(h))) return;

		using namespace libtorrent::aux;

		char* p = &m_buffer[0];
		int version = read_uint8(p);
		int method = read_uint8(p);

		if (version < m_version)
		{
			std::move(h)(error_code(socks_error::unsupported_version));
			return;
		}

		if (method == 0)
		{
			socks_connect(std::move(h));
		}
		else if (method == 2)
		{
			if (m_user.empty())
			{
				std::move(h)(error_code(socks_error::username_required));
				return;
			}

			// start sub-negotiation
			m_buffer.resize(m_user.size() + m_password.size() + 3);
			p = &m_buffer[0];
			write_uint8(1, p);
			TORRENT_ASSERT(m_user.size() < 0x100);
			write_uint8(uint8_t(m_user.size()), p);
			write_string(m_user, p);
			TORRENT_ASSERT(m_password.size() < 0x100);
			write_uint8(uint8_t(m_password.size()), p);
			write_string(m_password, p);

			ADD_OUTSTANDING_ASYNC("socks5_stream::handshake3");
			async_write(m_sock, boost::asio::buffer(m_buffer), wrap_allocator(
				[this](error_code const& ec, std::size_t const, Handler hn) {
					handshake3(ec, std::move(hn));
				}, std::move(h)));
		}
		else
		{
			std::move(h)(error_code(socks_error::unsupported_authentication_method));
			return;
		}
	}

	template <typename Handler>
	void handshake3(error_code const& e, Handler h)
	{
		COMPLETE_ASYNC("socks5_stream::handshake3");
		if (handle_error(e, std::move(h))) return;

		ADD_OUTSTANDING_ASYNC("socks5_stream::handshake4");
		m_buffer.resize(2);
		async_read(m_sock, boost::asio::buffer(m_buffer), wrap_allocator(
			[this](error_code const& ec, std::size_t, Handler hn) {
				handshake4(ec, std::move(hn));
			}, std::move(h)));
	}

	template <typename Handler>
	void handshake4(error_code const& e, Handler h)
	{
		COMPLETE_ASYNC("socks5_stream::handshake4");
		if (handle_error(e, std::move(h))) return;

		using namespace libtorrent::aux;

		char* p = &m_buffer[0];
		int version = read_uint8(p);
		int status = read_uint8(p);

		if (version != 1)
		{
			std::move(h)(error_code(socks_error::unsupported_authentication_version));
			return;
		}

		if (status != 0)
		{
			std::move(h)(error_code(socks_error::authentication_error));
			return;
		}

		std::vector<char>().swap(m_buffer);
		socks_connect(std::move(h));
	}

	template <typename Handler>
	void socks_connect(Handler h)
	{
		using namespace libtorrent::aux;

		if (m_version == 5)
		{
			// send SOCKS5 connect command
			m_buffer.resize(6 + (!m_dst_name.empty()
				? m_dst_name.size() + 1
				:(aux::is_v4(m_remote_endpoint) ? 4 : 16)));
			char* p = &m_buffer[0];
			write_uint8(5, p); // SOCKS VERSION 5
			write_uint8(std::uint8_t(m_command), p); // CONNECT command
			write_uint8(0, p); // reserved
			if (!m_dst_name.empty())
			{
				write_uint8(3, p); // address type
				TORRENT_ASSERT(m_dst_name.size() < 0x100);
				write_uint8(uint8_t(m_dst_name.size()), p);
				std::copy(m_dst_name.begin(), m_dst_name.end(), p);
				p += m_dst_name.size();
			}
			else
			{
				// we either need a hostname or a valid endpoint
				TORRENT_ASSERT(m_remote_endpoint.address() != address());

				write_uint8(aux::is_v4(m_remote_endpoint) ? 1 : 4, p); // address type
				write_address(m_remote_endpoint.address(), p);
			}
			write_uint16(m_remote_endpoint.port(), p);
		}
		else if (m_version == 4)
		{
			// SOCKS4 only supports IPv4
			if (!aux::is_v4(m_remote_endpoint))
			{
				std::move(h)(error_code(boost::asio::error::address_family_not_supported));
				return;
			}
			m_buffer.resize(m_user.size() + 9);
			char* p = &m_buffer[0];
			write_uint8(4, p); // SOCKS VERSION 4
			write_uint8(std::uint8_t(m_command), p); // CONNECT command
			write_uint16(m_remote_endpoint.port(), p);
			write_uint32(m_remote_endpoint.address().to_v4().to_uint(), p);
			std::copy(m_user.begin(), m_user.end(), p);
			p += m_user.size();
			write_uint8(0, p); // 0-terminator
		}
		else
		{
			std::move(h)(error_code(socks_error::unsupported_version));
			return;
		}

		ADD_OUTSTANDING_ASYNC("socks5_stream::connect1");
		async_write(m_sock, boost::asio::buffer(m_buffer), wrap_allocator(
			[this](error_code const& ec, std::size_t, Handler hn) {
				connect1(ec, std::move(hn));
			}, std::move(h)));
	}

	template <typename Handler>
	void connect1(error_code const& e, Handler h)
	{
		COMPLETE_ASYNC("socks5_stream::connect1");
		if (handle_error(e, std::move(h))) return;

		if (m_version == 5)
			m_buffer.resize(6 + 4); // assume an IPv4 address
		else if (m_version == 4)
			m_buffer.resize(8);

		ADD_OUTSTANDING_ASYNC("socks5_stream::connect2");
		async_read(m_sock, boost::asio::buffer(m_buffer), wrap_allocator(
			[this](error_code const& ec, std::size_t, Handler hn) {
				connect2(ec, std::move(hn));
			}, std::move(h)));
	}

	template <typename Handler>
	void connect2(error_code const& e, Handler h)
	{
		COMPLETE_ASYNC("socks5_stream::connect2");
		if (handle_error(e, std::move(h))) return;

		using namespace libtorrent::aux;

		char const* p = &m_buffer[0];
		int const version = read_uint8(p);
		int const response = read_uint8(p);

		if (m_version == 5)
		{
			if (version < m_version)
			{
				std::move(h)(error_code(socks_error::unsupported_version));
				return;
			}
			if (response != 0)
			{
				error_code ec(socks_error::general_failure);
				switch (response)
				{
					case 2: ec = boost::asio::error::no_permission; break;
					case 3: ec = boost::asio::error::network_unreachable; break;
					case 4: ec = boost::asio::error::host_unreachable; break;
					case 5: ec = boost::asio::error::connection_refused; break;
					case 6: ec = boost::asio::error::timed_out; break;
					case 7: ec = socks_error::command_not_supported; break;
					case 8: ec = boost::asio::error::address_family_not_supported; break;
				}
				std::move(h)(ec);
				return;
			}
			p += 1; // reserved
			int const atyp = read_uint8(p);
			// read the proxy IP it was bound to (this is variable length depending
			// on address type)
			if (atyp == 1)
			{
				std::vector<char>().swap(m_buffer);
				std::move(h)(e);
				return;
			}
			std::size_t extra_bytes = 0;
			if (atyp == 4)
			{
				// IPv6
				extra_bytes = 12;
			}
			else if (atyp == 3)
			{
				// hostname with length prefix
				extra_bytes = read_uint8(p) - 3;
			}
			else
			{
				std::move(h)(error_code(boost::asio::error::address_family_not_supported));
				return;
			}
			m_buffer.resize(m_buffer.size() + extra_bytes);

			ADD_OUTSTANDING_ASYNC("socks5_stream::connect3");
			TORRENT_ASSERT(extra_bytes > 0);
			async_read(m_sock, boost::asio::buffer(&m_buffer[m_buffer.size() - extra_bytes], extra_bytes)
				, wrap_allocator([this](error_code const& ec, std::size_t, Handler hn) {
					connect3(ec, std::move(hn));
				}, std::move(h)));
		}
		else if (m_version == 4)
		{
			if (version != 0)
			{
				std::move(h)(error_code(socks_error::general_failure));
				return;
			}

			// access granted
			if (response == 90)
			{
				std::vector<char>().swap(m_buffer);
				std::move(h)(e);
				return;
			}

			error_code ec(socks_error::general_failure);
			switch (response)
			{
				case 91: ec = boost::asio::error::connection_refused; break;
				case 92: ec = socks_error::no_identd; break;
				case 93: ec = socks_error::identd_error; break;
			}
			std::move(h)(ec);
		}
	}

	template <typename Handler>
	void connect3(error_code const& e, Handler h)
	{
		COMPLETE_ASYNC("socks5_stream::connect3");
		using namespace libtorrent::aux;

		if (handle_error(e, std::move(h))) return;

		std::vector<char>().swap(m_buffer);
		std::move(h)(e);
	}

	// send and receive buffer
	std::vector<char> m_buffer;
	// proxy authentication
	std::string m_user;
	std::string m_password;
	std::string m_dst_name;

	int m_version;

	// the socks command to send for this connection (connect or udp associate)
	int m_command;
};

}

#endif
