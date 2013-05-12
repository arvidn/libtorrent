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
#include "auth.hpp"
#include "libtorrent/atomic.hpp"

struct mg_connection;

namespace libtorrent
{
	struct libtorrent_webui : websocket_handler
	{
		libtorrent_webui(auth_interface const* auth);
		~libtorrent_webui();

		virtual bool handle_websocket_connect(mg_connection* conn,
			mg_request_info const* request_info);
		virtual bool handle_websocket_data(mg_connection* conn
			, int bits, char* data, size_t length);

		int start(mg_connection* conn, boost::uint16_t tid, char* data, int length);
		int stop(mg_connection* conn, boost::uint16_t tid, char* data, int length);
		int set_auto_managed(mg_connection* conn, boost::uint16_t tid, char* data, int length);
		int clear_auto_managed(mg_connection* conn, boost::uint16_t tid, char* data, int length);
		int queue_up(mg_connection* conn, boost::uint16_t tid, char* data, int length);
		int queue_down(mg_connection* conn, boost::uint16_t tid, char* data, int length);
		int queue_top(mg_connection* conn, boost::uint16_t tid, char* data, int length);
		int queue_bottom(mg_connection* conn, boost::uint16_t tid, char* data, int length);
		int remove(mg_connection* conn, boost::uint16_t tid, char* data, int length);
		int remove_and_data(mg_connection* conn, boost::uint16_t tid, char* data, int length);
		int force_recheck(mg_connection* conn, boost::uint16_t tid, char* data, int length);
		int set_sequential_download(mg_connection* conn, boost::uint16_t tid, char* data, int length);
		int clear_sequential_download(mg_connection* conn, boost::uint16_t tid, char* data, int length);

		bool call_rpc(mg_connection* conn, int function, int num_args, char const* data, int len);

	private:

		auth_interface const* m_auth;
		atomic_count m_transaction_id;

	};
}

#endif


