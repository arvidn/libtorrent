/*

Copyright (c) 2013, 2015-2017, 2019-2022, Arvid Norberg
Copyright (c) 2017, 2019, Steven Siloti
Copyright (c) 2018, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <iostream>
#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/crc.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"
#include "test.hpp"
#include "setup_transfer.hpp"
#include "test_utils.hpp"

#include "libtorrent/file_storage.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/aux_/string_util.hpp"

using namespace lt;

namespace {

// builds one buffer holding the concatenated bytes of each 32-byte SHA-256
// hash literal in ``hashes``, and returns it together with each hash's
// offset within the buffer. Used by tests exercising file_storage's v2
// root-hash offsets, which must all resolve against a single shared buffer.
std::pair<std::vector<char>, std::vector<std::int32_t>> build_root_hashes(
	std::initializer_list<char const*> hashes)
{
	std::vector<char> buf;
	std::vector<std::int32_t> offsets;
	for (char const* h : hashes)
	{
		offsets.push_back(std::int32_t(buf.size()));
		buf.insert(buf.end(), h, h + sha256_hash::size());
	}
	return {std::move(buf), std::move(offsets)};
}

// a single reusable root hash, for tests that only care that some v2 root
// hash is present (to trigger v2 file layout rules), not its actual value
char const dummy_root_hash[] = "01234567890123456789012345678901";

file_storage make_v2_storage() { return file_storage(dummy_root_hash); }

void setup_test_storage(file_storage& st)
{
	st.add_file_borrow({}, combine_path("test", "a"), 10000);
	st.add_file_borrow({}, combine_path("test", "b"), 20000);
	st.add_file_borrow({}, combine_path("test", combine_path("c", "a")), 30000);
	st.add_file_borrow({}, combine_path("test", combine_path("c", "b")), 40000);

	st.set_piece_length(0x4000);
	st.set_num_pieces(aux::calc_num_pieces(st));

	TEST_EQUAL(std::string(st.file_name(file_index_t{0})), "a");
	TEST_EQUAL(std::string(st.file_name(file_index_t{1})), "b");
	TEST_EQUAL(std::string(st.file_name(file_index_t{2})), "a");
	TEST_EQUAL(std::string(st.file_name(file_index_t{3})), "b");
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
	TEST_EQUAL(st.size_on_disk(), 100000);
	TEST_EQUAL(st.piece_length(), 0x4000);
	std::printf("%d\n", st.num_pieces());
	TEST_EQUAL(st.num_pieces(), (100000 + 0x3fff) / 0x4000);
}

} // anonymous namespace

#if TORRENT_ABI_VERSION < 4
TORRENT_TEST(coalesce_path)
{
	file_storage st;
	st.set_piece_length(0x4000);

	// the number of directory entries referenced by the torrent so far (not
	// counting the torrent's own root). add_file() resolves each path
	// independently and does not deduplicate, so files sharing a directory
	// each get their own path_element for it.
	auto count_dirs = [&] {
		auto const eh = st.compute_element_hashes();
		std::size_t count = 0;
		for (auto const idx : eh.is_dir.range())
			if (eh.is_dir[idx])
				++count;
		return count;
	};

	st.add_file_borrow({}, combine_path("test", "a"), 10000);
	TEST_EQUAL(count_dirs(), 0u); // no sub-directories yet
	st.add_file_borrow({}, combine_path("test", "b"), 20000);
	TEST_EQUAL(count_dirs(), 0u);
	st.add_file_borrow({}, combine_path("test", combine_path("c", "a")), 30000);
	TEST_EQUAL(count_dirs(), 1u); // "test/c"

	// a second file under the same directory gets its own "test/c"
	// path_element; add_file() does not deduplicate
	st.add_file_borrow({}, combine_path("test", combine_path("c", "b")), 40000);
	TEST_EQUAL(count_dirs(), 2u);

	// cause pad files to be created; pad files reference the pad-directory
	// sentinel directly, so they don't add another path_element, and
	// compute_element_hashes() deliberately doesn't track pad-only
	// directories at all (a naming collision involving a pad file is
	// never a real conflict, see resolve_duplicate_filenames())
	st.canonicalize();

	TEST_EQUAL(count_dirs(), 2u); // "test/c" (x2)
}

TORRENT_TEST(rename_file)
{
	// test rename_file_impl
	file_storage st;
	setup_test_storage(st);

	error_code ec;
	st.rename_file_impl(file_index_t{0}, combine_path("test", combine_path("c", "d")), ec);
	TEST_CHECK(!ec);
	TEST_EQUAL(st.file_path(file_index_t{0}, "."), combine_path(".", combine_path("test"
		, combine_path("c", "d"))));
	TEST_EQUAL(st.file_path(file_index_t{0}, ""), combine_path("test"
		, combine_path("c", "d")));

	st.rename_file_impl(file_index_t{0}, combine_path("test", "renamed"), ec);
	TEST_CHECK(!ec);
	TEST_EQUAL(
		st.file_path(file_index_t{0}, "."), combine_path(".", combine_path("test", "renamed")));
}

TORRENT_TEST(rename_file_absolute)
{
	// an absolute new_filename detaches the file from save_path, see
	// torrent_info::rename_file()
	file_storage st;
	setup_test_storage(st);

	std::string const abs = combine_path(complete("."), combine_path("some", "where"));
	error_code ec;
	st.rename_file_impl(file_index_t{0}, abs, ec);
	TEST_CHECK(!ec);
	TEST_CHECK(st.file_absolute_path(file_index_t{0}));
	TEST_EQUAL(st.file_path(file_index_t{0}, "save_path"), abs);
}

TORRENT_TEST(rename_pad_file)
{
	// pad files cannot be renamed, and have no stored name of their own;
	// file_path() synthesizes one from their size, file_name() does not
	file_storage st;
	st.set_piece_length(0x4000);
	st.add_file_borrow({}, combine_path("test", "a"), 10000);
	st.add_file_borrow(
		{}, combine_path("test", combine_path(".pad", "6384")), 6384, file_storage::flag_pad_file);

	// pad files live under name()/.pad
	std::string const pad_path = combine_path("test", combine_path(".pad", "6384"));
	TEST_EQUAL(st.file_path(file_index_t{1}, ""), pad_path);

	error_code ec;
	st.rename_file_impl(file_index_t{1}, combine_path("test", "renamed"), ec);
	TEST_CHECK(!ec);

	// the rename had no effect
	TEST_EQUAL(st.file_path(file_index_t{1}, ""), pad_path);
	TEST_EQUAL(std::string(st.file_name(file_index_t{1})), "");
}
#endif

#if TORRENT_ABI_VERSION < 4
TORRENT_TEST(rename_file2)
{
	// test rename_file_impl, starting from a single bare (root-less) file
	file_storage st;
	st.add_file_borrow({}, "a", 10000);
	TEST_EQUAL(st.file_path(file_index_t{0}, ""), "a");

	error_code ec;
	st.rename_file_impl(file_index_t{0}, combine_path("test", combine_path("c", "d")), ec);
	TEST_CHECK(!ec);
	TEST_EQUAL(st.file_path(file_index_t{0}, "."), combine_path(".", combine_path("test", combine_path("c", "d"))));
	TEST_EQUAL(st.file_path(file_index_t{0}, ""), combine_path("test", combine_path("c", "d")));

	st.rename_file_impl(file_index_t{0}, combine_path("tmp", "a"), ec);
	TEST_CHECK(!ec);
	TEST_EQUAL(st.file_path(file_index_t{0}, "."), combine_path("tmp", "a"));
}
#endif

TORRENT_TEST(file_hash_matches_file_path)
{
	// resolve_duplicate_filenames()'s rename-candidate loop hashes a
	// full file_path() string directly (lower-cased, byte for byte) and
	// looks that hash up alongside file_hash()'s, so the two must agree
	// on every file, or a colliding rename candidate goes undetected
	file_storage st;
	setup_test_storage(st);

	file_storage::element_hashes const eh = st.compute_element_hashes();
	for (file_index_t const i : st.file_range())
	{
		boost::crc_optimal<32, 0x1EDC6F41, 0xFFFFFFFF, 0xFFFFFFFF, true, true> crc;
		for (char const c : st.file_path(i))
			crc.process_byte(aux::to_lower(c) & 0xff);

		TEST_EQUAL(crc.checksum(), st.file_hash(eh, i));
	}
}

TORRENT_TEST(set_name)
{
	// test set_name. Make sure the name of the torrent is not encoded
	// in the paths of each individual file. When changing the name of the
	// torrent, the path of the files should change too
	file_storage st;
	setup_test_storage(st);

	st.set_name("test_2");
	TEST_EQUAL(st.file_path(file_index_t{0}, "."), combine_path(".", combine_path("test_2", "a")));
}

TORRENT_TEST(pointer_offset)
{
	// test applying pointer offset
	char const filename[] = "test1fooba";
	char const roothash[] = "01234567890123456789012345678912-----";

	file_storage st{roothash};
	st.set_piece_length(16 * 1024);
	st.set_name("test-torrent-1");

	error_code ec;
	// roothash is the file_storage(char const*) buffer itself, so its offset
	// within that buffer is 0
	st.add_file(ec, {filename, 5}, true, aux::path_element::torrent_root, 10, file_flags_t{}, 0, 0);
	TEST_CHECK(!ec);

	// test filename_ptr and filename_len
#if TORRENT_ABI_VERSION <= 2
	TEST_EQUAL(st.file_name_ptr(file_index_t{0}), filename);
	TEST_EQUAL(st.file_name_len(file_index_t{0}), 5);
#endif
	TEST_EQUAL(std::string(st.file_name(file_index_t{0})), string_view(filename, 5));
#if TORRENT_ABI_VERSION < 4
	TEST_EQUAL(st.hash(file_index_t{0}), sha1_hash());
#endif
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

	TEST_EQUAL(std::string(st.file_name(file_index_t{0})), "(");
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

	TEST_EQUAL(std::string(st.file_name(file_index_t{0})), "(");
	TEST_EQUAL(st.file_path(file_index_t{0}, ""), combine_path("+", combine_path("+", "(")));
}

TORRENT_TEST(map_file)
{
	// test map_file
	file_storage fs;
	fs.set_piece_length(512);
	fs.add_file_borrow({}, combine_path("temp_storage", "test1.tmp"), 17);
	fs.add_file_borrow({}, combine_path("temp_storage", "test2.tmp"), 612);
	fs.add_file_borrow({}, combine_path("temp_storage", "test3.tmp"), 0);
	fs.add_file_borrow({}, combine_path("temp_storage", "test4.tmp"), 0);
	fs.add_file_borrow({}, combine_path("temp_storage", "test5.tmp"), 3253);
	// size: 3882
	fs.add_file_borrow({}, combine_path("temp_storage", "test6.tmp"), 841);
	// size: 4723

	peer_request rq = fs.map_file(file_index_t{0}, 0, 10);
	TEST_EQUAL(rq.piece, 0_piece);
	TEST_EQUAL(rq.start, 0);
	TEST_EQUAL(rq.length, 10);
	rq = fs.map_file(file_index_t{5}, 0, 10);
	TEST_EQUAL(rq.piece, 7_piece);
	TEST_EQUAL(rq.start, 298);
	TEST_EQUAL(rq.length, 10);
	rq = fs.map_file(file_index_t{5}, 0, 1000);
	TEST_EQUAL(rq.piece, 7_piece);
	TEST_EQUAL(rq.start, 298);
	TEST_EQUAL(rq.length, 841);
}

#if TORRENT_ABI_VERSION < 4
// make sure every file is tail padded
TORRENT_TEST(canonicalize_pad)
{
	file_storage fs;
	fs.set_piece_length(0x4000);
	fs.add_file_borrow({}, combine_path("s", "2"), 0x7000);
	fs.add_file_borrow({}, combine_path("s", "1"), 1);
	fs.add_file_borrow({}, combine_path("s", "3"), 0x7001);
	TEST_EQUAL(fs.size_on_disk(), 0x7000 + 1 + 0x7001);

	fs.canonicalize();

	TEST_EQUAL(fs.num_files(), 6);

	TEST_EQUAL(fs.file_size(0_file), 1);
	TEST_EQUAL(std::string(fs.file_name(0_file)), "1");
	TEST_EQUAL(fs.pad_file_at(0_file), false);

	TEST_EQUAL(fs.file_size(1_file), 0x4000 - 1);
	TEST_EQUAL(fs.pad_file_at(1_file), true);

	TEST_EQUAL(fs.file_size(2_file), 0x7000);
	TEST_EQUAL(std::string(fs.file_name(2_file)), "2");
	TEST_EQUAL(fs.pad_file_at(2_file), false);

	TEST_EQUAL(fs.file_size(3_file), 0x8000 - 0x7000);
	TEST_EQUAL(fs.pad_file_at(3_file), true);

	TEST_EQUAL(fs.file_size(4_file), 0x7001);
	TEST_EQUAL(std::string(fs.file_name(4_file)), "3");
	TEST_EQUAL(fs.pad_file_at(4_file), false);

	TEST_EQUAL(fs.file_size(5_file), 0x8000 - 0x7001);
	TEST_EQUAL(fs.pad_file_at(5_file), true);

	TEST_EQUAL(fs.size_on_disk(), 0x7000 + 1 + 0x7001);
}

// make sure canonicalize sorts by path correctly
TORRENT_TEST(canonicalize_path)
{
	file_storage fs;
	fs.set_piece_length(0x4000);
	fs.add_file_borrow({}, combine_path("b", combine_path("2", "a")), 0x4000);
	fs.add_file_borrow({}, combine_path("b", combine_path("1", "a")), 0x4000);
	fs.add_file_borrow({}, combine_path("b", combine_path("3", "a")), 0x4000);
	fs.add_file_borrow({}, combine_path("b", "11"), 0x4000);

	fs.canonicalize();

	TEST_EQUAL(fs.num_files(), 4);

	TEST_EQUAL(fs.file_path(0_file), combine_path("b", combine_path("1", "a")));
	TEST_EQUAL(fs.file_path(1_file), combine_path("b", "11"));
	TEST_EQUAL(fs.file_path(2_file), combine_path("b", combine_path("2", "a")));
	TEST_EQUAL(fs.file_path(3_file), combine_path("b", combine_path("3", "a")));
}
#endif

TORRENT_TEST(piece_range_exclusive)
{
	int const piece_size = 16;
	file_storage fs;
	fs.set_piece_length(piece_size);
	fs.add_file_borrow({}, combine_path("temp_storage", "0"), piece_size);
	fs.add_file_borrow({}, combine_path("temp_storage", "1"), piece_size * 4 + 1);
	fs.add_file_borrow({}, combine_path("temp_storage", "2"), piece_size * 4 - 1);
	fs.set_num_pieces(aux::calc_num_pieces(fs));
	//        +---+---+---+---+---+---+---+---+---+
	// pieces | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
	//        +---+---+---+---+---+---+---+---+---+
	// files  | 0 |        1       |        2     |
	//        +---+----------------+--------------+

	TEST_CHECK((aux::file_piece_range_exclusive(fs, 0_file)
		== lt::index_range<piece_index_t>{0_piece, 1_piece}));
	TEST_CHECK((aux::file_piece_range_exclusive(fs, 1_file)
		== lt::index_range<piece_index_t>{1_piece, 5_piece}));
	TEST_CHECK((aux::file_piece_range_exclusive(fs, 2_file)
		== lt::index_range<piece_index_t>{6_piece, 9_piece}));
}

TORRENT_TEST(piece_range_inclusive)
{
	int const piece_size = 16;
	file_storage fs;
	fs.set_piece_length(piece_size);
	fs.add_file_borrow({}, combine_path("temp_storage", "0"), piece_size);
	fs.add_file_borrow({}, combine_path("temp_storage", "1"), piece_size * 4 + 1);
	fs.add_file_borrow({}, combine_path("temp_storage", "2"), piece_size * 4 - 1);
	fs.set_num_pieces(aux::calc_num_pieces(fs));
	//        +---+---+---+---+---+---+---+---+---+
	// pieces | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
	//        +---+---+---+---+---+---+---+---+---+
	// files  | 0 |        1       |        2     |
	//        +---+----------------+--------------+

	TEST_CHECK((aux::file_piece_range_inclusive(fs, 0_file)
		== lt::index_range<piece_index_t>{0_piece, 1_piece}));
	TEST_CHECK((aux::file_piece_range_inclusive(fs, 1_file)
		== lt::index_range<piece_index_t>{1_piece, 6_piece}));
	TEST_CHECK((aux::file_piece_range_inclusive(fs, 2_file)
		== lt::index_range<piece_index_t>{5_piece, 9_piece}));
}

TORRENT_TEST(piece_range)
{
	int const piece_size = 0x4000;
	file_storage fs;
	fs.set_piece_length(piece_size);
	fs.add_file_borrow({}, combine_path("temp_storage", "0"), piece_size * 3);
	fs.add_file_borrow({}, combine_path("temp_storage", "1"), piece_size * 3 + 0x30);
	fs.set_num_pieces(aux::calc_num_pieces(fs));
	//        +---+---+---+---+---+---+---+
	// pieces | 0 | 1 | 2 | 3 | 4 | 5 | 6 |
	//        +---+---+---+---+---+---+---+
	// files  |      0    |      1     |
	//        +---+-------+------------+

	TEST_CHECK((aux::file_piece_range_inclusive(fs, 0_file)
		== lt::index_range<piece_index_t>{0_piece, 3_piece}));
	TEST_CHECK((aux::file_piece_range_inclusive(fs, 1_file)
		== lt::index_range<piece_index_t>{3_piece, 7_piece}));

	TEST_CHECK((aux::file_piece_range_exclusive(fs, 0_file)
		== lt::index_range<piece_index_t>{0_piece, 3_piece}));
	TEST_CHECK((aux::file_piece_range_exclusive(fs, 1_file)
		== lt::index_range<piece_index_t>{3_piece, 7_piece}));
}

TORRENT_TEST(piece_size_last_piece)
{
	file_storage fs;
	fs.set_piece_length(1024);
	fs.add_file_borrow({}, "0", 100);
	fs.set_num_pieces(aux::calc_num_pieces(fs));
	TEST_EQUAL(fs.piece_size(0_piece), 100);
}

TORRENT_TEST(piece_size_middle_piece)
{
	file_storage fs;
	fs.set_piece_length(1024);
	fs.add_file_borrow({}, "0", 2000);
	fs.set_num_pieces(aux::calc_num_pieces(fs));
	TEST_EQUAL(fs.piece_size(0_piece), 1024);
	TEST_EQUAL(fs.piece_size(1_piece), 2000 - 1024);
}

TORRENT_TEST(file_index_at_offset)
{
	file_storage fs;
	fs.set_piece_length(1024);
	fs.add_file_borrow({}, "test/0", 1);
	fs.add_file_borrow({}, "test/1", 2);
	fs.add_file_borrow({}, "test/2", 3);
	fs.add_file_borrow({}, "test/3", 4);
	fs.add_file_borrow({}, "test/4", 5);
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
	fs.add_file_borrow({}, "test/0", 1);
	fs.add_file_borrow({}, "test/1", 2);
	fs.add_file_borrow({}, "test/2", 3);
	fs.add_file_borrow({}, "test/3", 4);
	fs.add_file_borrow({}, "test/4", 5);
	fs.set_num_pieces(aux::calc_num_pieces(fs));
	int len = 0;
	for (int f : {0, 1, 2, 2, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 5})
	{
		std::vector<file_slice> const map = fs.map_block(0_piece, 0, len);
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
	fs.add_file_borrow({}, "test/0", 1);
	fs.add_file_borrow({}, "test/1", 2);
	fs.add_file_borrow({}, "test/2", 3);
	fs.add_file_borrow({}, "test/3", 4);
	fs.add_file_borrow({}, "test/4", 5);
	fs.set_num_pieces(aux::calc_num_pieces(fs));
	int offset = 0;
	for (int f : {0, 1, 1, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4})
	{
		std::vector<file_slice> const map = fs.map_block(0_piece, offset, 1);
		TEST_EQUAL(int(map.size()), 1);
		auto const& file = map[0];
		TEST_EQUAL(file.file_index, file_index_t{f});
		TEST_CHECK(file.offset <= offset);
		TEST_EQUAL(file.size, 1);
		++offset;
	}
}

TORRENT_TEST(query_symlinks)
{
	file_storage fs;
	fs.set_piece_length(1024);
	fs.add_file_borrow({}, "test/0", 0, file_storage::flag_symlink, 0, "0");
	fs.add_file_borrow({}, "test/1", 0, file_storage::flag_symlink, 0, "1");
	fs.add_file_borrow({}, "test/2", 0, file_storage::flag_symlink, 0, "2");
	fs.add_file_borrow({}, "test/3", 0, file_storage::flag_symlink, 0, "3");

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
	fs.add_file_borrow({}, "test/0", 10);
	fs.add_file_borrow({}, "test/1", 10);
	fs.add_file_borrow({}, "test/2", 10);
	fs.add_file_borrow({}, "test/3", 10);

	TEST_CHECK(fs.symlink(file_index_t{0}).empty());
	TEST_CHECK(fs.symlink(file_index_t{1}).empty());
	TEST_CHECK(fs.symlink(file_index_t{2}).empty());
	TEST_CHECK(fs.symlink(file_index_t{3}).empty());
}

TORRENT_TEST(files_compatible)
{
	file_storage fs1;
	fs1.set_piece_length(0x4000);
	fs1.add_file_borrow({}, "test/0", 1);
	fs1.add_file_borrow({}, "test/1", 2);

	file_storage fs2;
	fs2.set_piece_length(0x4000);
	fs2.add_file_borrow({}, "test/0", 1);
	fs2.add_file_borrow({}, "test/1", 2);

	TEST_CHECK(lt::aux::files_compatible(fs1, fs2));
}

TORRENT_TEST(files_compatible_num_files)
{
	file_storage fs1;
	fs1.set_piece_length(0x4000);
	fs1.add_file_borrow({}, "test/0", 1);
	fs1.add_file_borrow({}, "test/1", 2);

	file_storage fs2;
	fs2.set_piece_length(0x4000);
	fs2.add_file_borrow({}, "test/0", 3);

	TEST_CHECK(!lt::aux::files_compatible(fs1, fs2));
}

TORRENT_TEST(files_compatible_size)
{
	file_storage fs1;
	fs1.set_piece_length(0x4000);
	fs1.add_file_borrow({}, "test/0", 2);
	fs1.add_file_borrow({}, "test/1", 1);

	file_storage fs2;
	fs2.set_piece_length(0x4000);
	fs2.add_file_borrow({}, "test/0", 1);
	fs2.add_file_borrow({}, "test/1", 2);

	TEST_CHECK(!lt::aux::files_compatible(fs1, fs2));
}

TORRENT_TEST(files_compatible_name)
{
	file_storage fs1;
	fs1.set_piece_length(0x4000);
	fs1.add_file_borrow({}, "test/1", 1);
	fs1.add_file_borrow({}, "test/0", 2);

	file_storage fs2;
	fs2.set_piece_length(0x4000);
	fs2.add_file_borrow({}, "test/0", 1);
	fs2.add_file_borrow({}, "test/1", 2);

	TEST_CHECK(!lt::aux::files_compatible(fs1, fs2));
}

TORRENT_TEST(files_compatible_hidden)
{
	file_storage fs1;
	fs1.set_piece_length(0x4000);
	fs1.add_file_borrow({}, "test/0", 1);
	fs1.add_file_borrow({}, "test/1", 2, file_storage::flag_hidden);

	file_storage fs2;
	fs2.set_piece_length(0x4000);
	fs2.add_file_borrow({}, "test/0", 1);
	fs2.add_file_borrow({}, "test/1", 2);

	// hidden attribute does not affect compatibility
	TEST_CHECK(lt::aux::files_compatible(fs1, fs2));
}

TORRENT_TEST(files_compatible_pad)
{
	file_storage fs1;
	fs1.set_piece_length(0x4000);
	fs1.add_file_borrow({}, "test/0", 1);
	fs1.add_file_borrow({}, "test/1", 2, file_storage::flag_pad_file);

	file_storage fs2;
	fs2.set_piece_length(0x4000);
	fs2.add_file_borrow({}, "test/0", 1);
	fs2.add_file_borrow({}, "test/1", 2);

	// pad attribute does affect compatibility
	TEST_CHECK(!lt::aux::files_compatible(fs1, fs2));
}

TORRENT_TEST(files_compatible_empty_file_order)
{
	file_storage fs1;
	fs1.set_piece_length(0x4000);
	fs1.add_file_borrow({}, "test/0", 1);
	fs1.add_file_borrow({}, "test/1", 0);
	fs1.add_file_borrow({}, "test/2", 0);
	fs1.add_file_borrow({}, "test/3", 0);

	file_storage fs2;
	fs2.set_piece_length(0x4000);
	fs2.add_file_borrow({}, "test/0", 1);
	fs2.add_file_borrow({}, "test/3", 0);
	fs2.add_file_borrow({}, "test/2", 0);
	fs2.add_file_borrow({}, "test/1", 0);

	// order of empty files does not affect compatibility
	TEST_CHECK(lt::aux::files_compatible(fs1, fs2));
}

TORRENT_TEST(files_compatible_mtime)
{
	file_storage fs1;
	fs1.set_piece_length(0x4000);
	fs1.add_file_borrow({}, "test/0", 1, {}, 1234);
	fs1.add_file_borrow({}, "test/1", 2, {}, 1235);

	file_storage fs2;
	fs2.set_piece_length(0x4000);
	fs2.add_file_borrow({}, "test/0", 1, {}, 1234);
	fs2.add_file_borrow({}, "test/1", 2, {}, 1234);

	// mtime does not affect compatibility
	TEST_CHECK(lt::aux::files_compatible(fs1, fs2));
}

TORRENT_TEST(files_compatible_piece_size)
{
	file_storage fs1;
	fs1.set_piece_length(0x8000);
	fs1.add_file_borrow({}, "test/0", 1);
	fs1.add_file_borrow({}, "test/1", 2);

	file_storage fs2;
	fs2.set_piece_length(0x4000);
	fs2.add_file_borrow({}, "test/0", 1);
	fs2.add_file_borrow({}, "test/1", 2);

	TEST_CHECK(!lt::aux::files_compatible(fs1, fs2));
}

TORRENT_TEST(files_compatible_different_symlink)
{
	file_storage fs1;
	fs1.set_piece_length(0x4000);
	fs1.add_file_borrow({}, "test/0", 1);
	fs1.add_file_borrow({}, "test/1", 2, file_storage::flag_symlink, 0, "test/0");

	file_storage fs2;
	fs2.set_piece_length(0x4000);
	fs2.add_file_borrow({}, "test/0", 1);
	fs2.add_file_borrow({}, "test/1", 2, file_storage::flag_symlink, 0, "test/1");

	TEST_CHECK(!lt::aux::files_compatible(fs1, fs2));
}

TORRENT_TEST(files_compatible_same_symlink)
{
	file_storage fs1;
	fs1.set_piece_length(0x4000);
	fs1.add_file_borrow({}, "test/0", 1);
	fs1.add_file_borrow({}, "test/1", 2, file_storage::flag_symlink, 0, "test/0");

	file_storage fs2;
	fs2.set_piece_length(0x4000);
	fs2.add_file_borrow({}, "test/0", 1);
	fs2.add_file_borrow({}, "test/1", 2, file_storage::flag_symlink, 0, "test/0");

	TEST_CHECK(lt::aux::files_compatible(fs1, fs2));
}

TORRENT_TEST(remove_tail_padding_not_last)
{
	file_storage fs1;
	fs1.set_piece_length(0x4000);
	fs1.add_file_borrow({}, "test/0", 1);
	fs1.add_file_borrow({}, "test/1", 2, file_storage::flag_pad_file);
	fs1.add_file_borrow({}, "test/2", 0);
	fs1.add_file_borrow({}, "test/3", 0);

	fs1.remove_tail_padding();

	file_storage fs2;
	fs2.set_piece_length(0x4000);
	fs2.add_file_borrow({}, "test/0", 1);
	fs2.add_file_borrow({}, "test/2", 0);
	fs2.add_file_borrow({}, "test/3", 0);

	TEST_CHECK(lt::aux::files_compatible(fs1, fs2));
}

TORRENT_TEST(remove_tail_padding_last)
{
	file_storage fs1;
	fs1.set_piece_length(0x4000);
	fs1.add_file_borrow({}, "test/0", 1);
	fs1.add_file_borrow({}, "test/1", 2, file_storage::flag_pad_file);

	fs1.remove_tail_padding();

	file_storage fs2;
	fs2.set_piece_length(0x4000);
	fs2.add_file_borrow({}, "test/0", 1);

	TEST_CHECK(lt::aux::files_compatible(fs1, fs2));
}

TORRENT_TEST(remove_tail_padding_no_op)
{
	file_storage fs1;
	fs1.set_piece_length(0x4000);
	fs1.add_file_borrow({}, "test/0", 1);
	fs1.add_file_borrow({}, "test/1", 0);
	fs1.add_file_borrow({}, "test/2", 0);
	fs1.add_file_borrow({}, "test/3", 0);

	fs1.remove_tail_padding();

	file_storage fs2;
	fs2.set_piece_length(0x4000);
	fs2.add_file_borrow({}, "test/0", 1);
	fs2.add_file_borrow({}, "test/1", 0);
	fs2.add_file_borrow({}, "test/2", 0);
	fs2.add_file_borrow({}, "test/3", 0);

	TEST_CHECK(lt::aux::files_compatible(fs1, fs2));
}


std::int64_t const int_max = std::numeric_limits<int>::max();

TORRENT_TEST(large_files)
{
	file_storage fs1;
	fs1.set_piece_length(0x4000);
	TEST_THROW(fs1.add_file_borrow({}, "test/0", int_max / 2 * lt::default_block_size + 1));

	error_code ec;
	fs1.add_file_borrow(ec, {}, "test/0", int_max * lt::default_block_size + 1);
	TEST_EQUAL(ec, make_error_code(boost::system::errc::file_too_large));

	// should not throw
	TEST_NOTHROW(fs1.add_file_borrow({}, "test/0", int_max / 2 * lt::default_block_size));
}

TORRENT_TEST(large_offset)
{
	file_storage fs1;
	fs1.set_piece_length(0x4000);
	for (int i = 0; i < 16; ++i)
		fs1.add_file_borrow(
			{}, ("test/" + std::to_string(i)).c_str(), int_max / 2 * lt::default_block_size);

	// this exceeds the 2^48-1 limit
	TEST_THROW(fs1.add_file_borrow({}, "test/16", 262144));

	error_code ec;
	fs1.add_file_borrow(ec, {}, "test/8", 262144);
	TEST_EQUAL(ec, make_error_code(errors::torrent_invalid_length));

	// this should be OK, but just
	fs1.add_file_borrow({}, "test/8", 262143);
}

TORRENT_TEST(large_filename)
{
	file_storage fs1;
	fs1.set_piece_length(0x4000);
	// yes, this creates an invalid string_view, as it claims to be larger than
	// the allocation. This should be OK though as the test for size never
	// actually looks at the string
	error_code ec;
	fs1.add_file(ec, string_view("0", 1 << 12), true, aux::path_element::torrent_root, 10);
	TEST_EQUAL(ec, make_error_code(boost::system::errc::filename_too_long));
}

TORRENT_TEST(piece_size2)
{
	file_storage fs = make_v2_storage();
	fs.set_piece_length(0x8000);
	// passing in a root hash (the last argument) makes it follow v2 rules, to
	// add pad files
	fs.add_file_borrow({}, "test/0", 0x5000, {}, 0, {}, 0);

	fs.set_num_pieces(aux::calc_num_pieces(fs));
	TEST_EQUAL(fs.num_pieces(), 1);
	TEST_EQUAL(fs.piece_size2(0_piece), 0x5000);

	fs.add_file_borrow({}, "test/1", 0x2000, {}, 0, {}, 0);
	fs.add_file_borrow({}, "test/2", 0x8000, {}, 0, {}, 0);

	fs.set_num_pieces(aux::calc_num_pieces(fs));
	TEST_EQUAL(fs.num_pieces(), 3);
	TEST_EQUAL(fs.piece_size2(2_piece), 0x8000);

	fs.add_file_borrow({}, "test/3", 8, {}, 0, {}, 0);

	fs.set_num_pieces(aux::calc_num_pieces(fs));
	TEST_EQUAL(fs.num_pieces(), 4);
	TEST_EQUAL(fs.piece_size2(0_piece), 0x5000);
	TEST_EQUAL(fs.piece_size2(1_piece), 0x2000);
	TEST_EQUAL(fs.piece_size2(2_piece), 0x8000);
	TEST_EQUAL(fs.piece_size2(3_piece), 8);

	fs.add_file_borrow({}, "test/4", 0x8001, {}, 0, {}, 0);

	fs.set_num_pieces(aux::calc_num_pieces(fs));
	TEST_EQUAL(fs.num_pieces(), 6);

	TEST_EQUAL(fs.piece_size2(0_piece), 0x5000);
	TEST_EQUAL(fs.piece_size2(1_piece), 0x2000);
	TEST_EQUAL(fs.piece_size2(2_piece), 0x8000);
	TEST_EQUAL(fs.piece_size2(3_piece), 8);
	TEST_EQUAL(fs.piece_size2(4_piece), 0x8000);
	TEST_EQUAL(fs.piece_size2(5_piece), 1);
}

#if TORRENT_ABI_VERSION < 4
TORRENT_TEST(file_num_blocks)
{
	file_storage fs = make_v2_storage();
	fs.set_piece_length(0x8000);
	fs.add_file_borrow({}, "test/0", 0x5000, {}, 0, {}, 0);
	fs.add_file_borrow({}, "test/1", 0x2000, {}, 0, {}, 0);
	fs.add_file_borrow({}, "test/2", 0x8000, {}, 0, {}, 0);
	fs.add_file_borrow({}, "test/3", 0x8001, {}, 0, {}, 0);
	fs.add_file_borrow({}, "test/4", 1, {}, 0, {}, 0);
	fs.add_file_borrow({}, "test/5", 0, {}, 0, {}, 0);

	fs.canonicalize();

	// generally the number of blocks in a file is:
	// (file_size + lt::default_block_size - 1) / lt::default_block_size

	TEST_EQUAL(fs.file_num_blocks(file_index_t{0}), 2);
	// pad file at index 1
	TEST_CHECK(fs.pad_file_at(1_file));
	TEST_EQUAL(fs.file_num_blocks(file_index_t{2}), 1);
	// pad file at index 3
	TEST_CHECK(fs.pad_file_at(3_file));
	TEST_EQUAL(fs.file_num_blocks(file_index_t{4}), 2);
	TEST_EQUAL(fs.file_num_blocks(file_index_t{5}), 3);
	// pad file at index 6
	TEST_CHECK(fs.pad_file_at(6_file));
	TEST_EQUAL(fs.file_num_blocks(file_index_t{7}), 1);
	// pad file at index 8
	TEST_CHECK(fs.pad_file_at(8_file));
	TEST_EQUAL(fs.file_num_blocks(file_index_t{9}), 0);
}

TORRENT_TEST(file_num_pieces)
{
	file_storage fs = make_v2_storage();
	fs.set_piece_length(0x8000);
	fs.add_file_borrow({}, "test/0", 0x5000, {}, 0, {}, 0);
	fs.add_file_borrow({}, "test/1", 0x2000, {}, 0, {}, 0);
	fs.add_file_borrow({}, "test/2", 0x8000, {}, 0, {}, 0);
	fs.add_file_borrow({}, "test/3", 0x8001, {}, 0, {}, 0);
	fs.add_file_borrow({}, "test/4", 1, {}, 0, {}, 0);
	fs.add_file_borrow({}, "test/5", 0, {}, 0, {}, 0);

	fs.canonicalize();

	// generally the number of blocks in a file is:
	// (file_size + lt::default_block_size - 1) / lt::default_block_size

	TEST_EQUAL(fs.file_num_pieces(file_index_t{0}), 1);
	// pad file at index 1
	TEST_CHECK(fs.pad_file_at(1_file));
	TEST_EQUAL(fs.file_num_pieces(file_index_t{2}), 1);
	// pad file at index 3
	TEST_CHECK(fs.pad_file_at(3_file));
	TEST_EQUAL(fs.file_num_pieces(file_index_t{4}), 1);
	TEST_EQUAL(fs.file_num_pieces(file_index_t{5}), 2);
	// pad file at index 6
	TEST_CHECK(fs.pad_file_at(6_file));
	TEST_EQUAL(fs.file_num_pieces(file_index_t{7}), 1);
	// pad file at index 8
	TEST_CHECK(fs.pad_file_at(8_file));
	TEST_EQUAL(fs.file_num_pieces(file_index_t{9}), 0);
}
#endif

namespace {
int first_piece_node(int piece_size, int file_size)
{
	file_storage fs = make_v2_storage();
	fs.set_piece_length(piece_size);
	fs.add_file_borrow({}, "test/0", file_size, {}, 0, {}, 0);
	fs.set_num_pieces(aux::calc_num_pieces(fs));
	return fs.file_first_piece_node(file_index_t{0});
}

int first_block_node(int file_size)
{
	file_storage fs = make_v2_storage();
	fs.set_piece_length(0x10000);
	fs.add_file_borrow({}, "test/0", file_size, {}, 0, {}, 0);
	fs.set_num_pieces(aux::calc_num_pieces(fs));
	return fs.file_first_block_node(file_index_t{0});
}
}

TORRENT_TEST(file_first_piece_node)
{
	// the size of the merkle tree is implied by the size of the file.
	// 0x500000 / 0x10000 = 80 pieces
	// a merkle tree must have a power of 2 number of leaves, so that's 128,
	// that's 7 layers
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
	file_storage st = make_v2_storage();
	st.set_piece_length(0x4000);

	error_code ec;
	st.add_file_borrow(ec, {}, combine_path("test", "a"), 10000);
	TEST_CHECK(!ec);
	st.add_file_borrow(ec, {}, combine_path("test", "B"), 10000, {}, 0, {}, 0);
	TEST_CHECK(ec);
}

TORRENT_TEST(mismatching_file_hash2)
{
	file_storage st = make_v2_storage();
	st.set_piece_length(0x4000);

	error_code ec;
	st.add_file_borrow(ec, {}, combine_path("test", "B"), 10000, {}, 0, {}, 0);
	TEST_CHECK(!ec);
	st.add_file_borrow(ec, {}, combine_path("test", "a"), 10000);
	TEST_CHECK(ec);
}

TORRENT_TEST(v2_detection_1)
{
	file_storage fs = make_v2_storage();
	fs.set_piece_length(0x8000);
	// symlinks (always 0 bytes) don't participate in v1/v2 detection, so
	// adding some up front doesn't lock in v1 mode; passing in a root hash
	// (the last argument) on a later, real file makes it follow v2 rules,
	// to add pad files
	fs.add_file_borrow({}, "test/0", 0x5000, file_storage::flag_symlink, 0, "symlink-test-1");
	fs.add_file_borrow({}, "test/1", 0x5000, file_storage::flag_symlink, 0, "symlink-test-2");

	fs.add_file_borrow({}, "test/2", 0x2000, {}, 0, {}, 0);
	// it's an error to add a v1 file to a v2 torrent
	TEST_THROW(fs.add_file_borrow({}, "test/3", 0x2000));
}

TORRENT_TEST(v2_detection_2)
{
	file_storage fs = make_v2_storage();
	fs.set_piece_length(0x8000);
	// symlinks (always 0 bytes) don't participate in v1/v2 detection, so
	// adding some up front doesn't lock in v1 mode; a later, real file
	// with no root hash then follows v1 rules
	fs.add_file_borrow({}, "test/0", 0x5000, file_storage::flag_symlink, 0, "symlink-test-1");
	fs.add_file_borrow({}, "test/1", 0x5000, file_storage::flag_symlink, 0, "symlink-test-2");

	fs.add_file_borrow({}, "test/2", 0x2000);

	// it's an error to add a v1 file to a v2 torrent
	TEST_THROW(fs.add_file_borrow({}, "test/3", 0x2000, {}, 0, {}, 0));
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
		file_storage fs = make_v2_storage();
		fs.set_piece_length(0x8000);
		fs.add_file_borrow({}, "test/0", t.first, {}, 0, {}, 0);
		fs.set_num_pieces(aux::calc_num_pieces(fs));
		TEST_EQUAL(fs.blocks_in_piece2(0_piece), t.second);
	}
}

TORRENT_TEST(file_index_for_root)
{
	auto [buf, off] = build_root_hashes({
		"11111111111111111111111111111111",
		"22222222222222222222222222222222",
		"33333333333333333333333333333333",
		"44444444444444444444444444444444",
	});
	file_storage fs{buf.data()};
	fs.set_piece_length(0x8000);
	fs.add_file_borrow({}, "test/0", 0x8000, {}, 0, {}, off[0]);
	fs.add_file_borrow({}, "test/1", 0x8000, {}, 0, {}, off[1]);
	fs.add_file_borrow({}, "test/2", 0x8000, {}, 0, {}, off[2]);
	fs.add_file_borrow({}, "test/3", 0x8000, {}, 0, {}, off[3]);

	TEST_EQUAL(fs.file_index_for_root(sha256_hash("11111111111111111111111111111111")), file_index_t{0});
	TEST_EQUAL(fs.file_index_for_root(sha256_hash("22222222222222222222222222222222")), file_index_t{1});
	TEST_EQUAL(fs.file_index_for_root(sha256_hash("33333333333333333333333333333333")), file_index_t{2});
	TEST_EQUAL(fs.file_index_for_root(sha256_hash("44444444444444444444444444444444")), file_index_t{3});
	TEST_EQUAL(fs.file_index_for_root(sha256_hash("55555555555555555555555555555555")), file_index_t{-1});
}

TORRENT_TEST(size_on_disk)
{
	auto [buf, off] = build_root_hashes({
		"11111111111111111111111111111111",
		"22222222222222222222222222222222",
		"33333333333333333333333333333333",
		"44444444444444444444444444444444",
	});
	file_storage fs{buf.data()};
	fs.set_piece_length(0x8000);

	std::int64_t size_on_disk = 0;
	TEST_EQUAL(fs.size_on_disk(), size_on_disk);
	fs.add_file_borrow({}, "test/0", 100, {}, 0, {}, off[0]);
	size_on_disk += 100;
	TEST_EQUAL(fs.size_on_disk(), size_on_disk);
	fs.add_file_borrow({}, "test/1", 800, {}, 0, {}, off[1]);
	size_on_disk += 800;
	TEST_EQUAL(fs.size_on_disk(), size_on_disk);
	fs.add_file_borrow({}, "test/2", 333, {}, 0, {}, off[2]);
	size_on_disk += 333;
	TEST_EQUAL(fs.size_on_disk(), size_on_disk);
	fs.add_file_borrow({}, "test/3", 1337, {}, 0, {}, off[3]);
	size_on_disk += 1337;
	TEST_EQUAL(fs.size_on_disk(), size_on_disk);
	TEST_CHECK(fs.size_on_disk() < fs.total_size());
}

TORRENT_TEST(size_on_disk_explicit_pads)
{
	auto [buf, off] = build_root_hashes({
		"11111111111111111111111111111111",
		"22222222222222222222222222222222",
		"33333333333333333333333333333333",
	});
	file_storage fs{buf.data()};
	fs.set_piece_length(0x8000);

	std::int64_t size_on_disk = 0;
	TEST_EQUAL(fs.size_on_disk(), size_on_disk);
	fs.add_file_borrow({}, "test/0", 100, {}, 0, {}, off[0]);
	size_on_disk += 100;
	TEST_EQUAL(fs.size_on_disk(), size_on_disk);

	// when adding a pad file, size_on_disk does not increment
	fs.add_file_borrow({}, "test/pad/0", 80, file_storage::flag_pad_file, 0, {}, off[1]);
	TEST_EQUAL(fs.size_on_disk(), size_on_disk);
	fs.add_file_borrow({}, "test/2", 333, {}, 0, {}, off[2]);
	size_on_disk += 333;
	TEST_EQUAL(fs.size_on_disk(), size_on_disk);
	TEST_CHECK(fs.size_on_disk() < fs.total_size());
}

TORRENT_TEST(test_renamed_files)
{
	auto [buf, off] = build_root_hashes({
		"11111111111111111111111111111111",
		"22222222222222222222222222222222",
		"33333333333333333333333333333333",
		"44444444444444444444444444444444",
	});
	file_storage fs{buf.data()};
	fs.set_piece_length(0x8000);
	fs.add_file_borrow({}, "test/0", 0x8000, {}, 0, {}, off[0]);
	fs.add_file_borrow({}, "test/1", 0x8000, {}, 0, {}, off[1]);
	fs.add_file_borrow({}, "test/2/1", 0x8000, {}, 0, {}, off[2]);
	fs.add_file_borrow({}, "test/2/2", 0x8000, {}, 0, {}, off[3]);

	renamed_files rf;

#ifdef TORRENT_WINDOWS
	TEST_EQUAL(rf.file_path(fs, 0_file, "d:\\root"), "d:\\root\\test\\0");
	TEST_EQUAL(rf.file_path(fs, 1_file, "d:\\root"), "d:\\root\\test\\1");
	TEST_EQUAL(rf.file_path(fs, 2_file, "d:\\root"), "d:\\root\\test\\2\\1");
	TEST_EQUAL(rf.file_path(fs, 3_file, "d:\\root"), "d:\\root\\test\\2\\2");
#else
	TEST_EQUAL(rf.file_path(fs, 0_file, "/root"), "/root/test/0");
	TEST_EQUAL(rf.file_path(fs, 1_file, "/root"), "/root/test/1");
	TEST_EQUAL(rf.file_path(fs, 2_file, "/root"), "/root/test/2/1");
	TEST_EQUAL(rf.file_path(fs, 3_file, "/root"), "/root/test/2/2");
#endif

	TEST_EQUAL(std::string(rf.file_name(fs, 0_file)), "0");
	TEST_EQUAL(std::string(rf.file_name(fs, 1_file)), "1");
	TEST_EQUAL(std::string(rf.file_name(fs, 2_file)), "1");
	TEST_EQUAL(std::string(rf.file_name(fs, 3_file)), "2");

	// no root path
	rf.rename_file(fs, 0_file, "foobar");
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(rf.file_path(fs, 0_file, "d:\\root"), "d:\\root\\foobar");
#else
	TEST_EQUAL(rf.file_path(fs, 0_file, "/root"), "/root/foobar");
#endif
	TEST_EQUAL(std::string(rf.file_name(fs, 0_file)), "foobar");

	// full path
#ifdef TORRENT_WINDOWS
	rf.rename_file(fs, 1_file, "test\\bar");
	TEST_EQUAL(rf.file_path(fs, 1_file, "d:\\root"), "d:\\root\\test\\bar");
#else
	rf.rename_file(fs, 1_file, "test/bar");
	TEST_EQUAL(rf.file_path(fs, 1_file, "/root"), "/root/test/bar");
#endif
	TEST_EQUAL(std::string(rf.file_name(fs, 1_file)), "bar");

	// absolute path
#ifdef TORRENT_WINDOWS
	rf.rename_file(fs, 2_file, "c:\\foobar\\foo");
	TEST_EQUAL(rf.file_path(fs, 2_file, "d:\\root"), "c:\\foobar\\foo");
#else
	rf.rename_file(fs, 2_file, "/foobar/foo");
	TEST_EQUAL(rf.file_path(fs, 2_file, "/root"), "/foobar/foo");
#endif
	TEST_EQUAL(std::string(rf.file_name(fs, 2_file)), "foo");
}


TORRENT_TEST(renamed_files_round_trip)
{
	auto [buf, off] = build_root_hashes({
		"11111111111111111111111111111111",
		"22222222222222222222222222222222",
		"33333333333333333333333333333333",
		"44444444444444444444444444444444",
	});
	file_storage fs{buf.data()};
	fs.set_piece_length(0x8000);
	fs.add_file_borrow({}, "test/0", 0x8000, {}, 0, {}, off[0]);
	fs.add_file_borrow({}, "test/1", 0x8000, {}, 0, {}, off[1]);
	fs.add_file_borrow({}, "test/2/1", 0x8000, {}, 0, {}, off[2]);
	fs.add_file_borrow({}, "test/2/2", 0x8000, {}, 0, {}, off[3]);

	renamed_files rf;

	// just the filename (no_root_path mode): not under the torrent root
	rf.rename_file(fs, 0_file, "foobar");

	// root path (full_path mode): includes the torrent name as the root
#ifdef TORRENT_WINDOWS
	rf.rename_file(fs, 1_file, "test\\bar");
#else
	rf.rename_file(fs, 1_file, "test/bar");
#endif

	// absolute path
#ifdef TORRENT_WINDOWS
	rf.rename_file(fs, 2_file, "c:\\absolute\\path\\baz");
#else
	rf.rename_file(fs, 2_file, "/absolute/path/baz");
#endif

	// file 3 is not renamed

	// record the original file paths
#ifdef TORRENT_WINDOWS
	auto const save_path = std::string("d:\\root");
#else
	auto const save_path = std::string("/root");
#endif

	std::string const path0 = rf.file_path(fs, 0_file, save_path);
	std::string const path1 = rf.file_path(fs, 1_file, save_path);
	std::string const path2 = rf.file_path(fs, 2_file, save_path);
	std::string const path3 = rf.file_path(fs, 3_file, save_path);

	// round-trip: export then import into a fresh renamed_files
	auto const exported = rf.export_filenames(fs);
	renamed_files rf2;
	rf2.import_filenames(fs, exported);

	TEST_EQUAL(rf2.file_path(fs, 0_file, save_path), path0);
	TEST_EQUAL(rf2.file_path(fs, 1_file, save_path), path1);
	TEST_EQUAL(rf2.file_path(fs, 2_file, save_path), path2);
	TEST_EQUAL(rf2.file_path(fs, 3_file, save_path), path3);

	TEST_EQUAL(std::string(rf2.file_name(fs, 0_file)), std::string(rf.file_name(fs, 0_file)));
	TEST_EQUAL(std::string(rf2.file_name(fs, 1_file)), std::string(rf.file_name(fs, 1_file)));
	TEST_EQUAL(std::string(rf2.file_name(fs, 2_file)), std::string(rf.file_name(fs, 2_file)));
	TEST_EQUAL(std::string(rf2.file_name(fs, 3_file)), std::string(rf.file_name(fs, 3_file)));

	TEST_EQUAL(rf2.file_absolute_path(fs, 0_file), rf.file_absolute_path(fs, 0_file));
	TEST_EQUAL(rf2.file_absolute_path(fs, 1_file), rf.file_absolute_path(fs, 1_file));
	TEST_EQUAL(rf2.file_absolute_path(fs, 2_file), rf.file_absolute_path(fs, 2_file));
	TEST_EQUAL(rf2.file_absolute_path(fs, 3_file), rf.file_absolute_path(fs, 3_file));
}

// TODO: test file attributes
// TODO: test symlinks
