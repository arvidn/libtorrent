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

#ifndef TORRENT_SAVE_RESUME_HPP
#define TORRENT_SAVE_RESUME_HPP

#include "libtorrent/session.hpp"
#include "libtorrent/deadline_timer.hpp"
#include "libtorrent/io_service.hpp"
#include "libtorrent/thread.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/alert_observer.hpp"

#include <string>
#include <map>

namespace libtorrent
{
	struct alert_handler;

	struct save_resume : alert_observer
	{
		save_resume(session& s, std::string const& resume_dir, alert_handler* alerts);
		~save_resume();

		void load(error_code& ec, add_torrent_params model);

		// implements alert_observer
		virtual void handle_alert(alert const* a);

		void save_all();
		bool ok_to_quit() const { return m_num_in_flight == 0; }

	private:

		session& m_ses;
		alert_handler* m_alerts;
		std::string m_resume_dir;

		// all torrents currently loaded
		boost::unordered_set<torrent_handle> m_torrents;

		// the next torrent to save (may point to end)
		boost::unordered_set<torrent_handle>::iterator m_cursor;

		// the number of times the cursor has been incremented
		// since the last time it wrapped
		int m_cursor_index;

		// the last time we wrapped the cursor and started
		// saving torrents from the start again.
		ptime m_last_save_wrap;

		// save resum data for all torrents every X seconds
		// must be at least 1
		time_duration m_interval;

		int m_num_in_flight;
	};
}

#endif

