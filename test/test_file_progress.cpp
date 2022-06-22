/*

Copyright (c) 2015-2019, 2021, Arvid Norberg
Copyright (c) 2018-2019, Alden Torres
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

using namespace lt;

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
	fs.set_num_pieces(aux::calc_num_pieces(fs));

	for (auto const idx : fs.piece_range())
	{
		piece_picker picker(fs.total_size(), fs.piece_length());
		picker.we_have(idx);

		aux::file_progress fp;
		fp.init(picker, fs);

		aux::vector<std::int64_t, file_index_t> vec;
		fp.export_progress(vec);

		std::int64_t sum = 0;
		for (file_index_t i(0); i < vec.end_index(); ++i)
			sum += vec[i];

		TEST_EQUAL(sum, fs.piece_size(idx));
		TEST_EQUAL(sum, fp.total_on_disk());
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
	fs.set_num_pieces(aux::calc_num_pieces(fs));

	for (auto const idx : fs.piece_range())
	{
		piece_picker picker(fs.total_size(), fs.piece_length());
		picker.we_have(idx);

		aux::vector<std::int64_t, file_index_t> vec;
		aux::file_progress fp;

		fp.init(picker, fs);
		fp.export_progress(vec);

		std::int64_t sum = 0;
		for (file_index_t i(0); i < vec.end_index(); ++i)
			sum += vec[i];

		TEST_EQUAL(sum, fs.piece_size(idx));
		TEST_EQUAL(sum, fp.total_on_disk());
	}
}

TORRENT_TEST(update_simple_sequential)
{
	int const piece_size = 256;

	file_storage fs;
	fs.add_file("torrent/1", 100000);
	fs.add_file("torrent/2", 100);
	fs.add_file("torrent/3", 45000);
	fs.set_piece_length(piece_size);
	fs.set_num_pieces(aux::calc_num_pieces(fs));

	piece_picker picker(fs.total_size(), fs.piece_length());

	aux::file_progress fp;

	fp.init(picker, fs);

	int count = 0;

	for (auto const idx : fs.piece_range())
	{
		fp.update(fs, idx, [&](file_index_t const file_index)
		{
			count++;

			aux::vector<std::int64_t, file_index_t> vec;
			fp.export_progress(vec);

			TEST_EQUAL(vec[file_index], fs.file_size(file_index));
		});
	}

	TEST_EQUAL(count, fs.num_files());
}

TORRENT_TEST(pad_file_completion_callback)
{
	int const piece_size = 256;

	file_storage fs;
	fs.add_file("torrent/1", 100000);
	fs.add_file("torrent/2", 100, file_storage::flag_pad_file);
	fs.add_file("torrent/3", 45000);
	fs.set_piece_length(piece_size);
	fs.set_num_pieces(aux::calc_num_pieces(fs));

	piece_picker picker(fs.total_size(), fs.piece_length());

	aux::file_progress fp;
	fp.init(picker, fs);
	int count = 0;
	for (auto const idx : fs.piece_range())
	{
		fp.update(fs, idx, [&](file_index_t const file_index)
		{
			// there is no callback for the pad-file, and it also doesn't count
			// as "total_on_disk()"
			if (count == 0) TEST_EQUAL(fp.total_on_disk(), 100000);
			else if (count == 1) TEST_EQUAL(fp.total_on_disk(), 145000);

			count++;

			aux::vector<std::int64_t, file_index_t> vec;
			fp.export_progress(vec);

			TEST_EQUAL(vec[file_index], fs.file_size(file_index));
		});
	}

	TEST_EQUAL(count, 2);
}
