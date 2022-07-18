/*

Copyright (c) 2022, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/path.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/mmap.hpp"
#include "libtorrent/aux_/open_mode.hpp"
#include "libtorrent/aux_/storage_utils.hpp"
#include "test.hpp"

#include <fstream>
#include <set>

#ifndef TORRENT_WINDOWS
#include <sys/mount.h>
#endif

#ifdef TORRENT_LINUX
#include <sys/statfs.h>
#include <linux/magic.h>
#endif

namespace {

void write_file(std::string const& filename, int size)
{
	std::vector<char> v;
	v.resize(std::size_t(size));
	for (int i = 0; i < size; ++i)
		v[std::size_t(i)] = char(i & 255);

	std::ofstream(filename.c_str()).write(v.data(), std::streamsize(v.size()));
}

bool compare_files(std::string const& file1, std::string const& file2)
{
	lt::error_code ec;
	lt::file_status st1;
	lt::file_status st2;
	lt::stat_file(file1, &st1, ec);
	TEST_CHECK(!ec);
	lt::stat_file(file2, &st2, ec);
	TEST_CHECK(!ec);
	if (st1.file_size != st2.file_size)
		return false;

	std::ifstream f1(file1.c_str());
	std::ifstream f2(file2.c_str());
	using it = std::istream_iterator<char>;
	return std::equal(it(f1), it{}, it(f2));
}

#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE
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
	static const std::set<fsword_t> sparse_filesystems{
		EXT4_SUPER_MAGIC, EXT3_SUPER_MAGIC, XFS_SUPER_MAGIC, fsword_t(BTRFS_SUPER_MAGIC)
			, ufs, REISERFS_SUPER_MAGIC, TMPFS_MAGIC
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
#endif
}

TORRENT_TEST(basic)
{
	write_file("basic-1", 10);
	lt::storage_error ec;
	lt::aux::copy_file("basic-1", "basic-1.copy", ec);
	TEST_CHECK(!ec);
	TEST_CHECK(compare_files("basic-1", "basic-1.copy"));

	write_file("basic-2", 1000000);
	lt::aux::copy_file("basic-2", "basic-2.copy", ec);
	TEST_CHECK(!ec);
	TEST_CHECK(compare_files("basic-2", "basic-2.copy"));
}

#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE
TORRENT_TEST(sparse_file)
{
	using lt::aux::file_handle;
	using lt::aux::file_mapping;
	namespace open_mode = lt::aux::open_mode;

	{

#if TORRENT_HAVE_MAP_VIEW_OF_FILE
		auto open_unmap_lock = std::make_shared<std::mutex>();
#endif
		file_handle f("sparse-1", 50'000'000
			, open_mode::write | open_mode::truncate | open_mode::sparse);
		auto map = std::make_shared<file_mapping>(std::move(f), lt::aux::open_mode::write, 50'000'000
#if TORRENT_HAVE_MAP_VIEW_OF_FILE
			, open_unmap_lock
#endif
			);
		auto range = map->range();
		TEST_CHECK(range.size() == 50'000'000);

		range[0] = 1;
		range[49'999'999] = 1;
	}

	// Find out if the filesystem we're running the test on supports sparse
	// files. If not, we don't expect any of the files to be sparse
	bool const supports_sparse_files = fs_supports_sparse_files();

	// make sure "sparse-1" is actually sparse
#ifdef TORRENT_WINDOWS
	DWORD high;
	std::int64_t const original_size = ::GetCompressedFileSizeA("sparse-1", &high);
	TEST_CHECK(original_size != INVALID_FILE_SIZE);
	TEST_CHECK(high == 0);
#else
	struct stat st;
	TEST_CHECK(::stat("sparse-1", &st) == 0);
	std::int64_t const original_size = st.st_blocks * 512;
#endif
	printf("original_size: %d\n", int(original_size));
	if (supports_sparse_files)
	{
		TEST_CHECK(original_size < 500'000);
	}
	else
	{
		TEST_CHECK(original_size >= 50'000'000);
	}

	lt::storage_error ec;
	lt::aux::copy_file("sparse-1", "sparse-1.copy", ec);
	TEST_CHECK(!ec);

	// make sure the copy is sparse
#ifdef TORRENT_WINDOWS
	WIN32_FILE_ATTRIBUTE_DATA out_stat;
	TEST_CHECK(::GetFileAttributesExA("sparse-1.copy", GetFileExInfoStandard, &out_stat));
	if (supports_sparse_files)
	{
		TEST_CHECK(out_stat.dwFileAttributes & FILE_ATTRIBUTE_SPARSE_FILE);
	}
	else
	{
		TEST_CHECK((out_stat.dwFileAttributes & FILE_ATTRIBUTE_SPARSE_FILE) == 0);
	}

	TEST_EQUAL(::GetCompressedFileSizeA("sparse-1.copy", &high), original_size);
	TEST_CHECK(high == 0);
#else
	TEST_CHECK(::stat("sparse-1.copy", &st) == 0);
	printf("copy_size: %d\n", int(st.st_blocks) * 512);
	TEST_EQUAL(st.st_blocks * 512, original_size);
#endif

	TEST_CHECK(compare_files("sparse-1", "sparse-1.copy"));
}
#endif

