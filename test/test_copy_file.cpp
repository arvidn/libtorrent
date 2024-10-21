/*

Copyright (c) 2022, Arvid Norberg
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

#include "libtorrent/aux_/path.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/mmap.hpp"
#include "libtorrent/aux_/open_mode.hpp"
#include "test.hpp"
#include "test_utils.hpp"

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
	printf("supports sparse files: %d\n", int(supports_sparse_files));

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
	TEST_CHECK(st.st_blocks * 512 < 500'000);
#endif

	TEST_CHECK(compare_files("sparse-1", "sparse-1.copy"));
}
#endif

