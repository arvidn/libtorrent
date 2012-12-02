/*

Copyright (c) 2012, Arvid Norberg, Magnus Jonsson
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

#include <deque>
#include <getopt.h> // for getopt_long
#include <stdlib.h> // for daemon()
#include <syslog.h>
#include <boost/unordered_map.hpp>

#include "libtorrent/session.hpp"
#include "libtorrent/alert_types.hpp"
#include "webui.hpp"

extern "C" {
#include "mongoose.h"
}

using namespace libtorrent;

static void *handle_http(mg_event event,
	mg_connection* conn)
{
	const mg_request_info *request_info = mg_get_request_info(conn);
	if (event != MG_NEW_REQUEST) return NULL;
	if (request_info->user_data == NULL) return NULL;

	bool ret = reinterpret_cast<webui_base*>(request_info->user_data)->handle_http(
		conn, request_info);
	return ret ? (void*)"" : NULL;
}

webui_base::webui_base()
	: m_ctx(NULL)
{}

webui_base::~webui_base() {}

void webui_base::remove_handler(http_handler* h)
{
	std::vector<http_handler*>::iterator i = std::find(m_handlers.begin(), m_handlers.end(), h);
	if (i != m_handlers.end()) m_handlers.erase(i);
}

bool webui_base::handle_http(mg_connection* conn
		, mg_request_info const* request_info)
{
	for (std::vector<http_handler*>::iterator i = m_handlers.begin()
		, end(m_handlers.end()); i != end; ++i)
	{
		if ((*i)->handle_http(conn, request_info)) return true;
	}
	return false;
}

void webui_base::start(int port, char const* cert_path)
{
	if (m_ctx) mg_stop(m_ctx);

	// start web interface
	char port_str[20];
	snprintf(port_str, sizeof(port_str), "%d%s", port, cert_path ? "s" : "");
	const char *options[] = {"listening_ports", port_str, "ssl_certificate", cert_path ? cert_path : "", NULL};
	m_ctx = mg_start(&::handle_http, this, options);
}

void webui_base::stop()
{
	mg_stop(m_ctx);
	m_ctx = NULL;
}

