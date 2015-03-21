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
#include "libtorrent/escape_string.hpp" // for from_hex

#include <boost/bind.hpp>

#ifdef TORRENT_WINDOWS
#include <direct.h> // for _getcwd
#endif

using namespace libtorrent;

int load_file(std::string const& filename, std::vector<char>& v, libtorrent::error_code& ec, int limit = 8000000)
{
	ec.clear();
	FILE* f = fopen(filename.c_str(), "rb");
	if (f == NULL)
	{
		ec.assign(errno, boost::system::generic_category());
		return -1;
	}

	int r = fseek(f, 0, SEEK_END);
	if (r != 0)
	{
		ec.assign(errno, boost::system::generic_category());
		fclose(f);
		return -1;
	}
	long s = ftell(f);
	if (s < 0)
	{
		ec.assign(errno, boost::system::generic_category());
		fclose(f);
		return -1;
	}

	if (s > limit)
	{
		fclose(f);
		return -2;
	}

	r = fseek(f, 0, SEEK_SET);
	if (r != 0)
	{
		ec.assign(errno, boost::system::generic_category());
		fclose(f);
		return -1;
	}

	v.resize(s);
	if (s == 0)
	{
		fclose(f);
		return 0;
	}

	r = fread(&v[0], 1, v.size(), f);
	if (r < 0)
	{
		ec.assign(errno, boost::system::generic_category());
		fclose(f);
		return -1;
	}

	fclose(f);

	if (r != s) return -3;

	return 0;
}

std::string branch_path(std::string const& f)
{
	if (f.empty()) return f;

#ifdef TORRENT_WINDOWS
	if (f == "\\\\") return "";
#endif
	if (f == "/") return "";

	int len = f.size();
	// if the last character is / or \ ignore it
	if (f[len-1] == '/' || f[len-1] == '\\') --len;
	while (len > 0)
	{
		--len;
		if (f[len] == '/' || f[len] == '\\')
			break;
	}

	if (f[len] == '/' || f[len] == '\\') ++len;
	return std::string(f.c_str(), len);
}

// do not include files and folders whose
// name starts with a .
bool file_filter(std::string const& f)
{
	if (f.empty()) return false;

	char const* first = f.c_str();
	char const* sep = strrchr(first, '/');
#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
	char const* altsep = strrchr(first, '\\');
	if (sep == 0 || altsep > sep) sep = altsep;
#endif
	// return false if the first character of the filename is a .
	if (sep == 0 && f[0] == '.') return false;
	if (sep[1] == '.') return false;

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
		"-m file       generate a merkle hash tree torrent.\n"
		"              merkle torrents require client support\n"
		"              the resulting full merkle tree is written to\n"
		"              the specified file\n"
		"-w url        adds a web seed to the torrent with\n"
		"              the specified url\n"
		"-t url        adds the specified tracker to the\n"
		"              torrent. For multiple trackers, specify more\n"
		"              -t options\n"
		"-c comment    sets the comment to the specified string\n"
		"-C creator    sets the created-by field to the specified string\n"
		"-p bytes      enables padding files. Files larger\n"
		"              than bytes will be piece-aligned\n"
		"-s bytes      specifies a piece size for the torrent\n"
		"              This has to be a multiple of 16 kiB\n"
		"-l            Don't follow symlinks, instead encode them as\n"
		"              links in the torrent file\n"
		"-o file       specifies the output filename of the torrent file\n"
		"              If this is not specified, the torrent file is\n"
		"              printed to the standard out, except on windows\n"
		"              where the filename defaults to a.torrent\n"
		"-r file       add root certificate to the torrent, to verify\n"
		"              the HTTPS tracker\n"
		"-S info-hash  add a similar torrent by info-hash. The similar\n"
		"              torrent is expected to share some files with this one\n"
		"-L collection add a collection name to this torrent. Other torrents\n"
		"              in the same collection is expected to share files\n"
		"              with this one.\n"
		"-M            make the torrent compatible with mutable torrents\n"
		"              this means aligning large files and pad them in order\n"
		"              for piece hashes to uniquely indentify a file without\n"
		"              overlap\n"
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
		std::vector<std::string> collections;
		std::vector<sha1_hash> similar;
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
				case 'M':
					flags |= create_torrent::mutable_torrent_support;
					pad_file_limit = 0x4000;
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
				case 'S':
					{
					++i;
					if (strlen(argv[i]) != 40)
					{
						fprintf(stderr, "invalid info-hash for -S. "
							"Expected 40 hex characters\n");
						print_usage();
						return 1;
					}
					sha1_hash ih;
					if (!from_hex(argv[i], 40, (char*)&ih[0]))
					{
						fprintf(stderr, "invalid info-hash for -S\n");
						print_usage();
						return 1;
					}
					similar.push_back(ih);
					}
					break;
				case 'L':
					++i;
					collections.push_back(argv[i]);
					break;
				default:
					print_usage();
					return 1;
			}
		}

		file_storage fs;
		std::string full_path = argv[1];
#ifdef TORRENT_WINDOWS
		if (full_path[1] != ':')
#else
		if (full_path[0] != '/')
#endif
		{
			char cwd[TORRENT_MAX_PATH];
#ifdef TORRENT_WINDOWS
			_getcwd(cwd, sizeof(cwd));
			full_path = cwd + ("\\" + full_path);
#else
			getcwd(cwd, sizeof(cwd));
			full_path = cwd + ("/" + full_path);
#endif
		}

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

		for (std::vector<std::string>::iterator i = collections.begin()
			, end(collections.end()); i != end; ++i)
			t.add_collection(*i);

		for (std::vector<sha1_hash>::iterator i = similar.begin()
			, end(similar.end()); i != end; ++i)
			t.add_similar_torrent(*i);

		error_code ec;
		set_piece_hashes(t, branch_path(full_path)
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
			if (ret != int(t.merkle_tree().size()))
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

