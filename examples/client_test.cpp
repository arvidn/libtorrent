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

#include <iostream>
#include <fstream>
#include <iterator>
#include <exception>

#include <boost/format.hpp>
//#include <boost/date_time/posix_time/posix_time.hpp>

#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/http_settings.hpp"

#ifdef WIN32

#if defined(_MSC_VER)
#	define for if (false) {} else for
#endif

#include <windows.h>
#include <conio.h>

bool sleep_and_input(char* c)
{
	Sleep(500);
	if (kbhit())
	{
		*c = getch();
		return true;
	}
	return false;
};

void set_cursor(int x, int y)
{
	HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
	COORD c = {x, y};
	SetConsoleCursorPosition(h, c);
}

void clear()
{
	HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
	COORD c = {0, 0};
	DWORD n;
	FillConsoleOutputCharacter(h, ' ', 80 * 50, c, &n);
}

#else

#include <stdlib.h>
#include <stdio.h>

#include <termios.h>
#include <string.h>

struct set_keypress
{
	set_keypress()
	{
		termios new_settings;
		tcgetattr(0,&stored_settings);
		new_settings = stored_settings;
		// Disable canonical mode, and set buffer size to 1 byte
		new_settings.c_lflag &= (~ICANON);
		new_settings.c_cc[VTIME] = 0;
		new_settings.c_cc[VMIN] = 1;
		tcsetattr(0,TCSANOW,&new_settings);
	}
	~set_keypress() { tcsetattr(0,TCSANOW,&stored_settings); }
	termios stored_settings;
};

bool sleep_and_input(char* c)
{
	// sets the terminal to single-character mode
	// and resets when destructed
	set_keypress s;

	fd_set set;
	FD_ZERO(&set);
	FD_SET(0, &set);
	timeval tv = {1, 0};
	if (select(1, &set, 0, 0, &tv) > 0)
	{
		*c = getc(stdin);
		return true;
	}
	return false;
}

void set_cursor(int x, int y)
{
	std::cout << "\033[" << y << ";" << x << "H";
}

void clear()
{
	std::cout << "\033[2J";
}

#endif

std::string add_suffix(float val)
{
	const char* prefix[] = {"B", "kB", "MB", "GB", "TB"};
	const int num_prefix = sizeof(prefix) / sizeof(const char*);
	int i;
	for (i = 0; i < num_prefix; ++i)
	{
		if (val < 1024.f)
			return boost::lexical_cast<std::string>(val) + prefix[i];
		val /= 1024.f;
	}
	return boost::lexical_cast<std::string>(val) + prefix[i];
}

int main(int argc, char* argv[])
{
	using namespace libtorrent;

	// TEMPORARY
//	boost::filesystem::path::default_name_check(boost::filesystem::no_check);

	if (argc < 2)
	{
		std::cerr << "usage: ./client_test torrent-files ...\n"
			"to stop the client, press q.\n";
		return 1;
	}

	http_settings settings;
//	settings.proxy_ip = "192.168.0.1";
//	settings.proxy_port = 80;
//	settings.proxy_login = "hyd";
//	settings.proxy_password = "foobar";
	settings.user_agent = "example";

	try
	{
		std::vector<torrent_handle> handles;
		session s(6881, "E\x1");
//		s.set_upload_rate_limit(20 * 1024);
		s.set_http_settings(settings);
		for (int i = 0; i < argc-1; ++i)
		{
			try
			{
				std::ifstream in(argv[i+1], std::ios_base::binary);
				in.unsetf(std::ios_base::skipws);
				entry e = bdecode(std::istream_iterator<char>(in), std::istream_iterator<char>());
				torrent_info t(e);
//				t.convert_file_names();
				t.print(std::cout);
				handles.push_back(s.add_torrent(t, boost::filesystem::path("", boost::filesystem::native)));
			}
			catch (std::exception& e)
			{
				std::cout << e.what() << "\n";
			}
		}

		std::vector<peer_info> peers;
		std::vector<partial_piece_info> queue;

		for (;;)
		{
			char c;
			if (sleep_and_input(&c))
			{
				if (c == 'q') break;
			}

			clear();
			set_cursor(0, 0);
			for (std::vector<torrent_handle>::iterator i = handles.begin();
				i != handles.end();
				++i)
			{
				torrent_status s = i->status();

				switch(s.state)
				{
					case torrent_status::queued_for_checking:
						std::cout << "queued ";
						break;
					case torrent_status::checking_files:
						std::cout << "checking ";
						break;
					case torrent_status::downloading:
						std::cout << "dloading ";
						break;
					case torrent_status::seeding:
						std::cout << "seeding ";
						break;
				};

				// calculate download and upload speeds
				i->get_peer_info(peers);
				float down = 0.f;
				float up = 0.f;
				unsigned int total_down = s.total_download;
				unsigned int total_up = s.total_upload;
				int num_peers = peers.size();

				for (std::vector<peer_info>::iterator i = peers.begin();
					i != peers.end();
					++i)
				{
					down += i->down_speed;
					up += i->up_speed;
				}
/*
				std::cout << boost::format("%f%% p:%d d:(%s) %s/s u:(%s) %s/s\n")
					% (s.progress*100)
					% num_peers
					% add_suffix(total_down)
					% add_suffix(down)
					% add_suffix(total_up)
					% add_suffix(up);
*/
				std::cout << (s.progress*100) << "% p:" << num_peers << " d:("
					<< add_suffix(total_down) << ") " << add_suffix(down) << "/s u:("
					<< add_suffix(total_up) << ") " << add_suffix(up) << "/s\n";

				boost::posix_time::time_duration t = s.next_announce;
//				std::cout << "next announce: " << boost::posix_time::to_simple_string(t) << "\n";
				std::cout << "next announce: " << t.hours() << ":" << t.minutes() << ":" << t.seconds() << "\n";

				i->get_download_queue(queue);
				for (std::vector<partial_piece_info>::iterator i = queue.begin();
					i != queue.end();
					++i)
				{
					std::cout << i->piece_index << ": ";
					for (int j = 0; j < i->blocks_in_piece; ++j)
					{
						if (i->finished_blocks[j]) std::cout << "+";
						else if (i->requested_blocks[j]) std::cout << "-";
						else std::cout << ".";
					}
					std::cout << "\n";
				}
				std::cout << "___________________________________\n";
			}
		}
	}
	catch (std::exception& e)
	{
  		std::cout << e.what() << "\n";
	}
	return 0;
}
