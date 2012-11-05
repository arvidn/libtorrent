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

#ifndef TORRENT_AUTO_LOAD_HPP
#define TORRENT_AUTO_LOAD_HPP

#include "libtorrent/session.hpp"
#include "libtorrent/deadline_timer.hpp"
#include "libtorrent/io_service.hpp"
#include "libtorrent/thread.hpp"

namespace libtorrent
{
	struct save_settings_interface;

	struct auto_load
	{
		auto_load(session& s, save_settings_interface* sett = NULL);
		~auto_load();

		void set_params_model(add_torrent_params const& p);
		add_torrent_params params_model() const;

		void set_auto_load_dir(std::string const& dir);
		std::string const& auto_load_dir() const { return m_dir; }

		int scan_interval() const { return m_scan_interval; }
		void set_scan_interval(int s);

		void set_remove_files(bool r);
		bool remove_files() const;

	private:

		void on_scan(error_code const& ec);

		void thread_fun();

		session& m_ses;
		boost::asio::io_service m_ios;
		deadline_timer m_timer;
		save_settings_interface* m_settings;

		// whether or not to remove .torrent files
		// as they are loaded
		bool m_remove_files;

		// when not removing files, keep track of
		// the ones we've already loaded to not
		// add them again
		std::set<std::string> m_already_loaded;

		add_torrent_params m_params_model;
		std::string m_dir;
		int m_scan_interval;
		bool m_abort;

		// used to protect m_abort, m_scan_interval, m_dir,
		// m_remove_files and m_params_model
		mutable mutex m_mutex;

		// this needs to be last in order to be initialized
		// last in the constructor. This way the object is
		// guaranteed to be completely constructed by the time
		// the thread function is started
		thread m_thread;
	};
}

#endif

