/*

Copyright (c) 2012, Arvid Norberg
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

#include <string.h>
#include "test.hpp"
#include "libtorrent/part_file.hpp"
#include "libtorrent/file.hpp"
#include "libtorrent/error_code.hpp"

using namespace libtorrent;

int test_main()
{
	error_code ec;
	std::string cwd = complete(".");

	remove_all(combine_path(cwd, "partfile_test_dir"), ec);
	if (ec) fprintf(stderr, "remove_all: %s\n", ec.message().c_str());
	remove_all(combine_path(cwd, "partfile_test_dir2"), ec);
	if (ec) fprintf(stderr, "remove_all: %s\n", ec.message().c_str());

	int piece_size = 16 * 0x4000;
	char buf[1024];

	{
		create_directory(combine_path(cwd, "partfile_test_dir"), ec);
		if (ec) fprintf(stderr, "create_directory: %s\n", ec.message().c_str());
		create_directory(combine_path(cwd, "partfile_test_dir2"), ec);
		if (ec) fprintf(stderr, "create_directory: %s\n", ec.message().c_str());

		part_file pf(combine_path(cwd, "partfile_test_dir"), "partfile.parts", 100, piece_size);
		pf.flush_metadata(ec);
		if (ec) fprintf(stderr, "flush_metadata: %s\n", ec.message().c_str());

		// since we don't have anything in the part file, it will have
		// not have been created yet

		TEST_CHECK(!exists(combine_path(combine_path(cwd, "partfile_test_dir"), "partfile.parts")));

		// write something to the metadata file
		for (int i = 0; i < 1024; ++i) buf[i] = i;

		file::iovec_t v = {&buf, 1024};
		pf.writev(&v, 1, 10, 0, ec);
		if (ec) fprintf(stderr, "part_file::writev: %s\n", ec.message().c_str());

		pf.flush_metadata(ec);
		if (ec) fprintf(stderr, "flush_metadata: %s\n", ec.message().c_str());

		// now wwe should have created the partfile
		TEST_CHECK(exists(combine_path(combine_path(cwd, "partfile_test_dir"), "partfile.parts")));

		pf.move_partfile(combine_path(cwd, "partfile_test_dir2"), ec);
		TEST_CHECK(!ec);
		if (ec) fprintf(stderr, "move_partfile: %s\n", ec.message().c_str());

		TEST_CHECK(!exists(combine_path(combine_path(cwd, "partfile_test_dir"), "partfile.parts")));
		TEST_CHECK(exists(combine_path(combine_path(cwd, "partfile_test_dir2"), "partfile.parts")));

		memset(buf, 0, sizeof(buf));

		pf.readv(&v, 1, 10, 0, ec);
		if (ec) fprintf(stderr, "part_file::readv: %s\n", ec.message().c_str());

		for (int i = 0; i < 1024; ++i)
			TEST_CHECK(buf[i] == char(i));
	}

	{
		// load the part file back in
		part_file pf(combine_path(cwd, "partfile_test_dir2"), "partfile.parts", 100, piece_size);
	
		memset(buf, 0, sizeof(buf));

		file::iovec_t v = {&buf, 1024};
		pf.readv(&v, 1, 10, 0, ec);
		if (ec) fprintf(stderr, "part_file::readv: %s\n", ec.message().c_str());

		for (int i = 0; i < 1024; ++i)
			TEST_CHECK(buf[i] == char(i));

		// test exporting the piece to a file

		std::string output_filename = combine_path(combine_path(cwd, "partfile_test_dir")
			, "part_file_test_export");
		file output(output_filename, file::read_write, ec);
		if (ec) fprintf(stderr, "export open file: %s\n", ec.message().c_str());

		pf.export_file(output, 10 * piece_size, 1024, ec);
		if (ec) fprintf(stderr, "export_file: %s\n", ec.message().c_str());

		pf.free_piece(10, ec);
		if (ec) fprintf(stderr, "free_piece: %s\n", ec.message().c_str());

		pf.flush_metadata(ec);
		if (ec) fprintf(stderr, "flush_metadata: %s\n", ec.message().c_str());

		// we just removed the last piece. The partfile does not
		// contain anything anymore, it should have deleted itself
		TEST_CHECK(!exists(combine_path(combine_path(cwd, "partfile_test_dir2"), "partfile.parts"), ec));
		TEST_CHECK(!ec);
		if (ec) fprintf(stderr, "exists: %s\n", ec.message().c_str());

		output.close();

		// verify that the exported file is what we expect it to be
		output.open(output_filename, file::read_only, ec);
		if (ec) fprintf(stderr, "exported file open: %s\n", ec.message().c_str());

		memset(buf, 0, sizeof(buf));

		output.readv(0, &v, 1, ec);
		if (ec) fprintf(stderr, "exported file read: %s\n", ec.message().c_str());

		for (int i = 0; i < 1024; ++i)
			TEST_CHECK(buf[i] == char(i));
	}

	return 0;
}

