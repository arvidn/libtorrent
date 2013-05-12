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

#ifndef TORRENT_WEBSOCKET_HPP
#define TORRENT_WEBSOCKET_HPP

#include "webui.hpp"
#include "libtorrent/thread.hpp"
#include <map>
#include <boost/shared_ptr.hpp>
#include <boost/cstdint.hpp>

namespace libtorrent
{
	struct websocket_handler : http_handler
	{
		bool send_packet(mg_connection* conn, int type, char const* buffer, int len);
		virtual bool handle_websocket_connect(mg_connection* conn,
			mg_request_info const* request_info);
		virtual void handle_end_request(mg_connection* conn);

	private:
	
		// all currently alive web sockets
		std::map<mg_connection*, boost::shared_ptr<mutex> > m_open_sockets;

		// for now, serialize all access
		mutex m_mutex;

	};
}

#endif

