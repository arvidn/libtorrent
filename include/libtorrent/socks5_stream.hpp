/*

Copyright (c) 2007-2014, Arvid Norberg
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

#include <boost/function/function1.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include "libtorrent/proxy_base.hpp"
#if defined TORRENT_ASIO_DEBUGGING
#include "libtorrent/debug.hpp"
#endif

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
	}

	// returns the error_category for SOCKS5 errors
	TORRENT_EXPORT boost::system::error_category& get_socks_category();

class socks5_stream : public proxy_base
{
public:

	explicit socks5_stream(io_service& io_service)
		: proxy_base(io_service)
		, m_version(5)
		, m_command(1)
		, m_listen(0)
	{}

	void set_version(int v) { m_version = v; }

	void set_command(int c) { m_command = c; }

	void set_username(std::string const& user
		, std::string const& password)
	{
		m_user = user;
		m_password = password;
	}

	void set_dst_name(std::string const& host)
	{
		m_dst_name = host;
		if (m_dst_name.size() > 255)
			m_dst_name.resize(255);
	}

	void close(error_code& ec)
	{
		m_hostname.clear();
		m_dst_name.clear();
		proxy_base::close(ec);
	}

#ifndef BOOST_NO_EXCEPTIONS
	void close()
	{
		m_hostname.clear();
		m_dst_name.clear();
		proxy_base::close();
	}
#endif

	typedef boost::function<void(error_code const&)> handler_type;

//#error fix error messages to use custom error_code category
//#error add async_connect() that takes a hostname and port as well
	template <class Handler>
	void async_connect(endpoint_type const& endpoint, Handler const& handler)
	{
		m_remote_endpoint = endpoint;

		// the connect is split up in the following steps:
		// 1. resolve name of proxy server
		// 2. connect to proxy server
		// 3. if version == 5:
		//   3.1 send SOCKS5 authentication method message
		//   3.2 read SOCKS5 authentication response
		//   3.3 send username+password
		// 4. send SOCKS command message

		// to avoid unnecessary copying of the handler,
		// store it in a shaed_ptr
		boost::shared_ptr<handler_type> h(new handler_type(handler));

#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("socks5_stream::name_lookup");
#endif
		tcp::resolver::query q(m_hostname, to_string(m_port).elems);
		m_resolver.async_resolve(q, boost::bind(
			&socks5_stream::name_lookup, this, _1, _2, h));
	}

private:

	void name_lookup(error_code const& e, tcp::resolver::iterator i
		, boost::shared_ptr<handler_type> h);
	void connected(error_code const& e, boost::shared_ptr<handler_type> h);
	void handshake1(error_code const& e, boost::shared_ptr<handler_type> h);
	void handshake2(error_code const& e, boost::shared_ptr<handler_type> h);
	void handshake3(error_code const& e, boost::shared_ptr<handler_type> h);
	void handshake4(error_code const& e, boost::shared_ptr<handler_type> h);
	void socks_connect(boost::shared_ptr<handler_type> h);
	void connect1(error_code const& e, boost::shared_ptr<handler_type> h);
	void connect2(error_code const& e, boost::shared_ptr<handler_type> h);
	void connect3(error_code const& e, boost::shared_ptr<handler_type> h);

	// send and receive buffer
	std::vector<char> m_buffer;
	// proxy authentication
	std::string m_user;
	std::string m_password;
	std::string m_dst_name;
	int m_version;
	int m_command;
	// set to one when we're waiting for the
	// second message to accept an incoming connection
	int m_listen;
};

}

#endif

