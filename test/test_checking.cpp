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
#include "setup_transfer.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/torrent_status.hpp"
#include "libtorrent/hex.hpp" // to_hex
#include "libtorrent/aux_/path.hpp"

static const int file_sizes[] =
{ 0, 5, 16 - 5, 16000, 17, 10, 8000, 8000, 1,1,1,1,1,100,1,1,1,1,100,1,1,1,1,1,1
	,1,1,1,1,1,1,13,65000,34,75,2,30,400,500,23000,900,43000,400,4300,6, 4};
const int num_files = sizeof(file_sizes)/sizeof(file_sizes[0]);

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

void test_checking(int flags = read_only_files)
{
	using namespace lt;

	std::printf("\n==== TEST CHECKING %s%s%s%s=====\n\n"
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
	if (ec) std::printf("ERROR: set_piece_hashes: (%d) %s\n"
		, ec.value(), ec.message().c_str());

	std::vector<char> buf;
	bencode(std::back_inserter(buf), t.generate());
	auto ti = std::make_shared<torrent_info>(buf, ec, from_span);

	std::printf("generated torrent: %s test_torrent_dir\n"
		, aux::to_hex(ti->info_hash()).c_str());

	// truncate every file in half
	if (flags & incomplete_files)
	{
		for (int i = 0; i < num_files; ++i)
		{
			char name[1024];
			std::snprintf(name, sizeof(name), "test%d", i);
			char dirname[200];
			std::snprintf(dirname, sizeof(dirname), "test_dir%d", i / 5);
			std::string path = combine_path("test_torrent_dir", dirname);
			path = combine_path(path, name);

			error_code ec;
			file f(path, aux::open_mode::write, ec);
			if (ec) std::printf("ERROR: opening file \"%s\": (%d) %s\n"
				, path.c_str(), ec.value(), ec.message().c_str());
			f.set_size(file_sizes[i] / 2, ec);
			if (ec) std::printf("ERROR: truncating file \"%s\": (%d) %s\n"
				, path.c_str(), ec.value(), ec.message().c_str());
		}
	}

	// overwrite the files with new random data
	if (flags & corrupt_files)
	{
		std::printf("corrupt file test. overwriting files\n");
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
		std::printf("making files read-only\n");
		for (int i = 0; i < num_files; ++i)
		{
			char name[1024];
			std::snprintf(name, sizeof(name), "test%d", i);
			char dirname[200];
			std::snprintf(dirname, sizeof(dirname), "test_dir%d", i / 5);

			std::string path = combine_path("test_torrent_dir", dirname);
			path = combine_path(path, name);
			std::printf("   %s\n", path.c_str());

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
		if (ec) std::printf("ERROR: removing \"test_torrent_dir_tmp\": (%d) %s\n"
			, ec.value(), ec.message().c_str());
		rename("test_torrent_dir", "test_torrent_dir_tmp", ec);
		if (ec) std::printf("ERROR: renaming dir \"test_torrent_dir\": (%d) %s\n"
			, ec.value(), ec.message().c_str());
	}

	auto const mask = alert::all_categories
		& ~(alert::progress_notification
			| alert::performance_warning
			| alert::stats_notification);

	settings_pack pack;
	pack.set_bool(settings_pack::enable_lsd, false);
	pack.set_bool(settings_pack::enable_natpmp, false);
	pack.set_bool(settings_pack::enable_upnp, false);
	pack.set_bool(settings_pack::enable_dht, false);
	pack.set_int(settings_pack::alert_mask, mask);
	pack.set_str(settings_pack::listen_interfaces, "0.0.0.0:48000");
	pack.set_int(settings_pack::max_retry_port_bind, 1000);
	lt::session ses1(pack);

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

		std::printf("%d %f %s\n", st.state, st.progress_ppm / 10000.f, st.errc.message().c_str());

		if (
#ifndef TORRENT_NO_DEPRECATE
			st.state != torrent_status::queued_for_checking &&
#endif
			st.state != torrent_status::checking_files
			&& st.state != torrent_status::checking_resume_data)
			break;

		if (st.errc) break;
		std::this_thread::sleep_for(lt::milliseconds(500));
	}

	if (flags & incomplete_files)
	{
		TEST_CHECK(!st.is_seeding);

		std::this_thread::sleep_for(lt::milliseconds(500));
		st = tor1.status();
		TEST_CHECK(!st.is_seeding);
	}

	if (flags & corrupt_files)
	{
		TEST_CHECK(!st.is_seeding);

		if (flags & read_only_files)
		{
			// we expect our checking of the files to trigger
			// attempts to truncate them, since the files are
			// read-only here, we expect the checking to fail.
			TEST_CHECK(st.errc);
			if (st.errc)
				std::printf("error: %s\n", st.errc.message().c_str());

			// wait a while to make sure libtorrent survived the error
			std::this_thread::sleep_for(lt::milliseconds(1000));

			st = tor1.status();
			TEST_CHECK(!st.is_seeding);
			TEST_CHECK(st.errc);
			if (st.errc)
				std::printf("error: %s\n", st.errc.message().c_str());
		}
		else
		{
			TEST_CHECK(!st.errc);
			if (st.errc)
				std::printf("error: %s\n", st.errc.message().c_str());
		}
	}

	if ((flags & (incomplete_files | corrupt_files)) == 0)
	{
		TEST_CHECK(st.is_seeding);
		if (st.errc)
			std::printf("error: %s\n", st.errc.message().c_str());
	}

	// make the files writable again
	if (flags & read_only_files)
	{
		for (int i = 0; i < num_files; ++i)
		{
			char name[1024];
			std::snprintf(name, sizeof(name), "test%d", i);
			char dirname[200];
			std::snprintf(dirname, sizeof(dirname), "test_dir%d", i / 5);

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
	if (ec) std::printf("ERROR: removing test_torrent_dir: (%d) %s\n"
		, ec.value(), ec.message().c_str());
}

TORRENT_TEST(checking)
{
	test_checking();
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

