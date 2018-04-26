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

#ifndef TORRENT_I2P_STREAM_HPP_INCLUDED
#define TORRENT_I2P_STREAM_HPP_INCLUDED

#include "libtorrent/config.hpp"

#if TORRENT_USE_I2P

#include <list>
#include <string>
#include <vector>
#include <functional>

#include "libtorrent/proxy_base.hpp"
#include "libtorrent/string_util.hpp"
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/aux_/proxy_settings.hpp"

namespace libtorrent {

	namespace i2p_error {

		// error values for the i2p_category error_category.
		enum i2p_error_code
		{
			no_error = 0,
			parse_failed,
			cant_reach_peer,
			i2p_error,
			invalid_key,
			invalid_id,
			timeout,
			key_not_found,
			duplicated_id,
			num_errors
		};

		// hidden
		TORRENT_EXPORT boost::system::error_code make_error_code(i2p_error_code e);
	}

	// returns the error category for I2P errors
	TORRENT_EXPORT boost::system::error_category& i2p_category();

#if TORRENT_ABI_VERSION == 1
	TORRENT_DEPRECATED
	inline boost::system::error_category& get_i2p_category()
	{ return i2p_category(); }
#endif

class i2p_stream : public proxy_base
{
public:

	explicit i2p_stream(io_service& io_service);
#if TORRENT_USE_ASSERTS
	~i2p_stream();
#endif

	enum command_t
	{
		cmd_none,
		cmd_create_session,
		cmd_connect,
		cmd_accept,
		cmd_name_lookup,
		cmd_incoming
	};

	void set_command(command_t c) { m_command = c; }

	void set_session_id(char const* id) { m_id = id; }

	void set_destination(string_view d) { m_dest = d.to_string(); }
	std::string const& destination() { return m_dest; }

	template <class Handler>
	void async_connect(endpoint_type const&, Handler const& handler)
	{
		// since we don't support regular endpoints, just ignore the one
		// provided and use m_dest.

		// the connect is split up in the following steps:
		// 1. resolve name of proxy server
		// 2. connect to SAM bridge
		// 4 send command message (CONNECT/ACCEPT)

		using std::placeholders::_1;
		using std::placeholders::_2;
		tcp::resolver::query q(m_hostname, to_string(m_port).data());
		m_resolver.async_resolve(q, std::bind(
			&i2p_stream::do_connect, this, _1, _2, handler_type(std::move(handler))));
	}

	std::string name_lookup() const { return m_name_lookup; }
	void set_name_lookup(char const* name) { m_name_lookup = name; }

	void send_name_lookup(handler_type h);

private:
	// explicitly disallow assignment, to silence msvc warning
	i2p_stream& operator=(i2p_stream const&);

	void do_connect(error_code const& e, tcp::resolver::iterator i
		, handler_type h);
	void connected(error_code const& e, handler_type& h);
	void start_read_line(error_code const& e, handler_type& h);
	void read_line(error_code const& e, handler_type& h);
	void send_connect(handler_type h);
	void send_accept(handler_type h);
	void send_session_create(handler_type h);

	// send and receive buffer
	aux::vector<char> m_buffer;
	char const* m_id;
	command_t m_command;
	std::string m_dest;
	std::string m_name_lookup;

	enum state_t
	{
		read_hello_response,
		read_connect_response,
		read_accept_response,
		read_session_create_response,
		read_name_lookup_response
	};

	state_t m_state;
#if TORRENT_USE_ASSERTS
	int m_magic;
#endif
};

class i2p_connection
{
public:
	explicit i2p_connection(io_service& ios);
	~i2p_connection();

	aux::proxy_settings proxy() const;

	bool is_open() const
	{
		return m_sam_socket
			&& m_sam_socket->is_open()
			&& m_state != sam_connecting;
	}
	void open(std::string const& hostname, int port, i2p_stream::handler_type h);
	void close(error_code&);

	char const* session_id() const { return m_session_id.c_str(); }
	std::string const& local_endpoint() const { return m_i2p_local_endpoint; }

	using name_lookup_handler = std::function<void(error_code const&, char const*)>;
	void async_name_lookup(char const* name, name_lookup_handler handler);

private:
	// explicitly disallow assignment, to silence msvc warning
	i2p_connection& operator=(i2p_connection const&);

	void on_sam_connect(error_code const& ec, i2p_stream::handler_type& h
		, std::shared_ptr<i2p_stream>);
	void do_name_lookup(std::string const& name
		, name_lookup_handler h);
	void on_name_lookup(error_code const& ec
		, name_lookup_handler& handler
		, std::shared_ptr<i2p_stream>);

	void set_local_endpoint(error_code const& ec, char const* dest
		, i2p_stream::handler_type& h);

	// to talk to i2p SAM bridge
	std::shared_ptr<i2p_stream> m_sam_socket;
	std::string m_hostname;
	int m_port;

	// our i2p endpoint key
	std::string m_i2p_local_endpoint;
	std::string m_session_id;

	std::list<std::pair<std::string, name_lookup_handler>> m_name_lookup;

	enum state_t
	{
		sam_connecting,
		sam_name_lookup,
		sam_idle
	};

	state_t m_state;

	io_service& m_io_service;
};

}

namespace boost { namespace system {

template<>
struct is_error_code_enum<libtorrent::i2p_error::i2p_error_code>
{ static const bool value = true; };

} }

#endif // TORRENT_USE_I2P

#endif
