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

#ifndef TORRENT_TRANSMISSION_WEBUI_HPP
#define TORRENT_TRANSMISSION_WEBUI_HPP

#include "webui.hpp"

extern "C" {
#include "jsmn.h"
}

#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include <boost/cstdint.hpp>
#include <vector>
#include <set>

namespace libtorrent
{
	struct save_settings_interface;
	struct permissions_interface;
	struct auth_interface;

	struct transmission_webui : http_handler
	{
		transmission_webui(session& s, save_settings_interface* sett, auth_interface const* auth = NULL);
		~transmission_webui();

		void set_params_model(add_torrent_params const& p)
		{ m_params_model = p; }

		virtual bool handle_http(mg_connection* conn,
			mg_request_info const* request_info);

		void add_torrent(std::vector<char>&, jsmntok_t* args, boost::int64_t tag, char* buffer, permissions_interface const* p);
		void get_torrent(std::vector<char>&, jsmntok_t* args, boost::int64_t tag, char* buffer, permissions_interface const* p);
		void set_torrent(std::vector<char>&, jsmntok_t* args, boost::int64_t tag, char* buffer, permissions_interface const* p);
		void start_torrent(std::vector<char>&, jsmntok_t* args, boost::int64_t tag, char* buffer, permissions_interface const* p);
		void start_torrent_now(std::vector<char>&, jsmntok_t* args, boost::int64_t tag, char* buffer, permissions_interface const* p);
		void stop_torrent(std::vector<char>&, jsmntok_t* args, boost::int64_t tag, char* buffer, permissions_interface const* p);
		void verify_torrent(std::vector<char>&, jsmntok_t* args, boost::int64_t tag, char* buffer, permissions_interface const* p);
		void reannounce_torrent(std::vector<char>&, jsmntok_t* args, boost::int64_t tag, char* buffer, permissions_interface const* p);
		void remove_torrent(std::vector<char>&, jsmntok_t* args, boost::int64_t tag, char* buffer, permissions_interface const* p);
		void session_stats(std::vector<char>&, jsmntok_t* args, boost::int64_t tag, char* buffer, permissions_interface const* p);
		void get_session(std::vector<char>& buf, jsmntok_t* args, boost::int64_t tag, char* buffer, permissions_interface const* p);
		void set_session(std::vector<char>& buf, jsmntok_t* args, boost::int64_t tag, char* buffer, permissions_interface const* p);

	private:

		void get_torrents(std::vector<torrent_handle>& handles, jsmntok_t* args
			, char* buffer);
		void handle_json_rpc(std::vector<char>& buf, jsmntok_t* tokens, char* buffer, permissions_interface const* p);
		void parse_ids(std::set<boost::uint32_t>& torrent_ids, jsmntok_t* args, char* buffer);

		time_t m_start_time;
		session& m_ses;
		auth_interface const* m_auth;
		save_settings_interface* m_settings;
		add_torrent_params m_params_model;
	};
}

#endif

