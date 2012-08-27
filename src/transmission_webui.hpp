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

#ifndef TORRENT_TRANSMISSION_WEBUI_HPP
#define TORRENT_TRANSMISSION_WEBUI_HPP

#include "webui.hpp"
#include "libtorrent/torrent_handle.hpp"
#include <boost/cstdint.hpp>
#include <vector>
#include <set>

struct jsmntok_t;

namespace libtorrent
{
	struct transmission_webui : webui_base
	{
		transmission_webui(session& s);
		~transmission_webui();

		virtual bool handle_http(mg_connection* conn,
			mg_request_info const* request_info);

		void handle_json_rpc(std::vector<char>& buf, jsmntok_t* tokens, char* buffer);
		void parse_ids(std::set<boost::uint32_t>& torrent_ids, jsmntok_t* args, char* buffer);
		void get_torrents(std::vector<torrent_handle>& handles, jsmntok_t* args
			, char* buffer);

		void add_torrent(std::vector<char>&, jsmntok_t* args, boost::int64_t tag, char* buffer);
		void add_torrent_multipart(mg_connection* conn, std::vector<char> const& post_body);
		void get_torrent(std::vector<char>&, jsmntok_t* args, boost::int64_t tag, char* buffer);
		void set_torrent(std::vector<char>&, jsmntok_t* args, boost::int64_t tag, char* buffer);
		void start_torrent(std::vector<char>&, jsmntok_t* args, boost::int64_t tag, char* buffer);
		void start_torrent_now(std::vector<char>&, jsmntok_t* args, boost::int64_t tag, char* buffer);
		void stop_torrent(std::vector<char>&, jsmntok_t* args, boost::int64_t tag, char* buffer);
		void verify_torrent(std::vector<char>&, jsmntok_t* args, boost::int64_t tag, char* buffer);
		void reannounce_torrent(std::vector<char>&, jsmntok_t* args, boost::int64_t tag, char* buffer);
		void remove_torrent(std::vector<char>&, jsmntok_t* args, boost::int64_t tag, char* buffer);
		void session_stats(std::vector<char>&, jsmntok_t* args, boost::int64_t tag, char* buffer);
		void get_session(std::vector<char>& buf, jsmntok_t* args, boost::int64_t tag, char* buffer);

	private:
		time_t m_start_time;
	};
}

#endif

