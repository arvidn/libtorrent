/*

Copyright (c) 2003, Arvid Norberg
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

#ifndef TORRENT_HTTP_TRACKER_CONNECTION_HPP_INCLUDED
#define TORRENT_HTTP_TRACKER_CONNECTION_HPP_INCLUDED

#include <vector>
#include <string>
#include <utility>
#include <ctime>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/shared_ptr.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/cstdint.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/socket.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/http_settings.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/peer.hpp"
#include "libtorrent/tracker_manager.hpp"

namespace libtorrent
{

	class http_tracker_connection: public tracker_connection
	{
	friend class tracker_manager;
	public:

		http_tracker_connection(
			tracker_manager& man
			, tracker_request const& req
			, std::string const& hostname
			, unsigned short port
			, std::string request
			, boost::weak_ptr<request_callback> c
			, const http_settings& stn
			, std::string const& password = "");
		virtual bool tick();
		virtual bool send_finished() const
		{ return m_send_buffer.empty(); }
		virtual tracker_request const& tracker_req() const
		{ return m_req; }

	private:

		void init_send_buffer(
			std::string const& hostname
			, std::string const& request);

		void parse(const entry& e);
		peer_entry extract_peer_info(const entry& e);

		tracker_manager& m_man;
		enum { read_status, read_header, read_body } m_state;

		enum { plain, gzip } m_content_encoding;
		int m_content_length;
		std::string m_location;

		boost::shared_ptr<socket> m_socket;
		int m_recv_pos;
		std::vector<char> m_buffer;
		std::string m_send_buffer;

		// used for time outs
		boost::posix_time::ptime m_request_time;

		std::string m_server_message;
		std::string m_server_protocol;

		const http_settings& m_settings;
		tracker_request m_req;
		std::string m_password;
		int m_code;

		// server string in http-reply
		std::string m_server;
	};

}

#endif // TORRENT_HTTP_TRACKER_CONNECTION_HPP_INCLUDED
