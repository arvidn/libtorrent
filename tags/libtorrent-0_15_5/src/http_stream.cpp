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

#include "libtorrent/http_stream.hpp"
#include "libtorrent/escape_string.hpp" // for base64encode

namespace libtorrent
{

	void http_stream::name_lookup(error_code const& e, tcp::resolver::iterator i
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
			&http_stream::connected, this, _1, h));
	}

	void http_stream::connected(error_code const& e, boost::shared_ptr<handler_type> h)
	{
		if (e)
		{
			(*h)(e);
			error_code ec;
			close(ec);
			return;
		}

		using namespace libtorrent::detail;

		if (m_no_connect)
		{
			std::vector<char>().swap(m_buffer);
			(*h)(e);
			return;
		}

		// send CONNECT
		std::back_insert_iterator<std::vector<char> > p(m_buffer);
		write_string("CONNECT " + print_endpoint(m_remote_endpoint)
			+ " HTTP/1.0\r\n", p);
		if (!m_user.empty())
		{
			write_string("Proxy-Authorization: Basic " + base64encode(
				m_user + ":" + m_password) + "\r\n", p);
		}
		write_string("\r\n", p);
		async_write(m_sock, asio::buffer(m_buffer)
			, boost::bind(&http_stream::handshake1, this, _1, h));
	}

	void http_stream::handshake1(error_code const& e, boost::shared_ptr<handler_type> h)
	{
		if (e)
		{
			(*h)(e);
			error_code ec;
			close(ec);
			return;
		}

		// read one byte from the socket
		m_buffer.resize(1);
		async_read(m_sock, asio::buffer(m_buffer)
			, boost::bind(&http_stream::handshake2, this, _1, h));
	}

	void http_stream::handshake2(error_code const& e, boost::shared_ptr<handler_type> h)
	{
		if (e)
		{
			(*h)(e);
			error_code ec;
			close(ec);
			return;
		}

		int read_pos = m_buffer.size();
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
			char* status = std::strchr(&m_buffer[0], ' ');
			if (status == 0)
			{
				(*h)(asio::error::operation_not_supported);
				error_code ec;
				close(ec);
				return;
			}

			status++;
			int code = std::atoi(status);
			if (code != 200)
			{
				(*h)(asio::error::operation_not_supported);
				error_code ec;
				close(ec);
				return;
			}

			(*h)(e);
			std::vector<char>().swap(m_buffer);
			return;
		}

		// read another byte from the socket
		m_buffer.resize(read_pos + 1);
		async_read(m_sock, asio::buffer(&m_buffer[0] + read_pos, 1)
			, boost::bind(&http_stream::handshake2, this, _1, h));
	}

}

