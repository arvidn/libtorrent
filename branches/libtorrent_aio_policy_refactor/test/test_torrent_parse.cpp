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
//	{ "" },
};

int test_main()
{
	for (int i = 0; i < sizeof(test_torrents)/sizeof(test_torrents[0]); ++i)
	{
		error_code ec;
		fprintf(stderr, "loading %s\n", test_torrents[i].file);
		boost::intrusive_ptr<torrent_info> ti(new torrent_info(combine_path("test_torrents", test_torrents[i].file), ec));
		TEST_CHECK(!ec);
		if (ec) fprintf(stderr, "  -> failed %s\n", ec.message().c_str());

		int index = 0;
		for (torrent_info::file_iterator i = ti->begin_files();
			i != ti->end_files(); ++i, ++index)
		{
			int first = ti->map_file(index, 0, 0).piece;
			int last = ti->map_file(index, (std::max)(size_type(i->size)-1, size_type(0)), 0).piece;
			fprintf(stderr, "  %11"PRId64" %c%c%c%c [ %4d, %4d ] %7u %s %s %s%s\n"
				, i->size
				, (i->pad_file?'p':'-')
				, (i->executable_attribute?'x':'-')
				, (i->hidden_attribute?'h':'-')
				, (i->symlink_attribute?'l':'-')
				, first, last
				, boost::uint32_t(ti->files().mtime(*i))
				, ti->files().hash(*i) != sha1_hash(0) ? to_hex(ti->files().hash(*i).to_string()).c_str() : ""
				, ti->files().file_path(*i).c_str()
				, i->symlink_attribute ? "-> ": ""
				, i->symlink_attribute && i->symlink_index != -1 ? ti->files().symlink(*i).c_str() : "");
		}
	}
	return 0;
}

