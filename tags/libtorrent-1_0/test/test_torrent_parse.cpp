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
#include "libtorrent/file.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/bencode.hpp"

#if TORRENT_USE_IOSTREAM
#include <sstream>
#endif

struct test_torrent_t
{
	char const* file;
};

using namespace libtorrent;

test_torrent_t test_torrents[] =
{
	{ "base.torrent" },
	{ "empty_path.torrent" },
	{ "parent_path.torrent" },
	{ "hidden_parent_path.torrent" },
	{ "single_multi_file.torrent" },
	{ "slash_path.torrent" },
	{ "backslash_path.torrent" },
	{ "url_list.torrent" },
	{ "url_list2.torrent" },
	{ "url_list3.torrent" },
	{ "httpseed.torrent" },
	{ "empty_httpseed.torrent" },
	{ "long_name.torrent" },
	{ "whitespace_url.torrent" },
	{ "duplicate_files.torrent" },
	{ "pad_file.torrent" },
	{ "creation_date.torrent" },
	{ "no_creation_date.torrent" },
	{ "url_seed.torrent" },
	{ "url_seed_multi.torrent" },
	{ "url_seed_multi_space.torrent" },
	{ "url_seed_multi_space_nolist.torrent" },
	{ "root_hash.torrent" },
	{ "empty_path_multi.torrent" },
	{ "duplicate_web_seeds.torrent" },
	{ "invalid_name3.torrent" },
	{ "symlink1.torrent" },
};

struct test_failing_torrent_t
{
	char const* file;
	error_code error; // the expected error
};

test_failing_torrent_t test_error_torrents[] =
{
	{ "missing_piece_len.torrent", errors::torrent_missing_piece_length },
	{ "invalid_piece_len.torrent", errors::torrent_missing_piece_length },
	{ "negative_piece_len.torrent", errors::torrent_missing_piece_length },
	{ "no_name.torrent", errors::torrent_missing_name },
	{ "invalid_name.torrent", errors::torrent_missing_name },
	{ "invalid_name2.torrent", errors::torrent_invalid_name },
	{ "invalid_info.torrent", errors::torrent_missing_info },
	{ "string.torrent", errors::torrent_is_no_dict },
	{ "negative_size.torrent", errors::torrent_invalid_length },
	{ "negative_file_size.torrent", errors::torrent_file_parse_failed },
	{ "invalid_path_list.torrent", errors::torrent_file_parse_failed },
	{ "missing_path_list.torrent", errors::torrent_file_parse_failed },
	{ "invalid_pieces.torrent", errors::torrent_missing_pieces },
	{ "unaligned_pieces.torrent", errors::torrent_invalid_hashes },
	{ "invalid_root_hash.torrent", errors::torrent_invalid_hashes },
	{ "invalid_root_hash2.torrent", errors::torrent_missing_pieces},
	{ "invalid_file_size.torrent", errors::torrent_file_parse_failed },
};

namespace libtorrent
{
	// defined in torrent_info.cpp
	TORRENT_EXPORT bool verify_encoding(std::string& target, bool path = true);
	TORRENT_EXPORT std::string sanitize_path(std::string const& p);
}

// TODO: test remap_files
// TODO: merkle torrents. specifically torrent_info::add_merkle_nodes and torrent with "root hash"
// TODO: torrent with 'p' (padfile) attribute
// TODO: torrent with 'h' (hidden) attribute
// TODO: torrent with 'x' (executable) attribute
// TODO: torrent with 'l' (symlink) attribute
// TODO: creating a merkle torrent (torrent_info::build_merkle_list)
// TODO: torrent with multiple trackers in multiple tiers, making sure we shuffle them (how do you test shuffling?, load it multiple times and make sure it's in different order at least once)

int test_main()
{
	error_code ec;

	// test merkle_*() functions

	// this is the structure:
	//             0
	//      1              2
	//   3      4       5       6
	//  7 8    9 10   11 12   13 14
	// num_leafs = 8

	TEST_EQUAL(merkle_num_leafs(1), 1);
	TEST_EQUAL(merkle_num_leafs(2), 2);
	TEST_EQUAL(merkle_num_leafs(3), 4);
	TEST_EQUAL(merkle_num_leafs(4), 4);
	TEST_EQUAL(merkle_num_leafs(5), 8);
	TEST_EQUAL(merkle_num_leafs(6), 8);
	TEST_EQUAL(merkle_num_leafs(7), 8);
	TEST_EQUAL(merkle_num_leafs(8), 8);
	TEST_EQUAL(merkle_num_leafs(9), 16);
	TEST_EQUAL(merkle_num_leafs(10), 16);
	TEST_EQUAL(merkle_num_leafs(11), 16);
	TEST_EQUAL(merkle_num_leafs(12), 16);
	TEST_EQUAL(merkle_num_leafs(13), 16);
	TEST_EQUAL(merkle_num_leafs(14), 16);
	TEST_EQUAL(merkle_num_leafs(15), 16);
	TEST_EQUAL(merkle_num_leafs(16), 16);
	TEST_EQUAL(merkle_num_leafs(17), 32);
	TEST_EQUAL(merkle_num_leafs(18), 32);

	// parents
	TEST_EQUAL(merkle_get_parent(1), 0);
	TEST_EQUAL(merkle_get_parent(2), 0);
	TEST_EQUAL(merkle_get_parent(3), 1);
	TEST_EQUAL(merkle_get_parent(4), 1);
	TEST_EQUAL(merkle_get_parent(5), 2);
	TEST_EQUAL(merkle_get_parent(6), 2);
	TEST_EQUAL(merkle_get_parent(7), 3);
	TEST_EQUAL(merkle_get_parent(8), 3);
	TEST_EQUAL(merkle_get_parent(9), 4);
	TEST_EQUAL(merkle_get_parent(10), 4);
	TEST_EQUAL(merkle_get_parent(11), 5);
	TEST_EQUAL(merkle_get_parent(12), 5);
	TEST_EQUAL(merkle_get_parent(13), 6);
	TEST_EQUAL(merkle_get_parent(14), 6);

	// siblings
	TEST_EQUAL(merkle_get_sibling(1), 2);
	TEST_EQUAL(merkle_get_sibling(2), 1);
	TEST_EQUAL(merkle_get_sibling(3), 4);
	TEST_EQUAL(merkle_get_sibling(4), 3);
	TEST_EQUAL(merkle_get_sibling(5), 6);
	TEST_EQUAL(merkle_get_sibling(6), 5);
	TEST_EQUAL(merkle_get_sibling(7), 8);
	TEST_EQUAL(merkle_get_sibling(8), 7);
	TEST_EQUAL(merkle_get_sibling(9), 10);
	TEST_EQUAL(merkle_get_sibling(10), 9);
	TEST_EQUAL(merkle_get_sibling(11), 12);
	TEST_EQUAL(merkle_get_sibling(12), 11);
	TEST_EQUAL(merkle_get_sibling(13), 14);
	TEST_EQUAL(merkle_get_sibling(14), 13);

	// total number of nodes given the number of leafs
	TEST_EQUAL(merkle_num_nodes(1), 1);
	TEST_EQUAL(merkle_num_nodes(2), 3);
	TEST_EQUAL(merkle_num_nodes(4), 7);
	TEST_EQUAL(merkle_num_nodes(8), 15);
	TEST_EQUAL(merkle_num_nodes(16), 31);

	// test sanitize_path

#ifdef TORRENT_WINDOWS
	TEST_EQUAL(sanitize_path("/a/b/c"), "a\\b\\c");
	TEST_EQUAL(sanitize_path("a/../c"), "a\\c");
#else
	TEST_EQUAL(sanitize_path("/a/b/c"), "a/b/c");
	TEST_EQUAL(sanitize_path("a/../c"), "a/c");
#endif
	TEST_EQUAL(sanitize_path("/.././c"), "c");
	TEST_EQUAL(sanitize_path("dev:"), "");
	TEST_EQUAL(sanitize_path("c:/b"), "b");
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(sanitize_path("c:\\.\\c"), "c");
	TEST_EQUAL(sanitize_path("\\c"), "c");
#else
	TEST_EQUAL(sanitize_path("//./c"), "c");
#endif

	// test torrent parsing

	entry info;
	info["pieces"] = "aaaaaaaaaaaaaaaaaaaa";
	info["name.utf-8"] = "test1";
	info["name"] = "test__";
	info["piece length"] = 16 * 1024;
	info["length"] = 3245;
	entry torrent;
	torrent["info"] = info;

	std::vector<char> buf;
	bencode(std::back_inserter(buf), torrent);
	torrent_info ti(&buf[0], buf.size(), ec);
	std::cerr << ti.name() << std::endl;
	TEST_CHECK(ti.name() == "test1");

#ifdef TORRENT_WINDOWS
	info["name.utf-8"] = "c:/test1/test2/test3";
#else
	info["name.utf-8"] = "/test1/test2/test3";
#endif
	torrent["info"] = info;
	buf.clear();
	bencode(std::back_inserter(buf), torrent);
	torrent_info ti2(&buf[0], buf.size(), ec);
	std::cerr << ti2.name() << std::endl;
#ifdef TORRENT_WINDOWS
	TEST_CHECK(ti2.name() == "test1\\test2\\test3");
#else
	TEST_CHECK(ti2.name() == "test1/test2/test3");
#endif

	info["name.utf-8"] = "test2/../test3/.././../../test4";
	torrent["info"] = info;
	buf.clear();
	bencode(std::back_inserter(buf), torrent);
	torrent_info ti3(&buf[0], buf.size(), ec);
	std::cerr << ti3.name() << std::endl;
#ifdef TORRENT_WINDOWS
	TEST_CHECK(ti3.name() == "test2\\test3\\test4");
#else
	TEST_CHECK(ti3.name() == "test2/test3/test4");
#endif

	// verify_encoding
	std::string test = "\b?filename=4";
	TEST_CHECK(!verify_encoding(test));
#ifdef TORRENT_WINDOWS
	TEST_CHECK(test == "__filename=4");
#else
	TEST_CHECK(test == "_?filename=4");
#endif

	test = "filename=4";
	TEST_CHECK(verify_encoding(test));
	TEST_CHECK(test == "filename=4");

	// valid 2-byte sequence
	test = "filename\xc2\xa1";
	TEST_CHECK(verify_encoding(test));
	fprintf(stderr, "%s\n", test.c_str());
	TEST_CHECK(test == "filename\xc2\xa1");

	// truncated 2-byte sequence
	test = "filename\xc2";
	TEST_CHECK(!verify_encoding(test));
	fprintf(stderr, "%s\n", test.c_str());
	TEST_CHECK(test == "filename_");

	// valid 3-byte sequence
	test = "filename\xe2\x9f\xb9";
	TEST_CHECK(verify_encoding(test));
	fprintf(stderr, "%s\n", test.c_str());
	TEST_CHECK(test == "filename\xe2\x9f\xb9");

	// truncated 3-byte sequence
	test = "filename\xe2\x9f";
	TEST_CHECK(!verify_encoding(test));
	fprintf(stderr, "%s\n", test.c_str());
	TEST_CHECK(test == "filename_");

	// truncated 3-byte sequence
	test = "filename\xe2";
	TEST_CHECK(!verify_encoding(test));
	fprintf(stderr, "%s\n", test.c_str());
	TEST_CHECK(test == "filename_");

	// valid 4-byte sequence
	test = "filename\xf0\x9f\x92\x88";
	TEST_CHECK(verify_encoding(test));
	fprintf(stderr, "%s\n", test.c_str());
	TEST_CHECK(test == "filename\xf0\x9f\x92\x88");

	// truncated 4-byte sequence
	test = "filename\xf0\x9f\x92";
	TEST_CHECK(!verify_encoding(test));
	fprintf(stderr, "%s\n", test.c_str());
	TEST_CHECK(test == "filename_");

	// 5-byte utf-8 sequence (not allowed)
	test = "filename\xf8\x9f\x9f\x9f\x9f""foobar";
	TEST_CHECK(!verify_encoding(test));
	fprintf(stderr, "%s\n", test.c_str());
	TEST_CHECK(test == "filename_____foobar");

	// trim_path_element

	fprintf(stderr, "TORRENT_MAX_PATH: %d\n", TORRENT_MAX_PATH);

	// 1100 characters
	test = "abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij012345.txt";
	std::string comparison = test;
	trim_path_element(test);
	if (comparison.size() > TORRENT_MAX_PATH)
	{
		comparison.resize(TORRENT_MAX_PATH - 4);
		comparison += ".txt"; // the extension is supposed to be preserved
	}
	TEST_EQUAL(test, comparison);

	// extensions > 15 characters are ignored
	test = "abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789"
		"abcdefghij0123456789abcdefghij0123456789abcdefghij0123456789abcdefghij.123456789abcdefghij0123456789";
	comparison = test;
	trim_path_element(test);
	if (comparison.size() > TORRENT_MAX_PATH)
		comparison.resize(TORRENT_MAX_PATH);
	TEST_EQUAL(test, comparison);

	std::string root_dir = parent_path(current_working_directory());
	for (int i = 0; i < sizeof(test_torrents)/sizeof(test_torrents[0]); ++i)
	{
		fprintf(stderr, "loading %s\n", test_torrents[i].file);
		boost::intrusive_ptr<torrent_info> ti(new torrent_info(combine_path(combine_path(root_dir, "test_torrents"), test_torrents[i].file), ec));
		TEST_CHECK(!ec);
		if (ec) fprintf(stderr, "  -> failed %s\n", ec.message().c_str());

		if (std::string(test_torrents[i].file) == "whitespace_url.torrent")
		{
			// make sure we trimmed the url
			TEST_CHECK(ti->trackers().size() > 0);
			if (ti->trackers().size() > 0)
				TEST_CHECK(ti->trackers()[0].url == "udp://test.com/announce");
		}
		else if (std::string(test_torrents[i].file) == "duplicate_files.torrent")
		{
			// make sure we disambiguated the files
			TEST_EQUAL(ti->num_files(), 2);
			TEST_CHECK(ti->file_at(0).path == combine_path(combine_path("temp", "foo"), "bar.txt"));
			TEST_CHECK(ti->file_at(1).path == combine_path(combine_path("temp", "foo"), "bar.1.txt"));
		}
		else if (std::string(test_torrents[i].file) == "pad_file.torrent")
		{
			TEST_EQUAL(ti->num_files(), 2);
			TEST_EQUAL(ti->file_at(0).pad_file, false);
			TEST_EQUAL(ti->file_at(1).pad_file, true);
		}
		else if (std::string(test_torrents[i].file) == "creation_date.torrent")
		{
			TEST_EQUAL(*ti->creation_date(), 1234567);
		}
		else if (std::string(test_torrents[i].file) == "duplicate_web_seeds.torrent")
		{
			TEST_EQUAL(ti->web_seeds().size(), 3);
		}
		else if (std::string(test_torrents[i].file) == "no_creation_date.torrent")
		{
			TEST_CHECK(!ti->creation_date());
		}
		else if (std::string(test_torrents[i].file) == "url_seed.torrent")
		{
			TEST_EQUAL(ti->web_seeds().size(), 1);
			TEST_EQUAL(ti->web_seeds()[0].url, "http://test.com/file");
#ifndef TORRENT_NO_DEPRECATE
			TEST_EQUAL(ti->http_seeds().size(), 0);
			TEST_EQUAL(ti->url_seeds().size(), 1);
			TEST_EQUAL(ti->url_seeds()[0], "http://test.com/file");
#endif
		}
		else if (std::string(test_torrents[i].file) == "url_seed_multi.torrent")
		{
			TEST_EQUAL(ti->web_seeds().size(), 1);
			TEST_EQUAL(ti->web_seeds()[0].url, "http://test.com/file/");
#ifndef TORRENT_NO_DEPRECATE
			TEST_EQUAL(ti->http_seeds().size(), 0);
			TEST_EQUAL(ti->url_seeds().size(), 1);
			TEST_EQUAL(ti->url_seeds()[0], "http://test.com/file/");
#endif
		}
		else if (std::string(test_torrents[i].file) == "url_seed_multi_space.torrent"
			|| std::string(test_torrents[i].file) == "url_seed_multi_space_nolist.torrent")
		{
			TEST_EQUAL(ti->web_seeds().size(), 1);
			TEST_EQUAL(ti->web_seeds()[0].url, "http://test.com/test%20file/foo%20bar/");
#ifndef TORRENT_NO_DEPRECATE
			TEST_EQUAL(ti->http_seeds().size(), 0);
			TEST_EQUAL(ti->url_seeds().size(), 1);
			TEST_EQUAL(ti->url_seeds()[0], "http://test.com/test%20file/foo%20bar/");
#endif
		}
		else if (std::string(test_torrents[i].file) == "invalid_name3.torrent")
		{
			TEST_EQUAL(ti->name(), "foobar");
		}

		file_storage const& fs = ti->files();
		for (int i = 0; i < fs.num_files(); ++i)
		{
			int first = ti->map_file(i, 0, 0).piece;
			int last = ti->map_file(i, (std::max)(fs.file_size(i)-1, size_type(0)), 0).piece;
			int flags = fs.file_flags(i);
			fprintf(stderr, "  %11" PRId64 " %c%c%c%c [ %4d, %4d ] %7u %s %s %s%s\n"
				, fs.file_size(i)
				, (flags & file_storage::flag_pad_file)?'p':'-'
				, (flags & file_storage::flag_executable)?'x':'-'
				, (flags & file_storage::flag_hidden)?'h':'-'
				, (flags & file_storage::flag_symlink)?'l':'-'
				, first, last
				, boost::uint32_t(fs.mtime(i))
				, fs.hash(i) != sha1_hash(0) ? to_hex(fs.hash(i).to_string()).c_str() : ""
				, fs.file_path(i).c_str()
				, flags & file_storage::flag_symlink ? "-> ": ""
				, flags & file_storage::flag_symlink ? fs.symlink(i).c_str() : "");
		}

		// test swap
#if !defined TORRENT_NO_DEPRECATE && TORRENT_USE_IOSTREAM
		std::stringstream str1;
		ti->print(str1);

		torrent_info temp("temp", ec);
		temp.swap(*ti);

		std::stringstream str2;
		temp.print(str2);
		TEST_EQUAL(str1.str(), str2.str());
#endif

	}

	for (int i = 0; i < sizeof(test_error_torrents)/sizeof(test_error_torrents[0]); ++i)
	{
		error_code ec;
		fprintf(stderr, "loading %s\n", test_error_torrents[i].file);
		boost::intrusive_ptr<torrent_info> ti(new torrent_info(combine_path(combine_path(root_dir, "test_torrents"), test_error_torrents[i].file), ec));
		fprintf(stderr, "E:        \"%s\"\nexpected: \"%s\"\n", ec.message().c_str(), test_error_torrents[i].error.message().c_str());
		TEST_CHECK(ec.message() == test_error_torrents[i].error.message());
	}

	return 0;
}

