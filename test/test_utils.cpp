/*

Copyright (c) 2016, Andrei Kurushin
Copyright (c) 2015-2016, 2019-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test_utils.hpp"
#include <string_view>
#include "libtorrent/time.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/aux_/merkle.hpp"
#include "libtorrent/aux_/random.hpp"
#include "libtorrent/write_resume_data.hpp" // for write_torrent_file_buf()
#include "libtorrent/add_torrent_params.hpp"

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

#if TORRENT_ABI_VERSION < 4
std::vector<char> serialize(lt::torrent_info const& ti)
{
	lt::create_torrent ct(ti);
	ct.set_creation_date(0);
	entry e = ct.generate();
	std::vector<char> const out_buffer = bencode(e);
	return out_buffer;
}

std::vector<char> serialize(lt::add_torrent_params atp)
{
	atp.creation_date = 0;
	return write_torrent_file_buf(atp, {});
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

filesystem_features query_filesystem_features()
{
#ifdef TORRENT_WINDOWS
	filesystem_features ret;
	ret.prealloc = true;
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
	ret.sparse_files = (fs_flags & FILE_SUPPORTS_SPARSE_FILES) != 0;
	return ret;
#else
	int test = ::open("__test__", O_RDWR | O_CREAT, 0755);
	TEST_CHECK(test >= 0);
	struct statfs st{};
	TEST_CHECK(fstatfs(test, &st) == 0);
	::close(test);
#ifdef TORRENT_LINUX
	using fsword_t = decltype(statfs::f_type);
	struct fs_entry
	{
		fsword_t magic;
		filesystem_features features;
	};
	static fs_entry const known_filesystems[] = {
		{EXT4_SUPER_MAGIC, {true, true, false, true}},
		{EXT3_SUPER_MAGIC, {true, true, false, true}},
		{XFS_SUPER_MAGIC, {true, true, false, true}},
		{fsword_t(BTRFS_SUPER_MAGIC), {true, true, false, true}},
		{0x00011954 /* ufs */, {true, true, false, true}},
		{REISERFS_SUPER_MAGIC, {true, true, false, true}},
		{TMPFS_MAGIC, {true, true, false, true}},
		{OVERLAYFS_SUPER_MAGIC, {true, true, false, true}},
		{0x2fc12fc1 /* zfs */, {true, false, false, true}},
		{fsword_t(0xf2f52010) /* f2fs */, {true, false, false, true}},
		{0x3153464a /* jfs */, {true, false, false, false}},
		{0x3434 /* nilfs2 */, {true, false, true, true}},
		{fsword_t(0xca451a4e) /* bcachefs */, {true, false, false, true}},
	};
	printf("filesystem: %ld\n", long(st.f_type));
	for (auto const& e : known_filesystems)
		if (e.magic == st.f_type)
			return e.features;
#else
	struct fs_entry
	{
		char const* name;
		filesystem_features features;
	};
	static fs_entry const known_filesystems[] = {
		{"ufs", {true, true, false, true}},
		{"ext4", {true, true, false, true}},
		{"xfs", {true, true, false, true}},
		{"apfs", {true, true, false, true}},
		{"btrfs", {true, true, false, true}},
		{"zfs", {true, false, false, true}},
		{"f2fs", {true, false, false, true}},
		{"jfs", {true, false, false, false}},
		{"nilfs2", {true, false, true, true}},
		{"bcachefs", {true, false, false, true}},
	};
	printf("filesystem: (%d) %s\n", int(st.f_type), st.f_fstypename);
	for (auto const& e : known_filesystems)
		if (std::string_view(st.f_fstypename) == e.name)
			return e.features;
#endif
	return {};
#endif
}
