/*

Copyright (c) 2007, Arvid Norberg
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

#include "libtorrent/pch.hpp"

#include "libtorrent/socks5_stream.hpp"
#include "libtorrent/assert.hpp"

namespace libtorrent
{

	void socks5_stream::name_lookup(error_code const& e, tcp::resolver::iterator i
		, boost::shared_ptr<handler_type> h)
	{
		if (e || i == tcp::resolver::iterator())
		{
			(*h)(e);
			error_code ec;
			close(ec);
			return;
		}

		m_sock.async_connect(i->endpoint(), boost::bind(
			&socks5_stream::connected, this, _1, h));
	}

	void socks5_stream::connected(error_code const& e, boost::shared_ptr<handler_type> h)
	{
		if (e)
		{
			(*h)(e);
			error_code ec;
			close(ec);
			return;
		}

		using namespace libtorrent::detail;
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
		async_write(m_sock, asio::buffer(m_buffer)
			, boost::bind(&socks5_stream::handshake1, this, _1, h));
	}

	void socks5_stream::handshake1(error_code const& e, boost::shared_ptr<handler_type> h)
	{
		if (e)
		{
			(*h)(e);
			error_code ec;
			close(ec);
			return;
		}

		m_buffer.resize(2);
		async_read(m_sock, asio::buffer(m_buffer)
			, boost::bind(&socks5_stream::handshake2, this, _1, h));
	}

	void socks5_stream::handshake2(error_code const& e, boost::shared_ptr<handler_type> h)
	{
		if (e)
		{
			(*h)(e);
			error_code ec;
			close(ec);
			return;
		}

		using namespace libtorrent::detail;

		char* p = &m_buffer[0];
		int version = read_uint8(p);
		int method = read_uint8(p);

		if (version < 5)
		{
			(*h)(asio::error::operation_not_supported);
			error_code ec;
			close(ec);
			return;
		}

		if (method == 0)
		{
			socks_connect(h);
		}
		else if (method == 2)
		{
			if (m_user.empty())
			{
				(*h)(asio::error::operation_not_supported);
				error_code ec;
				close(ec);
				return;
			}

			// start sub-negotiation
			m_buffer.resize(m_user.size() + m_password.size() + 3);
			char* p = &m_buffer[0];
			write_uint8(1, p);
			write_uint8(m_user.size(), p);
			write_string(m_user, p);
			write_uint8(m_password.size(), p);
			write_string(m_password, p);
			async_write(m_sock, asio::buffer(m_buffer)
				, boost::bind(&socks5_stream::handshake3, this, _1, h));
		}
		else
		{
			(*h)(asio::error::operation_not_supported);
			error_code ec;
			close(ec);
			return;
		}
	}

	void socks5_stream::handshake3(error_code const& e
		, boost::shared_ptr<handler_type> h)
	{
		if (e)
		{
			(*h)(e);
			error_code ec;
			close(ec);
			return;
		}

		m_buffer.resize(2);
		async_read(m_sock, asio::buffer(m_buffer)
			, boost::bind(&socks5_stream::handshake4, this, _1, h));
	}

	void socks5_stream::handshake4(error_code const& e
		, boost::shared_ptr<handler_type> h)
	{
		if (e)
		{
			(*h)(e);
			error_code ec;
			close(ec);
			return;
		}

		using namespace libtorrent::detail;

		char* p = &m_buffer[0];
		int version = read_uint8(p);
		int status = read_uint8(p);

		if (version != 1)
		{
			(*h)(asio::error::operation_not_supported);
			error_code ec;
			close(ec);
			return;
		}

		if (status != 0)
		{
			(*h)(asio::error::operation_not_supported);
			error_code ec;
			close(ec);
			return;
		}

		std::vector<char>().swap(m_buffer);
		socks_connect(h);
	}

	void socks5_stream::socks_connect(boost::shared_ptr<handler_type> h)
	{
		using namespace libtorrent::detail;

		// send SOCKS5 connect command
		m_buffer.resize(6 + (m_remote_endpoint.address().is_v4()?4:16));
		char* p = &m_buffer[0];
		write_uint8(5, p); // SOCKS VERSION 5
		write_uint8(1, p); // CONNECT command
		write_uint8(0, p); // reserved
		write_uint8(m_remote_endpoint.address().is_v4()?1:4, p); // address type
		write_endpoint(m_remote_endpoint, p);
		TORRENT_ASSERT(p - &m_buffer[0] == int(m_buffer.size()));

		async_write(m_sock, asio::buffer(m_buffer)
			, boost::bind(&socks5_stream::connect1, this, _1, h));
	}

	void socks5_stream::connect1(error_code const& e, boost::shared_ptr<handler_type> h)
	{
		if (e)
		{
			(*h)(e);
			error_code ec;
			close(ec);
			return;
		}

		m_buffer.resize(6 + 4); // assume an IPv4 address
		async_read(m_sock, asio::buffer(m_buffer)
			, boost::bind(&socks5_stream::connect2, this, _1, h));
	}

	void socks5_stream::connect2(error_code const& e, boost::shared_ptr<handler_type> h)
	{
		if (e)
		{
			(*h)(e);
			error_code ec;
			close(ec);
			return;
		}

		using namespace libtorrent::detail;

		// send SOCKS5 connect command
		char* p = &m_buffer[0];
		int version = read_uint8(p);
		if (version < 5)
		{
			(*h)(asio::error::operation_not_supported);
			error_code ec;
			close(ec);
			return;
		}
		int response = read_uint8(p);
		if (response != 0)
		{
			error_code e = asio::error::fault;
			switch (response)
			{
				case 1: e = asio::error::fault; break;
				case 2: e = asio::error::no_permission; break;
				case 3: e = asio::error::network_unreachable; break;
				case 4: e = asio::error::host_unreachable; break;
				case 5: e = asio::error::connection_refused; break;
				case 6: e = asio::error::timed_out; break;
				case 7: e = asio::error::operation_not_supported; break;
				case 8: e = asio::error::address_family_not_supported; break;
			}
			(*h)(e);
			error_code ec;
			close(ec);
			return;
		}
		p += 1; // reserved
		int atyp = read_uint8(p);
		// we ignore the proxy IP it was bound to
		if (atyp == 1)
		{
			std::vector<char>().swap(m_buffer);
			(*h)(e);
			return;
		}
		int skip_bytes = 0;
		if (atyp == 4)
		{
			skip_bytes = 12;
		}
		else if (atyp == 3)
		{
			skip_bytes = read_uint8(p) - 3;
		}
		else
		{
			(*h)(asio::error::operation_not_supported);
			error_code ec;
			close(ec);
			return;
		}
		m_buffer.resize(skip_bytes);

		async_read(m_sock, asio::buffer(m_buffer)
			, boost::bind(&socks5_stream::connect3, this, _1, h));
	}

	void socks5_stream::connect3(error_code const& e, boost::shared_ptr<handler_type> h)
	{
		if (e)
		{
			(*h)(e);
			error_code ec;
			close(ec);
			return;
		}

		std::vector<char>().swap(m_buffer);
		(*h)(e);
	}
}

