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

#ifndef TORRENT_HTTP_STREAM_HPP_INCLUDED
#define TORRENT_HTTP_STREAM_HPP_INCLUDED

#include <boost/function/function1.hpp>
#include "libtorrent/proxy_base.hpp"
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>

namespace libtorrent {

class http_stream : public proxy_base
{
public:

	explicit http_stream(io_service& io_service)
		: proxy_base(io_service)
		, m_no_connect(false)
	{}

	void set_no_connect(bool c) { m_no_connect = c; }

	void set_username(std::string const& user
		, std::string const& password)
	{
		m_user = user;
		m_password = password;
	}

	void set_dst_name(std::string const& host)
	{
		m_dst_name = host;
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

	typedef boost::function<void(error_code const&)> handler_type;

	template <class Handler>
	void async_connect(endpoint_type const& endpoint, Handler const& handler)
	{
		m_remote_endpoint = endpoint;

		// the connect is split up in the following steps:
		// 1. resolve name of proxy server
		// 2. connect to proxy server
		// 3. send HTTP CONNECT method and possibly username+password
		// 4. read CONNECT response

		// to avoid unnecessary copying of the handler,
		// store it in a shared_ptr
		boost::shared_ptr<handler_type> h(new handler_type(handler));

		tcp::resolver::query q(m_hostname, to_string(m_port).elems);
		m_resolver.async_resolve(q, boost::bind(
			&http_stream::name_lookup, this, _1, _2, h));
	}

private:

	void name_lookup(error_code const& e, tcp::resolver::iterator i
		, boost::shared_ptr<handler_type> h);
	void connected(error_code const& e, boost::shared_ptr<handler_type> h);
	void handshake1(error_code const& e, boost::shared_ptr<handler_type> h);
	void handshake2(error_code const& e, boost::shared_ptr<handler_type> h);

	// send and receive buffer
	std::vector<char> m_buffer;
	// proxy authentication
	std::string m_user;
	std::string m_password;
	std::string m_dst_name;

	// this is true if the connection is HTTP based and
	// want to talk directly to the proxy
	bool m_no_connect;
};

}

#endif
