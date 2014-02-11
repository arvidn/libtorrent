/*

Copyright (c) 2014, Arvid Norberg
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

#ifndef TORRENT_TEXT_UI_HPP
#define TORRENT_TEXT_UI_HPP

#include <string>
#include "libtorrent/alert_observer.hpp"

extern "C" {
#include <ncurses.h>
#include <cdk/cdk.h>
}

namespace libtorrent
{
	struct alert_handler;

	struct screen
	{
		screen();
		~screen();

		CDKSCREEN* native_handle() { return m_screen; }

		int width() const;
		int height() const;

		void refresh();
	private:
		CDKSCREEN* m_screen;
	};

	struct window
	{
		virtual int width() const = 0;
		virtual int height() const = 0;
		virtual void set_pos(int x, int y, int width, int height) = 0;
		virtual ~window() {}
	};

	struct log_window : window
	{
		log_window(screen& scr, int x, int y, int width, int height);
		~log_window();

		void log_line(std::string l);

		CDKSWINDOW* native_handle() { return m_win; }

		virtual int width() const;
		virtual int height() const;
		virtual void set_pos(int x, int y, int width, int height);
	private:
		CDKSWINDOW* m_win;
	};

	struct error_log : log_window, alert_observer
	{
		error_log(screen& scr, int x, int y, int width, int height
			, alert_handler* alerts);
		~error_log();

	private:
		virtual void handle_alert(alert const* a);
		alert_handler* m_alerts;
	};

	struct torrent_list : window, alert_observer
	{
	
	private:
		virtual void handle_alert(alert const* a);
		alert_handler* m_alerts;
	};
}

#endif

