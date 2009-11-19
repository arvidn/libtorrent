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

#include <iostream>
#include <fstream>
#include <iterator>
#include <iomanip>

#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/file.hpp"
#include "libtorrent/storage.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/create_torrent.hpp"

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/bind.hpp>

using namespace boost::filesystem;
using namespace libtorrent;

// do not include files and folders whose
// name starts with a .
bool file_filter(boost::filesystem::path const& filename)
{
	if (filename.leaf()[0] == '.') return false;
	std::cerr << filename << std::endl;
	return true;
}

void print_progress(int i, int num)
{
	std::cerr << "\r" << (i+1) << "/" << num;
}

int main(int argc, char* argv[])
{
	using namespace libtorrent;
	using namespace boost::filesystem;

	int piece_size = 256 * 1024;
	char const* creator_str = "libtorrent";

	if (argc != 4 && argc != 5)
	{
		std::cerr << "usage: make_torrent <output torrent-file> "
			"<announce url> <file or directory to create torrent from> "
			"[url-seed]\n";
		return 1;
	}

#ifndef BOOST_NO_EXCEPTIONS
	try
	{
#endif
		file_storage fs;
		file_pool fp;
		path full_path = complete(path(argv[3]));

		add_files(fs, full_path, file_filter);

		create_torrent t(fs, piece_size);
		t.add_tracker(argv[2]);
		set_piece_hashes(t, full_path.branch_path()
			, boost::bind(&print_progress, _1, t.num_pieces()));
		std::cerr << std::endl;
		t.set_creator(creator_str);

		if (argc == 5) t.add_url_seed(argv[4]);

		// create the torrent and print it to out
		ofstream out(complete(path(argv[1])), std::ios_base::binary);
		bencode(std::ostream_iterator<char>(out), t.generate());
#ifndef BOOST_NO_EXCEPTIONS
	}
	catch (std::exception& e)
	{
		std::cerr << e.what() << "\n";
	}
#endif

	return 0;
}

