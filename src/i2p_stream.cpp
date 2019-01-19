/*

Copyright (c) 2009-2018, Arvid Norberg
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

#if TORRENT_USE_I2P

#include "libtorrent/i2p_stream.hpp"
#include "libtorrent/aux_/proxy_settings.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/string_util.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/hex.hpp" // for to_hex
#include "libtorrent/debug.hpp"

#include <functional>
#include <cstring>

using namespace std::placeholders;

namespace libtorrent {

	struct i2p_error_category final : boost::system::error_category
	{
		const char* name() const BOOST_SYSTEM_NOEXCEPT override
		{ return "i2p error"; }
		std::string message(int ev) const override
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
		boost::system::error_condition default_error_condition(
			int ev) const BOOST_SYSTEM_NOEXCEPT override
		{ return {ev, *this}; }
	};


	boost::system::error_category& i2p_category()
	{
		static i2p_error_category i2p_category;
		return i2p_category;
	}

	namespace i2p_error
	{
		boost::system::error_code make_error_code(i2p_error_code e)
		{
			return {e, i2p_category()};
		}
	}

	i2p_connection::i2p_connection(io_service& ios)
		: m_port(0)
		, m_state(sam_idle)
		, m_io_service(ios)
	{}

	i2p_connection::~i2p_connection() = default;

	void i2p_connection::close(error_code& e)
	{
		if (m_sam_socket) m_sam_socket->close(e);
	}

	aux::proxy_settings i2p_connection::proxy() const
	{
		aux::proxy_settings ret;
		ret.hostname = m_hostname;
		ret.port = std::uint16_t(m_port);
		ret.type = settings_pack::i2p_proxy;
		return ret;
	}

	void i2p_connection::open(std::string const& s, int port
		, i2p_stream::handler_type handler)
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
		aux::random_bytes(tmp);
		m_session_id.resize(sizeof(tmp)*2);
		aux::to_hex(tmp, &m_session_id[0]);

		m_sam_socket.reset(new i2p_stream(m_io_service));
		m_sam_socket->set_proxy(m_hostname, m_port);
		m_sam_socket->set_command(i2p_stream::cmd_create_session);
		m_sam_socket->set_session_id(m_session_id.c_str());

		ADD_OUTSTANDING_ASYNC("i2p_stream::on_sam_connect");
		m_sam_socket->async_connect(tcp::endpoint()
			, std::bind(&i2p_connection::on_sam_connect, this, _1
				, std::move(handler), m_sam_socket));
	}

	void i2p_connection::on_sam_connect(error_code const& ec
		, i2p_stream::handler_type& h, std::shared_ptr<i2p_stream>)
	{
		COMPLETE_ASYNC("i2p_stream::on_sam_connect");
		m_state = sam_idle;

		if (ec)
		{
			h(ec);
			return;
		}

		do_name_lookup("ME", std::bind(&i2p_connection::set_local_endpoint
			, this, _1, _2, std::move(h)));
	}

	void i2p_connection::set_local_endpoint(error_code const& ec, char const* dest
		, i2p_stream::handler_type& h)
	{
		if (!ec && dest != nullptr)
			m_i2p_local_endpoint = dest;
		else
			m_i2p_local_endpoint.clear();

		h(ec);
	}

	void i2p_connection::async_name_lookup(char const* name
		, i2p_connection::name_lookup_handler handler)
	{
		if (m_state == sam_idle && m_name_lookup.empty() && is_open())
			do_name_lookup(name, std::move(handler));
		else
			m_name_lookup.emplace_back(std::string(name)
				, std::move(handler));
	}

	void i2p_connection::do_name_lookup(std::string const& name
		, name_lookup_handler handler)
	{
		TORRENT_ASSERT(m_state == sam_idle);
		m_state = sam_name_lookup;
		m_sam_socket->set_name_lookup(name.c_str());
		m_sam_socket->send_name_lookup(std::bind(&i2p_connection::on_name_lookup
			, this, _1, std::move(handler), m_sam_socket));
	}

	void i2p_connection::on_name_lookup(error_code const& ec
		, name_lookup_handler& handler, std::shared_ptr<i2p_stream>)
	{
		m_state = sam_idle;

		std::string name = m_sam_socket->name_lookup();
		if (!m_name_lookup.empty())
		{
			std::pair<std::string, name_lookup_handler>& nl = m_name_lookup.front();
			do_name_lookup(nl.first, std::move(nl.second));
			m_name_lookup.pop_front();
		}

		if (ec)
		{
			handler(ec, nullptr);
			return;
		}

		handler(ec, name.c_str());
	}

	i2p_stream::i2p_stream(io_service& io_service)
		: proxy_base(io_service)
		, m_id(nullptr)
		, m_command(cmd_create_session)
		, m_state(read_hello_response)
	{
#if TORRENT_USE_ASSERTS
		m_magic = 0x1337;
#endif
	}

#if TORRENT_USE_ASSERTS
	i2p_stream::~i2p_stream()
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		m_magic = 0;
	}
#endif

	void i2p_stream::do_connect(error_code const& e, tcp::resolver::iterator i
		, handler_type h)
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		if (e || i == tcp::resolver::iterator())
		{
			h(e);
			error_code ec;
			close(ec);
			return;
		}

		ADD_OUTSTANDING_ASYNC("i2p_stream::connected");
		m_sock.async_connect(i->endpoint(), std::bind(
			&i2p_stream::connected, this, _1, std::move(h)));
	}

	void i2p_stream::connected(error_code const& e, handler_type& h)
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		COMPLETE_ASYNC("i2p_stream::connected");
		if (handle_error(e, h)) return;

		// send hello command
		m_state = read_hello_response;
		static const char cmd[] = "HELLO VERSION MIN=3.0 MAX=3.0\n";

		ADD_OUTSTANDING_ASYNC("i2p_stream::start_read_line");
		async_write(m_sock, boost::asio::buffer(cmd, sizeof(cmd) - 1)
			, std::bind(&i2p_stream::start_read_line, this, _1, std::move(h)));
	}

	void i2p_stream::start_read_line(error_code const& e, handler_type& h)
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		COMPLETE_ASYNC("i2p_stream::start_read_line");
		if (handle_error(e, h)) return;

		ADD_OUTSTANDING_ASYNC("i2p_stream::read_line");
		m_buffer.resize(1);
		async_read(m_sock, boost::asio::buffer(m_buffer)
			, std::bind(&i2p_stream::read_line, this, _1, std::move(h)));
	}

	void i2p_stream::read_line(error_code const& e, handler_type& h)
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		COMPLETE_ASYNC("i2p_stream::read_line");
		if (handle_error(e, h)) return;

		auto const read_pos = int(m_buffer.size());

		// look for \n which means end of the response
		if (m_buffer[read_pos - 1] != '\n')
		{
			ADD_OUTSTANDING_ASYNC("i2p_stream::read_line");
			// read another byte from the socket
			m_buffer.resize(read_pos + 1);
			async_read(m_sock, boost::asio::buffer(&m_buffer[read_pos], 1)
				, std::bind(&i2p_stream::read_line, this, _1, std::move(h)));
			return;
		}
		m_buffer[read_pos - 1] = 0;

		if (m_command == cmd_incoming)
		{
			// this is the line containing the destination
			// of the incoming connection in an accept call
			m_dest = &m_buffer[0];
			h(e);
			std::vector<char>().swap(m_buffer);
			return;
		}

		error_code invalid_response(i2p_error::parse_failed
			, i2p_category());

		string_view expect1;
		string_view expect2;

		switch (m_state)
		{
			case read_hello_response:
				expect1 = "HELLO"_sv;
				expect2 = "REPLY"_sv;
				break;
			case read_connect_response:
			case read_accept_response:
				expect1 = "STREAM"_sv;
				expect2 = "STATUS"_sv;
				break;
			case read_session_create_response:
				expect1 = "SESSION"_sv;
				expect2 = "STATUS"_sv;
				break;
			case read_name_lookup_response:
				expect1 = "NAMING"_sv;
				expect2 = "REPLY"_sv;
				break;
		}

		string_view remaining(m_buffer.data(), m_buffer.size());
		string_view token;

		std::tie(token, remaining) = split_string(remaining, ' ');
		if (expect1.empty() || expect1 != token)
		{ handle_error(invalid_response, h); return; }

		std::tie(token, remaining) = split_string(remaining, ' ');
		if (expect2.empty() || expect2 != token)
		{ handle_error(invalid_response, h); return; }

		int result = 0;

		for(;;)
		{
			string_view name;
			std::tie(name, remaining) = split_string(remaining, '=');
			if (name.empty()) break;
			string_view value;
			std::tie(value, remaining) = split_string(remaining, ' ');
			if (value.empty()) { handle_error(invalid_response, h); return; }

			if ("RESULT"_sv == name)
			{
				if ("OK"_sv == value)
					result = i2p_error::no_error;
				else if ("CANT_REACH_PEER"_sv == value)
					result = i2p_error::cant_reach_peer;
				else if ("I2P_ERROR"_sv == value)
					result = i2p_error::i2p_error;
				else if ("INVALID_KEY"_sv == value)
					result = i2p_error::invalid_key;
				else if ("INVALID_ID"_sv == value)
					result = i2p_error::invalid_id;
				else if ("TIMEOUT"_sv == value)
					result = i2p_error::timeout;
				else if ("KEY_NOT_FOUND"_sv == value)
					result = i2p_error::key_not_found;
				else if ("DUPLICATED_ID"_sv == value)
					result = i2p_error::duplicated_id;
				else
					result = i2p_error::num_errors; // unknown error
			}
			/*else if ("MESSAGE" == name)
			{
			}
			else if ("VERSION"_sv == name)
			{
			}*/
			else if ("VALUE"_sv == name)
			{
				m_name_lookup = value.to_string();
			}
			else if ("DESTINATION"_sv == name)
			{
				m_dest = value.to_string();
			}
		}

		error_code ec(result, i2p_category());
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
					send_session_create(std::move(h));
					break;
				case cmd_accept:
					send_accept(std::move(h));
					break;
				case cmd_connect:
					send_connect(std::move(h));
					break;
				case cmd_none:
				case cmd_name_lookup:
				case cmd_incoming:
					h(e);
					std::vector<char>().swap(m_buffer);
			}
			break;
		case read_connect_response:
		case read_session_create_response:
		case read_name_lookup_response:
			h(ec);
			std::vector<char>().swap(m_buffer);
			break;
		case read_accept_response:
			// the SAM bridge is waiting for an incoming
			// connection.
			// wait for one more line containing
			// the destination of the remote peer
			m_command = cmd_incoming;
			m_buffer.resize(1);
			ADD_OUTSTANDING_ASYNC("i2p_stream::read_line");
			async_read(m_sock, boost::asio::buffer(m_buffer)
				, std::bind(&i2p_stream::read_line, this, _1, h));
			break;
		}
	}

	void i2p_stream::send_connect(handler_type h)
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		m_state = read_connect_response;
		char cmd[1024];
		int size = std::snprintf(cmd, sizeof(cmd), "STREAM CONNECT ID=%s DESTINATION=%s\n"
			, m_id, m_dest.c_str());
		ADD_OUTSTANDING_ASYNC("i2p_stream::start_read_line");
		async_write(m_sock, boost::asio::buffer(cmd, std::size_t(size))
			, std::bind(&i2p_stream::start_read_line, this, _1, std::move(h)));
	}

	void i2p_stream::send_accept(handler_type h)
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		m_state = read_accept_response;
		char cmd[400];
		int size = std::snprintf(cmd, sizeof(cmd), "STREAM ACCEPT ID=%s\n", m_id);
		ADD_OUTSTANDING_ASYNC("i2p_stream::start_read_line");
		async_write(m_sock, boost::asio::buffer(cmd, std::size_t(size))
			, std::bind(&i2p_stream::start_read_line, this, _1, std::move(h)));
	}

	void i2p_stream::send_session_create(handler_type h)
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		m_state = read_session_create_response;
		char cmd[400];
		int size = std::snprintf(cmd, sizeof(cmd), "SESSION CREATE STYLE=STREAM ID=%s DESTINATION=TRANSIENT\n"
			, m_id);
		ADD_OUTSTANDING_ASYNC("i2p_stream::start_read_line");
		async_write(m_sock, boost::asio::buffer(cmd, std::size_t(size))
			, std::bind(&i2p_stream::start_read_line, this, _1, std::move(h)));
	}

	void i2p_stream::send_name_lookup(handler_type h)
	{
		TORRENT_ASSERT(m_magic == 0x1337);
		m_state = read_name_lookup_response;
		char cmd[1024];
		int size = std::snprintf(cmd, sizeof(cmd), "NAMING LOOKUP NAME=%s\n", m_name_lookup.c_str());
		ADD_OUTSTANDING_ASYNC("i2p_stream::start_read_line");
		async_write(m_sock, boost::asio::buffer(cmd, std::size_t(size))
			, std::bind(&i2p_stream::start_read_line, this, _1, std::move(h)));
	}
}

#endif
