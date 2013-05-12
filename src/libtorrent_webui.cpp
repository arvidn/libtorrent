/*

Copyright (c) 2013, Arvid Norberg
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

#include "libtorrent_webui.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/buffer.hpp"
#include "local_mongoose.h"
#include <string.h>

namespace libtorrent
{
	namespace io = libtorrent::detail;

	libtorrent_webui::libtorrent_webui(auth_interface const* auth)
		: m_auth(auth)
	{}

	libtorrent_webui::~libtorrent_webui() {}

	bool libtorrent_webui::handle_websocket_connect(mg_connection* conn,
		mg_request_info const* request_info)
	{
		// we only provide access to /bt/control
		if (strcmp("/bt/control", request_info->uri) != 0) return false;

		// authenticate
		permissions_interface const* perms = parse_http_auth(conn, m_auth);
		if (!perms)
		{
			mg_printf(conn, "HTTP/1.1 401 Unauthorized\r\n"
				"WWW-Authenticate: Basic realm=\"BitTorrent\"\r\n"
				"Content-Length: 0\r\n\r\n");
			return true;
		}

		return websocket_handler::handle_websocket_connect(conn, request_info);
	}

	struct rpc_entry
	{
		char const* name;
		int(libtorrent_webui::*handler)(mg_connection*, boost::uint16_t, char*, int);
	};

	rpc_entry functions[] =
	{
		{ "start", &libtorrent_webui::start },
		{ "stop", &libtorrent_webui::stop },
		{ "set-auto-managed", &libtorrent_webui::set_auto_managed },
		{ "clear-auto-managed", &libtorrent_webui::clear_auto_managed },
		{ "queue-up", &libtorrent_webui::queue_up },
		{ "queue-down", &libtorrent_webui::queue_down },
		{ "queue-top", &libtorrent_webui::queue_top },
		{ "queue-bottom", &libtorrent_webui::queue_bottom },
		{ "remove", &libtorrent_webui::remove },
		{ "remove_and_data", &libtorrent_webui::remove_and_data },
		{ "force_recheck", &libtorrent_webui::force_recheck },
		{ "set-sequential-download", &libtorrent_webui::set_sequential_download },
		{ "clear-sequential-download", &libtorrent_webui::clear_sequential_download },
	};

	int libtorrent_webui::start(mg_connection* conn, boost::uint16_t tid, char* data, int length) {}
	int libtorrent_webui::stop(mg_connection* conn, boost::uint16_t tid, char* data, int length) {}
	int libtorrent_webui::set_auto_managed(mg_connection* conn, boost::uint16_t tid, char* data, int length) {}
	int libtorrent_webui::clear_auto_managed(mg_connection* conn, boost::uint16_t tid, char* data, int length) {}
	int libtorrent_webui::queue_up(mg_connection* conn, boost::uint16_t tid, char* data, int length) {}
	int libtorrent_webui::queue_down(mg_connection* conn, boost::uint16_t tid, char* data, int length) {}
	int libtorrent_webui::queue_top(mg_connection* conn, boost::uint16_t tid, char* data, int length) {}
	int libtorrent_webui::queue_bottom(mg_connection* conn, boost::uint16_t tid, char* data, int length) {}
	int libtorrent_webui::remove(mg_connection* conn, boost::uint16_t tid, char* data, int length) {}
	int libtorrent_webui::remove_and_data(mg_connection* conn, boost::uint16_t tid, char* data, int length) {}
	int libtorrent_webui::force_recheck(mg_connection* conn, boost::uint16_t tid, char* data, int length) {}
	int libtorrent_webui::set_sequential_download(mg_connection* conn, boost::uint16_t tid, char* data, int length) {}
	int libtorrent_webui::clear_sequential_download(mg_connection* conn, boost::uint16_t tid, char* data, int length) {}

	char const* fun_name(int function_id)
	{
		if (function_id < 0 || function_id >= sizeof(functions)/sizeof(functions[0]))
		{
			return "unknown function";
		}

		return functions[function_id].name;
	}

	bool libtorrent_webui::handle_websocket_data(mg_connection* conn
		, int bits, char* data, size_t length)
	{
		// TODO: this should really be handled at one layer below
		// ping
		if ((bits & 0xf) == 0x9)
		{
			// send pong
			return send_packet(conn, 0xa, NULL, 0);
		}

		// only support binary, non-fragmented frames
		if ((bits & 0xf) != 0x2) return false;

		// parse RPC message

		// RPC call is always at least 4 bytes.
		if (length < 4) return false;

		char const* ptr = data;
		boost::uint8_t function_id = io::read_uint8(ptr);
		boost::uint16_t transaction_id = io::read_uint16(ptr);
		boost::uint8_t num_args = io::read_uint8(ptr);

		if (function_id & 0x80)
		{
			// this is a response to a function call
			fprintf(stderr, "RETURNED: %s (status: %d)\n", fun_name(function_id & 0x7f), num_args);
		}
		else
		{
			fprintf(stderr, "CALL: %s (%d arguments)\n", fun_name(function_id), num_args);
			if (function_id >= 0 && function_id < sizeof(functions)/sizeof(functions[0]))
			{
				this->*(functions[function_id].handler)(conn, transaction_id, ptr, data + len - ptr);
			}
			else
			{
				// TODO: respond with unknown function
			}
		}
		
	}

	bool libtorrent_webui::call_rpc(mg_connection* conn, int function, int num_args, char const* data, int len)
	{
		buffer buf(len + 4);
		char* ptr = &buf[0];
		TORRENT_ASSERT(function >= 0 && function < 128);

		// function id
		io::write_uint8(function, ptr);

		// transaction id
		boost::uint16_t tid = m_transaction_id++;
		io::write_uint16(tid, ptr);

		// num-args
		io::write_uint8(num_args, ptr);

		if (len > 0) memcpy(ptr, data, len);

		return send_packet(conn, 0x2, &buf[0], buf.size());
	}

}

