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

#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/http_settings.hpp"

#ifdef WIN32
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

	if (argc < 2)
	{
		std::cerr << "usage: ./client_test torrent-files ...\n"
			"to stop the client, type a number and press enter.\n";
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

		s.set_http_settings(settings);
		for (int i = 0; i < argc-1; ++i)
		{
			try
			{
				std::ifstream in(argv[i+1], std::ios_base::binary);
				in.unsetf(std::ios_base::skipws);
				entry e = bdecode(std::istream_iterator<char>(in), std::istream_iterator<char>());
				torrent_info t(e);
				t.print(std::cout);
				handles.push_back(s.add_torrent(t, ""));
			}
			catch (std::exception& e)
			{
				std::cout << e.what() << "\n";
			}
		}

		std::vector<peer_info> peers;

		for (;;)
		{
			char c;
			if (sleep_and_input(&c))
			{
				if (c == 'q') break;
			}

			for (std::vector<torrent_handle>::iterator i = handles.begin();
				i != handles.end();
				++i)
			{
				torrent_status s = i->status();

				switch(s.state)
				{
					case torrent_status::queued_for_checking:
						std::cout << "queued for checking: ";
						break;
					case torrent_status::checking_files:
						std::cout << "checking files: ";
						break;
					case torrent_status::downloading:
						std::cout << "downloading: ";
						break;
					case torrent_status::seeding:
						std::cout << "seeding: ";
						break;
				};

				std::cout << s.progress*100 << "% ";

				// calculate download and upload speeds
				i->get_peer_info(peers);
				float down = 0.f;
				float up = 0.f;
				unsigned int total_down = 0;
				unsigned int total_up = 0;
				int num_peers = peers.size();

				for (std::vector<peer_info>::iterator i = peers.begin();
					i != peers.end();
					++i)
				{
					down += i->down_speed;
					up += i->up_speed;
					total_down += i->total_download;
					total_up += i->total_upload;
				}

				std::cout << "p:" << num_peers;
				
				std::cout << " d:("
					<< add_suffix(total_down) << ") " << add_suffix(down) << "/s up:("
					<< add_suffix(total_up) << ") " << add_suffix(up) << "/s\n";
			}
			std::cout << "----\n";
		}
	}
	catch (std::exception& e)
	{
  		std::cout << e.what() << "\n";
	}
	return 0;
}
