/*

Copyright (c) 2006-2017, Arvid Norberg
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
#include "libtorrent/storage.hpp"
#include "libtorrent/create_torrent.hpp"

#include <functional>
#include <cstdio>
#include <sstream>
#include <fstream>
#include <iostream>

#ifdef TORRENT_WINDOWS
#include <direct.h> // for _getcwd
#endif

using namespace std::placeholders;

std::vector<char> load_file(std::string const& filename)
{
	std::fstream in;
	in.exceptions(std::ifstream::failbit);
	in.open(filename.c_str(), std::ios_base::in | std::ios_base::binary);
	in.seekg(0, std::ios_base::end);
	size_t const size = size_t(in.tellg());
	in.seekg(0, std::ios_base::beg);
	std::vector<char> ret(size);
	in.read(ret.data(), ret.size());
	return ret;
}

std::string branch_path(std::string const& f)
{
	if (f.empty()) return f;

#ifdef TORRENT_WINDOWS
	if (f == "\\\\") return "";
#endif
	if (f == "/") return "";

	int len = int(f.size());
	// if the last character is / or \ ignore it
	if (f[len-1] == '/' || f[len-1] == '\\') --len;
	while (len > 0) {
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
	if (sep == nullptr || altsep > sep) sep = altsep;
#endif
	// if there is no parent path, just set 'sep'
	// to point to the filename.
	// if there is a parent path, skip the '/' character
	if (sep == nullptr) sep = first;
	else ++sep;

	// return false if the first character of the filename is a .
	if (sep[0] == '.') return false;

	std::cerr << f << "\n";
	return true;
}

void print_usage()
{
	std::cerr << R"(usage: make_torrent FILE [OPTIONS]

Generates a torrent file from the specified file
or directory and writes it to standard out


OPTIONS:
-m file       generate a merkle hash tree torrent.
              merkle torrents require client support
              the resulting full merkle tree is written to
              the specified file
-w url        adds a web seed to the torrent with
              the specified url
-t url        adds the specified tracker to the
              torrent. For multiple trackers, specify more
              -t options. Specify a dash character "-" as a tracker to indicate
              the following trackers should be in a higher tier.
-c comment    sets the comment to the specified string
-C creator    sets the created-by field to the specified string
-p bytes      enables padding files. Files larger
              than bytes will be piece-aligned
-s bytes      specifies a piece size for the torrent
              This has to be a multiple of 16 kiB
-l            Don't follow symlinks, instead encode them as
              links in the torrent file
-o file       specifies the output filename of the torrent file
              If this is not specified, the torrent file is
              printed to the standard out, except on windows
              where the filename defaults to a.torrent
-r file       add root certificate to the torrent, to verify
              the HTTPS tracker
-S info-hash  add a similar torrent by info-hash. The similar
              torrent is expected to share some files with this one
-L collection add a collection name to this torrent. Other torrents
              in the same collection is expected to share files
              with this one.
-M            make the torrent compatible with mutable torrents
              this means aligning large files and pad them in order
              for piece hashes to uniquely indentify a file without
              overlap
)";
}

int main(int argc_, char const* argv_[]) try
{
	lt::span<char const*> args(argv_, size_t(argc_));
	std::string creator_str = "libtorrent";
	std::string comment_str;

	if (args.size() < 2) {
		print_usage();
		return 1;
	}

	std::vector<std::string> web_seeds;
	std::vector<std::string> trackers;
	std::vector<std::string> collections;
	std::vector<lt::sha1_hash> similar;
	int pad_file_limit = -1;
	int piece_size = 0;
	lt::create_flags_t flags = {};
	std::string root_cert;

	std::string outfile;
	std::string merklefile;
#ifdef TORRENT_WINDOWS
	// don't ever write binary data to the console on windows
	// it will just be interpreted as text and corrupted
	outfile = "a.torrent";
#endif

	std::string full_path = args[1];
	args = args.subspan(2);

	for (; !args.empty(); args = args.subspan(1)) {
		if (args[0][0] != '-') {
			print_usage();
			return 1;
		}

		char const flag = args[0][1];

		switch (flag)
		{
			case 'M':
				flags |= lt::create_torrent::mutable_torrent_support;
				pad_file_limit = 0x4000;
				continue;
			case 'l':
				flags |= lt::create_torrent::symlinks;
				continue;
		}

		if (args.size() < 2) {
			print_usage();
			return 1;
		}

		switch (flag)
		{
			case 'w': web_seeds.push_back(args[1]); break;
			case 't': trackers.push_back(args[1]); break;
			case 's': piece_size = atoi(args[1]); break;
			case 'o': outfile = args[1]; break;
			case 'C': creator_str = args[1]; break;
			case 'c': comment_str = args[1]; break;
			case 'r': root_cert = args[1]; break;
			case 'L': collections.push_back(args[1]); break;
			case 'p':
				pad_file_limit = atoi(args[1]);
				flags |= lt::create_torrent::optimize_alignment;
				break;
			case 'm':
				merklefile = args[1];
				flags |= lt::create_torrent::merkle;
				break;
			case 'S': {
				if (strlen(args[1]) != 40) {
					std::cerr << "invalid info-hash for -S. "
						"Expected 40 hex characters\n";
					print_usage();
					return 1;
				}
				std::stringstream hash(args[1]);
				lt::sha1_hash ih;
				hash >> ih;
				if (hash.fail()) {
					std::cerr << "invalid info-hash for -S\n";
					print_usage();
					return 1;
				}
				similar.push_back(ih);
				break;
			}
			default:
				print_usage();
				return 1;
		}
		args = args.subspan(1);
	}

	lt::file_storage fs;
#ifdef TORRENT_WINDOWS
	if (full_path[1] != ':')
#else
	if (full_path[0] != '/')
#endif
	{
		char cwd[2048];
#ifdef TORRENT_WINDOWS
#define getcwd_ _getcwd
#else
#define getcwd_ getcwd
#endif

		char const* ret = getcwd_(cwd, sizeof(cwd));
		if (ret == nullptr) {
			std::cerr << "failed to get current working directory: "
				<< strerror(errno) << "\n";
			return 1;
		}

#undef getcwd_
#ifdef TORRENT_WINDOWS
		full_path = cwd + ("\\" + full_path);
#else
		full_path = cwd + ("/" + full_path);
#endif
	}

	lt::add_files(fs, full_path, file_filter, flags);
	if (fs.num_files() == 0) {
		std::cerr << "no files specified.\n";
		return 1;
	}

	lt::create_torrent t(fs, piece_size, pad_file_limit, flags);
	int tier = 0;
	for (std::string const& tr : trackers) {
		if (tr == "-") ++tier;
		else t.add_tracker(tr, tier);
	}

	for (std::string const& ws : web_seeds)
		t.add_url_seed(ws);

	for (std::string const& c : collections)
		t.add_collection(c);

	for (lt::sha1_hash const& s : similar)
		t.add_similar_torrent(s);

	auto const num = t.num_pieces();
	lt::set_piece_hashes(t, branch_path(full_path)
		, [num] (lt::piece_index_t const p) {
			std::cerr << "\r" << p << "/" << num;
		});

	std::cerr << "\n";
	t.set_creator(creator_str.c_str());
	if (!comment_str.empty()) {
		t.set_comment(comment_str.c_str());
	}

	if (!root_cert.empty()) {
		std::vector<char> const pem = load_file(root_cert);
		t.set_root_cert(std::string(&pem[0], pem.size()));
	}

	// create the torrent and print it to stdout
	std::vector<char> torrent;
	lt::bencode(back_inserter(torrent), t.generate());
	if (!outfile.empty()) {
		std::fstream out;
		out.exceptions(std::ifstream::failbit);
		out.open(outfile.c_str(), std::ios_base::out | std::ios_base::binary);
		out.write(torrent.data(), torrent.size());
	}
	else {
		std::cout.write(torrent.data(), torrent.size());
	}

	if (!merklefile.empty()) {
		std::fstream merkle;
		merkle.exceptions(std::ifstream::failbit);
		merkle.open(merklefile.c_str(), std::ios_base::out | std::ios_base::binary);
		auto const& tree = t.merkle_tree();
		merkle.write(reinterpret_cast<char const*>(tree.data()), tree.size() * 20);
	}
	return 0;
}
catch (std::exception& e) {
	std::cerr << "ERROR: " << e.what() << "\n";
	return 1;
}
