/*

Copyright (c) 2015, Arvid Norberg
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

#include "test_utils.hpp"
#include "libtorrent/aux_/file_progress.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/piece_picker.hpp"

using namespace libtorrent;

TORRENT_TEST(init)
{
	// test the init function to make sure it assigns
	// the correct number of bytes across the files
	const int piece_size = 256;

	file_storage fs;
	fs.add_file("torrent/1", 0);
	fs.add_file("torrent/2", 10);
	fs.add_file("torrent/3", 20);
	fs.add_file("torrent/4", 30);
	fs.add_file("torrent/5", 40);
	fs.add_file("torrent/6", 100000);
	fs.add_file("torrent/7", 30);
	fs.set_piece_length(piece_size);
	fs.set_num_pieces((fs.total_size() + piece_size - 1) / piece_size);

	for (int idx = 0; idx < fs.num_pieces(); ++idx)
	{
		piece_picker picker;
		picker.init(4, fs.total_size() % 4, fs.num_pieces());
		picker.we_have(idx);

		aux::file_progress fp;
		fp.init(picker, fs);

		std::vector<boost::int64_t> vec;
		fp.export_progress(vec);

		boost::uint64_t sum = 0;
		for (int i = 0; i < int(vec.size()); ++i)
			sum += vec[i];

		TEST_EQUAL(int(sum), fs.piece_size(idx));
	}
}

TORRENT_TEST(init2)
{
	// test the init function to make sure it assigns
	// the correct number of bytes across the files
	const int piece_size = 256;

	file_storage fs;
	fs.add_file("torrent/1", 100000);
	fs.add_file("torrent/2", 10);
	fs.set_piece_length(piece_size);
	fs.set_num_pieces((fs.total_size() + piece_size - 1) / piece_size);

	for (int idx = 0; idx < fs.num_pieces(); ++idx)
	{
		piece_picker picker;
		picker.init(4, fs.total_size() % 4, fs.num_pieces());
		picker.we_have(idx);

		std::vector<boost::int64_t> vec;
		aux::file_progress fp;

		fp.init(picker, fs);
		fp.export_progress(vec);

		boost::uint64_t sum = 0;
		for (int i = 0; i < int(vec.size()); ++i)
			sum += vec[i];

		TEST_EQUAL(int(sum), fs.piece_size(idx));
	}
}

// TODO: test the update function too

