/*

Copyright (c) 2016-2017, Alden Torres
Copyright (c) 2017, Pavel Pimenov
Copyright (c) 2007, 2010, 2015, 2019-2020, Arvid Norberg
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

#include "libtorrent/proxy_base.hpp"
#include "libtorrent/string_util.hpp"
#include "libtorrent/aux_/escape_string.hpp" // for base64encode
#include "libtorrent/socket_io.hpp" // for print_endpoint

namespace libtorrent {

class http_stream : public proxy_base
{
public:

	explicit http_stream(io_context& io_context)
		: proxy_base(io_context)
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

	template <class Handler>
	void async_connect(endpoint_type const& endpoint, Handler const& handler)
	{
		m_remote_endpoint = endpoint;

		// the connect is split up in the following steps:
		// 1. resolve name of proxy server
		// 2. connect to proxy server
		// 3. send HTTP CONNECT method and possibly username+password
		// 4. read CONNECT response

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
		if (handle_error(e, h)) return;

		auto i = ips.begin();
		m_sock.async_connect(i->endpoint(), wrap_allocator(
			[this](error_code const& ec, Handler hn) {
				connected(ec, std::move(hn));
			}, std::move(h)));
	}

	template <typename Handler>
	void connected(error_code const& e, Handler h)
	{
		if (handle_error(e, h)) return;

		using namespace libtorrent::aux;

		if (m_no_connect)
		{
			std::vector<char>().swap(m_buffer);
			std::move(h)(e);
			return;
		}

		// send CONNECT
		std::back_insert_iterator<std::vector<char>> p(m_buffer);
		std::string const endpoint = print_endpoint(m_remote_endpoint);
		write_string("CONNECT " + endpoint + " HTTP/1.0\r\n", p);
		if (!m_user.empty())
		{
			write_string("Proxy-Authorization: Basic " + base64encode(
				m_user + ":" + m_password) + "\r\n", p);
		}
		write_string("\r\n", p);
		async_write(m_sock, boost::asio::buffer(m_buffer), wrap_allocator(
			[this](error_code const& ec, std::size_t, Handler hn) {
				handshake1(ec, std::move(hn));
			}, std::move(h)));
	}

	template <typename Handler>
	void handshake1(error_code const& e, Handler h)
	{
		if (handle_error(e, h)) return;

		// read one byte from the socket
		m_buffer.resize(1);
		async_read(m_sock, boost::asio::buffer(m_buffer), wrap_allocator(
			[this](error_code const& ec, std::size_t, Handler hn) {
				handshake2(ec, std::move(hn));
			}, std::move(h)));
	}

	template <typename Handler>
	void handshake2(error_code const& e, Handler h)
	{
		if (handle_error(e, h)) return;

		std::size_t const read_pos = m_buffer.size();
		// look for \n\n and \r\n\r\n
		// both of which means end of http response header
		bool found_end = false;
		if (m_buffer[read_pos - 1] == '\n' && read_pos > 2)
		{
			if (m_buffer[read_pos - 2] == '\n')
			{
				found_end = true;
			}
			else if (read_pos > 4
				&& m_buffer[read_pos - 2] == '\r'
				&& m_buffer[read_pos - 3] == '\n'
				&& m_buffer[read_pos - 4] == '\r')
			{
				found_end = true;
			}
		}

		if (found_end)
		{
			m_buffer.push_back(0);
			char const* status = std::strchr(m_buffer.data(), ' ');
			if (status == nullptr)
			{
				h(boost::asio::error::operation_not_supported);
				error_code ec;
				close(ec);
				return;
			}

			status++;
			int const code = std::atoi(status);
			if (code != 200)
			{
				h(boost::asio::error::operation_not_supported);
				error_code ec;
				close(ec);
				return;
			}

			h(e);
			std::vector<char>().swap(m_buffer);
			return;
		}

		// read another byte from the socket
		m_buffer.resize(read_pos + 1);
		async_read(m_sock, boost::asio::buffer(m_buffer.data() + read_pos, 1), wrap_allocator(
			[this](error_code const& ec, std::size_t, Handler hn) {
				handshake2(ec, std::move(hn));
			}, std::move(h)));
	}

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
