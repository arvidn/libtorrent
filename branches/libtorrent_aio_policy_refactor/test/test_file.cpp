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

#include "libtorrent/file.hpp"
#include "test.hpp"
#include <vector>
#include <set>

using namespace libtorrent;

int touch_file(std::string const& filename, int size)
{
	using namespace libtorrent;

	std::vector<char> v;
	v.resize(size);
	for (int i = 0; i < size; ++i)
		v[i] = i & 255;

	file f;
	error_code ec;
	if (!f.open(filename, file::write_only, ec)) return -1;
	if (ec) return -1;
	file::iovec_t b = {&v[0], v.size()};
	size_type written = f.writev(0, &b, 1, ec);
	if (written != int(v.size())) return -3;
	if (ec) return -3;
	return 0;
}

int test_main()
{
	error_code ec;

	create_directory("file_test_dir", ec);
	if (ec) fprintf(stderr, "create_directory: %s\n", ec.message().c_str());
	TEST_CHECK(!ec);

	std::string cwd = current_working_directory();

	touch_file(combine_path("file_test_dir", "abc"), 10);
	touch_file(combine_path("file_test_dir", "def"), 100);
	touch_file(combine_path("file_test_dir", "ghi"), 1000);

	std::set<std::string> files;
	for (directory i("file_test_dir", ec); !i.done(); i.next(ec))
	{
		std::string f = i.file();
		TEST_CHECK(files.count(f) == 0);
		files.insert(f);
		fprintf(stderr, " %s\n", f.c_str());
	}

	TEST_CHECK(files.count("abc") == 1);
	TEST_CHECK(files.count("def") == 1);
	TEST_CHECK(files.count("ghi") == 1);
	TEST_CHECK(files.count("..") == 1);
	TEST_CHECK(files.count(".") == 1);
	files.clear();

	recursive_copy("file_test_dir", "file_test_dir2", ec);

	for (directory i("file_test_dir2", ec); !i.done(); i.next(ec))
	{
		std::string f = i.file();
		TEST_CHECK(files.count(f) == 0);
		files.insert(f);
		fprintf(stderr, " %s\n", f.c_str());
	}

	remove_all("file_test_dir", ec);
	if (ec) fprintf(stderr, "remove_all: %s\n", ec.message().c_str());
	remove_all("file_test_dir2", ec);
	if (ec) fprintf(stderr, "remove_all: %s\n", ec.message().c_str());

	return 0;
}

