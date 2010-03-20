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

#include "libtorrent/socks4_stream.hpp"

namespace libtorrent
{

	void socks4_stream::name_lookup(error_code const& e, tcp::resolver::iterator i
		, boost::shared_ptr<handler_type> h)
	{
		if (e)
		{
			(*h)(e);
			error_code ec;
			close(ec);
			return;
		}

		// SOCKS4 doesn't support IPv6 addresses
		while (i != tcp::resolver::iterator() && i->endpoint().address().is_v6())
			++i;

		if (i == tcp::resolver::iterator())
		{
			error_code ec = asio::error::operation_not_supported;
			(*h)(ec);
			close(ec);
			return;
		}

		m_sock.async_connect(i->endpoint(), boost::bind(
			&socks4_stream::connected, this, _1, h));
	}

	void socks4_stream::connected(error_code const& e, boost::shared_ptr<handler_type> h)
	{
		if (e)
		{
			(*h)(e);
			error_code ec;
			close(ec);
			return;
		}

		using namespace libtorrent::detail;

		m_buffer.resize(m_user.size() + 9);
		char* p = &m_buffer[0];
		write_uint8(4, p); // SOCKS VERSION 4
		write_uint8(1, p); // SOCKS CONNECT
		write_uint16(m_remote_endpoint.port(), p);
		write_uint32(m_remote_endpoint.address().to_v4().to_ulong(), p);
		std::copy(m_user.begin(), m_user.end(), p);
		p += m_user.size();
		write_uint8(0, p); // NULL terminator

		async_write(m_sock, asio::buffer(m_buffer)
			, boost::bind(&socks4_stream::handshake1, this, _1, h));
	}

	void socks4_stream::handshake1(error_code const& e, boost::shared_ptr<handler_type> h)
	{
		if (e)
		{
			(*h)(e);
			error_code ec;
			close(ec);
			return;
		}

		m_buffer.resize(8);
		async_read(m_sock, asio::buffer(m_buffer)
			, boost::bind(&socks4_stream::handshake2, this, _1, h));
	}

	void socks4_stream::handshake2(error_code const& e, boost::shared_ptr<handler_type> h)
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
		int reply_version = read_uint8(p);
		int status_code = read_uint8(p);

		if (reply_version != 0)
		{
			error_code ec = asio::error::operation_not_supported;
			(*h)(ec);
			close(ec);
			return;
		}

		// access granted
		if (status_code == 90)
		{
			std::vector<char>().swap(m_buffer);
			(*h)(e);
			return;
		}

		error_code ec = asio::error::fault;
		switch (status_code)
		{
			case 91: ec = asio::error::connection_refused; break;
			case 92: ec = asio::error::no_permission; break;
			case 93: ec = asio::error::no_permission; break;
		}
		(*h)(ec);
		close(ec);
	}

}

