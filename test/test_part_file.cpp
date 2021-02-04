/*

Copyright (c) 2014-2019, Arvid Norberg
Copyright (c) 2018, Alden Torres
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

#include <cstring>
#include <array>

#include "test.hpp"
#include "test_utils.hpp"
#include "libtorrent/part_file.hpp"
#include "libtorrent/aux_/posix_part_file.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/error_code.hpp"

using namespace lt;

TORRENT_TEST(part_file)
{
	error_code ec;
	std::string cwd = complete(".");

	remove_all(combine_path(cwd, "partfile_test_dir"), ec);
	if (ec) std::printf("remove_all: %s\n", ec.message().c_str());
	remove_all(combine_path(cwd, "partfile_test_dir2"), ec);
	if (ec) std::printf("remove_all: %s\n", ec.message().c_str());

	int piece_size = 16 * 0x4000;
	std::array<char, 1024> buf;

	{
		create_directory(combine_path(cwd, "partfile_test_dir"), ec);
		if (ec) std::printf("create_directory: %s\n", ec.message().c_str());
		create_directory(combine_path(cwd, "partfile_test_dir2"), ec);
		if (ec) std::printf("create_directory: %s\n", ec.message().c_str());

		part_file pf(combine_path(cwd, "partfile_test_dir"), "partfile.parts", 100, piece_size);
		pf.flush_metadata(ec);
		if (ec) std::printf("flush_metadata: %s\n", ec.message().c_str());

		// since we don't have anything in the part file, it will have
		// not have been created yet

		TEST_CHECK(!exists(combine_path(combine_path(cwd, "partfile_test_dir"), "partfile.parts")));

		// write something to the metadata file
		for (int i = 0; i < 1024; ++i) buf[std::size_t(i)] = char(i & 0xff);

		iovec_t v = buf;
		pf.writev(v, piece_index_t(10), 0, ec);
		if (ec) std::printf("part_file::writev: %s\n", ec.message().c_str());

		pf.flush_metadata(ec);
		if (ec) std::printf("flush_metadata: %s\n", ec.message().c_str());

		// now wwe should have created the partfile
		TEST_CHECK(exists(combine_path(combine_path(cwd, "partfile_test_dir"), "partfile.parts")));

		pf.move_partfile(combine_path(cwd, "partfile_test_dir2"), ec);
		TEST_CHECK(!ec);
		if (ec) std::printf("move_partfile: %s\n", ec.message().c_str());

		TEST_CHECK(!exists(combine_path(combine_path(cwd, "partfile_test_dir"), "partfile.parts")));
		TEST_CHECK(exists(combine_path(combine_path(cwd, "partfile_test_dir2"), "partfile.parts")));

		buf.fill(0);

		pf.readv(v, piece_index_t(10), 0, ec);
		if (ec) std::printf("part_file::readv: %s\n", ec.message().c_str());

		for (int i = 0; i < int(buf.size()); ++i)
			TEST_CHECK(buf[std::size_t(i)] == char(i));

		sha1_hash const cmp_hash = hasher(buf).final();

		hasher ph;
		pf.hashv(ph, sizeof(buf), piece_index_t(10), 0, ec);
		if (ec) std::printf("part_file::hashv: %s\n", ec.message().c_str());

		TEST_CHECK(ph.final() == cmp_hash);
	}

	{
		// load the part file back in
		part_file pf(combine_path(cwd, "partfile_test_dir2"), "partfile.parts", 100, piece_size);

		buf.fill(0);

		iovec_t v = buf;
		pf.readv(v, piece_index_t(10), 0, ec);
		if (ec) std::printf("part_file::readv: %s\n", ec.message().c_str());

		for (int i = 0; i < 1024; ++i)
			TEST_CHECK(buf[std::size_t(i)] == static_cast<char>(i));

		// test exporting the piece to a file

		std::string output_filename = combine_path(combine_path(cwd, "partfile_test_dir")
			, "part_file_test_export");

		pf.export_file([](std::int64_t file_offset, span<char> buf_data)
		{
			for (char i : buf_data)
			{
				// make sure we got the bytes we expected
				TEST_CHECK(i == static_cast<char>(file_offset));
				++file_offset;
			}
		}, 10 * piece_size, 1024, ec);
		if (ec) std::printf("export_file: %s\n", ec.message().c_str());

		pf.free_piece(piece_index_t(10));

		pf.flush_metadata(ec);
		if (ec) std::printf("flush_metadata: %s\n", ec.message().c_str());

		// we just removed the last piece. The partfile does not
		// contain anything anymore, it should have deleted itself
		TEST_CHECK(!exists(combine_path(combine_path(cwd, "partfile_test_dir2"), "partfile.parts"), ec));
		TEST_CHECK(!ec);
		if (ec) std::printf("exists: %s\n", ec.message().c_str());
	}
}

TORRENT_TEST(posix_part_file)
{
	error_code ec;
	std::string cwd = complete(".");

	remove_all(combine_path(cwd, "partfile_test_dir"), ec);
	if (ec) std::printf("remove_all: %s\n", ec.message().c_str());
	remove_all(combine_path(cwd, "partfile_test_dir2"), ec);
	if (ec) std::printf("remove_all: %s\n", ec.message().c_str());

	int piece_size = 16 * 0x4000;
	std::array<char, 1024> buf;

	{
		create_directory(combine_path(cwd, "partfile_test_dir"), ec);
		if (ec) std::printf("create_directory: %s\n", ec.message().c_str());
		create_directory(combine_path(cwd, "partfile_test_dir2"), ec);
		if (ec) std::printf("create_directory: %s\n", ec.message().c_str());

		aux::posix_part_file pf(combine_path(cwd, "partfile_test_dir"), "partfile.parts", 100, piece_size);
		pf.flush_metadata(ec);
		if (ec) std::printf("flush_metadata: %s\n", ec.message().c_str());

		// since we don't have anything in the part file, it will have
		// not have been created yet

		TEST_CHECK(!exists(combine_path(combine_path(cwd, "partfile_test_dir"), "partfile.parts")));

		// write something to the metadata file
		for (int i = 0; i < 1024; ++i) buf[std::size_t(i)] = char(i & 0xff);

		iovec_t v = buf;
		pf.writev(v, piece_index_t(10), 0, ec);
		if (ec) std::printf("posix_part_file::writev: %s\n", ec.message().c_str());

		pf.flush_metadata(ec);
		if (ec) std::printf("flush_metadata: %s\n", ec.message().c_str());

		// now wwe should have created the partfile
		TEST_CHECK(exists(combine_path(combine_path(cwd, "partfile_test_dir"), "partfile.parts")));

		pf.move_partfile(combine_path(cwd, "partfile_test_dir2"), ec);
		TEST_CHECK(!ec);
		if (ec) std::printf("move_partfile: %s\n", ec.message().c_str());

		TEST_CHECK(!exists(combine_path(combine_path(cwd, "partfile_test_dir"), "partfile.parts")));
		TEST_CHECK(exists(combine_path(combine_path(cwd, "partfile_test_dir2"), "partfile.parts")));

		buf.fill(0);

		pf.readv(v, piece_index_t(10), 0, ec);
		if (ec) std::printf("posix_part_file::readv: %s\n", ec.message().c_str());

		for (int i = 0; i < int(buf.size()); ++i)
			TEST_CHECK(buf[std::size_t(i)] == char(i));

		sha1_hash const cmp_hash = hasher(buf).final();

		hasher ph;
		pf.hashv(ph, sizeof(buf), piece_index_t(10), 0, ec);
		if (ec) std::printf("posix_part_file::hashv: %s\n", ec.message().c_str());

		TEST_CHECK(ph.final() == cmp_hash);
	}

	{
		// load the part file back in
		aux::posix_part_file pf(combine_path(cwd, "partfile_test_dir2"), "partfile.parts", 100, piece_size);

		buf.fill(0);

		iovec_t v = buf;
		pf.readv(v, piece_index_t(10), 0, ec);
		if (ec) std::printf("posix_part_file::readv: %s\n", ec.message().c_str());

		for (int i = 0; i < 1024; ++i)
			TEST_CHECK(buf[std::size_t(i)] == static_cast<char>(i));

		// test exporting the piece to a file

		std::string output_filename = combine_path(combine_path(cwd, "partfile_test_dir")
			, "part_file_test_export");

		pf.export_file([](std::int64_t file_offset, span<char> buf_data)
		{
			for (char i : buf_data)
			{
				// make sure we got the bytes we expected
				TEST_CHECK(i == static_cast<char>(file_offset));
				++file_offset;
			}
		}, 10 * piece_size, 1024, ec);
		if (ec) std::printf("export_file: %s\n", ec.message().c_str());

		pf.free_piece(piece_index_t(10));

		pf.flush_metadata(ec);
		if (ec) std::printf("flush_metadata: %s\n", ec.message().c_str());

		// we just removed the last piece. The partfile does not
		// contain anything anymore, it should have deleted itself
		TEST_CHECK(!exists(combine_path(combine_path(cwd, "partfile_test_dir2"), "partfile.parts"), ec));
		TEST_CHECK(!ec);
		if (ec) std::printf("exists: %s\n", ec.message().c_str());
	}
}
