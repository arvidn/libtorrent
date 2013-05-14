/*

Copyright (c) 2012, Arvid Norberg
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

#ifndef TORRENT_WEBUI_HPP
#define TORRENT_WEBUI_HPP

#include <vector>
#include <string>

struct mg_context;
struct mg_connection;
struct mg_request_info;

struct http_handler
{
	virtual bool handle_http(mg_connection* conn,
		mg_request_info const* request_info) { return false; }
	virtual bool handle_websocket_connect(mg_connection* conn,
		mg_request_info const* request_info) { return false; }
	virtual bool handle_websocket_data(mg_connection* conn
		, int bits, char* data, size_t length) { return false; }
	virtual void handle_end_request(mg_connection* conn) {}
};

namespace libtorrent
{
	class session;

	struct webui_base
	{
		webui_base();
		~webui_base();

		void add_handler(http_handler* h)
		{ m_handlers.push_back(h); }

		void remove_handler(http_handler* h);

		void start(int port, char const* cert_path = 0, int num_threads = 10);
		void stop();
		bool is_running() const;

		bool handle_http(mg_connection* conn
			, mg_request_info const* request_info);
		bool handle_websocket_connect(mg_connection* conn
			, mg_request_info const* request_info);
		bool handle_websocket_data(mg_connection* conn, int bits, char* data, size_t length);
		void handle_end_request(mg_connection* conn);
	
		void set_document_root(std::string r) { m_document_root = r; }

	private:

		std::vector<http_handler*> m_handlers;
		std::string m_document_root;

		mg_context* m_ctx;
	};

}

#endif

