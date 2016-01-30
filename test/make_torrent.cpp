/*

Copyright (c) 2016, Arvid Norberg
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

#include <boost/make_shared.hpp>
#include <deque>

#include "make_torrent.hpp"
#include "libtorrent/storage.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/file_pool.hpp"
#include "libtorrent/storage_defs.hpp"

using namespace libtorrent;

boost::shared_ptr<libtorrent::torrent_info> make_test_torrent(
	torrent_args const& args)
{
	entry e;

	entry::dictionary_type& info = e["info"].dict();
	int total_size = 0;

	if (args.m_priv)
	{
		info["priv"] = 1;
	}

	// torrent offset ranges where the pad files are
	// used when generating hashes
	std::deque<std::pair<int,int> > pad_files;

	int const piece_length = 32768;
	info["piece length"] = piece_length;

	if (args.m_files.size() == 1)
	{
		std::string const& ent = args.m_files[0];
		std::string name = "test_file-1";
		if (ent.find("name=") != std::string::npos)
		{
			int pos = ent.find("name=") + 5;
			name = ent.substr(pos, ent.find(',', pos));
		}
		info["name"] = name;
		int file_size = atoi(args.m_files[0].c_str());
		info["length"] = file_size;
		total_size = file_size;
	}
	else
	{
		info["name"] = args.m_name;

		entry::list_type& files = info["files"].list();
		for (int i = 0; i < int(args.m_files.size()); ++i)
		{
			int file_size = atoi(args.m_files[i].c_str());

			files.push_back(entry());
			entry::dictionary_type& file_entry = files.back().dict();
			std::string const& ent = args.m_files[i];
			if (ent.find("padfile") != std::string::npos)
			{
				file_entry["attr"].string() += "p";
				pad_files.push_back(std::make_pair(total_size, total_size + file_size));
			}
			if (ent.find("executable") != std::string::npos)
				file_entry["attr"].string() += "x";

			char filename[100];
			snprintf(filename, sizeof(filename), "test_file-%d", i);

			std::string name = filename;
			if (ent.find("name=") != std::string::npos)
			{
				int pos = ent.find("name=") + 5;
				name = ent.substr(pos, ent.find(',', pos));
			}
			file_entry["path"].list().push_back(name);
			file_entry["length"] = file_size;
			total_size += file_size;
		}
	}

	if (!args.m_url_seed.empty())
	{
		e["url-list"] = args.m_url_seed;
	}

	if (!args.m_http_seed.empty())
	{
		e["httpseeds"] = args.m_http_seed;
	}

	std::string piece_hashes;

	int num_pieces = (total_size + piece_length - 1) / piece_length;
	int torrent_offset = 0;
	for (int i = 0; i < num_pieces; ++i)
	{
		hasher h;
		int const piece_size = (i < num_pieces - 1) ? piece_length : total_size - (num_pieces - 1) * piece_length;

		char const data = i;
		char const zero = 0;
		for (int o = 0; o < piece_size; ++o, ++torrent_offset)
		{
			while (!pad_files.empty() && torrent_offset >= pad_files.front().second)
				pad_files.pop_front();

			if (!pad_files.empty() && torrent_offset >= pad_files.front().first)
			{
				h.update(&zero, 1);
			}
			else
			{
				h.update(&data, 1);
			}
		}
		piece_hashes += h.final().to_string();
	}

	info["pieces"] = piece_hashes;

	std::vector<char> tmp;
	std::back_insert_iterator<std::vector<char> > out(tmp);
	bencode(out, e);

	FILE* f = fopen("test.torrent", "w+");
	fwrite(&tmp[0], 1, tmp.size(), f);
	fclose(f);

	return boost::make_shared<torrent_info>(&tmp[0], tmp.size());
}

void generate_files(libtorrent::torrent_info const& ti, std::string const& path
	, bool alternate_data)
{
	file_pool fp;

	storage_params params;
	params.files = &ti.files();
	params.path = path;
	params.pool = &fp;

	default_storage st(params);

	int const num_pieces = ti.num_pieces();

	std::vector<char> buffer;
	for (int i = 0; i < num_pieces; ++i)
	{
		int const piece_size = ti.piece_size(i);
		buffer.resize(ti.piece_length());

		boost::uint8_t const data = alternate_data ? 255 - i : i;
		for (int o = 0; o < piece_size; ++o)
		{
			memcpy(&buffer[o], &data, 1);
		}

		file::iovec_t b = { &buffer[0], size_t(piece_size) };
		storage_error ec;
		int ret = st.writev(&b, 1, i, 0, 0, ec);
		if (ret != piece_size || ec)
		{
			fprintf(stderr, "ERROR writing files: (%d expected %d) %s\n"
				, ret, piece_size, ec.ec.message().c_str());
		}
	}
}


