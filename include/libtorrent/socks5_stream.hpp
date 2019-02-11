/*

Copyright (c) 2007-2018, Arvid Norberg
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
#include "libtorrent/broadcast_socket.hpp" // for is_ip_address
#include "libtorrent/assert.hpp"
#include "libtorrent/debug.hpp"
#include "libtorrent/string_util.hpp" // for to_string

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
		TORRENT_ASSERT(!is_ip_address(host));
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
	void async_connect(endpoint_type const& endpoint, Handler const& handler)
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

		using std::placeholders::_1;
		using std::placeholders::_2;
		ADD_OUTSTANDING_ASYNC("socks5_stream::name_lookup");
		tcp::resolver::query q(m_hostname, to_string(m_port).data());
		m_resolver.async_resolve(q, std::bind(
			&socks5_stream::name_lookup, this, _1, _2, handler_type(std::move(handler))));
	}

private:

	void name_lookup(error_code const& e, tcp::resolver::iterator i
		, handler_type h);
	void connected(error_code const& e, handler_type h);
	void handshake1(error_code const& e, handler_type h);
	void handshake2(error_code const& e, handler_type h);
	void handshake3(error_code const& e, handler_type h);
	void handshake4(error_code const& e, handler_type h);
	void socks_connect(handler_type h);
	void connect1(error_code const& e, handler_type h);
	void connect2(error_code const& e, handler_type h);
	void connect3(error_code const& e, handler_type h);

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

namespace boost { namespace system {

	template<> struct is_error_code_enum<libtorrent::socks_error::socks_error_code>
	{ static const bool value = true; };

} }

#endif
