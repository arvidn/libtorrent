/*

Copyright (c) 2009-2016, Arvid Norberg
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

#include "libtorrent/config.hpp"
#include "libtorrent/i2p_stream.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/string_util.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/hex.hpp"

#if TORRENT_USE_I2P

#include <boost/bind.hpp>

#if defined TORRENT_ASIO_DEBUGGING
#include "libtorrent/debug.hpp"
#endif

namespace libtorrent
{

	struct i2p_error_category : boost::system::error_category
	{
		virtual const char* name() const BOOST_SYSTEM_NOEXCEPT
		{ return "i2p error"; }
		virtual std::string message(int ev) const BOOST_SYSTEM_NOEXCEPT
		{
			static char const* messages[] =
			{
				"no error",
				"parse failed",
				"cannot reach peer",
				"i2p error",
				"invalid key",
				"invalid id",
				"timeout",
				"key not found",
				"duplicated id"
			};

			if (ev < 0 || ev >= i2p_error::num_errors) return "unknown error";
			return messages[ev];
		}
		virtual boost::system::error_condition default_error_condition(
			int ev) const BOOST_SYSTEM_NOEXCEPT
		{ return boost::system::error_condition(ev, *this); }
	};


	boost::system::error_category& get_i2p_category()
	{
		static i2p_error_category i2p_category;
		return i2p_category;
	}

	namespace i2p_error
	{
		boost::system::error_code make_error_code(i2p_error_code e)
		{
			return error_code(e, get_i2p_category());
		}
	}

	i2p_connection::i2p_connection(io_service& ios)
		: m_port(0)
		, m_state(sam_idle)
		, m_io_service(ios)
	{}

	i2p_connection::~i2p_connection()
	{}

	void i2p_connection::close(error_code& e)
	{
		if (m_sam_socket) m_sam_socket->close(e);
	}

	aux::proxy_settings i2p_connection::proxy() const
	{
		aux::proxy_settings ret;
		ret.hostname = m_hostname;
		ret.port = m_port;
		ret.type = settings_pack::i2p_proxy;
		return ret;
	}

	void i2p_connection::open(std::string const& s, int port
		, i2p_stream::handler_type const& handler)
	{
		// we already seem to have a session to this SAM router
		if (m_hostname == s
			&& m_port == port
			&& m_sam_socket
			&& (is_open() || m_state == sam_connecting)) return;

		m_hostname = s;
		m_port = port;

		if (m_hostname.empty()) return;

		m_state = sam_connecting;

		char tmp[20];
		std::generate(tmp, tmp + sizeof(tmp), &std::rand);
		m_session_id.resize(sizeof(tmp)*2);
		to_hex(tmp, 20, &m_session_id[0]);

		m_sam_socket.reset(new i2p_stream(m_io_service));
		m_sam_socket->set_proxy(m_hostname, m_port);
		m_sam_socket->set_command(i2p_stream::cmd_create_session);
		m_sam_socket->set_session_id(m_session_id.c_str());

#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("i2p_stream::on_sam_connect");
#endif
		m_sam_socket->async_connect(tcp::endpoint()
			, boost::bind(&i2p_connection::on_sam_connect, this, _1, handler, m_sam_socket));
	}

	void i2p_connection::on_sam_connect(error_code const& ec, i2p_stream::handler_type const& h, boost::shared_ptr<i2p_stream>)
	{
#if defined TORRENT_ASIO_DEBUGGING
		complete_async("i2p_stream::on_sam_connect");
#endif
		m_state = sam_idle;

		if (ec)
		{
			h(ec);
			return;
		}

		do_name_lookup("ME", boost::bind(&i2p_connection::set_local_endpoint, this, _1, _2, h));
	}

	void i2p_connection::set_local_endpoint(error_code const& ec, char const* dest
		, i2p_stream::handler_type const& h)
	{
		if (!ec && dest != 0)
			m_i2p_local_endpoint = dest;
		else
			m_i2p_local_endpoint.clear();

		h(ec);
	}

	void i2p_connection::async_name_lookup(char const* name
		, i2p_connection::name_lookup_handler handler)
	{
		if (m_state == sam_idle && m_name_lookup.empty() && is_open())
			do_name_lookup(name, handler);
		else
			m_name_lookup.push_back(std::make_pair(std::string(name), handler));
	}

	void i2p_connection::do_name_lookup(std::string const& name
		, name_lookup_handler const& handler)
	{
		TORRENT_ASSERT(m_state == sam_idle);
		m_state = sam_name_lookup;
		m_sam_socket->set_name_lookup(name.c_str());
		boost::shared_ptr<i2p_stream::handler_type> h(new i2p_stream::handler_type(
			boost::bind(&i2p_connection::on_name_lookup, this, _1, handler, m_sam_socket)));
		m_sam_socket->send_name_lookup(h);
	}

	void i2p_connection::on_name_lookup(error_code const& ec
		, name_lookup_handler handler, boost::shared_ptr<i2p_stream>)
	{
		m_state = sam_idle;

		std::string name = m_sam_socket->name_lookup();
		if (!m_name_lookup.empty())
		{
			std::pair<std::string, name_lookup_handler>& nl = m_name_lookup.front();
			do_name_lookup(nl.first, nl.second);
			m_name_lookup.pop_front();
		}

		if (ec)
		{
			handler(ec, 0);
			return;
		}

		handler(ec, name.c_str());
	}

	i2p_stream::i2p_stream(io_service& io_service)
		: proxy_base(io_service)
		, m_id(0)
		, m_command(cmd_create_session)
		, m_state(0)
	{
#if TORRENT_USE_ASSERTS
		m_magic = 0x1337;
#endif
	}

	i2p_stream::~i2p_stream()
	{
#if TORRENT_USE_ASSERTS
		TORRENT_ASSERT(m_magic == 0x1337);
		m_magic = 0;
#endif
	}

	void i2p_stream::do_connect(error_code const& e, tcp::resolver::iterator i
		, boost::shared_ptr<handler_type> h)
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		if (e || i == tcp::resolver::iterator())
		{
			(*h)(e);
			error_code ec;
			close(ec);
			return;
		}

#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("i2p_stream::connected");
#endif
		m_sock.async_connect(i->endpoint(), boost::bind(
			&i2p_stream::connected, this, _1, h));
	}

	void i2p_stream::connected(error_code const& e, boost::shared_ptr<handler_type> h)
	{
		TORRENT_ASSERT(m_magic == 0x1337);
#if defined TORRENT_ASIO_DEBUGGING
		complete_async("i2p_stream::connected");
#endif
		if (handle_error(e, h)) return;

		// send hello command
		m_state = read_hello_response;
		static const char cmd[] = "HELLO VERSION MIN=3.0 MAX=3.0\n";

#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("i2p_stream::start_read_line");
#endif
		async_write(m_sock, boost::asio::buffer(cmd, sizeof(cmd) - 1)
			, boost::bind(&i2p_stream::start_read_line, this, _1, h));
//		fprintf(stderr, ">>> %s", cmd);
	}

	void i2p_stream::start_read_line(error_code const& e, boost::shared_ptr<handler_type> h)
	{
		TORRENT_ASSERT(m_magic == 0x1337);
#if defined TORRENT_ASIO_DEBUGGING
		complete_async("i2p_stream::start_read_line");
#endif
		if (handle_error(e, h)) return;

#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("i2p_stream::read_line");
#endif
		m_buffer.resize(1);
		async_read(m_sock, boost::asio::buffer(m_buffer)
			, boost::bind(&i2p_stream::read_line, this, _1, h));
	}

	void i2p_stream::read_line(error_code const& e, boost::shared_ptr<handler_type> h)
	{
		TORRENT_ASSERT(m_magic == 0x1337);
#if defined TORRENT_ASIO_DEBUGGING
		complete_async("i2p_stream::read_line");
#endif
		if (handle_error(e, h)) return;

		int read_pos = m_buffer.size();

		// look for \n which means end of the response
		if (m_buffer[read_pos - 1] != '\n')
		{
#if defined TORRENT_ASIO_DEBUGGING
			add_outstanding_async("i2p_stream::read_line");
#endif
			// read another byte from the socket
			m_buffer.resize(read_pos + 1);
			async_read(m_sock, boost::asio::buffer(&m_buffer[read_pos], 1)
				, boost::bind(&i2p_stream::read_line, this, _1, h));
			return;
		}
		m_buffer[read_pos - 1] = 0;

		if (m_command == cmd_incoming)
		{
			// this is the line containing the destination
			// of the incoming connection in an accept call
			m_dest = &m_buffer[0];
			(*h)(e);
			std::vector<char>().swap(m_buffer);
			return;
		}

		error_code invalid_response(i2p_error::parse_failed
			, get_i2p_category());

		// null-terminate the string and parse it
		m_buffer.push_back(0);
		char* ptr = &m_buffer[0];
		char* next = ptr;

		char const* expect1 = 0;
		char const* expect2 = 0;

		switch (m_state)
		{
			case read_hello_response:
				expect1 = "HELLO";
				expect2 = "REPLY";
				break;
			case read_connect_response:
			case read_accept_response:
				expect1 = "STREAM";
				expect2 = "STATUS";
				break;
			case read_session_create_response:
				expect1 = "SESSION";
				expect2 = "STATUS";
				break;
			case read_name_lookup_response:
				expect1 = "NAMING";
				expect2 = "REPLY";
				break;
		}

//		fprintf(stderr, "<<< %s\n", &m_buffer[0]);
		ptr = string_tokenize(next, ' ', &next);
		if (ptr == 0 || expect1 == 0 || strcmp(expect1, ptr)) { handle_error(invalid_response, h); return; }
		ptr = string_tokenize(next, ' ', &next);
		if (ptr == 0 || expect2 == 0 || strcmp(expect2, ptr)) { handle_error(invalid_response, h); return; }

		int result = 0;
//		char const* message = 0;
//		float version = 3.0f;

		for(;;)
		{
			char* name = string_tokenize(next, '=', &next);
			if (name == 0) break;
//			fprintf(stderr, "name=\"%s\"\n", name);
			char* ptr2 = string_tokenize(next, ' ', &next);
			if (ptr2 == 0) { handle_error(invalid_response, h); return; }
//			fprintf(stderr, "value=\"%s\"\n", ptr2);

			if (strcmp("RESULT", name) == 0)
			{
				if (strcmp("OK", ptr2) == 0)
					result = i2p_error::no_error;
				else if (strcmp("CANT_REACH_PEER", ptr2) == 0)
					result = i2p_error::cant_reach_peer;
				else if (strcmp("I2P_ERROR", ptr2) == 0)
					result = i2p_error::i2p_error;
				else if (strcmp("INVALID_KEY", ptr2) == 0)
					result = i2p_error::invalid_key;
				else if (strcmp("INVALID_ID", ptr2) == 0)
					result = i2p_error::invalid_id;
				else if (strcmp("TIMEOUT", ptr2) == 0)
					result = i2p_error::timeout;
				else if (strcmp("KEY_NOT_FOUND", ptr2) == 0)
					result = i2p_error::key_not_found;
				else if (strcmp("DUPLICATED_ID", ptr2) == 0)
					result = i2p_error::duplicated_id;
				else
					result = i2p_error::num_errors; // unknown error
			}
			else if (strcmp("MESSAGE", name) == 0)
			{
//				message = ptr2;
			}
			else if (strcmp("VERSION", name) == 0)
			{
//				version = float(atof(ptr2));
			}
			else if (strcmp("VALUE", name) == 0)
			{
				m_name_lookup = ptr2;
			}
			else if (strcmp("DESTINATION", name) == 0)
			{
				m_dest = ptr2;
			}
		}

		error_code ec(result, get_i2p_category());
		switch (result)
		{
			case i2p_error::no_error:
			case i2p_error::invalid_key:
				break;
			default:
			{
				handle_error (ec, h);
				return;
			}
		}

		switch (m_state)
		{
		case read_hello_response:
			switch (m_command)
			{
				case cmd_create_session:
					send_session_create(h);
					break;
				case cmd_accept:
					send_accept(h);
					break;
				case cmd_connect:
					send_connect(h);
					break;
				default:
					(*h)(e);
					std::vector<char>().swap(m_buffer);
			}
			break;
		case read_connect_response:
		case read_session_create_response:
		case read_name_lookup_response:
			(*h)(ec);
			std::vector<char>().swap(m_buffer);
			break;
		case read_accept_response:
			// the SAM bridge is waiting for an incoming
			// connection.
			// wait for one more line containing
			// the destination of the remote peer
			m_command = cmd_incoming;
			m_buffer.resize(1);
#if defined TORRENT_ASIO_DEBUGGING
			add_outstanding_async("i2p_stream::read_line");
#endif
			async_read(m_sock, boost::asio::buffer(m_buffer)
				, boost::bind(&i2p_stream::read_line, this, _1, h));
			break;
		}

		return;
	}

	void i2p_stream::send_connect(boost::shared_ptr<handler_type> h)
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		m_state = read_connect_response;
		char cmd[1024];
		int size = snprintf(cmd, sizeof(cmd), "STREAM CONNECT ID=%s DESTINATION=%s\n"
			, m_id, m_dest.c_str());
//		fprintf(stderr, ">>> %s", cmd);
#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("i2p_stream::start_read_line");
#endif
		async_write(m_sock, boost::asio::buffer(cmd, size)
			, boost::bind(&i2p_stream::start_read_line, this, _1, h));
	}

	void i2p_stream::send_accept(boost::shared_ptr<handler_type> h)
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		m_state = read_accept_response;
		char cmd[400];
		int size = snprintf(cmd, sizeof(cmd), "STREAM ACCEPT ID=%s\n", m_id);
//		fprintf(stderr, ">>> %s", cmd);
#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("i2p_stream::start_read_line");
#endif
		async_write(m_sock, boost::asio::buffer(cmd, size)
			, boost::bind(&i2p_stream::start_read_line, this, _1, h));
	}

	void i2p_stream::send_session_create(boost::shared_ptr<handler_type> h)
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		m_state = read_session_create_response;
		char cmd[400];
		int size = snprintf(cmd, sizeof(cmd), "SESSION CREATE STYLE=STREAM ID=%s DESTINATION=TRANSIENT\n"
			, m_id);
//		fprintf(stderr, ">>> %s", cmd);
#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("i2p_stream::start_read_line");
#endif
		async_write(m_sock, boost::asio::buffer(cmd, size)
			, boost::bind(&i2p_stream::start_read_line, this, _1, h));
	}

	void i2p_stream::send_name_lookup(boost::shared_ptr<handler_type> h)
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		m_state = read_name_lookup_response;
		char cmd[1024];
		int size = snprintf(cmd, sizeof(cmd), "NAMING LOOKUP NAME=%s\n", m_name_lookup.c_str());
//		fprintf(stderr, ">>> %s", cmd);
#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("i2p_stream::start_read_line");
#endif
		async_write(m_sock, boost::asio::buffer(cmd, size)
			, boost::bind(&i2p_stream::start_read_line, this, _1, h));
	}
}

#endif

