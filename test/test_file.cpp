/*

Copyright (c) 2012-2020, Arvid Norberg
Copyright (c) 2016, 2018, Alden Torres
Copyright (c) 2017, Steven Siloti
Copyright (c) 2017-2018, Andrei Kurushin
Copyright (c) 2018, d-komarov
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
#include "libtorrent/aux_/directory.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"
#include "libtorrent/string_view.hpp"
#include "libtorrent/aux_/file_view_pool.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"
#include "test.hpp"
#include "test_utils.hpp"
#include <vector>
#include <set>
#include <thread>
#include <iostream>

using namespace lt;

namespace {

void touch_file(std::string const& filename, int size)
{
	std::vector<char> v;
	v.resize(aux::numeric_cast<std::size_t>(size));
	for (int i = 0; i < size; ++i)
		v[std::size_t(i)] = char(i & 255);

	ofstream(filename.c_str()).write(v.data(), lt::aux::numeric_cast<std::streamsize>(v.size()));
}

} // anonymous namespace

TORRENT_TEST(create_directory)
{
	error_code ec;
	create_directory("__foobar__", ec);
	if (ec) std::printf("ERROR: create_directory: (%d) %s\n"
		, ec.value(), ec.message().c_str());
	TEST_CHECK(!ec);

	file_status st;
	stat_file("__foobar__", &st, ec);
	if (ec) std::printf("ERROR: stat_file: (%d) %s\n"
		, ec.value(), ec.message().c_str());
	TEST_CHECK(!ec);

	TEST_CHECK(st.mode & file_status::directory);

	remove("__foobar__", ec);
	if (ec) std::printf("ERROR: remove: (%d) %s\n"
		, ec.value(), ec.message().c_str());
	TEST_CHECK(!ec);
}

TORRENT_TEST(file_status)
{
	error_code ec;

	// test that the modification timestamps
	touch_file("__test_timestamp__", 10);

	file_status st1;
	stat_file("__test_timestamp__", &st1, ec);
	TEST_CHECK(!ec);

	// sleep for 3 seconds and then make sure the difference in timestamp is
	// between 2-4 seconds after touching it again
	std::this_thread::sleep_for(lt::milliseconds(3000));

	touch_file("__test_timestamp__", 10);

	file_status st2;
	stat_file("__test_timestamp__", &st2, ec);
	TEST_CHECK(!ec);

	int diff = int(st2.mtime - st1.mtime);
	std::printf("timestamp difference: %d seconds. expected approx. 3 seconds\n"
		, diff);

	TEST_CHECK(diff >= 2 && diff <= 4);
}

TORRENT_TEST(directory)
{
	error_code ec;

	create_directory("file_test_dir", ec);
	if (ec) std::printf("create_directory: %s\n", ec.message().c_str());
	TEST_CHECK(!ec);

	std::string cwd = current_working_directory();

	touch_file(combine_path("file_test_dir", "abc"), 10);
	touch_file(combine_path("file_test_dir", "def"), 100);
	touch_file(combine_path("file_test_dir", "ghi"), 1000);

	std::set<std::string> files;
	for (aux::directory i("file_test_dir", ec); !i.done(); i.next(ec))
	{
		std::string f = i.file();
		TEST_CHECK(files.count(f) == 0);
		files.insert(f);
		std::printf(" %s\n", f.c_str());
	}

	TEST_CHECK(files.count("abc") == 1);
	TEST_CHECK(files.count("def") == 1);
	TEST_CHECK(files.count("ghi") == 1);
	TEST_CHECK(files.count("..") == 1);
	TEST_CHECK(files.count(".") == 1);
	files.clear();

	recursive_copy("file_test_dir", "file_test_dir2", ec);

	for (aux::directory i("file_test_dir2", ec); !i.done(); i.next(ec))
	{
		std::string f = i.file();
		TEST_CHECK(files.count(f) == 0);
		files.insert(f);
		std::printf(" %s\n", f.c_str());
	}

	remove_all("file_test_dir", ec);
	if (ec) std::printf("remove_all: %s\n", ec.message().c_str());
	remove_all("file_test_dir2", ec);
	if (ec) std::printf("remove_all: %s\n", ec.message().c_str());
}

// test path functions
TORRENT_TEST(paths)
{
	TEST_EQUAL(combine_path("test1/", "test2"), "test1/test2");
	TEST_EQUAL(combine_path("test1", "."), "test1");
	TEST_EQUAL(combine_path(".", "test1"), "test1");
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(combine_path("test1\\", "test2"), "test1\\test2");
	TEST_EQUAL(combine_path("test1", "test2"), "test1\\test2");
#else
	TEST_EQUAL(combine_path("test1", "test2"), "test1/test2");
#endif

	TEST_EQUAL(extension("blah"), "");
	TEST_EQUAL(extension("blah.exe"), ".exe");
	TEST_EQUAL(extension("blah.foo.bar"), ".bar");
	TEST_EQUAL(extension("blah.foo."), ".");
	TEST_EQUAL(extension("blah.foo/bar"), "");

	TEST_EQUAL(remove_extension("blah"), "blah");
	TEST_EQUAL(remove_extension("blah.exe"), "blah");
	TEST_EQUAL(remove_extension("blah.foo.bar"), "blah.foo");
	TEST_EQUAL(remove_extension("blah.foo."), "blah.foo");

#ifdef TORRENT_WINDOWS
	TEST_EQUAL(is_root_path("c:\\blah"), false);
	TEST_EQUAL(is_root_path("c:\\"), true);
	TEST_EQUAL(is_root_path("\\\\"), true);
	TEST_EQUAL(is_root_path("\\\\foobar"), true);
	TEST_EQUAL(is_root_path("\\\\foobar\\"), true);
	TEST_EQUAL(is_root_path("\\\\foobar/"), true);
	TEST_EQUAL(is_root_path("\\\\foo/bar"), false);
	TEST_EQUAL(is_root_path("\\\\foo\\bar\\"), false);
#else
	TEST_EQUAL(is_root_path("/blah"), false);
	TEST_EQUAL(is_root_path("/"), true);
#endif

#ifdef TORRENT_WINDOWS
	TEST_CHECK(path_equal("c:\\blah\\", "c:\\blah"));
	TEST_CHECK(path_equal("c:\\blah", "c:\\blah"));
	TEST_CHECK(path_equal("c:\\blah/", "c:\\blah"));
	TEST_CHECK(path_equal("c:\\blah", "c:\\blah\\"));
	TEST_CHECK(path_equal("c:\\blah", "c:\\blah"));
	TEST_CHECK(path_equal("c:\\blah", "c:\\blah/"));

	TEST_CHECK(!path_equal("c:\\bla", "c:\\blah/"));
	TEST_CHECK(!path_equal("c:\\bla", "c:\\blah"));
	TEST_CHECK(!path_equal("c:\\blah", "c:\\bla"));
	TEST_CHECK(!path_equal("c:\\blah\\sdf", "c:\\blah"));
#else
	TEST_CHECK(path_equal("/blah", "/blah"));
	TEST_CHECK(path_equal("/blah/", "/blah"));
	TEST_CHECK(path_equal("/blah", "/blah"));
	TEST_CHECK(path_equal("/blah", "/blah/"));

	TEST_CHECK(!path_equal("/bla", "/blah/"));
	TEST_CHECK(!path_equal("/bla", "/blah"));
	TEST_CHECK(!path_equal("/blah", "/bla"));
	TEST_CHECK(!path_equal("/blah/sdf", "/blah"));
#endif

	// if has_parent_path() returns false
	// parent_path() should return the empty string
	TEST_EQUAL(parent_path("blah"), "");
	TEST_EQUAL(has_parent_path("blah"), false);
	TEST_EQUAL(parent_path("/blah/foo/bar"), "/blah/foo/");
	TEST_EQUAL(has_parent_path("/blah/foo/bar"), true);
	TEST_EQUAL(parent_path("/blah/foo/bar/"), "/blah/foo/");
	TEST_EQUAL(has_parent_path("/blah/foo/bar/"), true);
	TEST_EQUAL(parent_path("/a"), "/");
	TEST_EQUAL(has_parent_path("/a"), true);
	TEST_EQUAL(parent_path("/"), "");
	TEST_EQUAL(has_parent_path("/"), false);
	TEST_EQUAL(parent_path(""), "");
	TEST_EQUAL(has_parent_path(""), false);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(parent_path("\\\\"), "");
	TEST_EQUAL(has_parent_path("\\\\"), false);
	TEST_EQUAL(parent_path("c:\\"), "");
	TEST_EQUAL(has_parent_path("c:\\"), false);
	TEST_EQUAL(parent_path("c:\\a"), "c:\\");
	TEST_EQUAL(has_parent_path("c:\\a"), true);
	TEST_EQUAL(has_parent_path("\\\\a"), false);
	TEST_EQUAL(has_parent_path("\\\\foobar/"), false);
	TEST_EQUAL(has_parent_path("\\\\foobar\\"), false);
	TEST_EQUAL(has_parent_path("\\\\foo/bar\\"), true);
#endif

#ifdef TORRENT_WINDOWS
	TEST_EQUAL(is_complete("c:\\"), true);
	TEST_EQUAL(is_complete("c:\\foo\\bar"), true);
	TEST_EQUAL(is_complete("\\\\foo\\bar"), true);
	TEST_EQUAL(is_complete("foo/bar"), false);
	TEST_EQUAL(is_complete("\\\\"), true);
#else
	TEST_EQUAL(is_complete("/foo/bar"), true);
	TEST_EQUAL(is_complete("foo/bar"), false);
	TEST_EQUAL(is_complete("/"), true);
	TEST_EQUAL(is_complete(""), false);
#endif

	TEST_EQUAL(complete("."), current_working_directory());

#ifdef TORRENT_WINDOWS
	TEST_EQUAL(complete(".\\foobar"), current_working_directory() + "\\foobar");
#else
	TEST_EQUAL(complete("./foobar"), current_working_directory() + "/foobar");
#endif
}

TORRENT_TEST(path_compare)
{
	TEST_EQUAL(path_compare("a/b/c", "x", "a/b/c", "x"), 0);

	// the path and filenames are implicitly concatenated when compared
	TEST_CHECK(path_compare("a/b/", "a", "a/b/c", "a") < 0);
	TEST_CHECK(path_compare("a/b/c", "a", "a/b/", "a") > 0);

	// if one path is shorter and a substring of the other, they are considered
	// equal. This case is invalid for the purposes of sorting files in v2
	// torrents and will fail anyway
	TEST_EQUAL(path_compare("a/b/", "c", "a/b/c", "a"), 0);
	TEST_EQUAL(path_compare("a/b/c", "a", "a/b", "c"), 0);

	TEST_CHECK(path_compare("foo/b/c", "x", "a/b/c", "x") > 0);
	TEST_CHECK(path_compare("a/b/c", "x", "foo/b/c", "x") < 0);
	TEST_CHECK(path_compare("aaa/b/c", "x", "a/b/c", "x") > 0);
	TEST_CHECK(path_compare("a/b/c", "x", "aaa/b/c", "x") < 0);
	TEST_CHECK(path_compare("a/b/c/2", "x", "a/b/c/1", "x") > 0);
	TEST_CHECK(path_compare("a/b/c/1", "x", "a/b/c/2", "x") < 0);
	TEST_CHECK(path_compare("a/1/c", "x", "a/2/c", "x") < 0);
	TEST_CHECK(path_compare("a/a/c", "x", "a/aa/c", "x") < 0);
	TEST_CHECK(path_compare("a/aa/c", "x", "a/a/c", "x") > 0);
}

TORRENT_TEST(filename)
{
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(filename("blah"), "blah");
	TEST_EQUAL(filename("\\blah\\foo\\bar"), "bar");
	TEST_EQUAL(filename("\\blah\\foo\\bar\\"), "bar");
	TEST_EQUAL(filename("blah\\"), "blah");
#endif
	TEST_EQUAL(filename("blah"), "blah");
	TEST_EQUAL(filename("/blah/foo/bar"), "bar");
	TEST_EQUAL(filename("/blah/foo/bar/"), "bar");
	TEST_EQUAL(filename("blah/"), "blah");
}

TORRENT_TEST(split_path)
{
	using r = std::pair<string_view, string_view>;

#ifdef TORRENT_WINDOWS
	TEST_CHECK(lsplit_path("\\b\\c\\d") == r("b", "c\\d"));
	TEST_CHECK(lsplit_path("a\\b\\c\\d") == r("a", "b\\c\\d"));
	TEST_CHECK(lsplit_path("a") == r("a", ""));
	TEST_CHECK(lsplit_path("") == r("", ""));

	TEST_CHECK(lsplit_path("a\\b/c\\d") == r("a", "b/c\\d"));
	TEST_CHECK(lsplit_path("a/b\\c\\d") == r("a", "b\\c\\d"));

	TEST_CHECK(rsplit_path("a\\b\\c\\d\\") == r("a\\b\\c", "d"));
	TEST_CHECK(rsplit_path("\\a\\b\\c\\d") == r("\\a\\b\\c", "d"));
	TEST_CHECK(rsplit_path("\\a") == r("", "a"));
	TEST_CHECK(rsplit_path("a") == r("", "a"));
	TEST_CHECK(rsplit_path("") == r("", ""));

	TEST_CHECK(rsplit_path("a\\b/c\\d\\") == r("a\\b/c", "d"));
	TEST_CHECK(rsplit_path("a\\b\\c/d\\") == r("a\\b\\c", "d"));
#endif
	TEST_CHECK(lsplit_path("/b/c/d") == r("b", "c/d"));
	TEST_CHECK(lsplit_path("a/b/c/d") == r("a", "b/c/d"));
	TEST_CHECK(lsplit_path("a") == r("a", ""));
	TEST_CHECK(lsplit_path("") == r("", ""));

	TEST_CHECK(rsplit_path("a/b/c/d/") == r("a/b/c", "d"));
	TEST_CHECK(rsplit_path("/a/b/c/d") == r("/a/b/c", "d"));
	TEST_CHECK(rsplit_path("/a") == r("", "a"));
	TEST_CHECK(rsplit_path("a") == r("", "a"));
	TEST_CHECK(rsplit_path("") == r("", ""));
}

TORRENT_TEST(split_path_pos)
{
	using r = std::pair<string_view, string_view>;

#ifdef TORRENT_WINDOWS
	TEST_CHECK(lsplit_path("\\b\\c\\d", 0) == r("b", "c\\d"));
	TEST_CHECK(lsplit_path("\\b\\c\\d", 1) == r("b", "c\\d"));
	TEST_CHECK(lsplit_path("\\b\\c\\d", 2) == r("b", "c\\d"));
	TEST_CHECK(lsplit_path("\\b\\c\\d", 3) == r("b\\c", "d"));
	TEST_CHECK(lsplit_path("\\b\\c\\d", 4) == r("b\\c", "d"));
	TEST_CHECK(lsplit_path("\\b\\c\\d", 5) == r("b\\c\\d", ""));
	TEST_CHECK(lsplit_path("\\b\\c\\d", 6) == r("b\\c\\d", ""));

	TEST_CHECK(lsplit_path("b\\c\\d", 0) == r("b", "c\\d"));
	TEST_CHECK(lsplit_path("b\\c\\d", 1) == r("b", "c\\d"));
	TEST_CHECK(lsplit_path("b\\c\\d", 2) == r("b\\c", "d"));
	TEST_CHECK(lsplit_path("b\\c\\d", 3) == r("b\\c", "d"));
	TEST_CHECK(lsplit_path("b\\c\\d", 4) == r("b\\c\\d", ""));
	TEST_CHECK(lsplit_path("b\\c\\d", 5) == r("b\\c\\d", ""));
#endif
	TEST_CHECK(lsplit_path("/b/c/d", 0) == r("b", "c/d"));
	TEST_CHECK(lsplit_path("/b/c/d", 1) == r("b", "c/d"));
	TEST_CHECK(lsplit_path("/b/c/d", 2) == r("b", "c/d"));
	TEST_CHECK(lsplit_path("/b/c/d", 3) == r("b/c", "d"));
	TEST_CHECK(lsplit_path("/b/c/d", 4) == r("b/c", "d"));
	TEST_CHECK(lsplit_path("/b/c/d", 5) == r("b/c/d", ""));
	TEST_CHECK(lsplit_path("/b/c/d", 6) == r("b/c/d", ""));

	TEST_CHECK(lsplit_path("b/c/d", 0) == r("b", "c/d"));
	TEST_CHECK(lsplit_path("b/c/d", 1) == r("b", "c/d"));
	TEST_CHECK(lsplit_path("b/c/d", 2) == r("b/c", "d"));
	TEST_CHECK(lsplit_path("b/c/d", 3) == r("b/c", "d"));
	TEST_CHECK(lsplit_path("b/c/d", 4) == r("b/c/d", ""));
	TEST_CHECK(lsplit_path("b/c/d", 5) == r("b/c/d", ""));
}

TORRENT_TEST(hard_link)
{
	// try to create a hard link to see what happens
	// first create a regular file to then add another link to.

	// create a file, write some stuff to it, create a hard link to that file,
	// read that file and assert we get the same stuff we wrote to the first file
	lt::span<char const> str = "abcdefghijklmnopqrstuvwxyz";
	ofstream("original_file").write(str.data(), str.size());

	error_code ec;
	hard_link("original_file", "second_link", ec);

	if (ec)
		std::printf("hard_link failed: [%s] %s\n", ec.category().name(), ec.message().c_str());
	TEST_EQUAL(ec, error_code());


	char test_buf[27] = {};
	std::ifstream("second_link").read(test_buf, 27);
	TEST_CHECK(test_buf == "abcdefghijklmnopqrstuvwxyz"_sv);

	remove("original_file", ec);
	if (ec)
		std::printf("remove failed: [%s] %s\n", ec.category().name(), ec.message().c_str());

	remove("second_link", ec);
	if (ec)
		std::printf("remove failed: [%s] %s\n", ec.category().name(), ec.message().c_str());
}

TORRENT_TEST(stat_file)
{
	file_status st;
	error_code ec;
	stat_file("no_such_file_or_directory.file", &st, ec);
	TEST_CHECK(ec);
	TEST_EQUAL(ec, boost::system::errc::no_such_file_or_directory);
}

TORRENT_TEST(relative_path)
{
#ifdef TORRENT_WINDOWS
#define S "\\"
#else
#define S "/"
#endif
	TEST_EQUAL(lexically_relative("A" S "B" S "C", "A" S "C" S "B")
		, ".." S ".." S "C" S "B");

	TEST_EQUAL(lexically_relative("A" S "B" S "C" S, "A" S "C" S "B")
		, ".." S ".." S "C" S "B");

	TEST_EQUAL(lexically_relative("A" S "B" S "C" S, "A" S "C" S "B" S)
		, ".." S ".." S "C" S "B");

	TEST_EQUAL(lexically_relative("A" S "B" S "C", "A" S "B" S "B")
		, ".." S "B");

	TEST_EQUAL(lexically_relative("A" S "B" S "C", "A" S "B" S "C")
		, "");

	TEST_EQUAL(lexically_relative("A" S "B", "A" S "B")
		, "");

	TEST_EQUAL(lexically_relative("A" S "B", "A" S "B" S "C")
		, "C");

	TEST_EQUAL(lexically_relative("A" S, "A" S)
		, "");

	TEST_EQUAL(lexically_relative("", "A" S "B" S "C")
		, "A" S "B" S "C");

	TEST_EQUAL(lexically_relative("A" S "B" S "C", "")
		, ".." S ".." S ".." S);

	TEST_EQUAL(lexically_relative("", ""), "");
}

// UNC tests
#if TORRENT_USE_UNC_PATHS

namespace {
std::tuple<int, bool> current_directory_caps()
{
#ifdef TORRENT_WINDOWS
	DWORD maximum_component_length = FILENAME_MAX;
	DWORD file_system_flags = 0;
	if (GetVolumeInformation(nullptr
		, nullptr, 0, nullptr
		, &maximum_component_length, &file_system_flags, nullptr, 0) == 0)
	{
		error_code ec(GetLastError(), system_category());
		std::printf("GetVolumeInformation: [%s : %d] %s\n"
			, ec.category().name(), ec.value(), ec.message().c_str());
		// ignore errors, this is best-effort
	}
	bool const support_hard_links = ((file_system_flags & FILE_SUPPORTS_HARD_LINKS) != 0);
	return std::make_tuple(int(maximum_component_length), support_hard_links);
#else
	return std::make_tuple(255, true);
#endif
}
} // anonymous namespace

TORRENT_TEST(unc_tests)
{
	using lt::canonicalize_path;
	TEST_EQUAL(canonicalize_path("c:\\a\\..\\b"), "c:\\b");
	TEST_EQUAL(canonicalize_path("a\\..\\b"), "b");
	TEST_EQUAL(canonicalize_path("a\\..\\.\\b"), "b");
	TEST_EQUAL(canonicalize_path("\\.\\a"), "\\a");
	TEST_EQUAL(canonicalize_path("\\\\bla\\.\\a"), "\\\\bla\\a");
	TEST_EQUAL(canonicalize_path("c:\\bla\\a"), "c:\\bla\\a");

	error_code ec;

	std::vector<std::string> special_names
	{
		"CON", "PRN", "AUX", "NUL",
		"COM1", "COM2", "COM3", "COM4",
		"COM5", "COM6", "COM7", "COM8",
		"COM9", "LPT1", "LPT2", "LPT3",
		"LPT4", "LPT5", "LPT6", "LPT7",
		"LPT8", "LPT9"
	};

	for (std::string special_name : special_names)
	{
		touch_file(special_name, 10);
		TEST_CHECK(exists(special_name));
		lt::remove(special_name, ec);
		TEST_EQUAL(ec, error_code());
		TEST_CHECK(!exists(special_name));
	}

	int maximum_component_length;
	bool support_hard_links;
	std::tie(maximum_component_length, support_hard_links) = current_directory_caps();

	std::cout << "max file path component length: " << maximum_component_length << "\n"
		<< "support hard links: " << (support_hard_links?"yes":"no") << "\n";

	std::string long_dir_name;
	maximum_component_length -= 12;
	long_dir_name.resize(size_t(maximum_component_length));
	for (int i = 0; i < maximum_component_length; ++i)
		long_dir_name[i] = static_cast<char>((i % 26) + 'A');

	std::string long_file_name1 = combine_path(long_dir_name, long_dir_name);
	long_file_name1.back() = '1';
	std::string long_file_name2 = long_file_name1;
	long_file_name2.back() = '2';

	lt::create_directory(long_dir_name, ec);
	TEST_EQUAL(ec, error_code());
	if (ec)
	{
		std::cout << "create_directory \"" << long_dir_name << "\" failed: " << ec.message() << "\n";
		std::wcout << convert_to_native_path_string(long_dir_name) << L"\n";
	}
	TEST_CHECK(exists(long_dir_name));
	TEST_CHECK(lt::is_directory(long_dir_name, ec));
	TEST_EQUAL(ec, error_code());
	if (ec)
	{
		std::cout << "is_directory \"" << long_dir_name << "\" failed " << ec.message() << "\n";
		std::wcout << convert_to_native_path_string(long_dir_name) << L"\n";
	}

	touch_file(long_file_name1, 10);
	TEST_CHECK(exists(long_file_name1));

	lt::rename(long_file_name1, long_file_name2, ec);
	TEST_EQUAL(ec, error_code());
	if (ec)
	{
		std::cout << "rename \"" << long_file_name1 << "\" failed " << ec.message() << "\n";
		std::wcout << convert_to_native_path_string(long_file_name1) << L"\n";
	}
	TEST_CHECK(!exists(long_file_name1));
	TEST_CHECK(exists(long_file_name2));

	lt::copy_file(long_file_name2, long_file_name1, ec);
	TEST_EQUAL(ec, error_code());
	if (ec)
	{
		std::cout << "copy_file \"" << long_file_name2 << "\" failed " << ec.message() << "\n";
		std::wcout << convert_to_native_path_string(long_file_name2) << L"\n";
	}
	TEST_CHECK(exists(long_file_name1));

	std::set<std::string> files;

	for (lt::aux::directory i(long_dir_name, ec); !i.done(); i.next(ec))
	{
		std::string f = i.file();
		files.insert(f);
	}

	TEST_EQUAL(files.size(), 4);

	lt::remove(long_file_name1, ec);
	TEST_EQUAL(ec, error_code());
	if (ec)
	{
		std::cout << "remove \"" << long_file_name1 << "\" failed " << ec.message() << "\n";
		std::wcout << convert_to_native_path_string(long_file_name1) << L"\n";
	}
	TEST_CHECK(!exists(long_file_name1));

	if (support_hard_links)
	{
		lt::hard_link(long_file_name2, long_file_name1, ec);
		TEST_EQUAL(ec, error_code());
		TEST_CHECK(exists(long_file_name1));

		lt::remove(long_file_name1, ec);
		TEST_EQUAL(ec, error_code());
		TEST_CHECK(!exists(long_file_name1));
	}
}

TORRENT_TEST(unc_paths)
{
	std::string const reserved_name = "con";
	error_code ec;
	{
		file f(reserved_name, aux::open_mode::write, ec);
		TEST_CHECK(!ec);
	}
	remove(reserved_name, ec);
	TEST_CHECK(!ec);
}

TORRENT_TEST(to_file_open_mode)
{
	TEST_CHECK(aux::to_file_open_mode(aux::open_mode::write) == file_open_mode::read_write);
	TEST_CHECK(aux::to_file_open_mode({}) == file_open_mode::read_only);
	TEST_CHECK(aux::to_file_open_mode(aux::open_mode::no_atime) == (file_open_mode::read_only | file_open_mode::no_atime));
	TEST_CHECK(aux::to_file_open_mode(aux::open_mode::write | aux::open_mode::no_atime) == (file_open_mode::read_write | file_open_mode::no_atime));
}


#endif
