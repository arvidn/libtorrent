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

#ifndef TORRENT_LIBTORRENT_WEBUI_HPP
#define TORRENT_LIBTORRENT_WEBUI_HPP

#include "websocket_handler.hpp"
#include "libtorrent/atomic.hpp"
#include "libtorrent/torrent_handle.hpp"

struct mg_connection;

namespace libtorrent
{
	struct permissions_interface;
	struct torrent_history;
	struct auth_interface;
	class session;

	struct libtorrent_webui : websocket_handler
	{
		libtorrent_webui(session& ses, torrent_history const* hist, auth_interface const* auth);
		~libtorrent_webui();

		virtual bool handle_websocket_connect(mg_connection* conn,
			mg_request_info const* request_info);
		virtual bool handle_websocket_data(mg_connection* conn
			, int bits, char* data, size_t length);

		struct conn_state
		{
			mg_connection* conn;
			int function_id;
			boost::uint16_t transaction_id;
			char* data;
			int len;
			permissions_interface const* perms;
		};

		bool get_torrent_updates(conn_state* st);
		bool start(conn_state* st);
		bool stop(conn_state* st);
		bool set_auto_managed(conn_state* st);
		bool clear_auto_managed(conn_state* st);
		bool queue_up(conn_state* st);
		bool queue_down(conn_state* st);
		bool queue_top(conn_state* st);
		bool queue_bottom(conn_state* st);
		bool remove(conn_state* st);
		bool remove_and_data(conn_state* st);
		bool force_recheck(conn_state* st);
		bool set_sequential_download(conn_state* st);
		bool clear_sequential_download(conn_state* st);

		// parse the arguments to the simple torrent commands
		int parse_torrent_args(std::vector<torrent_handle>& torrents, conn_state* st);

		bool call_rpc(mg_connection* conn, int function, char const* data, int len);

		bool respond(conn_state* st, int error, int val);

		// respond with an error to an RPC
		bool error(conn_state* st, int error);

		enum error_t
		{
			no_error,
			no_such_function,
			invalid_number_of_args,
			invalid_argument_type,
			invalid_argument,
			truncated_message,
		};

	private:

		session& m_ses;
		torrent_history const* m_hist;
		auth_interface const* m_auth;
		atomic_count m_transaction_id;

	};
}

#endif


