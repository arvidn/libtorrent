/*

Copyright (c) 2013, 2015-2017, 2019, Arvid Norberg
Copyright (c) 2017, 2019, Steven Siloti
Copyright (c) 2018, Alden Torres
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
	st.set_num_pieces(aux::calc_num_pieces(st));

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

constexpr aux::path_index_t operator""_path(unsigned long long i)
{ return aux::path_index_t(static_cast<std::uint32_t>(i)); }

} // anonymous namespace

TORRENT_TEST(coalesce_path)
{
	file_storage st;
	st.set_piece_length(0x4000);
	st.add_file(combine_path("test", "a"), 10000);
	TEST_EQUAL(st.paths().size(), 1);
	TEST_EQUAL(st.paths()[0_path], "");
	st.add_file(combine_path("test", "b"), 20000);
	TEST_EQUAL(st.paths().size(), 1);
	TEST_EQUAL(st.paths()[0_path], "");
	st.add_file(combine_path("test", combine_path("c", "a")), 30000);
	TEST_EQUAL(st.paths().size(), 2);
	TEST_EQUAL(st.paths()[0_path], "");
	TEST_EQUAL(st.paths()[1_path], "c");

	// make sure that two files with the same path shares the path entry
	st.add_file(combine_path("test", combine_path("c", "b")), 40000);
	TEST_EQUAL(st.paths().size(), 2);
	TEST_EQUAL(st.paths()[0_path], "");
	TEST_EQUAL(st.paths()[1_path], "c");

	// cause pad files to be created, to make sure the pad files also share the
	// same path entries
	st.canonicalize();

	TEST_EQUAL(st.paths().size(), 3);
	TEST_EQUAL(st.paths()[0_path], "");
	TEST_EQUAL(st.paths()[1_path], "c");
	TEST_EQUAL(st.paths()[2_path], ".pad");
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
	st.set_piece_length(16 * 1024);
	char const filename[] = "test1fooba";
	char const filehash[] = "01234567890123456789-----";
	char const roothash[] = "01234567890123456789012345678912-----";

	st.add_file_borrow({filename, 5}, combine_path("test-torrent-1", "test1")
		, 10, file_flags_t{}, filehash, 0, {}, roothash);

	// test filename_ptr and filename_len
#ifndef TORRENT_NO_DEPRECATE
	TEST_EQUAL(st.file_name_ptr(file_index_t{0}), filename);
	TEST_EQUAL(st.file_name_len(file_index_t{0}), 5);
#endif
	TEST_EQUAL(st.file_name(file_index_t{0}), string_view(filename, 5));
	TEST_EQUAL(st.hash(file_index_t{0}), sha1_hash(filehash));
	TEST_EQUAL(st.root(file_index_t{0}), sha256_hash(roothash));

	TEST_EQUAL(st.file_path(file_index_t{0}, ""), combine_path("test-torrent-1", "test1"));
	TEST_EQUAL(st.file_path(file_index_t{0}, "tmp"), combine_path("tmp"
		, combine_path("test-torrent-1", "test1")));
}

TORRENT_TEST(invalid_path1)
{
	file_storage st;
	st.set_piece_length(16 * 1024);
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
	st.set_piece_length(16 * 1024);
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

// make sure we fill in padding with small files
TORRENT_TEST(canonicalize_pad)
{
	file_storage fs;
	fs.set_piece_length(0x4000);
	fs.add_file(combine_path("s", "2"), 0x7000);
	fs.add_file(combine_path("s", "1"), 1);
	fs.add_file(combine_path("s", "3"), 0x7001);

	fs.canonicalize();

	TEST_EQUAL(fs.num_files(), 5);

	TEST_EQUAL(fs.file_size(file_index_t(0)), 1);
	TEST_EQUAL(fs.file_name(file_index_t(0)), "1");
	TEST_EQUAL(fs.pad_file_at(file_index_t(0)), false);

	TEST_EQUAL(fs.file_size(file_index_t(1)), 0x4000 - 1);
	TEST_EQUAL(fs.pad_file_at(file_index_t(1)), true);

	TEST_EQUAL(fs.file_size(file_index_t(2)), 0x7000);
	TEST_EQUAL(fs.file_name(file_index_t(2)), "2");
	TEST_EQUAL(fs.pad_file_at(file_index_t(2)), false);

	TEST_EQUAL(fs.file_size(file_index_t(3)), 0x8000 - 0x7000);
	TEST_EQUAL(fs.pad_file_at(file_index_t(3)), true);

	TEST_EQUAL(fs.file_size(file_index_t(4)), 0x7001);
	TEST_EQUAL(fs.file_name(file_index_t(4)), "3");
	TEST_EQUAL(fs.pad_file_at(file_index_t(4)), false);
}

// make sure canonicalize sorts by path correctly
TORRENT_TEST(canonicalize_path)
{
	file_storage fs;
	fs.set_piece_length(0x4000);
	fs.add_file(combine_path("b", combine_path("2", "a")), 0x4000);
	fs.add_file(combine_path("b", combine_path("1", "a")), 0x4000);
	fs.add_file(combine_path("b", combine_path("3", "a")), 0x4000);
	fs.add_file(combine_path("b", "11"), 0x4000);

	fs.canonicalize();

	TEST_EQUAL(fs.num_files(), 4);

	TEST_EQUAL(fs.file_path(file_index_t(0)), combine_path("b", combine_path("1", "a")));
	TEST_EQUAL(fs.file_path(file_index_t(1)), combine_path("b", "11"));
	TEST_EQUAL(fs.file_path(file_index_t(2)), combine_path("b", combine_path("2", "a")));
	TEST_EQUAL(fs.file_path(file_index_t(3)), combine_path("b", combine_path("3", "a")));
}

TORRENT_TEST(piece_range_exclusive)
{
	int const piece_size = 16;
	file_storage fs;
	fs.set_piece_length(piece_size);
	fs.add_file(combine_path("temp_storage", "0"), piece_size);
	fs.add_file(combine_path("temp_storage", "1"), piece_size * 4 + 1);
	fs.add_file(combine_path("temp_storage", "2"), piece_size * 4 - 1);
	fs.set_num_pieces(aux::calc_num_pieces(fs));
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
	fs.set_num_pieces(aux::calc_num_pieces(fs));
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
	fs.set_num_pieces(aux::calc_num_pieces(fs));
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

TORRENT_TEST(piece_size_last_piece)
{
	file_storage fs;
	fs.set_piece_length(1024);
	fs.add_file("0", 100);
	fs.set_num_pieces(aux::calc_num_pieces(fs));
	TEST_EQUAL(fs.piece_size(piece_index_t{0}), 100);
}

TORRENT_TEST(piece_size_middle_piece)
{
	file_storage fs;
	fs.set_piece_length(1024);
	fs.add_file("0", 2000);
	fs.set_num_pieces(aux::calc_num_pieces(fs));
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
	fs.set_num_pieces(aux::calc_num_pieces(fs));
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
	fs.set_num_pieces(aux::calc_num_pieces(fs));
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

TORRENT_TEST(query_symlinks2)
{
	file_storage fs;
	fs.set_piece_length(1024);
	fs.add_file("test/0", 10);
	fs.add_file("test/1", 10);
	fs.add_file("test/2", 10);
	fs.add_file("test/3", 10);

	TEST_CHECK(fs.symlink(file_index_t{0}).empty());
	TEST_CHECK(fs.symlink(file_index_t{1}).empty());
	TEST_CHECK(fs.symlink(file_index_t{2}).empty());
	TEST_CHECK(fs.symlink(file_index_t{3}).empty());
}

TORRENT_TEST(files_equal)
{
	file_storage fs1;
	fs1.set_piece_length(0x4000);
	fs1.add_file("test/0", 1);
	fs1.add_file("test/1", 2);

	file_storage fs2;
	fs2.set_piece_length(0x4000);
	fs2.add_file("test/0", 1);
	fs2.add_file("test/1", 2);

	TEST_CHECK(lt::aux::files_equal(fs1, fs2));
}

TORRENT_TEST(files_equal_num_files)
{
	file_storage fs1;
	fs1.set_piece_length(0x4000);
	fs1.add_file("test/0", 1);
	fs1.add_file("test/1", 2);

	file_storage fs2;
	fs2.set_piece_length(0x4000);
	fs2.add_file("test/0", 3);

	TEST_CHECK(!lt::aux::files_equal(fs1, fs2));
}

TORRENT_TEST(files_equal_size)
{
	file_storage fs1;
	fs1.set_piece_length(0x4000);
	fs1.add_file("test/0", 2);
	fs1.add_file("test/1", 1);

	file_storage fs2;
	fs2.set_piece_length(0x4000);
	fs2.add_file("test/0", 1);
	fs2.add_file("test/1", 2);

	TEST_CHECK(!lt::aux::files_equal(fs1, fs2));
}

TORRENT_TEST(files_equal_name)
{
	file_storage fs1;
	fs1.set_piece_length(0x4000);
	fs1.add_file("test/1", 1);
	fs1.add_file("test/0", 2);

	file_storage fs2;
	fs2.set_piece_length(0x4000);
	fs2.add_file("test/0", 1);
	fs2.add_file("test/1", 2);

	TEST_CHECK(!lt::aux::files_equal(fs1, fs2));
}

TORRENT_TEST(files_equal_flags)
{
	file_storage fs1;
	fs1.set_piece_length(0x4000);
	fs1.add_file("test/0", 1);
	fs1.add_file("test/1", 2, file_storage::flag_hidden);

	file_storage fs2;
	fs2.set_piece_length(0x4000);
	fs2.add_file("test/0", 1);
	fs2.add_file("test/1", 2);

	TEST_CHECK(!lt::aux::files_equal(fs1, fs2));
}

TORRENT_TEST(files_equal_mtime)
{
	file_storage fs1;
	fs1.set_piece_length(0x4000);
	fs1.add_file("test/0", 1, {}, 1234);
	fs1.add_file("test/1", 2, {}, 1235);

	file_storage fs2;
	fs2.set_piece_length(0x4000);
	fs2.add_file("test/0", 1, {}, 1234);
	fs2.add_file("test/1", 2, {}, 1234);

	TEST_CHECK(!lt::aux::files_equal(fs1, fs2));
}

TORRENT_TEST(files_equal_piece_size)
{
	file_storage fs1;
	fs1.set_piece_length(0x8000);
	fs1.add_file("test/0", 1);
	fs1.add_file("test/1", 2);

	file_storage fs2;
	fs2.set_piece_length(0x4000);
	fs2.add_file("test/0", 1);
	fs2.add_file("test/1", 2);

	TEST_CHECK(!lt::aux::files_equal(fs1, fs2));
}

TORRENT_TEST(files_equal_symlink)
{
	file_storage fs1;
	fs1.set_piece_length(0x4000);
	fs1.add_file("test/0", 1);
	fs1.add_file("test/1", 2, file_storage::flag_symlink, 0, "test/0");

	file_storage fs2;
	fs2.set_piece_length(0x4000);
	fs2.add_file("test/0", 1);
	fs2.add_file("test/1", 2, file_storage::flag_symlink, 0, "test/1");

	TEST_CHECK(!lt::aux::files_equal(fs1, fs2));
}

std::int64_t const int_max = std::numeric_limits<int>::max();

TORRENT_TEST(large_files)
{
	file_storage fs1;
	fs1.set_piece_length(0x4000);
	TEST_THROW(fs1.add_file("test/0", int_max / 2 * lt::default_block_size + 1));

	error_code ec;
	fs1.add_file(ec, "test/0", int_max * lt::default_block_size + 1);
	TEST_EQUAL(ec, make_error_code(boost::system::errc::file_too_large));

	// should not throw
	TEST_NOTHROW(fs1.add_file("test/0", int_max / 2 * lt::default_block_size));
}

TORRENT_TEST(large_offset)
{
	file_storage fs1;
	fs1.set_piece_length(0x4000);
	for (int i = 0; i < 16; ++i)
		fs1.add_file(("test/" + std::to_string(i)).c_str(), int_max / 2 * lt::default_block_size);

	// this exceeds the 2^48-1 limit
	TEST_THROW(fs1.add_file("test/16", 262144));

	error_code ec;
	fs1.add_file(ec, "test/8", 262144);
	TEST_EQUAL(ec, make_error_code(errors::torrent_invalid_length));

	// this should be OK, but just
	fs1.add_file("test/8", 262143);
}

TORRENT_TEST(large_filename)
{
	file_storage fs1;
	fs1.set_piece_length(0x4000);
	// yes, this creates an invalid string_view, as it claims to be larger than
	// the allocation. This should be OK though as the test for size never
	// actually looks at the string
	TEST_THROW(fs1.add_file_borrow(string_view("0", 1 << 12), "test/path/", 10));

	error_code ec;
	fs1.add_file_borrow(ec, string_view("0", 1 << 12), "test/path/", 10);
	TEST_EQUAL(ec, make_error_code(boost::system::errc::filename_too_long));
}

TORRENT_TEST(piece_size2)
{
	file_storage fs;
	fs.set_piece_length(0x8000);
	// passing in a root hash (the last argument) makes it follow v2 rules, to
	// add pad files
	fs.add_file("test/0", 0x5000, {}, 0, {}, "01234567890123456789012345678901");

	fs.set_num_pieces(aux::calc_num_pieces(fs));
	TEST_EQUAL(fs.num_pieces(), 1);
	TEST_EQUAL(fs.piece_size2(piece_index_t{0}), 0x5000);

	fs.add_file("test/1", 0x2000, {}, 0, {}, "01234567890123456789012345678901");
	fs.add_file("test/2", 0x8000, {}, 0, {}, "01234567890123456789012345678901");

	fs.set_num_pieces(aux::calc_num_pieces(fs));
	TEST_EQUAL(fs.num_pieces(), 3);
	TEST_EQUAL(fs.piece_size2(piece_index_t{2}), 0x8000);

	fs.add_file("test/3", 8, {}, 0, {}, "01234567890123456789012345678901");

	fs.set_num_pieces(aux::calc_num_pieces(fs));
	TEST_EQUAL(fs.num_pieces(), 4);
	TEST_EQUAL(fs.piece_size2(piece_index_t{0}), 0x5000);
	TEST_EQUAL(fs.piece_size2(piece_index_t{1}), 0x2000);
	TEST_EQUAL(fs.piece_size2(piece_index_t{2}), 0x8000);
	TEST_EQUAL(fs.piece_size2(piece_index_t{3}), 8);

	fs.add_file("test/4", 0x8001, {}, 0, {}, "01234567890123456789012345678901");

	fs.set_num_pieces(aux::calc_num_pieces(fs));
	TEST_EQUAL(fs.num_pieces(), 6);

	TEST_EQUAL(fs.piece_size2(piece_index_t{0}), 0x5000);
	TEST_EQUAL(fs.piece_size2(piece_index_t{1}), 0x2000);
	TEST_EQUAL(fs.piece_size2(piece_index_t{2}), 0x8000);
	TEST_EQUAL(fs.piece_size2(piece_index_t{3}), 8);
	TEST_EQUAL(fs.piece_size2(piece_index_t{4}), 0x8000);
	TEST_EQUAL(fs.piece_size2(piece_index_t{5}), 1);
}

TORRENT_TEST(file_num_blocks)
{
	file_storage fs;
	fs.set_piece_length(0x8000);
	fs.add_file("test/0", 0x5000, {}, 0, {}, "01234567890123456789012345678901");
	fs.add_file("test/1", 0x2000, {}, 0, {}, "01234567890123456789012345678901");
	fs.add_file("test/2", 0x8000, {}, 0, {}, "01234567890123456789012345678901");
	fs.add_file("test/3", 0x8001, {}, 0, {}, "01234567890123456789012345678901");
	fs.add_file("test/4", 1, {}, 0, {}, "01234567890123456789012345678901");

	// generally the number of blocks in a file is:
	// (file_size + lt::default_block_size - 1) / lt::default_block_size

	TEST_EQUAL(fs.file_num_blocks(file_index_t{0}), 2);
	// pad file at index 1
	TEST_EQUAL(fs.file_num_blocks(file_index_t{2}), 1);
	// pad file at index 3
	TEST_EQUAL(fs.file_num_blocks(file_index_t{4}), 2);
	TEST_EQUAL(fs.file_num_blocks(file_index_t{5}), 3);
	// pad file at index 6
	TEST_EQUAL(fs.file_num_blocks(file_index_t{7}), 1);
}

TORRENT_TEST(file_num_pieces)
{
	file_storage fs;
	fs.set_piece_length(0x8000);
	fs.add_file("test/0", 0x5000, {}, 0, {}, "01234567890123456789012345678901");
	fs.add_file("test/1", 0x2000, {}, 0, {}, "01234567890123456789012345678901");
	fs.add_file("test/2", 0x8000, {}, 0, {}, "01234567890123456789012345678901");
	fs.add_file("test/3", 0x8001, {}, 0, {}, "01234567890123456789012345678901");
	fs.add_file("test/4", 1, {}, 0, {}, "01234567890123456789012345678901");

	// generally the number of blocks in a file is:
	// (file_size + lt::default_block_size - 1) / lt::default_block_size

	TEST_EQUAL(fs.file_num_pieces(file_index_t{0}), 1);
	// pad file at index 1
	TEST_EQUAL(fs.file_num_pieces(file_index_t{2}), 1);
	// pad file at index 3
	TEST_EQUAL(fs.file_num_pieces(file_index_t{4}), 1);
	TEST_EQUAL(fs.file_num_pieces(file_index_t{5}), 2);
	// pad file at index 6
	TEST_EQUAL(fs.file_num_pieces(file_index_t{7}), 1);
}

namespace {
int first_piece_node(int piece_size, int file_size)
{
	file_storage fs;
	fs.set_piece_length(piece_size);
	fs.add_file("test/0", file_size, {}, 0, {}, "01234567890123456789012345678901");
	fs.set_num_pieces(aux::calc_num_pieces(fs));
	return fs.file_first_piece_node(file_index_t{0});
}

int first_block_node(int file_size)
{
	file_storage fs;
	fs.set_piece_length(0x10000);
	fs.add_file("test/0", file_size, {}, 0, {}, "01234567890123456789012345678901");
	fs.set_num_pieces(aux::calc_num_pieces(fs));
	return fs.file_first_block_node(file_index_t{0});
}
}

TORRENT_TEST(file_first_piece_node)
{
	// the size of the merkle tree is implied by the size of the file.
	// 0x500000 / 0x10000 = 80 pieces
	// a merkle tree must have a power of 2 number of leaves, so that's 128,
	// thats 7 layers
	TEST_EQUAL(first_piece_node(0x10000, 0x500000), 127);
	TEST_EQUAL(first_piece_node(0x8000, 0x500000), 255);
	TEST_EQUAL(first_piece_node(0x4000, 0x500000), 511);
	TEST_EQUAL(first_piece_node(0x2000, 0x500000), 1023);
	TEST_EQUAL(first_piece_node(0x1000, 0x500000), 2047);

	// also test boundary cases around exact power of two file size
	// technically piece size is not allowed to be less than 16kB
	TEST_EQUAL(first_piece_node(0x1000, 0x7fffff), 2047);
	TEST_EQUAL(first_piece_node(0x1000, 0x800000), 2047);
	TEST_EQUAL(first_piece_node(0x1000, 0x800001), 4095);

	TEST_EQUAL(first_piece_node(0x1000, 0x7fff), 7);
	TEST_EQUAL(first_piece_node(0x1000, 0x8000), 7);
	TEST_EQUAL(first_piece_node(0x1000, 0x8001), 15);

	// edge case of file smaller than one block
	TEST_EQUAL(first_piece_node(0x1000, 0x1000), 0);

	// edge case of file smaller than one piece
	TEST_EQUAL(first_piece_node(0x4000, 0x1000), 0);
}

TORRENT_TEST(file_first_block_node)
{
	// the full merkle tree, all the way down to blocks, does not depend on the
	// piece size. Blocks are always 0x4000 bytes.

	// there must be an even power of two number of leaves, e.g.
	// file size 0x500000 / 0x4000 = 320 blocks -> 512 leaves
	TEST_EQUAL(first_block_node(0x500000), 511);

	// edge case of file smaller than one block
	TEST_EQUAL(first_block_node(0x1000), 0);

	// even power-of-two boundary condition
	TEST_EQUAL(first_block_node(0x7fffff), 511);
	TEST_EQUAL(first_block_node(0x800000), 511);
	TEST_EQUAL(first_block_node(0x800001), 1023);
}

TORRENT_TEST(mismatching_file_hash1)
{
	file_storage st;
	st.set_piece_length(0x4000);

	error_code ec;
	st.add_file(ec, combine_path("test", "a"), 10000);
	TEST_CHECK(!ec);
	st.add_file(ec, combine_path("test", "B"), 10000, {}, 0, {}, "abababababababababababababababab");
	TEST_CHECK(ec);
}

TORRENT_TEST(mismatching_file_hash2)
{
	file_storage st;
	st.set_piece_length(0x4000);

	error_code ec;
	st.add_file(ec, combine_path("test", "B"), 10000, {}, 0, {}, "abababababababababababababababab");
	TEST_CHECK(!ec);
	st.add_file(ec, combine_path("test", "a"), 10000);
	TEST_CHECK(ec);
}

TORRENT_TEST(v2_detection_1)
{
	file_storage fs;
	fs.set_piece_length(0x8000);
	// passing in a root hash (the last argument) makes it follow v2 rules, to
	// add pad files
	fs.add_file("test/0", 0x5000, {}, 0, "symlink-test-1");
	fs.add_file("test/1", 0x5000, {}, 0, "symlink-test-2");

	fs.add_file("test/2", 0x2000, {}, 0, {}, "01234567890123456789012345678901");
	// it's an error to add a v1 file to a v2 torrent
	TEST_THROW(fs.add_file("test/3", 0x2000));
}

TORRENT_TEST(v2_detection_2)
{
	file_storage fs;
	fs.set_piece_length(0x8000);
	// passing in a root hash (the last argument) makes it follow v2 rules, to
	// add pad files
	fs.add_file("test/0", 0x5000, {}, 0, "symlink-test-1");
	fs.add_file("test/1", 0x5000, {}, 0, "symlink-test-2");

	fs.add_file("test/2", 0x2000);

	// it's an error to add a v1 file to a v2 torrent
	TEST_THROW(fs.add_file("test/3", 0x2000, {}, 0, {}, "01234567890123456789012345678901"));
}

TORRENT_TEST(blocks_in_piece2)
{
	static std::map<int, int> const piece_sizes = {
		{0x3fff, 1},
		{0x4000, 1},
		{0x4001, 2},
	};

	for (auto t : piece_sizes)
	{
		file_storage fs;
		fs.set_piece_length(0x8000);
		fs.add_file("test/0", t.first, {}, 0, {}, "01234567890123456789012345678901");
		fs.set_num_pieces(aux::calc_num_pieces(fs));
		TEST_EQUAL(fs.blocks_in_piece2(piece_index_t{ 0 }), t.second);
	}
}

TORRENT_TEST(file_index_for_root)
{
	file_storage fs;
	fs.set_piece_length(0x8000);
	fs.add_file("test/0", 0x8000, {}, 0, {}, "11111111111111111111111111111111");
	fs.add_file("test/1", 0x8000, {}, 0, {}, "22222222222222222222222222222222");
	fs.add_file("test/2", 0x8000, {}, 0, {}, "33333333333333333333333333333333");
	fs.add_file("test/3", 0x8000, {}, 0, {}, "44444444444444444444444444444444");

	TEST_EQUAL(fs.file_index_for_root(sha256_hash("11111111111111111111111111111111")), file_index_t{0});
	TEST_EQUAL(fs.file_index_for_root(sha256_hash("22222222222222222222222222222222")), file_index_t{1});
	TEST_EQUAL(fs.file_index_for_root(sha256_hash("33333333333333333333333333333333")), file_index_t{2});
	TEST_EQUAL(fs.file_index_for_root(sha256_hash("44444444444444444444444444444444")), file_index_t{3});
	TEST_EQUAL(fs.file_index_for_root(sha256_hash("55555555555555555555555555555555")), file_index_t{-1});
}

// TODO: test file attributes
// TODO: test symlinks
