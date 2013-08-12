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
	std::string root_dir = parent_path(current_working_directory());
	for (int i = 0; i < sizeof(test_torrents)/sizeof(test_torrents[0]); ++i)
	{
		error_code ec;
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
			TEST_CHECK(ti->file_at(0).pad_file == false);
			TEST_CHECK(ti->file_at(1).pad_file == true);
		}
		else if (std::string(test_torrents[i].file) == "creation_date.torrent")
		{
			TEST_CHECK(*ti->creation_date() == 1234567);
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

		file_storage const& fs = ti->files();
		for (int i = 0; i < fs.num_files(); ++i)
		{
			int first = ti->map_file(i, 0, 0).piece;
			int last = ti->map_file(i, (std::max)(fs.file_size(i)-1, size_type(0)), 0).piece;
			int flags = fs.file_flags(i);
			fprintf(stderr, "  %11"PRId64" %c%c%c%c [ %4d, %4d ] %7u %s %s %s%s\n"
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
		fprintf(stderr, "E: %s\nexpected: %s\n", ec.message().c_str(), test_error_torrents[i].error.message().c_str());
		TEST_EQUAL(ec, test_error_torrents[i].error);
	}

	return 0;
}

