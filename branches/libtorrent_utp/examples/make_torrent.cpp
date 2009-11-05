/*

Copyright (c) 2006, Arvid Norberg
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

#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/file.hpp"
#include "libtorrent/storage.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/file.hpp"

#include <boost/bind.hpp>

using namespace libtorrent;

// do not include files and folders whose
// name starts with a .
bool file_filter(std::string const& f)
{
	if (filename(f)[0] == '.') return false;
	fprintf(stderr, "%s\n", f.c_str());
	return true;
}

void print_progress(int i, int num)
{
	fprintf(stderr, "\r%d/%d", i+1, num);
}

void print_usage()
{
	fputs("usage: make_torrent FILE [OPTIONS]\n"
		"\n"
		"Generates a torrent file from the specified file\n"
		"or directory and writes it to standard out\n\n"
		"OPTIONS:\n"
		"-m          generate a merkle hash tree torrent.\n"
		"            merkle torrents require client support\n"
		"-w url      adds a web seed to the torrent with\n"
		"            the specified url\n"
		"-t url      adds the specified tracker to the\n"
		"            torrent\n"
		"-p bytes    enables padding files. Files larger\n"
		"            than bytes will be piece-aligned\n"
		"-s bytes    specifies a piece size for the torrent\n"
		"            This has to be a multiple of 16 kiB\n"
		, stderr);
}

int main(int argc, char* argv[])
{
	using namespace libtorrent;

	char const* creator_str = "libtorrent";

	if (argc < 2)
	{
		print_usage();
		return 1;
	}

#ifndef BOOST_NO_EXCEPTIONS
	try
	{
#endif
		std::vector<std::string> web_seeds;
		std::vector<std::string> trackers;
		int pad_file_limit = -1;
		int piece_size = 0;
		int flags = 0;

		for (int i = 2; i < argc; ++i)
		{
			if (argv[i][0] != '-')
			{
				print_usage();
				return 1;
			}

			switch (argv[i][1])
			{
				case 'w':
					++i;
					web_seeds.push_back(argv[i]);
					break;
				case 't':
					++i;
					trackers.push_back(argv[i]);
					break;
				case 'p':
					++i;
					pad_file_limit = atoi(argv[i]);
					flags |= create_torrent::optimize;
					break;
				case 's':
					++i;
					piece_size = atoi(argv[i]);
					break;
				case 'm':
					flags |= create_torrent::merkle;
					break;
				default:
					print_usage();
					return 1;
			}
		}

		file_storage fs;
		file_pool fp;
		std::string full_path = libtorrent::complete(argv[1]);

		add_files(fs, full_path, file_filter);

		create_torrent t(fs, piece_size, pad_file_limit, flags);
		for (std::vector<std::string>::iterator i = trackers.begin()
			, end(trackers.end()); i != end; ++i)
			t.add_tracker(*i);

		for (std::vector<std::string>::iterator i = web_seeds.begin()
			, end(web_seeds.end()); i != end; ++i)
			t.add_url_seed(*i);

		error_code ec;
		set_piece_hashes(t, parent_path(full_path)
			, boost::bind(&print_progress, _1, t.num_pieces()), ec);
		if (ec)
		{
			fprintf(stderr, "%s\n", ec.message().c_str());
			return 1;
		}

		fprintf(stderr, "\n");
		t.set_creator(creator_str);

		// create the torrent and print it to stdout
		std::vector<char> torrent;
		bencode(back_inserter(torrent), t.generate());
		fwrite(&torrent[0], 1, torrent.size(), stdout);

#ifndef BOOST_NO_EXCEPTIONS
	}
	catch (std::exception& e)
	{
		fprintf(stderr, "%s\n", e.what());
	}
#endif

	return 0;
}

