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
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/aux_/escape_string.hpp" // for convert_path_to_posix
#include <boost/make_shared.hpp>

#if TORRENT_USE_IOSTREAM
#include <sstream>
#endif

using namespace libtorrent;

#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS
TORRENT_TEST(mutable_torrents)
{
	file_storage fs;

	fs.add_file("test/temporary.txt", 0x4000);

	libtorrent::create_torrent t(fs, 0x4000);

	// calculate the hash for all pieces
	int num = t.num_pieces();
	sha1_hash ph;
	for (int i = 0; i < num; ++i)
		t.set_hash(i, ph);

	t.add_collection("collection1");
	t.add_collection("collection2");

	t.add_similar_torrent(sha1_hash("abababababababababab"));
	t.add_similar_torrent(sha1_hash("babababababababababa"));

	std::vector<char> tmp;
	std::back_insert_iterator<std::vector<char> > out(tmp);

	entry tor = t.generate();
	bencode(out, tor);

	torrent_info ti(&tmp[0], tmp.size());

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

struct test_torrent_t
{
	char const* file;
};

using namespace libtorrent;

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
	{ "url_seed_multi_space.torrent" },
	{ "url_seed_multi_space_nolist.torrent" },
	{ "root_hash.torrent" },
	{ "empty_path_multi.torrent" },
	{ "duplicate_web_seeds.torrent" },
	{ "invalid_name2.torrent" },
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
	{ "invalid_info.torrent", errors::torrent_missing_info },
	{ "string.torrent", errors::torrent_is_no_dict },
	{ "negative_size.torrent", errors::torrent_invalid_length },
	{ "negative_file_size.torrent", errors::torrent_invalid_length },
	{ "invalid_path_list.torrent", errors::torrent_missing_name},
	{ "missing_path_list.torrent", errors::torrent_missing_name },
	{ "invalid_pieces.torrent", errors::torrent_missing_pieces },
	{ "unaligned_pieces.torrent", errors::torrent_invalid_hashes },
	{ "invalid_root_hash.torrent", errors::torrent_invalid_hashes },
	{ "invalid_root_hash2.torrent", errors::torrent_missing_pieces },
	{ "invalid_file_size.torrent", errors::torrent_invalid_length },
};

// TODO: test remap_files
// TODO: merkle torrents. specifically torrent_info::add_merkle_nodes and torrent with "root hash"
// TODO: torrent with 'p' (padfile) attribute
// TODO: torrent with 'h' (hidden) attribute
// TODO: torrent with 'x' (executable) attribute
// TODO: torrent with 'l' (symlink) attribute
// TODO: creating a merkle torrent (torrent_info::build_merkle_list)
// TODO: torrent with multiple trackers in multiple tiers, making sure we shuffle them (how do you test shuffling?, load it multiple times and make sure it's in different order at least once)
// TODO: torrents with a missing name
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
// TODO: torrent_info::unload
// TODO: torrent_info constructor that takes an invalid bencoded buffer
// TODO: verify_encoding with a string that triggers character replacement

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
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_", 250);
	sanitize_append_path_element(path,
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcde.test", 250);
	TEST_EQUAL(path,
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_" SEPARATOR
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_.test");
}

TORRENT_TEST(sanitize_path_trailing_dots)
{
	std::string path;
	sanitize_append_path_element(path, "a", 1);
	sanitize_append_path_element(path, "abc...", 6);
	sanitize_append_path_element(path, "c", 1);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(path, "a" SEPARATOR "abc" SEPARATOR "c");
#else
	TEST_EQUAL(path, "a" SEPARATOR "abc..." SEPARATOR "c");
#endif

	path.clear();
	sanitize_append_path_element(path, "abc...", 6);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(path, "abc");
#else
	TEST_EQUAL(path, "abc...");
#endif

	path.clear();
	sanitize_append_path_element(path, "abc.", 4);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(path, "abc");
#else
	TEST_EQUAL(path, "abc.");
#endif


	path.clear();
	sanitize_append_path_element(path, "a. . .", 6);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(path, "a");
#else
	TEST_EQUAL(path, "a. . .");
#endif
}

TORRENT_TEST(sanitize_path_trailing_spaces)
{
	std::string path;
	sanitize_append_path_element(path, "a", 1);
	sanitize_append_path_element(path, "abc   ", 6);
	sanitize_append_path_element(path, "c", 1);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(path, "a" SEPARATOR "abc" SEPARATOR "c");
#else
	TEST_EQUAL(path, "a" SEPARATOR "abc   " SEPARATOR "c");
#endif

	path.clear();
	sanitize_append_path_element(path, "abc   ", 6);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(path, "abc");
#else
	TEST_EQUAL(path, "abc   ");
#endif

	path.clear();
	sanitize_append_path_element(path, "abc ", 4);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(path, "abc");
#else
	TEST_EQUAL(path, "abc ");
#endif
}

TORRENT_TEST(sanitize_path)
{
	std::string path;
	sanitize_append_path_element(path, "/a/", 3);
	sanitize_append_path_element(path, "b", 1);
	sanitize_append_path_element(path, "c", 1);
	TEST_EQUAL(path, "a" SEPARATOR "b" SEPARATOR "c");

	path.clear();
	sanitize_append_path_element(path, "a...b", 5);
	TEST_EQUAL(path, "a...b");

	path.clear();
	sanitize_append_path_element(path, "a", 1);
	sanitize_append_path_element(path, "..", 2);
	sanitize_append_path_element(path, "c", 1);
	TEST_EQUAL(path, "a" SEPARATOR "c");

	path.clear();
	sanitize_append_path_element(path, "a", 1);
	sanitize_append_path_element(path, "..", 2);
	TEST_EQUAL(path, "a");

	path.clear();
	sanitize_append_path_element(path, "/..", 3);
	sanitize_append_path_element(path, ".", 1);
	sanitize_append_path_element(path, "c", 1);
	TEST_EQUAL(path, "c");

	path.clear();
	sanitize_append_path_element(path, "dev:", 4);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(path, "dev");
#else
	TEST_EQUAL(path, "dev:");
#endif

	path.clear();
	sanitize_append_path_element(path, "c:", 2);
	sanitize_append_path_element(path, "b", 1);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(path, "c" SEPARATOR "b");
#else
	TEST_EQUAL(path, "c:" SEPARATOR "b");
#endif

	path.clear();
	sanitize_append_path_element(path, "c:", 2);
	sanitize_append_path_element(path, ".", 1);
	sanitize_append_path_element(path, "c", 1);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(path, "c" SEPARATOR "c");
#else
	TEST_EQUAL(path, "c:" SEPARATOR "c");
#endif

	path.clear();
	sanitize_append_path_element(path, "\\c", 2);
	sanitize_append_path_element(path, ".", 1);
	sanitize_append_path_element(path, "c", 1);
	TEST_EQUAL(path, "c" SEPARATOR "c");

	path.clear();
	sanitize_append_path_element(path, "\b", 1);
	TEST_EQUAL(path, "_");

	path.clear();
	sanitize_append_path_element(path, "\b", 1);
	sanitize_append_path_element(path, "filename", 8);
	TEST_EQUAL(path, "_" SEPARATOR "filename");

	path.clear();
	sanitize_append_path_element(path, "filename", 8);
	sanitize_append_path_element(path, "\b", 1);
	TEST_EQUAL(path, "filename" SEPARATOR "_");

	path.clear();
	sanitize_append_path_element(path, "abc", 3);
	sanitize_append_path_element(path, "", 0);
	TEST_EQUAL(path, "abc" SEPARATOR "_");

	path.clear();
	sanitize_append_path_element(path, "abc", 3);
	sanitize_append_path_element(path, "   ", 3);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(path, "abc");
#else
	TEST_EQUAL(path, "abc" SEPARATOR "   ");
#endif

	path.clear();
	sanitize_append_path_element(path, "", 0);
	sanitize_append_path_element(path, "abc", 3);
	TEST_EQUAL(path, "_" SEPARATOR "abc");

	path.clear();
	sanitize_append_path_element(path, "\b?filename=4", 12);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(path, "__filename=4");
#else
	TEST_EQUAL(path, "_?filename=4");
#endif

	path.clear();
	sanitize_append_path_element(path, "filename=4", 10);
	TEST_EQUAL(path, "filename=4");

	// valid 2-byte sequence
	path.clear();
	sanitize_append_path_element(path, "filename\xc2\xa1", 10);
	TEST_EQUAL(path, "filename\xc2\xa1");

	// truncated 2-byte sequence
	path.clear();
	sanitize_append_path_element(path, "filename\xc2", 9);
	TEST_EQUAL(path, "filename_");

	// valid 3-byte sequence
	path.clear();
	sanitize_append_path_element(path, "filename\xe2\x9f\xb9", 11);
	TEST_EQUAL(path, "filename\xe2\x9f\xb9");

	// truncated 3-byte sequence
	path.clear();
	sanitize_append_path_element(path, "filename\xe2\x9f", 10);
	TEST_EQUAL(path, "filename_");

	// truncated 3-byte sequence
	path.clear();
	sanitize_append_path_element(path, "filename\xe2", 9);
	TEST_EQUAL(path, "filename_");

	// valid 4-byte sequence
	path.clear();
	sanitize_append_path_element(path, "filename\xf0\x9f\x92\x88", 12);
	TEST_EQUAL(path, "filename\xf0\x9f\x92\x88");

	// truncated 4-byte sequence
	path.clear();
	sanitize_append_path_element(path, "filename\xf0\x9f\x92", 11);
	TEST_EQUAL(path, "filename_");

	// 5-byte utf-8 sequence (not allowed)
	path.clear();
	sanitize_append_path_element(path, "filename\xf8\x9f\x9f\x9f\x9f" "foobar", 19);
	TEST_EQUAL(path, "filename_____foobar");

	// redundant (overlong) 2-byte sequence
	// ascii code 0x2e encoded with a leading 0
	path.clear();
	sanitize_append_path_element(path, "filename\xc0\xae", 10);
	TEST_EQUAL(path, "filename_");

	// redundant (overlong) 3-byte sequence
	// ascii code 0x2e encoded with two leading 0s
	path.clear();
	sanitize_append_path_element(path, "filename\xe0\x80\xae", 11);
	TEST_EQUAL(path, "filename_");

	// redundant (overlong) 4-byte sequence
	// ascii code 0x2e encoded with three leading 0s
	path.clear();
	sanitize_append_path_element(path, "filename\xf0\x80\x80\xae", 12);
	TEST_EQUAL(path, "filename_");
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

	// redundant (overlong) 2-byte sequence
	// ascii code 0x2e encoded with a leading 0
	test = "filename\xc0\xae";
	TEST_CHECK(!verify_encoding(test));
	fprintf(stderr, "%s\n", test.c_str());
	TEST_CHECK(test == "filename__");

	// redundant (overlong) 3-byte sequence
	// ascii code 0x2e encoded with two leading 0s
	test = "filename\xe0\x80\xae";
	TEST_CHECK(!verify_encoding(test));
	fprintf(stderr, "%s\n", test.c_str());
	TEST_CHECK(test == "filename___");

	// redundant (overlong) 4-byte sequence
	// ascii code 0x2e encoded with three leading 0s
	test = "filename\xf0\x80\x80\xae";
	TEST_CHECK(!verify_encoding(test));
	fprintf(stderr, "%s\n", test.c_str());
	TEST_CHECK(test == "filename____");
}

TORRENT_TEST(parse_torrents)
{
	error_code ec;

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
	TEST_EQUAL(ti2.name(), "ctest1test2test3");
#else
	TEST_EQUAL(ti2.name(), "test1test2test3");
#endif

	info["name.utf-8"] = "test2/../test3/.././../../test4";
	torrent["info"] = info;
	buf.clear();
	bencode(std::back_inserter(buf), torrent);
	torrent_info ti3(&buf[0], buf.size(), ec);
	std::cerr << ti3.name() << std::endl;
	TEST_EQUAL(ti3.name(), "test2..test3.......test4");

	std::string root_dir = parent_path(current_working_directory());
	for (int i = 0; i < int(sizeof(test_torrents)/sizeof(test_torrents[0])); ++i)
	{
		fprintf(stderr, "loading %s\n", test_torrents[i].file);
		std::string filename = combine_path(combine_path(root_dir, "test_torrents")
			, test_torrents[i].file);
		boost::shared_ptr<torrent_info> ti(new torrent_info(filename, ec));
		TEST_CHECK(!ec);
		if (ec) fprintf(stderr, " loading(\"%s\") -> failed %s\n", filename.c_str()
			, ec.message().c_str());

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
			TEST_CHECK(ti->files().file_path(0) == combine_path(combine_path("temp", "foo"), "bar.txt"));
			TEST_CHECK(ti->files().file_path(1) == combine_path(combine_path("temp", "foo"), "bar.1.txt"));
		}
		else if (std::string(test_torrents[i].file) == "pad_file.torrent")
		{
			TEST_EQUAL(ti->num_files(), 2);
			TEST_EQUAL(ti->files().file_flags(0) & file_storage::flag_pad_file, false);
			TEST_EQUAL(ti->files().file_flags(1) & file_storage::flag_pad_file, true);
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
		else if (std::string(test_torrents[i].file) == "invalid_name2.torrent")
		{
			// if, after all invalid characters are removed from the name, it ends up
			// being empty, it's set to the info-hash. Some torrents also have an empty name
			// in which case it's also set to the info-hash
			TEST_EQUAL(ti->name(), "b61560c2918f463768cd122b6d2fdd47b77bdb35");
		}
		else if (std::string(test_torrents[i].file) == "invalid_name3.torrent")
		{
			// windows does not allow trailing spaces in filenames
#ifdef TORRENT_WINDOWS
			TEST_EQUAL(ti->name(), "foobar");
#else
			TEST_EQUAL(ti->name(), "foobar ");
#endif
		}
		else if (std::string(test_torrents[i].file) == "slash_path.torrent")
		{
			TEST_EQUAL(ti->num_files(), 1);
			TEST_EQUAL(ti->files().file_path(0), "temp" SEPARATOR "bar");
		}
		else if (std::string(test_torrents[i].file) == "slash_path2.torrent")
		{
			TEST_EQUAL(ti->num_files(), 1);
			TEST_EQUAL(ti->files().file_path(0), "temp" SEPARATOR "abc....def" SEPARATOR "bar");
		}
		else if (std::string(test_torrents[i].file) == "slash_path3.torrent")
		{
			TEST_EQUAL(ti->num_files(), 1);
			TEST_EQUAL(ti->files().file_path(0), "temp....abc");
		}

		file_storage const& fs = ti->files();
		for (int i = 0; i < fs.num_files(); ++i)
		{
			int first = ti->map_file(i, 0, 0).piece;
			int last = ti->map_file(i, (std::max)(fs.file_size(i)-1, boost::int64_t(0)), 0).piece;
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

	for (int i = 0; i < int(sizeof(test_error_torrents)/sizeof(test_error_torrents[0])); ++i)
	{
		error_code ec;
		fprintf(stderr, "loading %s\n", test_error_torrents[i].file);
		boost::shared_ptr<torrent_info> ti(new torrent_info(combine_path(
			combine_path(root_dir, "test_torrents"), test_error_torrents[i].file), ec));
		fprintf(stderr, "E:        \"%s\"\nexpected: \"%s\"\n", ec.message().c_str()
			, test_error_torrents[i].error.message().c_str());
		TEST_CHECK(ec.message() == test_error_torrents[i].error.message());
	}
}

void test_resolve_duplicates(int test_case)
{
	file_storage fs;

	switch (test_case)
	{
		case 0:
			fs.add_file("test/temporary.txt", 0x4000);
			fs.add_file("test/Temporary.txt", 0x4000);
			fs.add_file("test/TeMPorArY.txT", 0x4000);
			fs.add_file("test/test/TEMPORARY.TXT", 0x4000);
			break;
		case 1:
			fs.add_file("test/b.exe", 0x4000);
			fs.add_file("test/B.ExE", 0x4000);
			fs.add_file("test/B.exe", 0x4000);
			fs.add_file("test/filler", 0x4000);
			break;
		case 2:
			fs.add_file("test/A/tmp", 0x4000);
			fs.add_file("test/a", 0x4000);
			fs.add_file("test/A", 0x4000);
			fs.add_file("test/filler", 0x4000);
			break;
		case 3:
			fs.add_file("test/long/path/name/that/collides", 0x4000);
			fs.add_file("test/long/path", 0x4000);
			fs.add_file("test/filler-1", 0x4000);
			fs.add_file("test/filler-2", 0x4000);
			break;
	}

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

	char const* filenames[4][4] =
	{
		{ // case 0
			"test/temporary.txt",
			"test/Temporary.1.txt", // duplicate of temporary.txt
			"test/TeMPorArY.2.txT", // duplicate of temporary.txt
			// a file with the same name in a seprate directory is fine
			"test/test/TEMPORARY.TXT",
		},
		{ // case 1
			"test/b.exe",
			"test/B.1.ExE", // duplicate of b.exe
			"test/B.2.exe", // duplicate of b.exe
			"test/filler",
		},
		{ // case 2
			"test/A/tmp",
			"test/a.1", // a file may not have the same name as a directory
			"test/A.2", // duplicate of directory a
			"test/filler",
		},
		{ // case 3
			// a subset of this path collides with the next filename
			"test/long/path/name/that/collides",
			// so this file needs to be renamed, to not collide with the path name
			"test/long/path.1",
			"test/filler-1",
			"test/filler-2",
		}
	};

	for (int i = 0; i < ti.num_files(); ++i)
	{
		std::string p = ti.files().file_path(i);
		convert_path_to_posix(p);
		fprintf(stderr, "%s == %s\n", p.c_str(), filenames[test_case][i]);

		TEST_EQUAL(p, filenames[test_case][i]);
	}
}

TORRENT_TEST(resolve_duplicates)
{
	for (int i = 0; i < 4; ++i)
		test_resolve_duplicates(i);
}

TORRENT_TEST(copy)
{
	using namespace libtorrent;

	boost::shared_ptr<torrent_info> a(boost::make_shared<torrent_info>(
		combine_path(parent_path(current_working_directory())
		, combine_path("test_torrents", "sample.torrent"))));

	char const* expected_files[] =
	{
		"sample/text_file2.txt",
		"sample/.____padding_file/0",
		"sample/text_file.txt",
	};

	sha1_hash file_hashes[] =
	{
		sha1_hash(0),
		sha1_hash(0),
		sha1_hash("abababababababababab")
	};

	for (int i = 0; i < a->num_files(); ++i)
	{
		std::string p = a->files().file_path(i);
		convert_path_to_posix(p);
		TEST_EQUAL(p, expected_files[i]);
		fprintf(stderr, "%s\n", p.c_str());

		TEST_EQUAL(a->files().hash(i), file_hashes[i]);
	}

	// copy the torrent_info object
	boost::shared_ptr<torrent_info> b(boost::make_shared<torrent_info>(*a));

	// clear out the  buffer for a, just to make sure b doesn't have any
	// references into it by mistake
	int s = a->metadata_size();
	memset(a->metadata().get(), 0, s);

	a.reset();

	TEST_EQUAL(b->num_files(), 3);

	for (int i = 0; i < b->num_files(); ++i)
	{
		std::string p = b->files().file_path(i);
		convert_path_to_posix(p);
		TEST_EQUAL(p, expected_files[i]);
		fprintf(stderr, "%s\n", p.c_str());

		TEST_EQUAL(b->files().hash(i), file_hashes[i]);
	}
}


