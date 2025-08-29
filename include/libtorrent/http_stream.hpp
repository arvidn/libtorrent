/*

Copyright (c) 2016-2017, Alden Torres
Copyright (c) 2017, Pavel Pimenov
Copyright (c) 2007, 2010, 2015, 2019, 2021, Arvid Norberg
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
#include <cctype> // for std::isdigit

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

	void set_host(std::string const& host)
	{
		m_host = host;
	}

	void close(error_code& ec)
	{
		m_host.clear();
		proxy_base::close(ec);
	}

#ifndef BOOST_NO_EXCEPTIONS
	void close()
	{
		m_host.clear();
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

	// format a hostname (not a numeric IP) with a port for an HTTP CONNECT request.
	// rules:
	// - if port == 0, return host unchanged
	// - if host is bracketed IPv6: [addr] or [addr]:port, append port only if missing
	// - if host contains no ':', append ":port"
	// - if host contains colon(s):
	//     * if suffix after last ':' is all digits, assume it's already a host:port -> leave unchanged
	//     * otherwise treat it as an (unbracketed) IPv6 literal or hostname with colons and wrap
	//       it in brackets and append :port -> [host]:port
	static std::string format_host_for_connect(std::string host, unsigned short const port)
	{
		if (port == 0) return host;

		if (!host.empty() && host.front() == '[')
		{
			auto const rb = host.find(']');
			bool const has_port = (rb != std::string::npos && rb + 1 < host.size() && host[rb + 1] == ':');
			if (!has_port) host += ":" + std::to_string(port);
			return host;
		}

		auto const last_colon = host.rfind(':');
		if (last_colon == std::string::npos)
		{
			host += ":" + std::to_string(port);
			return host;
		}

		// Check whether the suffix after the last colon is all digits
		bool suffix_digits = last_colon + 1 < host.size();
		if (suffix_digits)
		{
			for (std::size_t i = last_colon + 1; i < host.size(); ++i)
			{
				if (!std::isdigit(static_cast<unsigned char>(host[i]))) { suffix_digits = false; break; }
			}
		}

		// If the suffix is digits, the string may be either "host:port" or an
		// unbracketed IPv6 literal (e.g. "2001:db8::1") where the last
		// segment happens to be numeric. Use inet_pton to detect IPv6
		// literals:
		// - if the whole host parses as IPv6 -> bracket and append port
		// - else if the head (before the last colon) parses as IPv6 -> it's
		//   IPv6-with-port (leave unchanged)
		// - otherwise keep current behavior (leave as host:port)
		if (suffix_digits)
		{
			in6_addr addr;
			// whole host might be an IPv6 literal (no port)
			if (inet_pton(AF_INET6, host.c_str(), &addr) == 1)
			{
				host = "[" + host + "]:" + std::to_string(port);
				return host;
			}

			// check head (before last colon) for IPv6 literal with an explicit port
			std::string head = host.substr(0, last_colon);
			if (inet_pton(AF_INET6, head.c_str(), &addr) == 1)
			{
				// Treat as already host:port (leave unchanged)
				return host;
			}

			// not IPv6; treat as host:port (leave unchanged)
			return host;
		}

		// suffix not all digits -> treat as unbracketed IPv6 or hostname with
		// colons: bracket and append port
		host = "[" + host + "]:" + std::to_string(port);
		return host;
	}

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
		// if we were given the original host (domain or IP), prefer using it (lets proxy resolve domains)
		if (!m_host.empty())
		{
			std::string const remote_host = format_host_for_connect(m_host, m_remote_endpoint.port());
			write_string("CONNECT " + remote_host + " HTTP/1.0\r\n", p);
		}
		else
		{
			write_string("CONNECT " + endpoint + " HTTP/1.0\r\n", p);
		}
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
		if (read_pos > 2 && m_buffer[read_pos - 1] == '\n')
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
	std::string m_host;

	// this is true if the connection is HTTP based and
	// want to talk directly to the proxy
	bool m_no_connect;
};

}

#endif
