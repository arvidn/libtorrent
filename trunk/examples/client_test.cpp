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
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/bind.hpp>

#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/http_settings.hpp"
#include "libtorrent/identify_client.hpp"

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
	CONSOLE_SCREEN_BUFFER_INFO si;
	HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
	GetConsoleScreenBufferInfo(h, &si);
	COORD c = {0, 0};
	DWORD n;
	FillConsoleOutputCharacter(h, ' ', si.dwSize.X * si.dwSize.Y, c, &n);
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
	timeval tv = {0, 500000};
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

std::string to_string(float v, int width)
{
	std::stringstream s;
	s.precision(width-2);
	s.flags(std::ios_base::right);
	s.width(width);
	s.fill(' ');
	s << v;
	return s.str();
}

std::string pos_to_string(float v, int width)
{
	std::stringstream s;
	s.precision(width-1);
	s.flags(std::ios_base::right);
	s.width(width);
	s.fill(' ');
	s << fabs(v);
	return s.str();
}

std::string ratio(float a, float b)
{
	std::stringstream s;
	if (a > b)
	{
		if (b < 0.001f) s << " inf:1";
		else s << pos_to_string(a/b, 4) << ":1";
	}
	else if (a < b)
	{
		if (a < 0.001f) s << " 1:inf";
		else s << "1:" << pos_to_string(b/a, 4);
	}
	else
	{
		s << "   1:1";
	}

	return s.str();
}

std::string add_suffix(float val)
{
	const char* prefix[] = {"B", "kB", "MB", "GB", "TB"};
	const int num_prefix = sizeof(prefix) / sizeof(const char*);
	int i;
	for (i = 0; i < num_prefix; ++i)
	{
		if (fabs(val) < 1000.f)
			return to_string(val, i==0?7:6) + prefix[i];
		val /= 1000.f;
	}
	return to_string(val, 6) + prefix[i];
}

std::string progress_bar(float progress, int width)
{
	std::vector<char> bar;
	bar.reserve(width);

	std::fill_n(std::back_inserter(bar), progress * width, '#');
	std::fill_n(std::back_inserter(bar), width - (progress * width), '-');
	return std::string(bar.begin(), bar.end());
}

int main(int argc, char* argv[])
{
	using namespace libtorrent;

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
	settings.user_agent = "client_test";

	std::deque<std::string> events;

	try
	{
		std::vector<torrent_handle> handles;
		session ses(6881);

//		ses.set_upload_rate_limit(30 * 1024);
		ses.set_http_settings(settings);
		ses.set_severity_level(alert::debug);

		for (int i = 0; i < argc-1; ++i)
		{
			try
			{
				std::ifstream in(argv[i+1], std::ios_base::binary);
				in.unsetf(std::ios_base::skipws);
				entry e = bdecode(std::istream_iterator<char>(in), std::istream_iterator<char>());
				torrent_info t(e);
				t.print(std::cout);

				boost::filesystem::path save_path("");
				entry resume_data;
				try
				{
					std::stringstream s;
					s << t.name() << ".fastresume";
					boost::filesystem::ifstream resume_file(save_path / s.str(), std::ios_base::binary);
					resume_file.unsetf(std::ios_base::skipws);
					resume_data = bdecode(std::istream_iterator<char>(resume_file)
						, std::istream_iterator<char>());
				}
				catch (invalid_encoding&)
				{}

				handles.push_back(ses.add_torrent(t, save_path, resume_data));
				handles.back().set_max_connections(60);
				handles.back().set_max_uploads(-1);
				handles.back().set_ratio(1.02);
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
				if (c == 'q')
				{
					for (std::vector<torrent_handle>::iterator i = handles.begin();
						i != handles.end();
						++i)
					{
						torrent_handle h = *i;
						entry data = h.write_resume_data();
						std::stringstream s;
						s << h.get_torrent_info().name() << ".fastresume";
						boost::filesystem::ofstream out(h.save_path() / s.str(), std::ios_base::binary);
						out.unsetf(std::ios_base::skipws);
						bencode(std::ostream_iterator<char>(out), data);
					}
					break;
				}

				if(c == 'r')
				{
					// force reannounce on all torrents
					std::for_each(
						handles.begin()
						, handles.end()
						, boost::bind(&torrent_handle::force_reannounce, _1));
				}
			}

			std::auto_ptr<alert> a;
			a = ses.pop_alert();
			while (a.get())
			{
				if (events.size() >= 10) events.pop_front();
				events.push_back(a->msg());
				a = ses.pop_alert();
			}

			std::stringstream out;
			for (std::vector<torrent_handle>::iterator i = handles.begin();
				i != handles.end();
				++i)
			{
				torrent_status s = i->status();

				switch(s.state)
				{
					case torrent_status::queued_for_checking:
						out << "queued ";
						break;
					case torrent_status::checking_files:
						out << "checking ";
						break;
					case torrent_status::connecting_to_tracker:
						out << "connecting to tracker ";
						break;
					case torrent_status::downloading:
						out << "downloading ";
						break;
					case torrent_status::seeding:
						out << "seeding ";
						break;
				};

				i->get_peer_info(peers);
				float down = s.download_rate;
				float up = s.upload_rate;
				int total_down = s.total_download;
				int total_up = s.total_upload;
				int num_peers = peers.size();

				out.precision(4);
				out.width(5);
				out.fill(' ');
				out << (s.progress*100) << "% ";
				out << progress_bar(s.progress, 49);
				out << "\n";
				out << "total downloaded: " << s.total_done << " Bytes\n";
				out << "peers: " << num_peers << " "
					<< "d:" << add_suffix(down) << "/s "
					<< "(" << add_suffix(total_down) << ") "
					<< "u:" << add_suffix(up) << "/s "
					<< "(" << add_suffix(total_up) << ") "
					<< "ratio: " << ratio(total_down, total_up) << "\n";

				boost::posix_time::time_duration t = s.next_announce;
				out << "next announce: " << boost::posix_time::to_simple_string(t) << "\n";

				out << "___________________________________\n";

				for (std::vector<peer_info>::iterator i = peers.begin();
					i != peers.end();
					++i)
				{
					out << "d: " << add_suffix(i->down_speed) << "/s "
						<< "(" << add_suffix(i->total_download) << ") "
						<< "u: " << add_suffix(i->up_speed) << "/s "
						<< "(" << add_suffix(i->total_upload) << ") "
						<< "ul:" << add_suffix(i->upload_limit) << "/s "
//						<< "uc:" << add_suffix(i->upload_ceiling) << "/s "
//						<< "df: " << ratio(i->total_download, i->total_upload) << " "
//						<< "q: " << i->download_queue_length << " "
						<< "r: " << i->upload_queue_length << " "
						<< "f: "
						<< static_cast<const char*>((i->flags & peer_info::interesting)?"I":"_")
						<< static_cast<const char*>((i->flags & peer_info::choked)?"C":"_")
						<< static_cast<const char*>((i->flags & peer_info::remote_interested)?"i":"_")
						<< static_cast<const char*>((i->flags & peer_info::remote_choked)?"c":"_")
						<< static_cast<const char*>((i->flags & peer_info::supports_extensions)?"e":"_")
						<< static_cast<const char*>((i->flags & peer_info::local_connection)?"l":"r")
						<< "\n";
/*
					if (i->downloading_piece_index >= 0)
					{
						out.width(5);
						out.fill('0');
						out << i->downloading_piece_index << ";"
							<< i->downloading_block_index << ": ";
						out << progress_bar(
							i->downloading_progress / static_cast<float>(i->downloading_total)
							, 50);
						out << "\n";
					}
*/
				}
/*
				out << "___________________________________\n";

				i->get_download_queue(queue);
				for (std::vector<partial_piece_info>::iterator i = queue.begin();
					i != queue.end();
					++i)
				{
					out.width(4);
					out.fill(' ');
					out << i->piece_index << ": |";
					for (int j = 0; j < i->blocks_in_piece; ++j)
					{
						if (i->finished_blocks[j]) out << "#";
						else if (i->requested_blocks[j]) out << "+";
						else out << ".";
					}
					out << "|\n";
				}

				out << "___________________________________\n";
*/
			}

			for (std::deque<std::string>::iterator i = events.begin();
				i != events.end();
				++i)
			{
				out << *i << "\n";
			}

			clear();
			set_cursor(0, 0);
			puts(out.str().c_str());
//			std::cout << out.str();
//			std::cout.flush();
		}
	}
	catch (std::exception& e)
	{
  		std::cout << e.what() << "\n";
	}
	return 0;
}
