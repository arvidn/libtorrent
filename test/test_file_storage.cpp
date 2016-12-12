/*

Copyright (c) 2013, Arvid Norberg
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
#include "setup_transfer.hpp"

#include "libtorrent/file_storage.hpp"
#include "libtorrent/file.hpp"

using namespace libtorrent;

void setup_test_storage(file_storage& st)
{
	st.add_file(combine_path("test", "a"), 10000);
	st.add_file(combine_path("test", "b"), 20000);
	st.add_file(combine_path("test", combine_path("c", "a")), 30000);
	st.add_file(combine_path("test", combine_path("c", "b")), 40000);

	st.set_piece_length(0x4000);
	st.set_num_pieces((int(st.total_size()) + st.piece_length() - 1) / 0x4000);

	TEST_EQUAL(st.file_name(file_index_t{0}), "a");
	TEST_EQUAL(st.file_name(file_index_t{1}), "b");
	TEST_EQUAL(st.file_name(file_index_t{2}), "a");
	TEST_EQUAL(st.file_name(file_index_t{3}), "b");
	TEST_EQUAL(st.name(), "test");

	TEST_EQUAL(st.file_path(file_index_t{0}), combine_path("test", "a"));
	TEST_EQUAL(st.file_path(file_index_t{1}), combine_path("test", "b"));
	TEST_EQUAL(st.file_path(file_index_t{2}), combine_path("test", combine_path("c", "a")));
	TEST_EQUAL(st.file_path(file_index_t{3}), combine_path("test", combine_path("c", "b")));

	TEST_EQUAL(st.file_size(file_index_t{0}), 10000);
	TEST_EQUAL(st.file_size(file_index_t{1}), 20000);
	TEST_EQUAL(st.file_size(file_index_t{2}), 30000);
	TEST_EQUAL(st.file_size(file_index_t{3}), 40000);

	TEST_EQUAL(st.file_offset(file_index_t{0}), 0);
	TEST_EQUAL(st.file_offset(file_index_t{1}), 10000);
	TEST_EQUAL(st.file_offset(file_index_t{2}), 30000);
	TEST_EQUAL(st.file_offset(file_index_t{3}), 60000);

	TEST_EQUAL(st.total_size(), 100000);
	TEST_EQUAL(st.piece_length(), 0x4000);
	std::printf("%d\n", st.num_pieces());
	TEST_EQUAL(st.num_pieces(), (100000 + 0x3fff) / 0x4000);
}

TORRENT_TEST(rename_file)
{
	// test rename_file
	file_storage st;
	setup_test_storage(st);

	st.rename_file(file_index_t{0}, combine_path("test", combine_path("c", "d")));
	TEST_EQUAL(st.file_path(file_index_t{0}, "."), combine_path(".", combine_path("test"
		, combine_path("c", "d"))));
	TEST_EQUAL(st.file_path(file_index_t{0}, ""), combine_path("test"
		, combine_path("c", "d")));

	// files with absolute paths should ignore the save_path argument
	// passed in to file_path()
#ifdef TORRENT_WINDOWS
	st.rename_file(file_index_t{0}, "c:\\tmp\\a");
	TEST_EQUAL(st.file_path(file_index_t{0}, "."), "c:\\tmp\\a");
#else
	st.rename_file(file_index_t{0}, "/tmp/a");
	TEST_EQUAL(st.file_path(file_index_t{0}, "."), "/tmp/a");
#endif
}

TORRENT_TEST(set_name)
{
	// test set_name. Make sure the name of the torrent is not encoded
	// in the paths of each individual file. When changing the name of the
	// torrent, the path of the files should change too
	file_storage st;
	setup_test_storage(st);

	st.set_name("test_2");
	TEST_EQUAL(st.file_path(file_index_t{0}, "."), combine_path(".", combine_path("test_2"
		, "a")));
}

TORRENT_TEST(rename_file2)
{
	// test rename_file
	file_storage st;
	st.add_file("a", 10000);
	TEST_EQUAL(st.file_path(file_index_t{0}, ""), "a");

	st.rename_file(file_index_t{0}, combine_path("test", combine_path("c", "d")));
	TEST_EQUAL(st.file_path(file_index_t{0}, "."), combine_path(".", combine_path("test", combine_path("c", "d"))));
	TEST_EQUAL(st.file_path(file_index_t{0}, ""), combine_path("test", combine_path("c", "d")));

#ifdef TORRENT_WINDOWS
	st.rename_file(file_index_t{0}, "c:\\tmp\\a");
	TEST_EQUAL(st.file_path(file_index_t{0}, "."), "c:\\tmp\\a");
	TEST_EQUAL(st.file_path(file_index_t{0}, "c:\\test-1\\test2"), "c:\\tmp\\a");
#else
	st.rename_file(file_index_t{0}, "/tmp/a");
	TEST_EQUAL(st.file_path(file_index_t{0}, "."), "/tmp/a");
	TEST_EQUAL(st.file_path(file_index_t{0}, "/usr/local/temp"), "/tmp/a");
#endif

	st.rename_file(file_index_t{0}, combine_path("tmp", "a"));
	TEST_EQUAL(st.file_path(file_index_t{0}, "."), combine_path("tmp", "a"));
}

TORRENT_TEST(pointer_offset)
{
	// test applying pointer offset
	file_storage st;
	char const filename[] = "test1fooba";

	st.add_file_borrow(filename, 5, combine_path("test-torrent-1", "test1")
		, 10);

	// test filename_ptr and filename_len
	TEST_EQUAL(st.file_name_ptr(file_index_t{0}), filename);
	TEST_EQUAL(st.file_name_len(file_index_t{0}), 5);

	TEST_EQUAL(st.file_path(file_index_t{0}, ""), combine_path("test-torrent-1", "test1"));
	TEST_EQUAL(st.file_path(file_index_t{0}, "tmp"), combine_path("tmp"
		, combine_path("test-torrent-1", "test1")));

	// apply a pointer offset of 5 bytes. The name of the file should
	// change to "fooba".

	st.apply_pointer_offset(5);

	TEST_EQUAL(st.file_path(file_index_t{0}, ""), combine_path("test-torrent-1", "fooba"));
	TEST_EQUAL(st.file_path(file_index_t{0}, "tmp"), combine_path("tmp"
		, combine_path("test-torrent-1", "fooba")));

	// test filename_ptr and filename_len
	TEST_EQUAL(st.file_name_ptr(file_index_t{0}), filename + 5);
	TEST_EQUAL(st.file_name_len(file_index_t{0}), 5);
}

TORRENT_TEST(map_file)
{
	// test map_file
	file_storage fs;
	fs.set_piece_length(512);
	fs.add_file(combine_path("temp_storage", "test1.tmp"), 17);
	fs.add_file(combine_path("temp_storage", "test2.tmp"), 612);
	fs.add_file(combine_path("temp_storage", "test3.tmp"), 0);
	fs.add_file(combine_path("temp_storage", "test4.tmp"), 0);
	fs.add_file(combine_path("temp_storage", "test5.tmp"), 3253);
	// size: 3882
	fs.add_file(combine_path("temp_storage", "test6.tmp"), 841);
	// size: 4723

	peer_request rq = fs.map_file(file_index_t{0}, 0, 10);
	TEST_EQUAL(rq.piece, piece_index_t{0});
	TEST_EQUAL(rq.start, 0);
	TEST_EQUAL(rq.length, 10);
	rq = fs.map_file(file_index_t{5}, 0, 10);
	TEST_EQUAL(rq.piece, piece_index_t{7});
	TEST_EQUAL(rq.start, 298);
	TEST_EQUAL(rq.length, 10);
	rq = fs.map_file(file_index_t{5}, 0, 1000);
	TEST_EQUAL(rq.piece, piece_index_t{7});
	TEST_EQUAL(rq.start, 298);
	TEST_EQUAL(rq.length, 841);
}

TORRENT_TEST(file_path_hash)
{
	// test file_path_hash and path_hash. Make sure we can detect a path
	// whose name collides with
	file_storage fs;
	fs.set_piece_length(512);
	fs.add_file(combine_path("temp_storage", "Foo"), 17);
	fs.add_file(combine_path("temp_storage", "foo"), 612);

	std::printf("path: %s\n", fs.file_path(file_index_t(0)).c_str());
	std::printf("file: %s\n", fs.file_path(file_index_t(1)).c_str());
	std::uint32_t file_hash0 = fs.file_path_hash(file_index_t(0), "a");
	std::uint32_t file_hash1 = fs.file_path_hash(file_index_t(1), "a");
	TEST_EQUAL(file_hash0, file_hash1);
}

// make sure that files whose size is a multiple by the alignment are picked
// first, since that lets us keep placing files aligned
TORRENT_TEST(optimize_aligned_sizes)
{
	file_storage fs;
	fs.set_piece_length(512);
	fs.add_file(combine_path("s", "1"), 1);
	fs.add_file(combine_path("s", "2"), 40000);
	fs.add_file(combine_path("s", "3"), 1024);

	fs.optimize(512, 512, false);

	// since the size of file 3 is a multiple of the alignment (512), it should
	// be prioritized, to minimize the amount of padding.
	// after that, we want to pick the largest file (2), and since file 1 is
	// smaller than the pad-file limit (512) we won't pad it. Since tail_padding
	// is false, we won't pad the tail of the torrent either

	TEST_EQUAL(fs.num_files(), 3);

	TEST_EQUAL(fs.file_size(file_index_t(0)), 1024);
	TEST_EQUAL(fs.file_name(file_index_t(0)), "3");
	TEST_EQUAL(fs.pad_file_at(file_index_t(0)), false);

	TEST_EQUAL(fs.file_size(file_index_t(1)), 40000);
	TEST_EQUAL(fs.file_name(file_index_t(1)), "2");
	TEST_EQUAL(fs.pad_file_at(file_index_t(1)), false);

	TEST_EQUAL(fs.file_size(file_index_t(2)), 1);
	TEST_EQUAL(fs.file_name(file_index_t(2)), "1");
	TEST_EQUAL(fs.pad_file_at(file_index_t(2)), false);
}

// make sure we pad the end of the torrent when tail_padding is specified
TORRENT_TEST(optimize_tail_padding)
{
	file_storage fs;
	fs.set_piece_length(512);
	fs.add_file(combine_path("s", "1"), 700);

	fs.optimize(512, 512, true);

	// since the size of file 3 is a multiple of the alignment (512), it should
	// be prioritized, to minimize the amount of padding.
	// after that, we want to pick the largest file (2), and since file 1 is
	// smaller than the pad-file limit (512) we won't pad it. Since tail_padding
	// is false, we won't pad the tail of the torrent either

	TEST_EQUAL(fs.num_files(), 2);

	TEST_EQUAL(fs.file_size(file_index_t(0)), 700);
	TEST_EQUAL(fs.file_name(file_index_t(0)), "1");
	TEST_EQUAL(fs.pad_file_at(file_index_t(0)), false);

	TEST_EQUAL(fs.file_size(file_index_t(1)), 1024 - 700);
	TEST_EQUAL(fs.pad_file_at(file_index_t(1)), true);
}


// make sure we fill in padding with small files
TORRENT_TEST(optimize_pad_fillers)
{
	file_storage fs;
	fs.set_piece_length(512);
	fs.add_file(combine_path("s", "1"), 1);
	fs.add_file(combine_path("s", "2"), 1000);
	fs.add_file(combine_path("s", "3"), 1001);

	fs.optimize(512, 512, false);

	// first we pick the largest file, then we need to add padding, since file 1
	// is smaller than the pad file limit, it won't be aligned anyway, so we
	// place that as part of the padding

	TEST_EQUAL(fs.num_files(), 4);

	TEST_EQUAL(fs.file_size(file_index_t(0)), 1001);
	TEST_EQUAL(fs.file_name(file_index_t(0)), "3");
	TEST_EQUAL(fs.pad_file_at(file_index_t(0)), false);

	TEST_EQUAL(fs.file_size(file_index_t(1)), 1);
	TEST_EQUAL(fs.file_name(file_index_t(1)), "1");
	TEST_EQUAL(fs.pad_file_at(file_index_t(1)), false);

	TEST_EQUAL(fs.file_size(file_index_t(2)), 1024 - (1001 + 1));
	TEST_EQUAL(fs.pad_file_at(file_index_t(2)), true);

	TEST_EQUAL(fs.file_size(file_index_t(3)), 1000);
	TEST_EQUAL(fs.file_name(file_index_t(3)), "2");
	TEST_EQUAL(fs.pad_file_at(file_index_t(3)), false);
}

TORRENT_TEST(piece_range_exclusive)
{
	int const piece_size = 16;
	file_storage fs;
	fs.set_piece_length(piece_size);
	fs.add_file(combine_path("temp_storage", "0"), piece_size);
	fs.add_file(combine_path("temp_storage", "1"), piece_size * 4 + 1);
	fs.add_file(combine_path("temp_storage", "2"), piece_size * 4 - 1);
	fs.set_num_pieces(int((fs.total_size() + piece_size - 1) / piece_size));
	//        +---+---+---+---+---+---+---+---+---+
	// pieces | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
	//        +---+---+---+---+---+---+---+---+---+
	// files  | 0 |        1       |        2     |
	//        +---+----------------+--------------+

	TEST_CHECK(aux::file_piece_range_exclusive(fs, file_index_t(0)) == std::make_tuple(piece_index_t(0), piece_index_t(1)));
	TEST_CHECK(aux::file_piece_range_exclusive(fs, file_index_t(1)) == std::make_tuple(piece_index_t(1), piece_index_t(5)));
	TEST_CHECK(aux::file_piece_range_exclusive(fs, file_index_t(2)) == std::make_tuple(piece_index_t(6), piece_index_t(9)));
}

TORRENT_TEST(piece_range_inclusive)
{
	int const piece_size = 16;
	file_storage fs;
	fs.set_piece_length(piece_size);
	fs.add_file(combine_path("temp_storage", "0"), piece_size);
	fs.add_file(combine_path("temp_storage", "1"), piece_size * 4 + 1);
	fs.add_file(combine_path("temp_storage", "2"), piece_size * 4 - 1);
	fs.set_num_pieces(int((fs.total_size() + piece_size - 1) / piece_size));
	//        +---+---+---+---+---+---+---+---+---+
	// pieces | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
	//        +---+---+---+---+---+---+---+---+---+
	// files  | 0 |        1       |        2     |
	//        +---+----------------+--------------+

	TEST_CHECK(aux::file_piece_range_inclusive(fs, file_index_t(0)) == std::make_tuple(piece_index_t(0), piece_index_t(1)));
	TEST_CHECK(aux::file_piece_range_inclusive(fs, file_index_t(1)) == std::make_tuple(piece_index_t(1), piece_index_t(6)));
	TEST_CHECK(aux::file_piece_range_inclusive(fs, file_index_t(2)) == std::make_tuple(piece_index_t(5), piece_index_t(9)));
}

TORRENT_TEST(piece_range)
{
	int const piece_size = 0x4000;
	file_storage fs;
	fs.set_piece_length(piece_size);
	fs.add_file(combine_path("temp_storage", "0"), piece_size * 3);
	fs.add_file(combine_path("temp_storage", "1"), piece_size * 3 + 0x30);
	fs.set_num_pieces(int((fs.total_size() + piece_size - 1) / piece_size));
	//        +---+---+---+---+---+---+---+
	// pieces | 0 | 1 | 2 | 3 | 4 | 5 | 6 |
	//        +---+---+---+---+---+---+---+
	// files  |      0    |      1     |
	//        +---+-------+------------+

	TEST_CHECK(aux::file_piece_range_inclusive(fs, file_index_t(0)) == std::make_tuple(piece_index_t(0), piece_index_t(3)));
	TEST_CHECK(aux::file_piece_range_inclusive(fs, file_index_t(1)) == std::make_tuple(piece_index_t(3), piece_index_t(7)));

	TEST_CHECK(aux::file_piece_range_exclusive(fs, file_index_t(0)) == std::make_tuple(piece_index_t(0), piece_index_t(3)));
	TEST_CHECK(aux::file_piece_range_exclusive(fs, file_index_t(1)) == std::make_tuple(piece_index_t(3), piece_index_t(7)));
}

// TODO: test file_storage::optimize
// TODO: test map_block
// TODO: test piece_size(int piece)
// TODO: test file_index_at_offset
// TODO: test file attributes
// TODO: test symlinks
// TODO: test pad_files
// TODO: test reorder_file (make sure internal_file_entry::swap() is used)

