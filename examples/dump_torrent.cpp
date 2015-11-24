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
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/bdecode.hpp"
#include "libtorrent/magnet_uri.hpp"

int load_file(std::string const& filename, std::vector<char>& v
	, libtorrent::error_code& ec, int limit = 8000000)
{
	ec.clear();
	FILE* f = fopen(filename.c_str(), "rb");
	if (f == NULL)
	{
		ec.assign(errno, boost::system::system_category());
		return -1;
	}

	int r = fseek(f, 0, SEEK_END);
	if (r != 0)
	{
		ec.assign(errno, boost::system::system_category());
		fclose(f);
		return -1;
	}
	long s = ftell(f);
	if (s < 0)
	{
		ec.assign(errno, boost::system::system_category());
		fclose(f);
		return -1;
	}

	if (s > limit)
	{
		fclose(f);
		return -2;
	}

	r = fseek(f, 0, SEEK_SET);
	if (r != 0)
	{
		ec.assign(errno, boost::system::system_category());
		fclose(f);
		return -1;
	}

	v.resize(s);
	if (s == 0)
	{
		fclose(f);
		return 0;
	}

	r = fread(&v[0], 1, v.size(), f);
	if (r < 0)
	{
		ec.assign(errno, boost::system::system_category());
		fclose(f);
		return -1;
	}

	fclose(f);

	if (r != s) return -3;

	return 0;
}

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

	std::vector<char> buf;
	error_code ec;
	int ret = load_file(argv[1], buf, ec, 40 * 1000000);
	if (ret == -1)
	{
		fprintf(stderr, "file too big, aborting\n");
		return 1;
	}

	if (ret != 0)
	{
		fprintf(stderr, "failed to load file: %s\n", ec.message().c_str());
		return 1;
	}
	bdecode_node e;
	int pos = -1;
	printf("decoding. recursion limit: %d total item count limit: %d\n"
		, depth_limit, item_limit);
	ret = bdecode(&buf[0], &buf[0] + buf.size(), e, ec, &pos
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
	file_storage const& st = t.files();
	for (int i = 0; i < st.num_files(); ++i)
	{
		int first = st.map_file(i, 0, 0).piece;
		int last = st.map_file(i, (std::max)(boost::int64_t(st.file_size(i))-1, boost::int64_t(0)), 0).piece;
		int flags = st.file_flags(i);
		printf(" %8" PRIx64 " %11" PRId64 " %c%c%c%c [ %5d, %5d ] %7u %s %s %s%s\n"
			, st.file_offset(i)
			, st.file_size(i)
			, ((flags & file_storage::flag_pad_file)?'p':'-')
			, ((flags & file_storage::flag_executable)?'x':'-')
			, ((flags & file_storage::flag_hidden)?'h':'-')
			, ((flags & file_storage::flag_symlink)?'l':'-')
			, first, last
			, boost::uint32_t(st.mtime(i))
			, st.hash(i) != sha1_hash(0) ? to_hex(st.hash(i).to_string()).c_str() : ""
			, st.file_path(i).c_str()
			, (flags & file_storage::flag_symlink) ? "-> " : ""
			, (flags & file_storage::flag_symlink) ? st.symlink(i).c_str() : "");
	}

	return 0;
}

