/*

Copyright (c) 2003, Arvid Norberg
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

#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/lazy_entry.hpp"
#include "libtorrent/magnet_uri.hpp"

int main(int argc, char* argv[])
{
	using namespace libtorrent;

	if (argc < 2 || argc > 4)
	{
		fputs("usage: dump_torrent torrent-file [total-items-limit] [recursion-limit]\n", stderr);
		return 1;
	}

	int item_limit = 1000000;
	int depth_limit = 1000;

	if (argc > 2) item_limit = atoi(argv[2]);
	if (argc > 3) depth_limit = atoi(argv[3]);

	int size = file_size(argv[1]);
	if (size > 40 * 1000000)
	{
		fprintf(stderr, "file too big (%d), aborting\n", size);
		return 1;
	}
	std::vector<char> buf(size);
	error_code ec;
	int ret = load_file(argv[1], buf, ec, 40 * 1000000);
	if (ret != 0)
	{
		fprintf(stderr, "failed to load file: %s\n", ec.message().c_str());
		return 1;
	}
	lazy_entry e;
	int pos;
	printf("decoding. recursion limit: %d total item count limit: %d\n"
		, depth_limit, item_limit);
	ret = lazy_bdecode(&buf[0], &buf[0] + buf.size(), e, ec, &pos
		, depth_limit, item_limit);

	printf("\n\n----- raw info -----\n\n%s\n", print_entry(e).c_str());

	if (ret != 0)
	{
		fprintf(stderr, "failed to decode: '%s' at character: %d\n", ec.message().c_str(), pos);
		return 1;
	}

	torrent_info t(e, ec);
	if (ec)
	{
		fprintf(stderr, "%s\n", ec.message().c_str());
		return 1;
	}
	e.clear();
	std::vector<char>().swap(buf);

	// print info about torrent
	printf("\n\n----- torrent file info -----\n\n"
		"nodes:\n");

	typedef std::vector<std::pair<std::string, int> > node_vec;
	node_vec const& nodes = t.nodes();
	for (node_vec::const_iterator i = nodes.begin(), end(nodes.end());
		i != end; ++i)
	{
		printf("%s: %d\n", i->first.c_str(), i->second);
	}
	puts("trackers:\n");
	for (std::vector<announce_entry>::const_iterator i = t.trackers().begin();
		i != t.trackers().end(); ++i)
	{
		printf("%2d: %s\n", i->tier, i->url.c_str());
	}

	char ih[41];
	to_hex((char const*)&t.info_hash()[0], 20, ih);
	printf("number of pieces: %d\n"
		"piece length: %d\n"
		"info hash: %s\n"
		"comment: %s\n"
		"created by: %s\n"
		"magnet link: %s\n"
		"name: %s\n"
		"number of files: %d\n"
		"files:\n"
		, t.num_pieces()
		, t.piece_length()
		, ih
		, t.comment().c_str()
		, t.creator().c_str()
		, make_magnet_uri(t).c_str()
		, t.name().c_str()
		, t.num_files());
	int index = 0;
	for (torrent_info::file_iterator i = t.begin_files();
		i != t.end_files(); ++i, ++index)
	{
		int first = t.map_file(index, 0, 0).piece;
		int last = t.map_file(index, (std::max)(size_type(i->size)-1, size_type(0)), 0).piece;
		printf("  %11" PRId64 " %c%c%c%c [ %4d, %4d ] %7u %s %s %s%s\n"
			, i->size
			, (i->pad_file?'p':'-')
			, (i->executable_attribute?'x':'-')
			, (i->hidden_attribute?'h':'-')
			, (i->symlink_attribute?'l':'-')
			, first, last
			, boost::uint32_t(t.files().mtime(*i))
			, t.files().hash(*i) != sha1_hash(0) ? to_hex(t.files().hash(*i).to_string()).c_str() : ""
			, t.files().file_path(*i).c_str()
			, i->symlink_attribute ? "-> ": ""
			, i->symlink_attribute && i->symlink_index != -1 ? t.files().symlink(*i).c_str() : "");
	}

	return 0;
}

