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


#ifndef NDEBUG
struct cout_logger: libtorrent::logger
{
public:
	virtual void log(const char* text) { std::cout << text; }
	virtual void clear() {}
};

struct cout_log_creator: libtorrent::log_spawner
{
	virtual libtorrent::logger* create_logger(const char* title)
	{
		cout_logger* log = new cout_logger();
		return log;
	}
};

#endif

int main(int argc, char* argv[])
{
	using namespace libtorrent;

	if (argc < 2)
	{
		std::cerr << "usage: torrent torrent-files ...\n"
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
#ifndef NDEBUG
		cout_log_creator l;
		session s(6881, &l);
#else
		session s(6881);
#endif
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

		while (!handles.empty())
		{
			int a;
			std::cin >> a;
			handles.back().abort();
			handles.pop_back();
		}

	}
	catch (std::exception& e)
	{
  		std::cout << e.what() << "\n";
	}

	return 0;
}
