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
#include "libtorrent/file_pool.hpp"

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
		"-m file     generate a merkle hash tree torrent.\n"
		"            merkle torrents require client support\n"
		"            the resulting full merkle tree is written to\n"
		"            the specified file\n"
		"-f          include sha-1 file hashes in the torrent\n"
		"            this helps supporting mixing sources from\n"
		"            other networks\n"
		"-w url      adds a web seed to the torrent with\n"
		"            the specified url\n"
		"-t url      adds the specified tracker to the\n"
		"            torrent. For multiple trackers, specify more\n"
		"            -t options\n"
		"-c comment  sets the comment to the specified string\n"
		"-C creator  sets the created-by field to the specified string\n"
		"-p bytes    enables padding files. Files larger\n"
		"            than bytes will be piece-aligned\n"
		"-s bytes    specifies a piece size for the torrent\n"
		"            This has to be a multiple of 16 kiB\n"
		"-l          Don't follow symlinks, instead encode them as\n"
		"            links in the torrent file\n"
		"-o file     specifies the output filename of the torrent file\n"
		"            If this is not specified, the torrent file is\n"
		"            printed to the standard out, except on windows\n"
		"            where the filename defaults to a.torrent\n"
		"-r file     add root certificate to the torrent, to verify\n"
		"            the HTTPS tracker\n"
		, stderr);
}

int main(int argc, char* argv[])
{
	using namespace libtorrent;

	std::string creator_str = "libtorrent";
	std::string comment_str;

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
		std::string root_cert;

		std::string outfile;
		std::string merklefile;
#ifdef TORRENT_WINDOWS
		// don't ever write binary data to the console on windows
		// it will just be interpreted as text and corrupted
		outfile = "a.torrent";
#endif

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
					++i;
					merklefile = argv[i];
					flags |= create_torrent::merkle;
					break;
				case 'o':
					++i;
					outfile = argv[i];
					break;
				case 'f':
					flags |= create_torrent::calculate_file_hashes;
					break;
				case 'l':
					flags |= create_torrent::symlinks;
					break;
				case 'C':
					++i;
					creator_str = argv[i];
					break;
				case 'c':
					++i;
					comment_str = argv[i];
					break;
				case 'r':
					++i;
					root_cert = argv[i];
					break;
				default:
					print_usage();
					return 1;
			}
		}

		file_storage fs;
		file_pool fp;
		std::string full_path = libtorrent::complete(argv[1]);

		add_files(fs, full_path, file_filter, flags);
		if (fs.num_files() == 0)
		{
			fputs("no files specified.\n", stderr);
			return 1;
		}

		create_torrent t(fs, piece_size, pad_file_limit, flags);
		int tier = 0;
		for (std::vector<std::string>::iterator i = trackers.begin()
			, end(trackers.end()); i != end; ++i, ++tier)
			t.add_tracker(*i, tier);

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
		t.set_creator(creator_str.c_str());
		if (!comment_str.empty())
			t.set_comment(comment_str.c_str());

		if (!root_cert.empty())
		{
			std::vector<char> pem;
			load_file(root_cert, pem, ec, 10000);
			if (ec)
			{
				fprintf(stderr, "failed to load root certificate for tracker: %s\n", ec.message().c_str());
			}
			else
			{
				t.set_root_cert(std::string(&pem[0], pem.size()));
			}
		}

		// create the torrent and print it to stdout
		std::vector<char> torrent;
		bencode(back_inserter(torrent), t.generate());
		FILE* output = stdout;
		if (!outfile.empty())
			output = fopen(outfile.c_str(), "wb+");
		if (output == NULL)
		{
			fprintf(stderr, "failed to open file \"%s\": (%d) %s\n"
				, outfile.c_str(), errno, strerror(errno));
			return 1;
		}
		fwrite(&torrent[0], 1, torrent.size(), output);

		if (output != stdout)
			fclose(output);

		if (!merklefile.empty())
		{
			output = fopen(merklefile.c_str(), "wb+");
			if (output == NULL)
			{
				fprintf(stderr, "failed to open file \"%s\": (%d) %s\n"
					, merklefile.c_str(), errno, strerror(errno));
				return 1;
			}
			int ret = fwrite(&t.merkle_tree()[0], 20, t.merkle_tree().size(), output);
			if (ret != t.merkle_tree().size())
			{
				fprintf(stderr, "failed to write %s: (%d) %s\n"
					, merklefile.c_str(), errno, strerror(errno));
			}
			fclose(output);
		}

#ifndef BOOST_NO_EXCEPTIONS
	}
	catch (std::exception& e)
	{
		fprintf(stderr, "%s\n", e.what());
	}
#endif

	return 0;
}

