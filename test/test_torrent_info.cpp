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
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/aux_/escape_string.hpp" // for convert_path_to_posix
#include "libtorrent/hex.hpp" // to_hex

#include <iostream>

using namespace lt;

#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS
TORRENT_TEST(mutable_torrents)
{
	file_storage fs;

	fs.add_file("test/temporary.txt", 0x4000);

	lt::create_torrent t(fs, 0x4000);

	// calculate the hash for all pieces
	sha1_hash ph;
	for (auto const i : fs.piece_range())
		t.set_hash(i, ph);

	t.add_collection("collection1");
	t.add_collection("collection2");

	t.add_similar_torrent(sha1_hash("abababababababababab"));
	t.add_similar_torrent(sha1_hash("babababababababababa"));

	std::vector<char> tmp;
	std::back_insert_iterator<std::vector<char>> out(tmp);

	entry tor = t.generate();
	bencode(out, tor);

	torrent_info ti(tmp, from_span);

	std::vector<sha1_hash> similar;
	similar.push_back(sha1_hash("abababababababababab"));
	similar.push_back(sha1_hash("babababababababababa"));

	std::vector<std::string> collections;
	collections.push_back("collection1");
	collections.push_back("collection2");

	TEST_CHECK(similar == ti.similar_torrents());
	TEST_CHECK(collections == ti.collections());
}
#endif

namespace {

struct test_torrent_t
{
	char const* file;
};

using namespace lt;

static test_torrent_t test_torrents[] =
{
	{ "base.torrent" },
	{ "empty_path.torrent" },
	{ "parent_path.torrent" },
	{ "hidden_parent_path.torrent" },
	{ "single_multi_file.torrent" },
	{ "slash_path.torrent" },
	{ "slash_path2.torrent" },
	{ "slash_path3.torrent" },
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
	{ "url_seed_multi_single_file.torrent" },
	{ "url_seed_multi_space.torrent" },
	{ "url_seed_multi_space_nolist.torrent" },
	{ "root_hash.torrent" },
	{ "empty_path_multi.torrent" },
	{ "duplicate_web_seeds.torrent" },
	{ "invalid_name2.torrent" },
	{ "invalid_name3.torrent" },
	{ "symlink1.torrent" },
	{ "symlink2.torrent" },
	{ "unordered.torrent" },
	{ "symlink_zero_size.torrent" },
	{ "pad_file_no_path.torrent" },
	{ "large.torrent" },
	{ "absolute_filename.torrent" },
	{ "invalid_filename.torrent" },
	{ "invalid_filename2.torrent" },
	{ "overlapping_symlinks.torrent" },
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
	{ "bad_name.torrent", errors::torrent_missing_name },
	{ "invalid_name.torrent", errors::torrent_missing_name },
	{ "invalid_info.torrent", errors::torrent_missing_info },
	{ "string.torrent", errors::torrent_is_no_dict },
	{ "negative_size.torrent", errors::torrent_invalid_length },
	{ "negative_file_size.torrent", errors::torrent_invalid_length },
	{ "invalid_path_list.torrent", errors::torrent_invalid_name},
	{ "missing_path_list.torrent", errors::torrent_missing_name },
	{ "invalid_pieces.torrent", errors::torrent_missing_pieces },
	{ "unaligned_pieces.torrent", errors::torrent_invalid_hashes },
	{ "invalid_root_hash.torrent", errors::torrent_invalid_hashes },
	{ "invalid_root_hash2.torrent", errors::torrent_missing_pieces },
	{ "invalid_merkle.torrent", errors::no_files_in_torrent},
	{ "invalid_file_size.torrent", errors::torrent_invalid_length },
	{ "invalid_symlink.torrent", errors::torrent_invalid_name },
	{ "many_pieces.torrent", errors::too_many_pieces_in_torrent },
	{ "no_files.torrent", errors::no_files_in_torrent},
};

} // anonymous namespace

// TODO: test remap_files
// TODO: merkle torrents. specifically torrent_info::add_merkle_nodes and torrent with "root hash"
// TODO: torrent with 'p' (padfile) attribute
// TODO: torrent with 'h' (hidden) attribute
// TODO: torrent with 'x' (executable) attribute
// TODO: torrent with 'l' (symlink) attribute
// TODO: creating a merkle torrent (torrent_info::build_merkle_list)
// TODO: torrent with multiple trackers in multiple tiers, making sure we
// shuffle them (how do you test shuffling?, load it multiple times and make
// sure it's in different order at least once)
// TODO: torrents with a zero-length name
// TODO: torrents with a merkle tree and add_merkle_nodes
// TODO: torrent with a non-dictionary info-section
// TODO: torrents with DHT nodes
// TODO: torrent with url-list as a single string
// TODO: torrent with http seed as a single string
// TODO: torrent with a comment
// TODO: torrent with an SSL cert
// TODO: torrent with attributes (executable and hidden)
// TODO: torrent_info::add_tracker
// TODO: torrent_info constructor that takes an invalid bencoded buffer
// TODO: verify_encoding with a string that triggers character replacement

TORRENT_TEST(url_list_and_httpseeds)
{
	entry info;
	info["pieces"] = "aaaaaaaaaaaaaaaaaaaa";
	info["name.utf-8"] = "test1";
	info["name"] = "test__";
	info["piece length"] = 16 * 1024;
	info["length"] = 3245;
	entry::list_type l;
	l.push_back(entry("http://foo.com/bar1"));
	l.push_back(entry("http://foo.com/bar1"));
	l.push_back(entry("http://foo.com/bar2"));
	entry const e(l);
	entry torrent;
	torrent["url-list"] = e;
	torrent["httpseeds"] = e;
	torrent["info"] = info;
	std::vector<char> buf;
	bencode(std::back_inserter(buf), torrent);
	torrent_info ti(buf, from_span);
	TEST_EQUAL(ti.web_seeds().size(), 4);
}

TORRENT_TEST(add_url_seed)
{
	torrent_info ti(sha1_hash("                   "));
	TEST_EQUAL(ti.web_seeds().size(), 0);

	ti.add_url_seed("http://test.com");

	TEST_EQUAL(ti.web_seeds().size(), 1);
	web_seed_entry we = ti.web_seeds()[0];
	TEST_EQUAL(we.type, web_seed_entry::url_seed);
	TEST_EQUAL(we.url, "http://test.com");
}

TORRENT_TEST(add_http_seed)
{
	torrent_info ti(sha1_hash("                   "));
	TEST_EQUAL(ti.web_seeds().size(), 0);

	ti.add_http_seed("http://test.com");

	TEST_EQUAL(ti.web_seeds().size(), 1);
	web_seed_entry we = ti.web_seeds()[0];
	TEST_EQUAL(we.type, web_seed_entry::http_seed);
	TEST_EQUAL(we.url, "http://test.com");
}

TORRENT_TEST(set_web_seeds)
{
	torrent_info ti(sha1_hash("                   "));
	TEST_EQUAL(ti.web_seeds().size(), 0);

	std::vector<web_seed_entry> seeds;
	web_seed_entry e1("http://test1.com", web_seed_entry::url_seed);
	seeds.push_back(e1);
	web_seed_entry e2("http://test2com", web_seed_entry::http_seed);
	seeds.push_back(e2);

	ti.set_web_seeds(seeds);

	TEST_EQUAL(ti.web_seeds().size(), 2);
	TEST_CHECK(ti.web_seeds() == seeds);
}

#ifdef TORRENT_WINDOWS
#define SEPARATOR "\\"
#else
#define SEPARATOR "/"
#endif

TORRENT_TEST(sanitize_long_path)
{
	// test sanitize_append_path_element

	std::string path;
	sanitize_append_path_element(path,
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_");
	sanitize_append_path_element(path,
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcde.test");
	TEST_EQUAL(path,
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_" SEPARATOR
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_.test");
}

TORRENT_TEST(sanitize_path_trailing_dots)
{
	std::string path;
	sanitize_append_path_element(path, "a");
	sanitize_append_path_element(path, "abc...");
	sanitize_append_path_element(path, "c");
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(path, "a" SEPARATOR "abc" SEPARATOR "c");
#else
	TEST_EQUAL(path, "a" SEPARATOR "abc..." SEPARATOR "c");
#endif

	path.clear();
	sanitize_append_path_element(path, "abc...");
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(path, "abc");
#else
	TEST_EQUAL(path, "abc...");
#endif

	path.clear();
	sanitize_append_path_element(path, "abc.");
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(path, "abc");
#else
	TEST_EQUAL(path, "abc.");
#endif


	path.clear();
	sanitize_append_path_element(path, "a. . .");
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(path, "a");
#else
	TEST_EQUAL(path, "a. . .");
#endif
}

TORRENT_TEST(sanitize_path_trailing_spaces)
{
	std::string path;
	sanitize_append_path_element(path, "a");
	sanitize_append_path_element(path, "abc   ");
	sanitize_append_path_element(path, "c");
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(path, "a" SEPARATOR "abc" SEPARATOR "c");
#else
	TEST_EQUAL(path, "a" SEPARATOR "abc   " SEPARATOR "c");
#endif

	path.clear();
	sanitize_append_path_element(path, "abc   ");
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(path, "abc");
#else
	TEST_EQUAL(path, "abc   ");
#endif

	path.clear();
	sanitize_append_path_element(path, "abc ");
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(path, "abc");
#else
	TEST_EQUAL(path, "abc ");
#endif
}

TORRENT_TEST(sanitize_path)
{
	std::string path;
	sanitize_append_path_element(path, "\0\0\xed\0\x80");
	TEST_EQUAL(path, "_");

	path.clear();
	sanitize_append_path_element(path, "/a/");
	sanitize_append_path_element(path, "b");
	sanitize_append_path_element(path, "c");
	TEST_EQUAL(path, "a" SEPARATOR "b" SEPARATOR "c");

	path.clear();
	sanitize_append_path_element(path, "a...b");
	TEST_EQUAL(path, "a...b");

	path.clear();
	sanitize_append_path_element(path, "a");
	sanitize_append_path_element(path, "..");
	sanitize_append_path_element(path, "c");
	TEST_EQUAL(path, "a" SEPARATOR "c");

	path.clear();
	sanitize_append_path_element(path, "a");
	sanitize_append_path_element(path, "..");
	TEST_EQUAL(path, "a");

	path.clear();
	sanitize_append_path_element(path, "/..");
	sanitize_append_path_element(path, ".");
	sanitize_append_path_element(path, "c");
	TEST_EQUAL(path, "c");

	path.clear();
	sanitize_append_path_element(path, "dev:");
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(path, "dev_");
#else
	TEST_EQUAL(path, "dev:");
#endif

	path.clear();
	sanitize_append_path_element(path, "c:");
	sanitize_append_path_element(path, "b");
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(path, "c_" SEPARATOR "b");
#else
	TEST_EQUAL(path, "c:" SEPARATOR "b");
#endif

	path.clear();
	sanitize_append_path_element(path, "c:");
	sanitize_append_path_element(path, ".");
	sanitize_append_path_element(path, "c");
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(path, "c_" SEPARATOR "c");
#else
	TEST_EQUAL(path, "c:" SEPARATOR "c");
#endif

	path.clear();
	sanitize_append_path_element(path, "\\c");
	sanitize_append_path_element(path, ".");
	sanitize_append_path_element(path, "c");
	TEST_EQUAL(path, "c" SEPARATOR "c");

	path.clear();
	sanitize_append_path_element(path, "\b");
	TEST_EQUAL(path, "_");

	path.clear();
	sanitize_append_path_element(path, "\b");
	sanitize_append_path_element(path, "filename");
	TEST_EQUAL(path, "_" SEPARATOR "filename");

	path.clear();
	sanitize_append_path_element(path, "filename");
	sanitize_append_path_element(path, "\b");
	TEST_EQUAL(path, "filename" SEPARATOR "_");

	path.clear();
	sanitize_append_path_element(path, "abc");
	sanitize_append_path_element(path, "");
	TEST_EQUAL(path, "abc" SEPARATOR "_");

	path.clear();
	sanitize_append_path_element(path, "abc");
	sanitize_append_path_element(path, "   ");
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(path, "abc");
#else
	TEST_EQUAL(path, "abc" SEPARATOR "   ");
#endif

	path.clear();
	sanitize_append_path_element(path, "");
	sanitize_append_path_element(path, "abc");
	TEST_EQUAL(path, "_" SEPARATOR "abc");

	path.clear();
	sanitize_append_path_element(path, "\b?filename=4");
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(path, "__filename=4");
#else
	TEST_EQUAL(path, "_?filename=4");
#endif

	path.clear();
	sanitize_append_path_element(path, "filename=4");
	TEST_EQUAL(path, "filename=4");

	// valid 2-byte sequence
	path.clear();
	sanitize_append_path_element(path, "filename\xc2\xa1");
	TEST_EQUAL(path, "filename\xc2\xa1");

	// truncated 2-byte sequence
	path.clear();
	sanitize_append_path_element(path, "filename\xc2");
	TEST_EQUAL(path, "filename_");

	// valid 3-byte sequence
	path.clear();
	sanitize_append_path_element(path, "filename\xe2\x9f\xb9");
	TEST_EQUAL(path, "filename\xe2\x9f\xb9");

	// truncated 3-byte sequence
	path.clear();
	sanitize_append_path_element(path, "filename\xe2\x9f");
	TEST_EQUAL(path, "filename_");

	// truncated 3-byte sequence
	path.clear();
	sanitize_append_path_element(path, "filename\xe2");
	TEST_EQUAL(path, "filename_");

	// valid 4-byte sequence
	path.clear();
	sanitize_append_path_element(path, "filename\xf0\x9f\x92\x88");
	TEST_EQUAL(path, "filename\xf0\x9f\x92\x88");

	// truncated 4-byte sequence
	path.clear();
	sanitize_append_path_element(path, "filename\xf0\x9f\x92");
	TEST_EQUAL(path, "filename_");

	// 5-byte utf-8 sequence (not allowed)
	path.clear();
	sanitize_append_path_element(path, "filename\xf8\x9f\x9f\x9f\x9f" "foobar");
	TEST_EQUAL(path, "filename_foobar");

	// redundant (overlong) 2-byte sequence
	// ascii code 0x2e encoded with a leading 0
	path.clear();
	sanitize_append_path_element(path, "filename\xc0\xae");
	TEST_EQUAL(path, "filename_");

	// redundant (overlong) 3-byte sequence
	// ascii code 0x2e encoded with two leading 0s
	path.clear();
	sanitize_append_path_element(path, "filename\xe0\x80\xae");
	TEST_EQUAL(path, "filename_");

	// redundant (overlong) 4-byte sequence
	// ascii code 0x2e encoded with three leading 0s
	path.clear();
	sanitize_append_path_element(path, "filename\xf0\x80\x80\xae");
	TEST_EQUAL(path, "filename_");

	// a filename where every character is filtered is not replaced by an understcore
	path.clear();
	sanitize_append_path_element(path, "//\\");
	TEST_EQUAL(path, "");

	// make sure suspicious unicode characters are filtered out
	path.clear();
	// that's utf-8 for U+200e LEFT-TO-RIGHT MARK
	sanitize_append_path_element(path, "foo\xe2\x80\x8e" "bar");
	TEST_EQUAL(path, "foobar");

	// make sure suspicious unicode characters are filtered out
	path.clear();
	// that's utf-8 for U+202b RIGHT-TO-LEFT EMBEDDING
	sanitize_append_path_element(path, "foo\xe2\x80\xab" "bar");
	TEST_EQUAL(path, "foobar");
}

TORRENT_TEST(sanitize_path_zeroes)
{
	std::string path;
	sanitize_append_path_element(path, "\0foo");
	TEST_EQUAL(path, "_");

	path.clear();
	sanitize_append_path_element(path, "\0\0\0\0");
	TEST_EQUAL(path, "_");
}

TORRENT_TEST(sanitize_path_colon)
{
	std::string path;
	sanitize_append_path_element(path, "foo:bar");
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(path, "foo_bar");
#else
	TEST_EQUAL(path, "foo:bar");
#endif
}

TORRENT_TEST(verify_encoding)
{
	// verify_encoding
	std::string test = "\b?filename=4";
	TEST_CHECK(verify_encoding(test));
	TEST_CHECK(test == "\b?filename=4");

	test = "filename=4";
	TEST_CHECK(verify_encoding(test));
	TEST_CHECK(test == "filename=4");

	// valid 2-byte sequence
	test = "filename\xc2\xa1";
	TEST_CHECK(verify_encoding(test));
	std::printf("%s\n", test.c_str());
	TEST_CHECK(test == "filename\xc2\xa1");

	// truncated 2-byte sequence
	test = "filename\xc2";
	TEST_CHECK(!verify_encoding(test));
	std::printf("%s\n", test.c_str());
	TEST_CHECK(test == "filename_");

	// valid 3-byte sequence
	test = "filename\xe2\x9f\xb9";
	TEST_CHECK(verify_encoding(test));
	std::printf("%s\n", test.c_str());
	TEST_CHECK(test == "filename\xe2\x9f\xb9");

	// truncated 3-byte sequence
	test = "filename\xe2\x9f";
	TEST_CHECK(!verify_encoding(test));
	std::printf("%s\n", test.c_str());
	TEST_CHECK(test == "filename_");

	// truncated 3-byte sequence
	test = "filename\xe2";
	TEST_CHECK(!verify_encoding(test));
	std::printf("%s\n", test.c_str());
	TEST_CHECK(test == "filename_");

	// valid 4-byte sequence
	test = "filename\xf0\x9f\x92\x88";
	TEST_CHECK(verify_encoding(test));
	std::printf("%s\n", test.c_str());
	TEST_CHECK(test == "filename\xf0\x9f\x92\x88");

	// truncated 4-byte sequence
	test = "filename\xf0\x9f\x92";
	TEST_CHECK(!verify_encoding(test));
	std::printf("%s\n", test.c_str());
	TEST_CHECK(test == "filename_");

	// 5-byte utf-8 sequence (not allowed)
	test = "filename\xf8\x9f\x9f\x9f\x9f""foobar";
	TEST_CHECK(!verify_encoding(test));
	std::printf("%s\n", test.c_str());
	TEST_CHECK(test == "filename_____foobar");

	// redundant (overlong) 2-byte sequence
	// ascii code 0x2e encoded with a leading 0
	test = "filename\xc0\xae";
	TEST_CHECK(!verify_encoding(test));
	std::printf("%s\n", test.c_str());
	TEST_CHECK(test == "filename__");

	// redundant (overlong) 3-byte sequence
	// ascii code 0x2e encoded with two leading 0s
	test = "filename\xe0\x80\xae";
	TEST_CHECK(!verify_encoding(test));
	std::printf("%s\n", test.c_str());
	TEST_CHECK(test == "filename___");

	// redundant (overlong) 4-byte sequence
	// ascii code 0x2e encoded with three leading 0s
	test = "filename\xf0\x80\x80\xae";
	TEST_CHECK(!verify_encoding(test));
	std::printf("%s\n", test.c_str());
	TEST_CHECK(test == "filename____");

	// missing byte header
	test = "filename\xed\0\x80";
	TEST_CHECK(!verify_encoding(test));
	fprintf(stdout, "%s\n", test.c_str());
	TEST_CHECK(test == "filename_");
}

TORRENT_TEST(parse_torrents)
{
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
	torrent_info ti1(buf, from_span);
	std::cout << ti1.name() << std::endl;
	TEST_CHECK(ti1.name() == "test1");

#ifdef TORRENT_WINDOWS
	info["name.utf-8"] = "c:/test1/test2/test3";
#else
	info["name.utf-8"] = "/test1/test2/test3";
#endif
	torrent["info"] = info;
	buf.clear();
	bencode(std::back_inserter(buf), torrent);
	torrent_info ti2(buf, from_span);
	std::cout << ti2.name() << std::endl;
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(ti2.name(), "c_test1test2test3");
#else
	TEST_EQUAL(ti2.name(), "test1test2test3");
#endif

	info["name.utf-8"] = "test2/../test3/.././../../test4";
	torrent["info"] = info;
	buf.clear();
	bencode(std::back_inserter(buf), torrent);
	torrent_info ti3(buf, from_span);
	std::cout << ti3.name() << std::endl;
	TEST_EQUAL(ti3.name(), "test2..test3.......test4");

	std::string root_dir = parent_path(current_working_directory());
	for (auto const t : test_torrents)
	{
		std::printf("loading %s\n", t.file);
		std::string filename = combine_path(combine_path(root_dir, "test_torrents")
			, t.file);
		error_code ec;
		auto ti = std::make_shared<torrent_info>(filename, ec);
		TEST_CHECK(!ec);
		if (ec) std::printf(" loading(\"%s\") -> failed %s\n", filename.c_str()
			, ec.message().c_str());

		if (t.file == "whitespace_url.torrent"_sv)
		{
			// make sure we trimmed the url
			TEST_CHECK(ti->trackers().size() > 0);
			if (ti->trackers().size() > 0)
				TEST_CHECK(ti->trackers()[0].url == "udp://test.com/announce");
		}
		else if (t.file == "duplicate_files.torrent"_sv)
		{
			// make sure we disambiguated the files
			TEST_EQUAL(ti->num_files(), 2);
			TEST_CHECK(ti->files().file_path(file_index_t{0}) == combine_path(combine_path("temp", "foo"), "bar.txt"));
			TEST_CHECK(ti->files().file_path(file_index_t{1}) == combine_path(combine_path("temp", "foo"), "bar.1.txt"));
		}
		else if (t.file == "pad_file.torrent"_sv)
		{
			TEST_EQUAL(ti->num_files(), 2);
			TEST_EQUAL(bool(ti->files().file_flags(file_index_t{0}) & file_storage::flag_pad_file), false);
			TEST_EQUAL(bool(ti->files().file_flags(file_index_t{1}) & file_storage::flag_pad_file), true);
		}
		else if (t.file == "creation_date.torrent"_sv)
		{
			TEST_EQUAL(ti->creation_date(), 1234567);
		}
		else if (t.file == "duplicate_web_seeds.torrent"_sv)
		{
			TEST_EQUAL(ti->web_seeds().size(), 3);
		}
		else if (t.file == "no_creation_date.torrent"_sv)
		{
			TEST_CHECK(!ti->creation_date());
		}
		else if (t.file == "url_seed.torrent"_sv)
		{
			TEST_EQUAL(ti->web_seeds().size(), 1);
			TEST_EQUAL(ti->web_seeds()[0].url, "http://test.com/file");
#if TORRENT_ABI_VERSION == 1
			TEST_EQUAL(ti->http_seeds().size(), 0);
			TEST_EQUAL(ti->url_seeds().size(), 1);
			TEST_EQUAL(ti->url_seeds()[0], "http://test.com/file");
#endif
		}
		else if (t.file == "url_seed_multi.torrent"_sv)
		{
			TEST_EQUAL(ti->web_seeds().size(), 1);
			TEST_EQUAL(ti->web_seeds()[0].url, "http://test.com/file/");
#if TORRENT_ABI_VERSION == 1
			TEST_EQUAL(ti->http_seeds().size(), 0);
			TEST_EQUAL(ti->url_seeds().size(), 1);
			TEST_EQUAL(ti->url_seeds()[0], "http://test.com/file/");
#endif
		}
		else if (t.file == "url_seed_multi_single_file.torrent"_sv)
		{
			TEST_EQUAL(ti->web_seeds().size(), 1);
			TEST_EQUAL(ti->web_seeds()[0].url, "http://test.com/file/temp/foo/bar.txt");
		}
		else if (t.file == "url_seed_multi_space.torrent"_sv
			|| t.file == "url_seed_multi_space_nolist.torrent"_sv)
		{
			TEST_EQUAL(ti->web_seeds().size(), 1);
			TEST_EQUAL(ti->web_seeds()[0].url, "http://test.com/test%20file/foo%20bar/");
#if TORRENT_ABI_VERSION == 1
			TEST_EQUAL(ti->http_seeds().size(), 0);
			TEST_EQUAL(ti->url_seeds().size(), 1);
			TEST_EQUAL(ti->url_seeds()[0], "http://test.com/test%20file/foo%20bar/");
#endif
		}
		else if (t.file == "invalid_name2.torrent"_sv)
		{
			// if, after all invalid characters are removed from the name, it ends up
			// being empty, it's set to the info-hash. Some torrents also have an empty name
			// in which case it's also set to the info-hash
			TEST_EQUAL(ti->name(), "b61560c2918f463768cd122b6d2fdd47b77bdb35");
		}
		else if (t.file == "invalid_name3.torrent"_sv)
		{
			// windows does not allow trailing spaces in filenames
#ifdef TORRENT_WINDOWS
			TEST_EQUAL(ti->name(), "foobar");
#else
			TEST_EQUAL(ti->name(), "foobar ");
#endif
		}
		else if (t.file == "symlink1.torrent"_sv)
		{
			TEST_EQUAL(ti->num_files(), 2);
			TEST_EQUAL(ti->files().symlink(file_index_t{1}), "temp" SEPARATOR "a" SEPARATOR "b" SEPARATOR "bar");
		}
		else if (t.file == "symlink2.torrent"_sv)
		{
			TEST_EQUAL(ti->num_files(), 5);
			TEST_EQUAL(ti->files().symlink(file_index_t{0}), "Some.framework" SEPARATOR "Versions" SEPARATOR "A" SEPARATOR "SDL2");
			TEST_EQUAL(ti->files().symlink(file_index_t{4}), "Some.framework" SEPARATOR "Versions" SEPARATOR "A");
		}
		else if (t.file == "slash_path.torrent"_sv)
		{
			TEST_EQUAL(ti->num_files(), 1);
			TEST_EQUAL(ti->files().file_path(file_index_t{0}), "temp" SEPARATOR "bar");
		}
		else if (t.file == "slash_path2.torrent"_sv)
		{
			TEST_EQUAL(ti->num_files(), 1);
			TEST_EQUAL(ti->files().file_path(file_index_t{0}), "temp" SEPARATOR "abc....def" SEPARATOR "bar");
		}
		else if (t.file == "slash_path3.torrent"_sv)
		{
			TEST_EQUAL(ti->num_files(), 1);
			TEST_EQUAL(ti->files().file_path(file_index_t{0}), "temp....abc");
		}
		else if (t.file == "symlink_zero_size.torrent"_sv)
		{
			TEST_EQUAL(ti->num_files(), 2);
			TEST_EQUAL(ti->files().symlink(file_index_t(1)), "temp" SEPARATOR "a" SEPARATOR "b" SEPARATOR "bar");
		}
		else if (t.file == "pad_file_no_path.torrent"_sv)
		{
			TEST_EQUAL(ti->num_files(), 2);
			TEST_EQUAL(ti->files().file_path(file_index_t{1}), combine_path(".pad", "0"));
		}
		else if (t.file == "absolute_filename.torrent"_sv)
		{
			TEST_EQUAL(ti->num_files(), 2);
			TEST_EQUAL(ti->files().file_path(file_index_t{0}), combine_path("temp", "abcde"));
			TEST_EQUAL(ti->files().file_path(file_index_t{1}), combine_path("temp", "foobar"));
		}
		else if (t.file == "invalid_filename.torrent"_sv)
		{
			TEST_EQUAL(ti->num_files(), 2);
		}
		else if (t.file == "invalid_filename2.torrent"_sv)
		{
			TEST_EQUAL(ti->num_files(), 3);
		}
		else if (t.file == "overlapping_symlinks.torrent"_sv)
		{
			TEST_CHECK(ti->num_files() > 3);
			TEST_EQUAL(ti->files().symlink(file_index_t{0}), "SDL2.framework" SEPARATOR "Versions" SEPARATOR "Current" SEPARATOR "Headers");
			TEST_EQUAL(ti->files().symlink(file_index_t{1}), "SDL2.framework" SEPARATOR "Versions" SEPARATOR "Current" SEPARATOR "Resources");
			TEST_EQUAL(ti->files().symlink(file_index_t{2}), "SDL2.framework" SEPARATOR "Versions" SEPARATOR "Current" SEPARATOR "SDL2");
		}

		file_storage const& fs = ti->files();
		for (file_index_t idx{0}; idx != file_index_t(fs.num_files()); ++idx)
		{
			piece_index_t const first = ti->map_file(idx, 0, 0).piece;
			piece_index_t const last = ti->map_file(idx, std::max(fs.file_size(idx)-1, std::int64_t(0)), 0).piece;
			file_flags_t const flags = fs.file_flags(idx);
			sha1_hash const ih = fs.hash(idx);
			std::printf("  %11" PRId64 " %c%c%c%c [ %4d, %4d ] %7u %s %s %s%s\n"
				, fs.file_size(idx)
				, (flags & file_storage::flag_pad_file)?'p':'-'
				, (flags & file_storage::flag_executable)?'x':'-'
				, (flags & file_storage::flag_hidden)?'h':'-'
				, (flags & file_storage::flag_symlink)?'l':'-'
				, static_cast<int>(first), static_cast<int>(last)
				, std::uint32_t(fs.mtime(idx))
				, ih != sha1_hash(nullptr) ? aux::to_hex(ih).c_str() : ""
				, fs.file_path(idx).c_str()
				, flags & file_storage::flag_symlink ? "-> ": ""
				, flags & file_storage::flag_symlink ? fs.symlink(idx).c_str() : "");
		}
	}

	for (int i = 0; i < int(sizeof(test_error_torrents)/sizeof(test_error_torrents[0])); ++i)
	{
		error_code ec;
		std::printf("loading %s\n", test_error_torrents[i].file);
		auto ti = std::make_shared<torrent_info>(combine_path(
			combine_path(root_dir, "test_torrents"), test_error_torrents[i].file), ec);
		std::printf("E:        \"%s\"\nexpected: \"%s\"\n", ec.message().c_str()
			, test_error_torrents[i].error.message().c_str());
		TEST_CHECK(ec.message() == test_error_torrents[i].error.message());
		TEST_EQUAL(ti->is_valid(), false);
	}
}

namespace {

struct file_t
{
	std::string filename;
	int size;
	file_flags_t flags;
	string_view expected_filename;
};

std::vector<lt::aux::vector<file_t, lt::file_index_t>> const test_cases
{
	{
		{"test/temporary.txt", 0x4000, {}, "test/temporary.txt"},
		{"test/Temporary.txt", 0x4000, {}, "test/Temporary.1.txt"},
		{"test/TeMPorArY.txT", 0x4000, {}, "test/TeMPorArY.2.txT"},
		// a file with the same name in a seprate directory is fine
		{"test/test/TEMPORARY.TXT", 0x4000, {}, "test/test/TEMPORARY.TXT"},
	},
	{
		{"test/b.exe", 0x4000, {}, "test/b.exe"},
		// duplicate of b.exe
		{"test/B.ExE", 0x4000, {}, "test/B.1.ExE"},
		// duplicate of b.exe
		{"test/B.exe", 0x4000, {}, "test/B.2.exe"},
		{"test/filler", 0x4000, {}, "test/filler"},
	},
	{
		{"test/a/b/c/d/e/f/g/h/i/j/k/l/m", 0x4000, {}, "test/a/b/c/d/e/f/g/h/i/j/k/l/m"},
		{"test/a", 0x4000, {}, "test/a.1"},
		{"test/a/b", 0x4000, {}, "test/a/b.1"},
		{"test/a/b/c", 0x4000, {}, "test/a/b/c.1"},
		{"test/a/b/c/d", 0x4000, {}, "test/a/b/c/d.1"},
		{"test/a/b/c/d/e", 0x4000, {}, "test/a/b/c/d/e.1"},
		{"test/a/b/c/d/e/f", 0x4000, {}, "test/a/b/c/d/e/f.1"},
		{"test/a/b/c/d/e/f/g", 0x4000, {}, "test/a/b/c/d/e/f/g.1"},
		{"test/a/b/c/d/e/f/g/h", 0x4000, {}, "test/a/b/c/d/e/f/g/h.1"},
		{"test/a/b/c/d/e/f/g/h/i", 0x4000, {}, "test/a/b/c/d/e/f/g/h/i.1"},
		{"test/a/b/c/d/e/f/g/h/i/j", 0x4000, {}, "test/a/b/c/d/e/f/g/h/i/j.1"},
	},
	{
		// it doesn't matter whether the file comes before the directory,
		// directories take precedence
		{"test/a", 0x4000, {}, "test/a.1"},
		{"test/a/b", 0x4000, {}, "test/a/b"},
	},
	{
		{"test/A/tmp", 0x4000, {}, "test/A/tmp"},
		// a file may not have the same name as a directory
		{"test/a", 0x4000, {}, "test/a.1"},
		// duplicate of directory a
		{"test/A", 0x4000, {}, "test/A.2"},
		{"test/filler", 0x4000, {}, "test/filler"},
	},
	{
		// a subset of this path collides with the next filename
		{"test/long/path/name/that/collides", 0x4000, {}, "test/long/path/name/that/collides"},
		// so this file needs to be renamed, to not collide with the path name
		{"test/long/path", 0x4000, {}, "test/long/path.1"},
		{"test/filler-1", 0x4000, {}, "test/filler-1"},
		{"test/filler-2", 0x4000, {}, "test/filler-2"},
	},
	{
		// pad files are allowed to collide, as long as they have the same size
		{"test/.pad/1234", 0x4000, file_storage::flag_pad_file, "test/.pad/1234"},
		{"test/filler-1", 0x4000, {}, "test/filler-1"},
		{"test/.pad/1234", 0x4000, file_storage::flag_pad_file, "test/.pad/1234"},
		{"test/filler-2", 0x4000, {}, "test/filler-2"},
	},
	{
		// pad files of different sizes are NOT allowed to collide
		{"test/.pad/1234", 0x8000, file_storage::flag_pad_file, "test/.pad/1234"},
		{"test/filler-1", 0x4000, {}, "test/filler-1"},
		{"test/.pad/1234", 0x4000, file_storage::flag_pad_file, "test/.pad/1234.1"},
		{"test/filler-2", 0x4000, {}, "test/filler-2"},
	},
	{
		// pad files are NOT allowed to collide with normal files
		{"test/.pad/1234", 0x4000, {}, "test/.pad/1234"},
		{"test/filler-1", 0x4000, {}, "test/filler-1"},
		{"test/.pad/1234", 0x4000, file_storage::flag_pad_file, "test/.pad/1234.1"},
		{"test/filler-2", 0x4000, {}, "test/filler-2"},
	},
	{
		// normal files are NOT allowed to collide with pad files
		{"test/.pad/1234", 0x4000, file_storage::flag_pad_file, "test/.pad/1234"},
		{"test/filler-1", 0x4000, {}, "test/filler-1"},
		{"test/.pad/1234", 0x4000, {}, "test/.pad/1234.1"},
		{"test/filler-2", 0x4000, {}, "test/filler-2"},
	},
	{
		// pad files are NOT allowed to collide with directories
		{"test/.pad/1234", 0x4000, file_storage::flag_pad_file, "test/.pad/1234.1"},
		{"test/filler-1", 0x4000, {}, "test/filler-1"},
		{"test/.pad/1234/filler-2", 0x4000, {}, "test/.pad/1234/filler-2"},
	},
};

void test_resolve_duplicates(aux::vector<file_t, file_index_t> const& test)
{
	file_storage fs;
	for (auto const& f : test) fs.add_file(f.filename, f.size, f.flags);

	lt::create_torrent t(fs, 0x4000);

	// calculate the hash for all pieces
	sha1_hash ph;
	for (auto const i : fs.piece_range())
		t.set_hash(i, ph);

	std::vector<char> tmp;
	std::back_insert_iterator<std::vector<char>> out(tmp);

	entry tor = t.generate();
	bencode(out, tor);

	torrent_info ti(tmp, from_span);
	for (auto const i : fs.file_range())
	{
		std::string p = ti.files().file_path(i);
		convert_path_to_posix(p);
		std::printf("%s == %s\n", p.c_str(), test[i].expected_filename.to_string().c_str());

		TEST_EQUAL(p, test[i].expected_filename);
	}
}

} // anonymous namespace

TORRENT_TEST(resolve_duplicates)
{
	for (auto const& t : test_cases)
		test_resolve_duplicates(t);
}

TORRENT_TEST(empty_file)
{
	error_code ec;
	auto ti = std::make_shared<torrent_info>("", ec, from_span);
	TEST_CHECK(ec);
}

TORRENT_TEST(empty_file2)
{
	try
	{
		auto ti = std::make_shared<torrent_info>("", from_span);
		TEST_ERROR("expected exception thrown");
	}
	catch (system_error const& e)
	{
		std::printf("Expected error: %s\n", e.code().message().c_str());
	}
}

TORRENT_TEST(copy)
{
	using namespace lt;

	std::shared_ptr<torrent_info> a = std::make_shared<torrent_info>(
		combine_path(parent_path(current_working_directory())
		, combine_path("test_torrents", "sample.torrent")));

	aux::vector<char const*, file_index_t> expected_files =
	{
		"sample/text_file2.txt",
		"sample/.____padding_file/0",
		"sample/text_file.txt",
	};

	aux::vector<sha1_hash, file_index_t> file_hashes =
	{
		sha1_hash(nullptr),
		sha1_hash(nullptr),
		sha1_hash("abababababababababab")
	};

	file_storage const& fs = a->files();
	for (auto const i : fs.file_range())
	{
		std::string p = fs.file_path(i);
		convert_path_to_posix(p);
		TEST_EQUAL(p, expected_files[i]);
		std::printf("%s\n", p.c_str());

		TEST_EQUAL(a->files().hash(i), file_hashes[i]);
	}

	// copy the torrent_info object
	std::shared_ptr<torrent_info> b = std::make_shared<torrent_info>(*a);

	// clear out the  buffer for a, just to make sure b doesn't have any
	// references into it by mistake
	int s = a->metadata_size();
	std::memset(a->metadata().get(), 0, std::size_t(s));

	a.reset();

	TEST_EQUAL(b->num_files(), 3);

	file_storage const& fs2 = b->files();
	for (auto const i : fs2.file_range())
	{
		std::string p = fs2.file_path(i);
		convert_path_to_posix(p);
		TEST_EQUAL(p, expected_files[i]);
		std::printf("%s\n", p.c_str());

		TEST_EQUAL(fs2.hash(i), file_hashes[i]);
	}
}

struct A
{
	int val;
};

TORRENT_TEST(copy_ptr)
{
	copy_ptr<A> a(new A{4});
	copy_ptr<A> b(a);

	TEST_EQUAL(a->val, b->val);
	TEST_CHECK(&*a != &*b);
	a->val = 5;
	TEST_EQUAL(b->val, 4);
}
