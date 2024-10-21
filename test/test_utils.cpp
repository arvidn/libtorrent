/*

Copyright (c) 2016, Andrei Kurushin
Copyright (c) 2015-2016, 2019-2021, Arvid Norberg
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

#include "test_utils.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/aux_/merkle.hpp"
#include "libtorrent/random.hpp"

#ifdef _WIN32
#include <io.h>
#include <fcntl.h> // for _O_WRONLY
#endif

#ifdef TORRENT_LINUX
#include <sys/statfs.h>
#include <linux/magic.h>
#endif

#ifndef TORRENT_WINDOWS
#include <sys/mount.h>
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
		static int port = int(random(10000) + 10000);
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

#if defined _WIN32 && !defined TORRENT_MINGW
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

std::vector<char> serialize(lt::torrent_info const& ti)
{
	lt::create_torrent ct(ti);
	ct.set_creation_date(0);
	entry e = ct.generate();
	std::vector<char> out_buffer;
	bencode(std::back_inserter(out_buffer), e);
	return out_buffer;
}

lt::file_storage make_files(std::vector<file_ent> const files, int const piece_size)
{
	file_storage fs;
	int i = 0;
	for (auto const& e : files)
	{
		char filename[200];
		std::snprintf(filename, sizeof(filename), "t/test%d", int(i++));
		fs.add_file(filename, e.size, e.pad ? file_storage::flag_pad_file : file_flags_t{});
	}

	fs.set_piece_length(piece_size);
	fs.set_num_pieces(aux::calc_num_pieces(fs));

	return fs;
}

#if defined TORRENT_WINDOWS
bool fs_supports_sparse_files()
{
#ifdef TORRENT_WINRT
	HANDLE test = ::CreateFile2(L"test"
			, GENERIC_WRITE
			, FILE_SHARE_READ
			, OPEN_ALWAYS
			, nullptr);
#else
	HANDLE test = ::CreateFileA("test"
			, GENERIC_WRITE
			, FILE_SHARE_READ
			, nullptr
			, OPEN_ALWAYS
			, FILE_FLAG_SEQUENTIAL_SCAN
			, nullptr);
#endif
	TEST_CHECK(test != INVALID_HANDLE_VALUE);
	DWORD fs_flags = 0;
	wchar_t fs_name[50];
	TEST_CHECK(::GetVolumeInformationByHandleW(test, nullptr, 0, nullptr, nullptr
		, &fs_flags, fs_name, sizeof(fs_name)) != 0);
	::CloseHandle(test);
	printf("filesystem: %S\n", fs_name);
	return (fs_flags & FILE_SUPPORTS_SPARSE_FILES) != 0;
}

#else

bool fs_supports_sparse_files()
{
	int test = ::open("test", O_RDWR | O_CREAT, 0755);
	TEST_CHECK(test >= 0);
	struct statfs st{};
	TEST_CHECK(fstatfs(test, &st) == 0);
	::close(test);
#ifdef TORRENT_LINUX
	using fsword_t = decltype(statfs::f_type);
	static fsword_t const ufs = 0x00011954;
	static fsword_t const zfs = 0x2fc12fc1;
	static const std::set<fsword_t> sparse_filesystems{
		EXT4_SUPER_MAGIC, EXT3_SUPER_MAGIC, XFS_SUPER_MAGIC, fsword_t(BTRFS_SUPER_MAGIC)
			, ufs, zfs, REISERFS_SUPER_MAGIC, TMPFS_MAGIC, OVERLAYFS_SUPER_MAGIC
	};
	printf("filesystem: %ld\n", long(st.f_type));
	return sparse_filesystems.count(st.f_type);
#else
	printf("filesystem: (%d) %s\n", int(st.f_type), st.f_fstypename);
	static const std::set<std::string> sparse_filesystems{
		"ufs", "zfs", "ext4", "xfs", "apfs", "btrfs"};
	return sparse_filesystems.count(st.f_fstypename);
#endif
}

#endif
bool fs_supports_prealloc()
{
#ifdef TORRENT_WINDOWS
	return true;
#else
	int test = ::open("__test__", O_RDWR | O_CREAT, 0755);
	TEST_CHECK(test >= 0);
	struct statfs st{};
	TEST_CHECK(fstatfs(test, &st) == 0);
	::close(test);
	// notably, ZFS does not support fallocate(). Even if glibc implements it to
	// write zeroes, ZFS (when compression is enabled) will not write them to
	// disk.
#ifdef TORRENT_LINUX
	using fsword_t = decltype(statfs::f_type);
	static fsword_t const ufs = 0x00011954;
	static const std::set<fsword_t> prealloc_filesystems{
		EXT4_SUPER_MAGIC, EXT3_SUPER_MAGIC, XFS_SUPER_MAGIC, fsword_t(BTRFS_SUPER_MAGIC)
			, ufs, REISERFS_SUPER_MAGIC, TMPFS_MAGIC, OVERLAYFS_SUPER_MAGIC
	};
	printf("filesystem: %ld\n", long(st.f_type));
	return prealloc_filesystems.count(st.f_type);
#else
	printf("filesystem: (%d) %s\n", int(st.f_type), st.f_fstypename);
	static const std::set<std::string> prealloc_filesystems{
		"ufs", "ext4", "xfs", "apfs", "btrfs"};
	return prealloc_filesystems.count(st.f_fstypename);
#endif
#endif
}
