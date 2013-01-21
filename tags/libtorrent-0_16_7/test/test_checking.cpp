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

#include "libtorrent/session.hpp"
#include "test.hpp"
#include "setup_transfer.hpp"
#include "libtorrent/create_torrent.hpp"

#include <fstream>

static const int file_sizes[] =
{ 5, 16 - 5, 16000, 17, 10, 8000, 8000, 1,1,1,1,1,100,1,1,1,1,100,1,1,1,1,1,1
	,1,1,1,1,1,1,13,65000,34,75,2,30,400,500,23000,900,43000,400,4300,6, 4};
const int num_files = sizeof(file_sizes)/sizeof(file_sizes[0]);

void test_checking(bool read_only_files, bool corrupt_files = false)
{
	using namespace libtorrent;

	fprintf(stderr, "==== TEST CHECKING %s%s=====\n"
		, read_only_files?"read-only-files ":""
		, corrupt_files?"corrupt ":"");

	// make the files writable again
	for (int i = 0; i < num_files; ++i)
	{
		char name[1024];
		snprintf(name, sizeof(name), "test%d", i);
		std::string path = combine_path("tmp1_checking", "test_torrent_dir");
		path = combine_path(path, name);
#ifdef TORRENT_WINDOWS
		SetFileAttributesA(path.c_str(), FILE_ATTRIBUTE_NORMAL);
#else
		chmod(path.c_str(), S_IRUSR | S_IWUSR);
#endif
	}

	// in case the previous run was terminated
	error_code ec;
	remove_all("tmp1_checking", ec);
	if (ec) fprintf(stderr, "ERROR: removing tmp1_checking: (%d) %s\n"
		, ec.value(), ec.message().c_str());

	create_directory("tmp1_checking", ec);
	if (ec) fprintf(stderr, "ERROR: creating directory tmp1_checking: (%d) %s\n"
		, ec.value(), ec.message().c_str());
	create_directory(combine_path("tmp1_checking", "test_torrent_dir"), ec);
	if (ec) fprintf(stderr, "ERROR: creating directory test_torrent_dir: (%d) %s\n"
		, ec.value(), ec.message().c_str());
	
	file_storage fs;
	std::srand(10);
	int piece_size = 0x4000;

	create_random_files(combine_path("tmp1_checking", "test_torrent_dir"), file_sizes, num_files);

	add_files(fs, combine_path("tmp1_checking", "test_torrent_dir"));
	libtorrent::create_torrent t(fs, piece_size, 0x4000, libtorrent::create_torrent::optimize);

	// calculate the hash for all pieces
	set_piece_hashes(t, "tmp1_checking", ec);
	if (ec) fprintf(stderr, "ERROR: set_piece_hashes: (%d) %s\n"
		, ec.value(), ec.message().c_str());

	std::vector<char> buf;
	bencode(std::back_inserter(buf), t.generate());
	boost::intrusive_ptr<torrent_info> ti(new torrent_info(&buf[0], buf.size(), ec));

	fprintf(stderr, "generated torrent: %s tmp1_checking/test_torrent_dir\n"
		, to_hex(ti->info_hash().to_string()).c_str());

	// overwrite the files with new random data
	if (corrupt_files)
	{
		// increase the size of some files. When they're read only that forces
		// the checker to open them in write-mode to truncate them
		static const int file_sizes2[] =
		{ 5, 16 - 5, 16001, 30, 10, 8000, 8000, 1,1,1,1,1,100,1,1,1,1,100,1,1,1,1,1,1
			,1,1,1,1,1,1,13,65000,34,75,2,30,400,500,23000,900,43000,400,4300,6, 4};
		create_random_files(combine_path("tmp1_checking", "test_torrent_dir"), file_sizes2, num_files);
	}

	// make the files read only
	if (read_only_files)
	{
		for (int i = 0; i < num_files; ++i)
		{
			char name[1024];
			snprintf(name, sizeof(name), "test%d", i);
			std::string path = combine_path("tmp1_checking", "test_torrent_dir");
			path = combine_path(path, name);
#ifdef TORRENT_WINDOWS
			SetFileAttributesA(path.c_str(), FILE_ATTRIBUTE_READONLY);
#else
			chmod(path.c_str(), S_IRUSR);
#endif
		}
	}

	session ses1(fingerprint("LT", 0, 1, 0, 0), std::make_pair(48000, 49000), "0.0.0.0", 0);
	ses1.set_alert_mask(alert::all_categories);

	add_torrent_params p;
	p.save_path = "tmp1_checking";
	p.ti = ti;
	torrent_handle tor1 = ses1.add_torrent(p, ec);
	TEST_CHECK(!ec);

	torrent_status st;
	for (int i = 0; i < 5; ++i)
	{
		print_alerts(ses1, "ses1");

		st = tor1.status();

		printf("%d %f %s\n", st.state, st.progress_ppm / 10000.f, st.error.c_str());

		if (st.state != torrent_status::queued_for_checking
			&& st.state != torrent_status::checking_files
			&& st.state != torrent_status::checking_resume_data)
			break;

		if (!st.error.empty()) break;
		test_sleep(1000);
	}
	if (corrupt_files)
	{
		TEST_CHECK(!st.is_seeding);
		TEST_CHECK(!st.error.empty());
		// wait a while to make sure libtorrent survived the error
		test_sleep(5000);

		st = tor1.status();
		TEST_CHECK(!st.is_seeding);
		TEST_CHECK(!st.error.empty());
	}
	else
	{
		TEST_CHECK(st.is_seeding);
	}

	// make the files writable again
	if (read_only_files)
	{
		for (int i = 0; i < num_files; ++i)
		{
			char name[1024];
			snprintf(name, sizeof(name), "test%d", i);
			std::string path = combine_path("tmp1_checking", "test_torrent_dir");
			path = combine_path(path, name);
#ifdef TORRENT_WINDOWS
			SetFileAttributesA(path.c_str(), FILE_ATTRIBUTE_NORMAL);
#else
			chmod(path.c_str(), S_IRUSR | S_IWUSR);
#endif
		}
	}

	remove_all("tmp1_checking", ec);
	if (ec) fprintf(stderr, "ERROR: removing tmp1_checking: (%d) %s\n"
		, ec.value(), ec.message().c_str());
}

int test_main()
{
	test_checking(false);
	test_checking(true);
	test_checking(true, true);

	return 0;
}

