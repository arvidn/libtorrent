/*

Copyright (c) 2014, Arvid Norberg
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
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/resolve_links.hpp"
#include "libtorrent/file.hpp" // for combine_path
#include <boost/make_shared.hpp>
#include <boost/bind.hpp>

using namespace libtorrent;

struct test_torrent_t
{
	char const* filename1;
	char const* filename2;
	int expected_matches;
};

test_torrent_t test_torrents[] = {
	// no match because shared file in test2 and test3 is not padded/aligned
	{ "test2", "test1_pad_files", 0},
	{ "test3", "test1_pad_files", 0},

	// in this case, test1 happens to have the shared file as the first one,
	// which makes it padded, however, the tail of it isn't padded, so it
	// still overlaps with the next file
	{ "test1", "test1_pad_files", 0},

	// test2 and test3 don't have the shared file aligned
	{ "test2", "test1_pad_files", 0},
	{ "test3", "test1_pad_files", 0},
	{ "test2", "test1_single", 0},

	// these are all padded. The first small file will accidentally also
	// match, even though it's not tail padded, the following file is identical
	{ "test2_pad_files", "test1_pad_files", 2},
	{ "test3_pad_files", "test1_pad_files", 2},
	{ "test3_pad_files", "test2_pad_files", 2},
	{ "test1_pad_files", "test2_pad_files", 2},
	{ "test1_pad_files", "test3_pad_files", 2},
	{ "test2_pad_files", "test3_pad_files", 2},

	// one might expect this to work, but since the tail of the single file
	// torrent is not padded, the last piece hash won't match
	{ "test1_pad_files", "test1_single", 0},

	// if it's padded on the other hand, it will work
	{ "test1_pad_files", "test1_single_padded", 1},

	// TODO: test files with different piece size (negative test)
};

// TODO: it would be nice to test resolving of more than just 2 files as well.
// like 3 single file torrents merged into one, resolving all 3 files.

int test_main()
{

#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS
	std::string path
		= combine_path("..", "mutable_test_torrents");

	for (int i = 0; i < sizeof(test_torrents)/sizeof(test_torrents[0]); ++i)
	{
		test_torrent_t const& e = test_torrents[i];

		std::string p = combine_path(path, e.filename1) + ".torrent";
		fprintf(stderr, "loading %s\n", p.c_str());
		boost::shared_ptr<torrent_info> ti1 = boost::make_shared<torrent_info>(p);

		p = combine_path(path, e.filename2) + ".torrent";
		fprintf(stderr, "loading %s\n", p.c_str());
		boost::shared_ptr<torrent_info> ti2 = boost::make_shared<torrent_info>(p);

		fprintf(stderr, "resolving\n");
		resolve_links l(ti1);
		l.match(ti2, ".");

		std::vector<resolve_links::link_t> const& links = l.get_links();

		int num_matches = std::count_if(links.begin(), links.end()
			, boost::bind(&resolve_links::link_t::ti, _1));

		// some debug output in case the test fails
		if (num_matches > e.expected_matches)
		{
			file_storage const& fs = ti1->files();
			for (int i = 0; i < links.size(); ++i)
			{
				TORRENT_ASSERT(i < fs.num_files());
				fprintf(stderr, "%s --> %s : %d\n", fs.file_name(i).c_str()
					, links[i].ti ? to_hex(links[i].ti->info_hash()
						.to_string()).c_str() : "", links[i].file_idx);
			}
		}

		TEST_EQUAL(num_matches, e.expected_matches);

	}
#endif // TORRENT_DISABLE_MUTABLE_TORRENTS
	return 0;
}

