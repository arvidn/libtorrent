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

#include "websocket_handler.hpp"
#include "libtorrent/io.hpp"
#include <boost/make_shared.hpp>
#include "local_mongoose.h"

namespace libtorrent
{
	// TODO: this should be a writev-like function
	bool websocket_handler::send_packet(mg_connection* conn, int type, char const* buffer, int len)
	{
		mutex::scoped_lock l(m_mutex);

		namespace io = libtorrent::detail;

		std::map<mg_connection*, boost::shared_ptr<mutex> >::iterator i
			= m_open_sockets.find(conn);
		if (i == m_open_sockets.end()) return false;
		boost::shared_ptr<mutex> m = i->second;
		mutex::scoped_lock l2(*m);
		l.unlock();

		// header
		int header_len = 2;
		boost::uint8_t h[20];
		h[0] = 0x80 | (type & 0xf); 
		if (len < 126)
			h[1] = len;
		else if (len < 65536)
		{
			h[1] = 126;
			boost::uint8_t* ptr = &h[2];
			io::write_uint16(len, ptr);
			header_len = 4;
		}
		else
		{
			h[1] = 127;
			boost::uint8_t* ptr = &h[2];
			io::write_uint64(len, ptr);
			header_len = 10;
		}

		// TODO: it would be nice to have an mg_writev()
		int ret = mg_write(conn, h, header_len);
		if (ret < header_len) return false;

		if (len > 0)
		{
			ret = mg_write(conn, buffer, len);
			if (ret < len) return false;
		}
		return true;
	}

	bool websocket_handler::handle_websocket_connect(mg_connection* conn
		, mg_request_info const* request_info)
	{
		mutex::scoped_lock l(m_mutex);
		m_open_sockets.insert(std::make_pair(conn, boost::make_shared<mutex>()));
		return true;
	}

	// TODO: it would be nice to have a layer that merges fragments.
	// there need to be a number of fragments per frame limit and
	// a total size limit per frame
	// This layer should also handle ping messages
//	bool libtorrent_webui::handle_websocket_data(mg_connection* conn
//		, int bits, char* data, size_t length)
//	{
//	}

	void websocket_handler::handle_end_request(mg_connection* conn)
	{
		mutex::scoped_lock l(m_mutex);
		std::map<mg_connection*, boost::shared_ptr<mutex> >::iterator i
			= m_open_sockets.find(conn);
		if (i == m_open_sockets.end()) return;

		m_open_sockets.erase(i);
	}
}

