/*

Copyright (c) 2016, Andrei Kurushin
Copyright (c) 2015-2016, 2019-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test_utils.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/aux_/merkle.hpp"
#include "libtorrent/aux_/random.hpp"

#ifdef _WIN32
#include <io.h>
#include <fcntl.h> // for _O_WRONLY
#endif

namespace libtorrent
{
	std::string time_now_string()
	{
		return time_to_string(clock_type::now());
	}

	std::string time_to_string(time_point const tp)
	{
		static const time_point start = clock_type::now();
		char ret[200];
		int t = int(total_milliseconds(tp - start));
		int h = t / 1000 / 60 / 60;
		t -= h * 60 * 60 * 1000;
		int m = t / 1000 / 60;
		t -= m * 60 * 1000;
		int s = t / 1000;
		t -= s * 1000;
		int ms = t;
		std::snprintf(ret, sizeof(ret), "%02d:%02d:%02d.%03d", h, m, s, ms);
		return ret;
	}

	std::string test_listen_interface()
	{
		static int port = int(aux::random(10000) + 10000);
		char ret[200];
		std::snprintf(ret, sizeof(ret), "0.0.0.0:%d", port);
		++port;
		return ret;
	}
}

using namespace lt;

aux::vector<sha256_hash> build_tree(int const size)
{
	int const num_leafs = merkle_num_leafs(size);
	aux::vector<sha256_hash> full_tree(merkle_num_nodes(num_leafs));

	for (int i = 0; i < size; i++)
	{
		std::uint32_t hash[32 / 4];
		std::fill(std::begin(hash), std::end(hash), i + 1);
		full_tree[full_tree.end_index() - num_leafs + i] = sha256_hash(reinterpret_cast<char*>(hash));
	}

	merkle_fill_tree(full_tree, num_leafs);
	return full_tree;
}

#ifdef _WIN32
int EXPORT truncate(char const* file, std::int64_t size)
{
	int fd = ::_open(file, _O_WRONLY);
	if (fd < 0) return -1;
	int const err = ::_chsize_s(fd, size);
	::_close(fd);
	if (err == 0) return 0;
	errno = err;
	return -1;
}
#endif

ofstream::ofstream(char const* filename)
{
	exceptions(std::ofstream::failbit);
	native_path_string const name = convert_to_native_path_string(filename);
	open(name.c_str(), std::fstream::out | std::fstream::binary);
}

bool exists(std::string const& f)
{
	lt::error_code ec;
	return lt::exists(f, ec);
}

#if TORRENT_ABI_VERSION < 4
std::vector<char> serialize(lt::torrent_info const& ti)
{
	lt::create_torrent ct(ti);
	ct.set_creation_date(0);
	entry e = ct.generate();
	std::vector<char> out_buffer;
	bencode(std::back_inserter(out_buffer), e);
	return out_buffer;
}
#endif

std::vector<lt::create_file_entry> make_files(std::vector<file_ent> const files)
{
	std::vector<lt::create_file_entry> fs;
	int i = 0;
	for (auto const& e : files)
	{
		char filename[200];
		std::snprintf(filename, sizeof(filename), "t/test%d", int(i++));
		fs.emplace_back(filename, e.size, e.pad ? file_storage::flag_pad_file : file_flags_t{});
	}

	return fs;
}

