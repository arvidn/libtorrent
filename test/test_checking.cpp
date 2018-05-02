/*

Copyright (c) 2013, Arvid Norberg
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

#include <sys/stat.h> // for chmod

#include "libtorrent/session.hpp"
#include "test.hpp"
#include "settings.hpp"
#include "setup_transfer.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/torrent_status.hpp"

namespace
{
	const int file_sizes[] =
	{ 0, 5, 16 - 5, 16000, 17, 10, 8000, 8000, 1,1,1,1,1,100,1,1,1,1,100,1,1,1,1,1,1
		,1,1,1,1,1,1,13,65000,34,75,2,30,400,500,23000,900,43000,400,4300,6, 4 };
	const int num_files = sizeof(file_sizes) / sizeof(file_sizes[0]);

	bool is_checking(int const state)
	{
		return state == lt::torrent_status::checking_files
#ifndef TORRENT_NO_DEPRECATE
			|| state == lt::torrent_status::queued_for_checking
#endif
			|| state == lt::torrent_status::checking_resume_data;
	};
}

enum
{
	// make sure we don't accidentally require files to be writable just to
	// check their hashes
	read_only_files = 1,

	// make sure we detect corrupt files and mark the appropriate pieces
	// as not had
	corrupt_files = 2,

	incomplete_files = 4,

	// make the files not be there when starting up, move the files in place and
	// force-recheck. Make sure the stat cache is cleared and let us pick up the
	// new files
	force_recheck = 8,
};

void test_checking(int flags)
{
	using namespace libtorrent;
	namespace lt = libtorrent;

	fprintf(stdout, "\n==== TEST CHECKING %s%s%s%s=====\n\n"
		, (flags & read_only_files) ? "read-only-files ":""
		, (flags & corrupt_files) ? "corrupt ":""
		, (flags & incomplete_files) ? "incomplete ":""
		, (flags & force_recheck) ? "force_recheck ":"");

	error_code ec;
	create_directory("test_torrent_dir", ec);
	if (ec) fprintf(stdout, "ERROR: creating directory test_torrent_dir: (%d) %s\n"
		, ec.value(), ec.message().c_str());

	file_storage fs;
	std::srand(10);
	int piece_size = 0x4000;

	create_random_files("test_torrent_dir", file_sizes, num_files, &fs);

	lt::create_torrent t(fs, piece_size, 0x4000
		, lt::create_torrent::optimize_alignment);

	// calculate the hash for all pieces
	set_piece_hashes(t, ".", ec);
	if (ec) fprintf(stdout, "ERROR: set_piece_hashes: (%d) %s\n"
		, ec.value(), ec.message().c_str());

	std::vector<char> buf;
	bencode(std::back_inserter(buf), t.generate());
	boost::shared_ptr<torrent_info> ti(new torrent_info(&buf[0], buf.size(), ec));

	fprintf(stdout, "generated torrent: %s test_torrent_dir\n"
		, to_hex(ti->info_hash().to_string()).c_str());

	// truncate every file in half
	if (flags & incomplete_files)
	{
		for (int i = 0; i < num_files; ++i)
		{
			char name[1024];
			snprintf(name, sizeof(name), "test%d", i);
			char dirname[200];
			snprintf(dirname, sizeof(dirname), "test_dir%d", i / 5);
			std::string path = combine_path("test_torrent_dir", dirname);
			path = combine_path(path, name);

			error_code ec;
			file f(path, file::read_write, ec);
			if (ec) fprintf(stdout, "ERROR: opening file \"%s\": (%d) %s\n"
				, path.c_str(), ec.value(), ec.message().c_str());
			f.set_size(file_sizes[i] / 2, ec);
			if (ec) fprintf(stdout, "ERROR: truncating file \"%s\": (%d) %s\n"
				, path.c_str(), ec.value(), ec.message().c_str());
		}
	}

	// overwrite the files with new random data
	if (flags & corrupt_files)
	{
		fprintf(stdout, "corrupt file test. overwriting files\n");
		// increase the size of some files. When they're read only that forces
		// the checker to open them in write-mode to truncate them
		static const int file_sizes2[] =
		{ 0, 5, 16 - 5, 16001, 30, 10, 8000, 8000, 1,1,1,1,1,100,1,1,1,1,100,1,1,1,1,1,1
			,1,1,1,1,1,1,13,65000,34,75,2,30,400,500,23000,900,43000,400,4300,6, 4};
		create_random_files("test_torrent_dir", file_sizes2, num_files);
	}

	// make the files read only
	if (flags & read_only_files)
	{
		fprintf(stdout, "making files read-only\n");
		for (int i = 0; i < num_files; ++i)
		{
			char name[1024];
			snprintf(name, sizeof(name), "test%d", i);
			char dirname[200];
			snprintf(dirname, sizeof(dirname), "test_dir%d", i / 5);

			std::string path = combine_path("test_torrent_dir", dirname);
			path = combine_path(path, name);
			fprintf(stdout, "   %s\n", path.c_str());

#ifdef TORRENT_WINDOWS
			SetFileAttributesA(path.c_str(), FILE_ATTRIBUTE_READONLY);
#else
			chmod(path.c_str(), S_IRUSR);
#endif
		}
	}

	if (flags & force_recheck)
	{
		remove_all("test_torrent_dir_tmp", ec);
		if (ec) fprintf(stdout, "ERROR: removing \"test_torrent_dir_tmp\": (%d) %s\n"
			, ec.value(), ec.message().c_str());
		rename("test_torrent_dir", "test_torrent_dir_tmp", ec);
		if (ec) fprintf(stdout, "ERROR: renaming dir \"test_torrent_dir\": (%d) %s\n"
			, ec.value(), ec.message().c_str());
	}

	lt::session ses1(settings());

	add_torrent_params p;
	p.save_path = ".";
	p.ti = ti;
	torrent_handle tor1 = ses1.add_torrent(p, ec);
	TEST_CHECK(!ec);

	if (flags & force_recheck)
	{
		// first make sure the session tries to check for the file and can't find
		// them
		libtorrent::alert const* a = wait_for_alert(
			ses1, torrent_checked_alert::alert_type, "checking");
		TEST_CHECK(a);

		// now, move back the files and force-recheck. make sure we pick up the
		// files this time
		remove_all("test_torrent_dir", ec);
		if (ec) fprintf(stdout, "ERROR: removing \"test_torrent_dir\": (%d) %s\n"
			, ec.value(), ec.message().c_str());
		rename("test_torrent_dir_tmp", "test_torrent_dir", ec);
		if (ec) fprintf(stdout, "ERROR: renaming dir \"test_torrent_dir_tmp\": (%d) %s\n"
			, ec.value(), ec.message().c_str());
		tor1.force_recheck();
	}

	torrent_status st;
	for (int i = 0; i < 20; ++i)
	{
		print_alerts(ses1, "ses1");

		st = tor1.status();

		printf("%d %f %s\n", st.state, st.progress_ppm / 10000.f, st.errc.message().c_str());

		if (!is_checking(st.state) || st.errc) break;
		test_sleep(500);
	}

	if (flags & incomplete_files)
	{
		TEST_CHECK(!st.is_seeding);

		test_sleep(500);
		st = tor1.status();
		TEST_CHECK(!st.is_seeding);
	}

	if (flags & corrupt_files)
	{
		TEST_CHECK(!st.is_seeding);

		TEST_CHECK(!st.errc);
		if (st.errc)
			fprintf(stdout, "error: %s\n", st.errc.message().c_str());
	}

	if ((flags & (incomplete_files | corrupt_files)) == 0)
	{
		TEST_CHECK(st.is_seeding);
		if (st.errc)
			fprintf(stdout, "error: %s\n", st.errc.message().c_str());
	}

	// make the files writable again
	if (flags & read_only_files)
	{
		for (int i = 0; i < num_files; ++i)
		{
			char name[1024];
			snprintf(name, sizeof(name), "test%d", i);
			char dirname[200];
			snprintf(dirname, sizeof(dirname), "test_dir%d", i / 5);

			std::string path = combine_path("test_torrent_dir", dirname);
			path = combine_path(path, name);
#ifdef TORRENT_WINDOWS
			SetFileAttributesA(path.c_str(), FILE_ATTRIBUTE_NORMAL);
#else
			chmod(path.c_str(), S_IRUSR | S_IWUSR);
#endif
		}
	}

	remove_all("test_torrent_dir", ec);
	if (ec) fprintf(stdout, "ERROR: removing test_torrent_dir: (%d) %s\n"
		, ec.value(), ec.message().c_str());
}

TORRENT_TEST(checking)
{
	test_checking(0);
}

TORRENT_TEST(read_only_corrupt)
{
	test_checking(read_only_files | corrupt_files);
}

TORRENT_TEST(read_only)
{
	test_checking(read_only_files);
}

TORRENT_TEST(incomplete)
{
	test_checking(incomplete_files);
}

TORRENT_TEST(corrupt)
{
	test_checking(corrupt_files);
}

TORRENT_TEST(force_recheck)
{
	test_checking(force_recheck);
}

TORRENT_TEST(discrete_checking)
{
	using namespace lt;
	printf("\n==== TEST CHECKING discrete =====\n\n");
	error_code ec;
	create_directory("test_torrent_dir", ec);
	if (ec) printf("ERROR: creating directory test_torrent_dir: (%d) %s\n", ec.value(), ec.message().c_str());

	int const megabyte = 0x100000;
	int const piece_size = 2 * megabyte;
	int const file_sizes[] = { 9 * megabyte, 4 * megabyte };
	int const num_files = sizeof(file_sizes) / sizeof(file_sizes[0]);

	file_storage fs;
	create_random_files("test_torrent_dir", file_sizes, num_files, &fs);
	lt::create_torrent t(fs, piece_size, 0, lt::create_torrent::optimize_alignment);
	set_piece_hashes(t, ".", ec);
	if (ec) printf("ERROR: set_piece_hashes: (%d) %s\n", ec.value(), ec.message().c_str());

	std::vector<char> buf;
	bencode(std::back_inserter(buf), t.generate());
	boost::shared_ptr<torrent_info> ti(new torrent_info(&buf[0], buf.size(), ec));
	printf("generated torrent: %s test_torrent_dir\n", to_hex(ti->info_hash().to_string()).c_str());
	TEST_EQUAL(ti->num_files(), 3);
	{
		session ses1(settings());
		add_torrent_params p;
		p.file_priorities.resize(ti->num_files());
		p.file_priorities[0] = 1;
		p.save_path = ".";
		p.ti = ti;
		torrent_handle tor1 = ses1.add_torrent(p, ec);
		// change the priority of a file while checking and make sure it doesn't interrupt the checking.
		std::vector<int> prio(ti->num_files(), 0);
		prio[2] = 1;
		tor1.prioritize_files(prio);
		TEST_CHECK(wait_for_alert(ses1, torrent_checked_alert::alert_type, "torrent checked", 1, seconds(50)));
		TEST_CHECK(tor1.status(0).is_seeding);
	}
	remove_all("test_torrent_dir", ec);
	if (ec) fprintf(stdout, "ERROR: removing test_torrent_dir: (%d) %s\n", ec.value(), ec.message().c_str());
}
