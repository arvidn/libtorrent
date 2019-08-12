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

#include <iostream>
#include "test.hpp"
#include "setup_transfer.hpp"

#include "libtorrent/file_storage.hpp"
#include "libtorrent/aux_/path.hpp"

using namespace lt;

namespace {

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

} // anonymous namespace

TORRENT_TEST(coalesce_path)
{
	file_storage st;
	st.add_file(combine_path("test", "a"), 10000);
	TEST_EQUAL(st.paths().size(), 1);
	TEST_EQUAL(st.paths()[0], "");
	st.add_file(combine_path("test", "b"), 20000);
	TEST_EQUAL(st.paths().size(), 1);
	TEST_EQUAL(st.paths()[0], "");
	st.add_file(combine_path("test", combine_path("c", "a")), 30000);
	TEST_EQUAL(st.paths().size(), 2);
	TEST_EQUAL(st.paths()[0], "");
	TEST_EQUAL(st.paths()[1], "c");

	// make sure that two files with the same path shares the path entry
	st.add_file(combine_path("test", combine_path("c", "b")), 40000);
	TEST_EQUAL(st.paths().size(), 2);
	TEST_EQUAL(st.paths()[0], "");
	TEST_EQUAL(st.paths()[1], "c");

	// cause pad files to be created, to make sure the pad files also share the
	// same path entries
	st.optimize(0, 1024, true);

	TEST_EQUAL(st.paths().size(), 3);
	TEST_EQUAL(st.paths()[0], "");
	TEST_EQUAL(st.paths()[1], "c");
	TEST_EQUAL(st.paths()[2], ".pad");
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

	st.rename_file(file_index_t{0}, combine_path("test__", "a"));
	TEST_EQUAL(st.file_path(file_index_t{0}, "."), combine_path(".", combine_path("test__"
		, "a")));
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

	st.add_file_borrow({filename, 5}, combine_path("test-torrent-1", "test1")
		, 10);

	// test filename_ptr and filename_len
	TEST_EQUAL(st.file_name_ptr(file_index_t{0}), filename);
	TEST_EQUAL(st.file_name_len(file_index_t{0}), 5);
	TEST_EQUAL(st.file_name(file_index_t{0}), string_view(filename, 5));

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

TORRENT_TEST(invalid_path1)
{
	file_storage st;
#ifdef TORRENT_WINDOWS
	st.add_file_borrow({}, R"(+\\\()", 10);
#else
	st.add_file_borrow({}, "+///(", 10);
#endif

	TEST_EQUAL(st.file_name(file_index_t{0}), "(");
	TEST_EQUAL(st.file_path(file_index_t{0}, ""), combine_path("+", "("));
}

TORRENT_TEST(invalid_path2)
{
	file_storage st;
#ifdef TORRENT_WINDOWS
	st.add_file_borrow({}, R"(+\\\+\\()", 10);
#else
	st.add_file_borrow({}, "+///+//(", 10);
#endif

	TEST_EQUAL(st.file_name(file_index_t{0}), "(");
	TEST_EQUAL(st.file_path(file_index_t{0}, ""), combine_path("+", combine_path("+", "(")));
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

namespace {

void test_optimize(std::vector<int> file_sizes
	, int const alignment
	, int const pad_file_limit
	, bool const tail_padding
	, std::vector<int> const expected_order)
{
	file_storage fs;
	int i = 0;
	for (int s : file_sizes)
	{
		fs.add_file(combine_path("test", std::to_string(i++)), s);
	}
	fs.optimize(pad_file_limit, alignment, tail_padding);

	TEST_EQUAL(fs.num_files(), int(expected_order.size()));
	if (fs.num_files() != int(expected_order.size())) return;

	std::cout << "{ ";
	for (auto const idx : fs.file_range())
	{
		if (fs.file_flags(idx) & file_storage::flag_pad_file) std::cout << "*";
		std::cout << fs.file_size(idx) << " ";
	}
	std::cout << "}\n";

	file_index_t idx{0};
	int num_pad_files = 0;
	for (int expect : expected_order)
	{
		if (expect == -1)
		{
			TEST_CHECK(fs.file_flags(idx) & file_storage::flag_pad_file);
			TEST_EQUAL(fs.file_name(idx), std::to_string(num_pad_files++));
		}
		else
		{
			TEST_EQUAL(fs.file_name(idx), std::to_string(expect));
			TEST_EQUAL(fs.file_size(idx), file_sizes[std::size_t(expect)]);
		}
		++idx;
	}
}

} // anonymous namespace

TORRENT_TEST(optimize_order_large_first)
{
	test_optimize({1000, 3000, 10000}, 1024, 1024, false, {2, -1, 1, 0});
}

TORRENT_TEST(optimize_tail_padding2)
{
	// when tail padding is enabled, a pad file is added at the end
	test_optimize({2000}, 1024, 1024, true, {0, -1});
}

TORRENT_TEST(optimize_tail_padding3)
{
	// when tail padding is enabled, a pad file is added at the end, even if the
	// file is smaller than the alignment, as long as pad_file_limit is 0 *(which
	// means files are aligned unconditionally)
	test_optimize({1000}, 1024, 0, true, {0, -1});
}

TORRENT_TEST(optimize_tail_padding_small_files)
{
	// files smaller than the pad file limit are not tail-padded
	test_optimize({1000, 1, 2}, 1024, 50, true, {0, -1, 2, 1});
}

TORRENT_TEST(optimize_tail_padding_small_files2)
{
	// files larger than the pad file limit are not tail-padded
	test_optimize({1000, 1, 2}, 1024, 0, true, {0, -1, 2, -1, 1, -1});
}

TORRENT_TEST(optimize_prioritize_aligned_size)
{
	// file 0 of size 1024 will be chosen over the larger file, since it won't
	// affect the alignment of the next file
	test_optimize({1024, 3000, 10}, 1024, 1024, false, {0, 1, 2});
}

TORRENT_TEST(optimize_fill_with_small_files)
{
	// fill in space that otherwise would just be a pad file with other small
	// files.
	test_optimize({2000, 5000, 48, 120}, 1024, 1024, false, {1, 3, 0, 2});
}

TORRENT_TEST(optimize_pad_all)
{
	// when pad_size_limit is 0, every file is padded to alignment, regardless of
	// how big it is
	// the empty file is first, since it doesn't affect alignment of the next
	// file
	test_optimize({48, 1, 0, 5000}, 1024, 0, false, {2, 3, -1, 0, -1, 1});
}

TORRENT_TEST(optimize_pad_all_with_tail)
{
	// when pad_size_limit is 0, every file is padded to alignment, regardless of
	// how big it is, also with tail-padding enabled
	test_optimize({48, 1, 0, 5000}, 1024, 0, true, {2, 3, -1, 0, -1, 1, -1});
}

TORRENT_TEST(piece_size_last_piece)
{
	file_storage fs;
	fs.set_piece_length(1024);
	fs.add_file("0", 100);
	fs.set_num_pieces(int((fs.total_size() + 1023) / 1024));
	TEST_EQUAL(fs.piece_size(piece_index_t{0}), 100);
}

TORRENT_TEST(piece_size_middle_piece)
{
	file_storage fs;
	fs.set_piece_length(1024);
	fs.add_file("0", 2000);
	fs.set_num_pieces(int((fs.total_size() + 1023) / 1024));
	TEST_EQUAL(fs.piece_size(piece_index_t{0}), 1024);
	TEST_EQUAL(fs.piece_size(piece_index_t{1}), 2000 - 1024);
}

TORRENT_TEST(file_index_at_offset)
{
	file_storage fs;
	fs.set_piece_length(1024);
	fs.add_file("test/0", 1);
	fs.add_file("test/1", 2);
	fs.add_file("test/2", 3);
	fs.add_file("test/3", 4);
	fs.add_file("test/4", 5);
	std::int64_t offset = 0;
	for (int f : {0, 1, 1, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4})
	{
		TEST_EQUAL(fs.file_index_at_offset(offset++), file_index_t{f});
	}
}

TORRENT_TEST(map_block_start)
{
	file_storage fs;
	fs.set_piece_length(1024);
	fs.add_file("test/0", 1);
	fs.add_file("test/1", 2);
	fs.add_file("test/2", 3);
	fs.add_file("test/3", 4);
	fs.add_file("test/4", 5);
	fs.set_num_pieces(int((fs.total_size() + 1023) / 1024));
	int len = 0;
	for (int f : {0, 1, 2, 2, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 5})
	{
		std::vector<file_slice> const map = fs.map_block(piece_index_t{0}, 0, len);
		TEST_EQUAL(int(map.size()), f);
		file_index_t file_index{0};
		std::int64_t actual_len = 0;
		for (auto file : map)
		{
			TEST_EQUAL(file.file_index, file_index++);
			TEST_EQUAL(file.offset, 0);
			actual_len += file.size;
		}
		TEST_EQUAL(actual_len, len);
		++len;
	}
}

TORRENT_TEST(map_block_mid)
{
	file_storage fs;
	fs.set_piece_length(1024);
	fs.add_file("test/0", 1);
	fs.add_file("test/1", 2);
	fs.add_file("test/2", 3);
	fs.add_file("test/3", 4);
	fs.add_file("test/4", 5);
	fs.set_num_pieces(int((fs.total_size() + 1023) / 1024));
	int offset = 0;
	for (int f : {0, 1, 1, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4})
	{
		std::vector<file_slice> const map = fs.map_block(piece_index_t{0}, offset, 1);
		TEST_EQUAL(int(map.size()), 1);
		auto const& file = map[0];
		TEST_EQUAL(file.file_index, file_index_t{f});
		TEST_CHECK(file.offset <= offset);
		TEST_EQUAL(file.size, 1);
		++offset;
	}
}

#ifdef TORRENT_WINDOWS
#define SEP "\\"
#else
#define SEP "/"
#endif

TORRENT_TEST(sanitize_symlinks)
{
	file_storage fs;
	fs.set_piece_length(1024);

	// invalid
#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
	fs.add_file("test/0", 0, file_storage::flag_symlink, 0, "C:\\invalid\\target\\path");
#else
	fs.add_file("test/0", 0, file_storage::flag_symlink, 0, "/invalid/target/path");
#endif

	// there is no file with this name, so this is invalid
	fs.add_file("test/1", 0, file_storage::flag_symlink, 0, "ZZ");

	// there is no file with this name, so this is invalid
	fs.add_file("test/2", 0, file_storage::flag_symlink, 0, "B" SEP "B" SEP "ZZ");

	// this should be OK
	fs.add_file("test/3", 0, file_storage::flag_symlink, 0, "0");

	// this should be OK
	fs.add_file("test/4", 0, file_storage::flag_symlink, 0, "A");

	// this is advanced, but OK
	fs.add_file("test/5", 0, file_storage::flag_symlink, 0, "4" SEP "B");

	// this is advanced, but OK
	fs.add_file("test/6", 0, file_storage::flag_symlink, 0, "5" SEP "C");

	// this is not OK
	fs.add_file("test/7", 0, file_storage::flag_symlink, 0, "4" SEP "B" SEP "C" SEP "ZZ");

	// this is the only actual content
	fs.add_file("test/A" SEP "B" SEP "C", 10000);
	fs.set_num_pieces(int((fs.total_size() + 1023) / 1024));

	fs.sanitize_symlinks();

	// these were all invalid symlinks, so they're made to point to themselves
	TEST_EQUAL(fs.symlink(file_index_t{0}), "test" SEP "0");
	TEST_EQUAL(fs.symlink(file_index_t{1}), "test" SEP "1");
	TEST_EQUAL(fs.symlink(file_index_t{2}), "test" SEP "2");

	// ok
	TEST_EQUAL(fs.symlink(file_index_t{3}), "test" SEP "0");
	TEST_EQUAL(fs.symlink(file_index_t{4}), "test" SEP "A");
	TEST_EQUAL(fs.symlink(file_index_t{5}), "test" SEP "4" SEP "B");
	TEST_EQUAL(fs.symlink(file_index_t{6}), "test" SEP "5" SEP "C");

	// does not point to a valid file
	TEST_EQUAL(fs.symlink(file_index_t{7}), "test" SEP "7");
}

TORRENT_TEST(sanitize_symlinks_single_file)
{
	file_storage fs;
	fs.set_piece_length(1024);
	fs.add_file("test", 1);
	fs.set_num_pieces(int((fs.total_size() + 1023) / 1024));

	fs.sanitize_symlinks();

	TEST_EQUAL(fs.file_path(file_index_t{0}), "test");
}

TORRENT_TEST(sanitize_symlinks_cascade)
{
	file_storage fs;
	fs.set_piece_length(1024);

	fs.add_file("test/0", 0, file_storage::flag_symlink, 0, "1" SEP "ZZ");
	fs.add_file("test/1", 0, file_storage::flag_symlink, 0, "2");
	fs.add_file("test/2", 0, file_storage::flag_symlink, 0, "3");
	fs.add_file("test/3", 0, file_storage::flag_symlink, 0, "4");
	fs.add_file("test/4", 0, file_storage::flag_symlink, 0, "5");
	fs.add_file("test/5", 0, file_storage::flag_symlink, 0, "6");
	fs.add_file("test/6", 0, file_storage::flag_symlink, 0, "7");
	fs.add_file("test/7", 0, file_storage::flag_symlink, 0, "A");
	fs.add_file("test/no-exist", 0, file_storage::flag_symlink, 0, "1" SEP "ZZZ");

	// this is the only actual content
	fs.add_file("test/A" SEP "ZZ", 10000);
	fs.set_num_pieces(int((fs.total_size() + 1023) / 1024));

	fs.sanitize_symlinks();

	TEST_EQUAL(fs.symlink(file_index_t{0}), "test" SEP "1" SEP "ZZ");
	TEST_EQUAL(fs.symlink(file_index_t{1}), "test" SEP "2");
	TEST_EQUAL(fs.symlink(file_index_t{2}), "test" SEP "3");
	TEST_EQUAL(fs.symlink(file_index_t{3}), "test" SEP "4");
	TEST_EQUAL(fs.symlink(file_index_t{4}), "test" SEP "5");
	TEST_EQUAL(fs.symlink(file_index_t{5}), "test" SEP "6");
	TEST_EQUAL(fs.symlink(file_index_t{6}), "test" SEP "7");
	TEST_EQUAL(fs.symlink(file_index_t{7}), "test" SEP "A");
	TEST_EQUAL(fs.symlink(file_index_t{8}), "test" SEP "no-exist");
}

TORRENT_TEST(sanitize_symlinks_circular)
{
	file_storage fs;
	fs.set_piece_length(1024);

	fs.add_file("test/0", 0, file_storage::flag_symlink, 0, "1");
	fs.add_file("test/1", 0, file_storage::flag_symlink, 0, "0");

	// when this is resolved, we end up in an infinite loop. Make sure we can
	// handle that
	fs.add_file("test/2", 0, file_storage::flag_symlink, 0, "0/ZZ");

	// this is the only actual content
	fs.add_file("test/A" SEP "ZZ", 10000);
	fs.set_num_pieces(int((fs.total_size() + 1023) / 1024));

	fs.sanitize_symlinks();

	TEST_EQUAL(fs.symlink(file_index_t{0}), "test" SEP "1");
	TEST_EQUAL(fs.symlink(file_index_t{1}), "test" SEP "0");

	// this was invalid, so it points to itself
	TEST_EQUAL(fs.symlink(file_index_t{2}), "test" SEP "2");
}

TORRENT_TEST(query_symlinks)
{
	file_storage fs;
	fs.set_piece_length(1024);
	fs.add_file("test/0", 0, file_storage::flag_symlink, 0, "0");
	fs.add_file("test/1", 0, file_storage::flag_symlink, 0, "1");
	fs.add_file("test/2", 0, file_storage::flag_symlink, 0, "2");
	fs.add_file("test/3", 0, file_storage::flag_symlink, 0, "3");

	auto const& ret1 = fs.symlink(file_index_t{0});
	auto const& ret2 = fs.symlink(file_index_t{1});
	auto const& ret3 = fs.symlink(file_index_t{2});
	auto const& ret4 = fs.symlink(file_index_t{3});

	TEST_CHECK(ret1 != ret2);
	TEST_CHECK(ret1 != ret3);
	TEST_CHECK(ret1 != ret4);
	TEST_CHECK(ret2 != ret3);
	TEST_CHECK(ret2 != ret4);
	TEST_CHECK(ret3 != ret4);
}

// TODO: test file attributes
// TODO: test symlinks
// TODO: test reorder_file (make sure internal_file_entry::swap() is used)
