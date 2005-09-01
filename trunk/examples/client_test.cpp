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

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/exception.hpp>
#include <boost/bind.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/http_settings.hpp"
#include "libtorrent/identify_client.hpp"
#include "libtorrent/alert_types.hpp"

using boost::bind;

#ifdef _WIN32

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

#define ANSI_TERMINAL_COLORS

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

std::string esc(char const* code)
{
#ifdef ANSI_TERMINAL_COLORS
	std::string ret;
	ret += char(0x1b);
	ret += "[";
	ret += code;
	ret += "m";
	return ret;
#else
	return std::string();
#endif
}

std::string to_string(float v, int width, int precision = 3)
{
	std::stringstream s;
	s.precision(precision);
	s.flags(std::ios_base::right);
	s.width(width);
	s.fill(' ');
	s << v;
	return s.str();
}

std::string pos_to_string(float v, int width, int precision = 4)
{
	std::stringstream s;
	s.precision(precision);
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
	for (int i = 0; i < num_prefix; ++i)
	{
		if (fabs(val) < 1000.f)
			return to_string(val, i==0?5:4) + prefix[i];
		val /= 1000.f;
	}
	return to_string(val, 6) + "PB";
}

std::string progress_bar(float progress, int width, char const* code = "33")
{
	std::string bar;
	bar.reserve(width);

	int progress_chars = static_cast<int>(progress * width + .5f);
	bar = esc(code);
	std::fill_n(std::back_inserter(bar), progress_chars, '#');
	bar += esc("0");
	std::fill_n(std::back_inserter(bar), width - progress_chars, '-');
	return std::string(bar.begin(), bar.end());
}

char const* peer_index(libtorrent::address addr, std::vector<libtorrent::peer_info> const& peers)
{
	using namespace libtorrent;
	std::vector<peer_info>::const_iterator i = std::find_if(peers.begin()
		, peers.end(), bind(std::equal_to<address>(), bind(&peer_info::ip, _1), addr));
	if (i == peers.end()) return "+";

	static char str[] = " ";
	int index = i - peers.begin();
	str[0] = (index < 10)?'0' + index:'A' + index - 10;
	return str;
}

void print_peer_info(std::ostream& out, std::vector<libtorrent::peer_info> const& peers)
{
	using namespace libtorrent;

	out << " down     up       q  r flags  block progress  client \n";

	for (std::vector<peer_info>::const_iterator i = peers.begin();
		i != peers.end(); ++i)
	{
		out.fill(' ');
		out.width(2);
		out << esc("32") << add_suffix(i->down_speed) << "/s " << esc("0")
//						<< "(" << add_suffix(i->total_download) << ") "
			<< esc("31") << add_suffix(i->up_speed) << "/s " << esc("0")
//						<< "(" << add_suffix(i->total_upload) << ") "
//						<< "ul:" << add_suffix(i->upload_limit) << "/s "
//						<< "uc:" << add_suffix(i->upload_ceiling) << "/s "
//						<< "df:" << ratio(i->total_download, i->total_upload) << " "
			<< to_string(i->download_queue_length, 2, 2) << " "
			<< to_string(i->upload_queue_length, 2, 2) << " "
			<< static_cast<const char*>((i->flags & peer_info::interesting)?"I":"_")
			<< static_cast<const char*>((i->flags & peer_info::choked)?"C":"_")
			<< static_cast<const char*>((i->flags & peer_info::remote_interested)?"i":"_")
			<< static_cast<const char*>((i->flags & peer_info::remote_choked)?"c":"_")
			<< static_cast<const char*>((i->flags & peer_info::supports_extensions)?"e":"_")
			<< static_cast<const char*>((i->flags & peer_info::local_connection)?"l":"r") << " ";

		if (i->downloading_piece_index >= 0)
		{
			out << progress_bar(
				i->downloading_progress / static_cast<float>(i->downloading_total)
				, 15);
		}
		else
		{
			out << progress_bar(0.f, 15);
		}

		out << " " << identify_client(i->id) << "\n";
	}
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

	namespace fs = boost::filesystem;
	fs::path::default_name_check(fs::no_check);
	
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
		session ses;

		ses.listen_on(std::make_pair(6880, 6889));
		//ses.set_upload_rate_limit(512 * 1024);
		ses.set_http_settings(settings);
		ses.set_severity_level(alert::debug);
//		ses.set_severity_level(alert::info);

		// look for ipfilter.dat
		// poor man's parser
		// reads emule ipfilter files.
		// with the following format:
		// 
		// <first-ip> - <last-ip> , <access> , <comment>
		// 
		// first-ip is an ip address that defines the first
		// address of the range
		// last-ip is the last ip address in the range
		// access is a number specifying the access control
		// for this ip-range. Right now values > 127 = allowed
		// and numbers <= 127 = blocked
		// the rest of the line is ignored
		//
		// In the original spec ranges may not overlap, but
		// here ranges may overlap, and it is the last added
		// rule that has precedence for addresses that may fall
		// into more than one range.
		std::ifstream in("ipfilter.dat");
		ip_filter filter;
		while (in.good())
		{
			char line[300];
			in.getline(line, 300);
			int len = in.gcount();
			if (len <= 0) continue;
			if (line[0] == '#') continue;
			int a, b, c, d;
			char dummy;
			in >> a >> dummy >> b >> dummy >> c >> dummy >> d >> dummy;
			address start(a, b, c, d, 0);
			in >> a >> dummy >> b >> dummy >> c >> dummy >> d >> dummy;
			address last(a, b, c, d, 0);
			int flags;
			in >> flags;
			if (flags <= 127) flags = ip_filter::blocked;
			else flags = 0;
			if (in.fail()) break;
			filter.add_rule(start, last, flags);
		}
	
		ses.set_ip_filter(filter);
	
		for (int i = 0; i < argc-1; ++i)
		{
			try
			{
				boost::filesystem::path save_path("./");

				if (std::string(argv[i+1]).substr(0, 7) == "http://")
				{
					sha1_hash info_hash = boost::lexical_cast<sha1_hash>(argv[i+2]);

					handles.push_back(ses.add_torrent(argv[i+1], info_hash, save_path));
					handles.back().set_max_connections(60);
					handles.back().set_max_uploads(-1);
//					handles.back().set_ratio(1.1f);
					++i;

					continue;
				}
				std::ifstream in(argv[i+1], std::ios_base::binary);
				in.unsetf(std::ios_base::skipws);
				entry e = bdecode(std::istream_iterator<char>(in), std::istream_iterator<char>());
				torrent_info t(e);
				t.print(std::cout);

				entry resume_data;
				try
				{
					std::stringstream s;
					s << t.name() << ".fastresume";
					boost::filesystem::ifstream resume_file(save_path / s.str(), std::ios_base::binary);
					resume_file.unsetf(std::ios_base::skipws);
					resume_data = bdecode(
						std::istream_iterator<char>(resume_file)
						, std::istream_iterator<char>());
				}
				catch (invalid_encoding&) {}
				catch (boost::filesystem::filesystem_error&) {}

				handles.push_back(ses.add_torrent(e, save_path, resume_data, true, 64 * 1024));
				handles.back().set_max_connections(60);
				handles.back().set_max_uploads(-1);
//				handles.back().set_ratio(1.02f);
				
//				std::vector<bool> ffilter(t.num_files(), true);
//				ffilter[0] = false;
//				handles.back().filter_files(ffilter);

			}
			catch (std::exception& e)
			{
				std::cout << e.what() << "\n";
			}
		}

		if (handles.empty()) return 1;

		std::vector<peer_info> peers;
		std::vector<partial_piece_info> queue;

		bool print_peers = false;
		bool print_log = false;
		bool print_downloads = false;

		for (;;)
		{
			char c;
			if (sleep_and_input(&c))
			{
				if (c == 'q')
				{
					for (std::vector<torrent_handle>::iterator i = handles.begin();
						i != handles.end(); ++i)
					{
						torrent_handle h = *i;
						if (!h.get_torrent_info().is_valid()) continue;

						h.pause();
						entry data = h.write_resume_data();
						std::stringstream s;
						s << h.get_torrent_info().name() << ".fastresume";
						boost::filesystem::ofstream out(h.save_path() / s.str(), std::ios_base::binary);
						out.unsetf(std::ios_base::skipws);
						bencode(std::ostream_iterator<char>(out), data);
						ses.remove_torrent(*i);
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

				if(c == 'p')
				{
					// pause all torrents
					std::for_each(
						handles.begin()
						, handles.end()
						, boost::bind(&torrent_handle::pause, _1));
				}

				if(c == 'u')
				{
					// unpause all torrents
					std::for_each(
						handles.begin()
						, handles.end()
						, boost::bind(&torrent_handle::resume, _1));
				}

				if (c == 'i') print_peers = !print_peers;
				if (c == 'l') print_log = !print_log;
				if (c == 'd') print_downloads = !print_downloads;

			}

			// loop through the alert queue to see if anything has happened.
			std::auto_ptr<alert> a;
			a = ses.pop_alert();
			while (a.get())
			{
				if (torrent_finished_alert* p = dynamic_cast<torrent_finished_alert*>(a.get()))
				{
					// limit the bandwidth for all seeding torrents
					p->handle.set_max_connections(10);
					//p->handle.set_max_uploads(5);
					//p->handle.set_upload_limit(10000);

					// all finished downloades are
					// moved into this directory
					//p->handle.move_storage("finished");
					events.push_back(
						p->handle.get_torrent_info().name() + ": " + a->msg());
				}
				else if (peer_error_alert* p = dynamic_cast<peer_error_alert*>(a.get()))
				{
					events.push_back(identify_client(p->id) + ": " + a->msg());
				}
				else if (invalid_request_alert* p = dynamic_cast<invalid_request_alert*>(a.get()))
				{
					events.push_back(identify_client(p->id) + ": " + a->msg());
				}
				else
				{
					events.push_back(a->msg());
				}

				if (events.size() >= 10) events.pop_front();
				a = ses.pop_alert();
			}

			session_status sess_stat = ses.status();
			
			std::stringstream out;
			for (std::vector<torrent_handle>::iterator i = handles.begin();
				i != handles.end(); ++i)
			{
				if (!i->is_valid())
				{
					handles.erase(i);
					--i;
					continue;
				}
				out << "name: " << esc("37");
				if (i->has_metadata()) out << i->get_torrent_info().name();
				else out << "-";
				out << esc("0") << "\n";
				torrent_status s = i->status();

				if (s.state != torrent_status::seeding)
				{
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
						case torrent_status::downloading_metadata:
							out << "downloading metadata ";
							break;
						case torrent_status::downloading:
							out << "downloading ";
							break;
						case torrent_status::finished:
							out << "finished ";
							break;
						case torrent_status::seeding:
							out << "seeding ";
							break;
					};
				}

				i->get_peer_info(peers);

				if (s.state != torrent_status::seeding)
				{
					char const* progress_bar_color = "33"; // yellow
					if (s.state == torrent_status::checking_files
						|| s.state == torrent_status::downloading_metadata)
					{
						progress_bar_color = "35"; // magenta
					}
					else if (s.current_tracker.empty())
					{
						progress_bar_color = "31"; // red
					}
					else if (sess_stat.has_incoming_connections)
					{
						progress_bar_color = "32"; // green
					}
					out.precision(4);
					out.width(5);
					out.fill(' ');
					out << (s.progress*100) << "% ";
					out << progress_bar(s.progress, 49, progress_bar_color);
					out << "\n";
					out << "total downloaded: " << esc("32") << s.total_done << esc("0") << " Bytes\n";
					out	<< "peers: " << s.num_peers << " "
						<< "seeds: " << s.num_seeds << " "
						<< "distributed copies: " << s.distributed_copies << "\n";
				}
				out << "download: " << esc("32") << add_suffix(s.download_rate) << "/s " << esc("0")
					<< "(" << esc("32") << add_suffix(s.total_download) << esc("0") << ") "
					<< "upload: " << esc("31") << add_suffix(s.upload_rate) << "/s " << esc("0")
					<< "(" << esc("31") << add_suffix(s.total_upload) << esc("0") << ") "
					<< "ratio: " << ratio(s.total_payload_download, s.total_payload_upload) << "\n";
				if (s.state != torrent_status::seeding)
				{
					out << "info-hash: " << i->info_hash() << "\n";

					boost::posix_time::time_duration t = s.next_announce;
					out << "next announce: " << esc("37") << boost::posix_time::to_simple_string(t) << esc("0") << "\n";
					out << "tracker: " << s.current_tracker << "\n";
				}

				out << "___________________________________\n";

				if (print_peers && !peers.empty())
				{
					print_peer_info(out, peers);
					out << "___________________________________\n";
				}

				if (print_downloads && s.state != torrent_status::seeding)
				{
					i->get_download_queue(queue);
					for (std::vector<partial_piece_info>::iterator i = queue.begin();
						i != queue.end(); ++i)
					{
						out.width(4);
						out.fill(' ');
						out << i->piece_index << ": [";
						for (int j = 0; j < i->blocks_in_piece; ++j)
						{
							char const* peer_str = peer_index(i->peer[j], peers);
#ifdef ANSI_TERMINAL_COLORS
							if (i->finished_blocks[j]) out << esc("32;7") << peer_str << esc("0");
							else if (i->requested_blocks[j]) out << peer_str;
							else out << "-";
#else
							if (i->finished_blocks[j]) out << "#";
							else if (i->requested_blocks[j]) out << peer_str;
							else out << "-";
#endif
						}
						out << "]\n";
					}

					out << "___________________________________\n";
				}
			}

			if (print_log)
			{
				for (std::deque<std::string>::iterator i = events.begin();
					i != events.end(); ++i)
				{
					out << *i << "\n";
				}
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
