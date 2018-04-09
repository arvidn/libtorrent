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

#include "libtorrent/socks5_stream.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/socket_io.hpp"

namespace libtorrent
{

	namespace socks_error
	{
		boost::system::error_code make_error_code(socks_error_code e)
		{
			return error_code(e, socks_category());
		}
	}

	struct socks_error_category : boost::system::error_category
	{
		virtual const char* name() const BOOST_SYSTEM_NOEXCEPT
		{ return "socks error"; }
		virtual std::string message(int ev) const BOOST_SYSTEM_NOEXCEPT
		{
			static char const* messages[] =
			{
				"SOCKS no error",
				"SOCKS unsupported version",
				"SOCKS unsupported authentication method",
				"SOCKS unsupported authentication version",
				"SOCKS authentication error",
				"SOCKS username required",
				"SOCKS general failure",
				"SOCKS command not supported",
				"SOCKS no identd running",
				"SOCKS identd could not identify username"
			};

			if (ev < 0 || ev >= socks_error::num_errors) return "unknown error";
			return messages[ev];
		}
		virtual boost::system::error_condition default_error_condition(
			int ev) const BOOST_SYSTEM_NOEXCEPT
		{ return boost::system::error_condition(ev, *this); }
	};

	boost::system::error_category& socks_category()
	{
		static socks_error_category cat;
		return cat;
	}

#ifndef TORRENT_NO_DEPRECATE
	boost::system::error_category& get_socks_category()
	{ return socks_category(); }
#endif

	void socks5_stream::name_lookup(error_code const& e, tcp::resolver::iterator i
		, boost::shared_ptr<handler_type> h)
	{
#if defined TORRENT_ASIO_DEBUGGING
		complete_async("socks5_stream::name_lookup");
#endif
		if (handle_error(e, h)) return;

		error_code ec;
		if (!m_sock.is_open())
		{
			m_sock.open(i->endpoint().protocol(), ec);
			if (handle_error(ec, h)) return;
		}

		// TOOD: we could bind the socket here, since we know what the
		// target endpoint is of the proxy
#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("socks5_stream::connected");
#endif
		m_sock.async_connect(i->endpoint(), boost::bind(
			&socks5_stream::connected, this, _1, h));
	}

	void socks5_stream::connected(error_code const& e, boost::shared_ptr<handler_type> h)
	{
#if defined TORRENT_ASIO_DEBUGGING
		complete_async("socks5_stream::connected");
#endif
		if (handle_error(e, h)) return;

		using namespace libtorrent::detail;
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
#if defined TORRENT_ASIO_DEBUGGING
			add_outstanding_async("socks5_stream::handshake1");
#endif
			async_write(m_sock, boost::asio::buffer(m_buffer)
				, boost::bind(&socks5_stream::handshake1, this, _1, h));
		}
		else if (m_version == 4)
		{
			socks_connect(h);
		}
		else
		{
			(*h)(socks_error::unsupported_version);
		}
	}

	void socks5_stream::handshake1(error_code const& e, boost::shared_ptr<handler_type> h)
	{
#if defined TORRENT_ASIO_DEBUGGING
		complete_async("socks5_stream::handshake1");
#endif
		if (handle_error(e, h)) return;

#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("socks5_stream::handshake2");
#endif
		m_buffer.resize(2);
		async_read(m_sock, boost::asio::buffer(m_buffer)
			, boost::bind(&socks5_stream::handshake2, this, _1, h));
	}

	void socks5_stream::handshake2(error_code const& e, boost::shared_ptr<handler_type> h)
	{
#if defined TORRENT_ASIO_DEBUGGING
		complete_async("socks5_stream::handshake2");
#endif
		if (handle_error(e, h)) return;

		using namespace libtorrent::detail;

		char* p = &m_buffer[0];
		int version = read_uint8(p);
		int method = read_uint8(p);

		if (version < m_version)
		{
			(*h)(socks_error::unsupported_version);
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
				(*h)(socks_error::username_required);
				return;
			}

			// start sub-negotiation
			m_buffer.resize(m_user.size() + m_password.size() + 3);
			p = &m_buffer[0];
			write_uint8(1, p);
			write_uint8(m_user.size(), p);
			write_string(m_user, p);
			write_uint8(m_password.size(), p);
			write_string(m_password, p);

#if defined TORRENT_ASIO_DEBUGGING
			add_outstanding_async("socks5_stream::handshake3");
#endif
			async_write(m_sock, boost::asio::buffer(m_buffer)
				, boost::bind(&socks5_stream::handshake3, this, _1, h));
		}
		else
		{
			(*h)(socks_error::unsupported_authentication_method);
			return;
		}
	}

	void socks5_stream::handshake3(error_code const& e
		, boost::shared_ptr<handler_type> h)
	{
#if defined TORRENT_ASIO_DEBUGGING
		complete_async("socks5_stream::handshake3");
#endif
		if (handle_error(e, h)) return;

#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("socks5_stream::handshake4");
#endif
		m_buffer.resize(2);
		async_read(m_sock, boost::asio::buffer(m_buffer)
			, boost::bind(&socks5_stream::handshake4, this, _1, h));
	}

	void socks5_stream::handshake4(error_code const& e
		, boost::shared_ptr<handler_type> h)
	{
#if defined TORRENT_ASIO_DEBUGGING
		complete_async("socks5_stream::handshake4");
#endif
		if (handle_error(e, h)) return;

		using namespace libtorrent::detail;

		char* p = &m_buffer[0];
		int version = read_uint8(p);
		int status = read_uint8(p);

		if (version != 1)
		{
			(*h)(socks_error::unsupported_authentication_version);
			return;
		}

		if (status != 0)
		{
			(*h)(socks_error::authentication_error);
			return;
		}

		std::vector<char>().swap(m_buffer);
		socks_connect(h);
	}

	void socks5_stream::socks_connect(boost::shared_ptr<handler_type> h)
	{
		using namespace libtorrent::detail;

		if (m_version == 5)
		{
			// send SOCKS5 connect command
			m_buffer.resize(6 + (!m_dst_name.empty()
				?m_dst_name.size() + 1
				:(m_remote_endpoint.address().is_v4()?4:16)));
			char* p = &m_buffer[0];
			write_uint8(5, p); // SOCKS VERSION 5
			write_uint8(m_command, p); // CONNECT command
			write_uint8(0, p); // reserved
			if (!m_dst_name.empty())
			{
				write_uint8(3, p); // address type
				TORRENT_ASSERT(m_dst_name.size() <= 255);
				write_uint8(m_dst_name.size(), p);
				std::copy(m_dst_name.begin(), m_dst_name.end(), p);
				p += m_dst_name.size();
			}
			else
			{
				// we either need a hostname or a valid endpoint
				TORRENT_ASSERT(m_remote_endpoint.address() != address());

				write_uint8(m_remote_endpoint.address().is_v4()?1:4, p); // address type
				write_address(m_remote_endpoint.address(), p);
			}
			write_uint16(m_remote_endpoint.port(), p);
		}
		else if (m_version == 4)
		{
			// SOCKS4 only supports IPv4
			if (!m_remote_endpoint.address().is_v4())
			{
				(*h)(boost::asio::error::address_family_not_supported);
				return;
			}
			m_buffer.resize(m_user.size() + 9);
			char* p = &m_buffer[0];
			write_uint8(4, p); // SOCKS VERSION 4
			write_uint8(m_command, p); // CONNECT command
			write_uint16(m_remote_endpoint.port(), p);
			write_uint32(m_remote_endpoint.address().to_v4().to_ulong(), p);
			std::copy(m_user.begin(), m_user.end(), p);
			p += m_user.size();
			write_uint8(0, p); // NULL terminator
		}
		else
		{
			(*h)(socks_error::unsupported_version);
			return;
		}

#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("socks5_stream::connect1");
#endif
		async_write(m_sock, boost::asio::buffer(m_buffer)
			, boost::bind(&socks5_stream::connect1, this, _1, h));
	}

	void socks5_stream::connect1(error_code const& e, boost::shared_ptr<handler_type> h)
	{
#if defined TORRENT_ASIO_DEBUGGING
		complete_async("socks5_stream::connect1");
#endif
		if (handle_error(e, h)) return;

		if (m_version == 5)
			m_buffer.resize(6 + 4); // assume an IPv4 address
		else if (m_version == 4)
			m_buffer.resize(8);

#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("socks5_stream::connect2");
#endif
		async_read(m_sock, boost::asio::buffer(m_buffer)
			, boost::bind(&socks5_stream::connect2, this, _1, h));
	}

	void socks5_stream::connect2(error_code const& e, boost::shared_ptr<handler_type> h)
	{
#if defined TORRENT_ASIO_DEBUGGING
		complete_async("socks5_stream::connect2");
#endif
		if (handle_error(e, h)) return;

		using namespace libtorrent::detail;

		char const* p = &m_buffer[0];
		int const version = read_uint8(p);
		int const response = read_uint8(p);

		if (m_version == 5)
		{
			if (version < m_version)
			{
				(*h)(socks_error::unsupported_version);
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
				(*h)(ec);
				return;
			}
			p += 1; // reserved
			int const atyp = read_uint8(p);
			// read the proxy IP it was bound to (this is variable length depending
			// on address type)
			if (atyp == 1)
			{
				std::vector<char>().swap(m_buffer);
				(*h)(e);
				return;
			}
			int extra_bytes = 0;
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
				(*h)(boost::asio::error::address_family_not_supported);
				return;
			}
			m_buffer.resize(m_buffer.size() + extra_bytes);

#if defined TORRENT_ASIO_DEBUGGING
			add_outstanding_async("socks5_stream::connect3");
#endif
			TORRENT_ASSERT(extra_bytes > 0);
			async_read(m_sock, boost::asio::buffer(&m_buffer[m_buffer.size() - extra_bytes], extra_bytes)
				, boost::bind(&socks5_stream::connect3, this, _1, h));
		}
		else if (m_version == 4)
		{
			if (version != 0)
			{
				(*h)(socks_error::general_failure);
				return;
			}

			// access granted
			if (response == 90)
			{
				std::vector<char>().swap(m_buffer);
				(*h)(e);
				return;
			}

			error_code ec(socks_error::general_failure);
			switch (response)
			{
				case 91: ec = boost::asio::error::connection_refused; break;
				case 92: ec = socks_error::no_identd; break;
				case 93: ec = socks_error::identd_error; break;
			}
			(*h)(ec);
		}
	}

	void socks5_stream::connect3(error_code const& e, boost::shared_ptr<handler_type> h)
	{
#if defined TORRENT_ASIO_DEBUGGING
		complete_async("socks5_stream::connect3");
#endif
		using namespace libtorrent::detail;

		if (handle_error(e, h)) return;

		std::vector<char>().swap(m_buffer);
		(*h)(e);
	}
}

