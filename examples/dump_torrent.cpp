/*

Copyright (c) 2003-2017, Arvid Norberg
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

#include <cstdio> // for snprintf
#include <cinttypes> // for PRId64 et.al.

#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/bdecode.hpp"
#include "libtorrent/magnet_uri.hpp"

#include <fstream>
#include <iostream>

std::vector<char> load_file(std::string const& filename)
{
	std::fstream in;
	in.exceptions(std::ifstream::failbit);
	in.open(filename.c_str(), std::ios_base::in | std::ios_base::binary);
	in.seekg(0, std::ios_base::end);
	size_t const size = size_t(in.tellg());
	in.seekg(0, std::ios_base::beg);
	std::vector<char> ret(size);
	in.read(ret.data(), size);
	return ret;
}

int main(int argc, char* argv[]) try
{
	if (argc < 2 || argc > 5) {
		std::cerr << "usage: dump_torrent torrent-file [total-items-limit] [recursion-limit] [piece-count-limit]\n";
		return 1;
	}

	lt::load_torrent_limits cfg;

	if (argc > 2) cfg.max_decode_tokens = atoi(argv[2]);
	if (argc > 3) cfg.max_decode_depth = atoi(argv[3]);
	if (argc > 4) cfg.max_pieces = atoi(argv[4]);

	std::vector<char> buf = load_file(argv[1]);
	int pos = -1;
	lt::error_code ec;
	std::cout << "decoding. recursion limit: " << cfg.max_decode_depth
		<< " total item count limit: " << cfg.max_decode_tokens << "\n";
	lt::bdecode_node const e = lt::bdecode(buf, ec, &pos, cfg.max_decode_depth
		, cfg.max_decode_tokens);

	std::printf("\n\n----- raw info -----\n\n%s\n", print_entry(e).c_str());

	if (ec) {
		std::cerr << "failed to decode: '" << ec.message() << "' at character: " << pos<< "\n";
		return 1;
	}

	lt::torrent_info const t(std::move(e), cfg);
	buf.clear();

	// print info about torrent
	std::printf("\n\n----- torrent file info -----\n\nnodes:\n");
	for (auto const& i : t.nodes())
		std::printf("%s: %d\n", i.first.c_str(), i.second);

	puts("trackers:\n");
	for (auto const& i : t.trackers())
		std::printf("%2d: %s\n", i.tier, i.url.c_str());

	std::stringstream ih;
	ih << t.info_hash();
	std::printf("number of pieces: %d\n"
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
		, ih.str().c_str()
		, t.comment().c_str()
		, t.creator().c_str()
		, make_magnet_uri(t).c_str()
		, t.name().c_str()
		, t.num_files());
	lt::file_storage const& st = t.files();
	for (auto const i : st.file_range())
	{
		auto const first = st.map_file(i, 0, 0).piece;
		auto const last = st.map_file(i, std::max(std::int64_t(st.file_size(i)) - 1, std::int64_t(0)), 0).piece;
		auto const flags = st.file_flags(i);
		std::stringstream file_hash;
		if (!st.hash(i).is_all_zeros())
			file_hash << st.hash(i);
		std::printf(" %8" PRIx64 " %11" PRId64 " %c%c%c%c [ %5d, %5d ] %7u %s %s %s%s\n"
			, st.file_offset(i)
			, st.file_size(i)
			, ((flags & lt::file_storage::flag_pad_file)?'p':'-')
			, ((flags & lt::file_storage::flag_executable)?'x':'-')
			, ((flags & lt::file_storage::flag_hidden)?'h':'-')
			, ((flags & lt::file_storage::flag_symlink)?'l':'-')
			, static_cast<int>(first)
			, static_cast<int>(last)
			, std::uint32_t(st.mtime(i))
			, file_hash.str().c_str()
			, st.file_path(i).c_str()
			, (flags & lt::file_storage::flag_symlink) ? "-> " : ""
			, (flags & lt::file_storage::flag_symlink) ? st.symlink(i).c_str() : "");
	}
	std::printf("web seeds:\n");
	for (auto const& ws : t.web_seeds())
	{
		std::printf("%s %s\n"
			, ws.type == lt::web_seed_entry::url_seed ? "BEP19" : "BEP17"
			, ws.url.c_str());
	}

	return 0;
}
catch (std::exception const& e)
{
	std::cerr << "ERROR: " << e.what() << "\n";
}
