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

#ifndef TORRENT_UT_WEBUI_HPP
#define TORRENT_UT_WEBUI_HPP

#include "webui.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include <boost/cstdint.hpp>
#include <vector>
#include <set>

namespace libtorrent
{
	struct utorrent_webui : http_handler
	{
		utorrent_webui(session& s);
		~utorrent_webui();

		void set_params_model(add_torrent_params const& p)
		{ m_params_model = p; }

		virtual bool handle_http(mg_connection* conn,
			mg_request_info const* request_info);

		void start(std::vector<char>&, char const* args);
		void stop(std::vector<char>&, char const* args);
		void force_start(std::vector<char>&, char const* args);
		void recheck(std::vector<char>&, char const* args);
		void remove_torrent(std::vector<char>&, char const* args);
		void remove_torrent_and_data(std::vector<char>&, char const* args);

		void queue_up(std::vector<char>&, char const* args);
		void queue_down(std::vector<char>&, char const* args);
		void queue_top(std::vector<char>&, char const* args);
		void queue_bottom(std::vector<char>&, char const* args);

		void get_settings(std::vector<char>&, char const* args);
		void set_settings(std::vector<char>&, char const* args);

		void get_properties(std::vector<char>&, char const* args);

		void send_file_list(std::vector<char>&, char const* args);
		void print_torrent_list(std::vector<char>&, char const* args);

	private:

		std::vector<torrent_handle> parse_torrents(char const* args) const;
		
		time_t m_start_time;
		session& m_ses;
		add_torrent_params m_params_model;
		std::string m_webui_cookie;

		int m_version;
		std::string m_token;
	};
}

#endif

