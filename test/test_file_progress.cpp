/*

Copyright (c) 2015-2020, Arvid Norberg
Copyright (c) 2018-2019, 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test_utils.hpp"
#include "libtorrent/aux_/file_progress.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/aux_/piece_picker.hpp"

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
		aux::piece_picker picker(4, fs.total_size() % 4, fs.num_pieces());
		picker.we_have(idx);

		aux::file_progress fp;
		fp.init(picker, fs);

		aux::vector<std::int64_t, file_index_t> vec;
		fp.export_progress(vec);

		std::int64_t sum = 0;
		for (file_index_t i(0); i < vec.end_index(); ++i)
			sum += vec[i];

		TEST_EQUAL(sum, fs.piece_size(idx));
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
		aux::piece_picker picker(4, fs.total_size() % 4, fs.num_pieces());
		picker.we_have(idx);

		aux::vector<std::int64_t, file_index_t> vec;
		aux::file_progress fp;

		fp.init(picker, fs);
		fp.export_progress(vec);

		std::int64_t sum = 0;
		for (file_index_t i(0); i < vec.end_index(); ++i)
			sum += vec[i];

		TEST_EQUAL(sum, fs.piece_size(idx));
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

	aux::piece_picker picker(4, fs.total_size() % 4, fs.num_pieces());

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
