/*

Copyright (c) 2012, Arvid Norberg
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

#include "test.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/create_torrent.hpp"

using namespace libtorrent;

int test_main()
{

	file_storage fs;

	fs.add_file("test/temporary.txt", 0x4000);
	fs.add_file("test/A/tmp", 0x4000);
	fs.add_file("test/Temporary.txt", 0x4000);
	fs.add_file("test/TeMPorArY.txT", 0x4000);
	fs.add_file("test/a", 0x4000);
	fs.add_file("test/b.exe", 0x4000);
	fs.add_file("test/B.ExE", 0x4000);
	fs.add_file("test/B.exe", 0x4000);
	fs.add_file("test/test/TEMPORARY.TXT", 0x4000);
	fs.add_file("test/A", 0x4000);

	libtorrent::create_torrent t(fs, 0x4000);

	// calculate the hash for all pieces
	int num = t.num_pieces();
	sha1_hash ph;
	for (int i = 0; i < num; ++i)
		t.set_hash(i, ph);

	std::vector<char> tmp;
	std::back_insert_iterator<std::vector<char> > out(tmp);

	entry tor = t.generate();
	bencode(out, tor);

	torrent_info ti(&tmp[0], tmp.size());

	char const* filenames[10] =
	{
	"test/temporary.txt",
	"test/A/tmp",
	"test/Temporary.1.txt", // duplicate of temporary.txt
	"test/TeMPorArY.2.txT", // duplicate of temporary.txt

	// a file may not have the same name as a directory
	// however, detecting this is not supported currently.
	// it is in the next major release
	"test/a",

	"test/b.exe",
	"test/B.1.ExE", // duplicate of b.exe
	"test/B.2.exe", // duplicate of b.exe
	"test/test/TEMPORARY.TXT", // a file with the same name in a seprate directory is fine
	"test/A.1", // duplicate of directory a
	};

	for (int i = 0; i < ti.num_files(); ++i)
	{
		std::string p = ti.file_at(i).path;
		convert_path_to_posix(p);
		fprintf(stderr, "%s == %s\n", p.c_str(), filenames[i]);
		TEST_CHECK(p == filenames[i]);
	}

	return 0;
}
