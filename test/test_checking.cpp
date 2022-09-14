/*

Copyright (c) 2013-2021, Arvid Norberg
Copyright (c) 2017, Steven Siloti
Copyright (c) 2018, Alden Torres
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

#include <sys/stat.h> // for chmod

#include "libtorrent/session.hpp"
#include "libtorrent/session_params.hpp"
#include "test.hpp"
#include "settings.hpp"
#include "setup_transfer.hpp"
#include "test_utils.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/torrent_status.hpp"
#include "libtorrent/hex.hpp" // to_hex
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/aux_/open_mode.hpp"

namespace {

namespace
{
	bool is_checking(int const state)
	{
		return state == lt::torrent_status::checking_files
#if TORRENT_ABI_VERSION == 1
			|| state == lt::torrent_status::queued_for_checking
#endif
			|| state == lt::torrent_status::checking_resume_data;
	}
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

	// files that are *bigger* than expected still considered OK. We don't
	// truncate files willy-nilly. Checking is a read-only operation.
	extended_files = 16,

	v2 = 32,

	single_file = 64,
};

void test_checking(int const flags)
{
	using namespace lt;

	std::printf("\n==== TEST CHECKING %s%s%s%s%s%s%s=====\n\n"
		, (flags & read_only_files) ? "read-only-files ":""
		, (flags & corrupt_files) ? "corrupt ":""
		, (flags & incomplete_files) ? "incomplete ":""
		, (flags & force_recheck) ? "force_recheck ":""
		, (flags & extended_files) ? "extended_files ":""
		, (flags & v2) ? "v2 ":""
		, (flags & single_file) ? "single_file ":"");

	error_code ec;
	create_directory("test_torrent_dir", ec);
	if (ec) fprintf(stdout, "ERROR: creating directory test_torrent_dir: (%d) %s\n"
		, ec.value(), ec.message().c_str());

	file_storage fs;
	int const piece_size = (flags & single_file) ? 0x8000 : 0x4000;

	auto const file_sizes = (flags & single_file)
		? std::vector<int>{500000}
		: std::vector<int>{0, 5, 16 - 5, 16000, 17, 10, 8000, 8000, 1,1,1,1,1,100,1,1,1,1,100,1,1,1,1,1,1
		,1,1,1,1,1,1,13,65000,34,75,2,30,400,50000,73000,900,43000,400,4300,6, 4 };

	create_random_files("test_torrent_dir", file_sizes, &fs);

	lt::create_torrent t(fs, piece_size, (flags & v2) ? create_torrent::v2_only : create_torrent::v1_only);

	// calculate the hash for all pieces
	set_piece_hashes(t, ".", ec);
	if (ec) std::printf("ERROR: set_piece_hashes: (%d) %s\n"
		, ec.value(), ec.message().c_str());

	std::vector<char> buf;
	bencode(std::back_inserter(buf), t.generate());
	auto ti = std::make_shared<torrent_info>(buf, ec, from_span);
	TEST_CHECK(ti->is_valid());

	std::printf("generated torrent: %s test_torrent_dir\n"
		, aux::to_hex(ti->info_hashes().v1).c_str());

	// truncate every file in half
	if (flags & (incomplete_files | extended_files))
	{
		for (std::size_t i = 0; i < file_sizes.size(); ++i)
		{
			if ((i & 1) == 1) continue;
			char name[1024];
			std::snprintf(name, sizeof(name), "test%d", int(i));
			char dirname[200];
			std::snprintf(dirname, sizeof(dirname), "test_dir%d", int(i) / 5);
			std::string path = combine_path("test_torrent_dir", dirname);
			path = combine_path(path, name);

			std::int64_t const new_len = (flags & extended_files)
				? file_sizes[i] + 10
				: file_sizes[i] * 2 / 3;

			int const ret = ::truncate(path.c_str(), new_len);
			if (ret < 0)
			{
				std::printf("ERROR: truncating file \"%s\": (%d) %s\n"
					, path.c_str(), errno, strerror(errno));
			}
		}
	}

	// overwrite the files with new random data
	if (flags & corrupt_files)
	{
		std::printf("corrupt file test. overwriting files\n");
		// increase the size of some files. When they're read only that forces
		// the checker to open them in write-mode to truncate them
		std::vector<int> const file_sizes2
		{{ 0, 5, 16 - 5, 16001, 30, 10, 8000, 8000, 1,1,1,1,1,100,1,1,1,1,100,1,1,1,1,1,1
			,1,1,1,1,1,1,13,65000,34,75,2,30,400,500,23000,900,43000,400,4300,6, 4}};
		create_random_files("test_torrent_dir", (flags & single_file) ? file_sizes : file_sizes2);
	}

	// make the files read only
	if (flags & read_only_files)
	{
		std::printf("making files read-only\n");
		for (std::size_t i = 0; i < file_sizes.size(); ++i)
		{
			char name[1024];
			std::snprintf(name, sizeof(name), "test%d", int(i));
			char dirname[200];
			std::snprintf(dirname, sizeof(dirname), "test_dir%d", int(i) / 5);

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
		lt::alert const* a = wait_for_alert(
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

		std::printf("%d %f %s\n", st.state, st.progress_ppm / 10000.0, st.errc.message().c_str());

		if (!is_checking(st.state) || st.errc) break;
		std::this_thread::sleep_for(lt::milliseconds(500));
	}

	if (flags & (incomplete_files | corrupt_files))
	{
		TEST_CHECK(!st.is_seeding);

		std::this_thread::sleep_for(lt::milliseconds(500));
		st = tor1.status();

		TEST_CHECK(!st.errc);
		if (st.errc)
			std::printf("error: %s\n", st.errc.message().c_str());
		std::vector<std::int64_t> const file_progress = tor1.file_progress();
		bool one_incomplete = false;
		file_storage const& fs1 = ti->files();
		for (file_index_t i : fs1.file_range())
		{
			if (fs1.pad_file_at(i)) continue;
			std::printf("file: %d progress: %" PRId64 " / %" PRId64 "\n", static_cast<int>(i)
				, file_progress[std::size_t(static_cast<int>(i))], fs1.file_size(i));
			if (fs1.file_size(i) == file_progress[std::size_t(static_cast<int>(i))]) continue;
			one_incomplete = true;
		}
		TEST_CHECK(one_incomplete);
		TEST_CHECK(st.num_pieces < ti->num_pieces());
	}
	else
	{
		TEST_CHECK(st.num_pieces == ti->num_pieces());
		TEST_CHECK(st.is_seeding);
		if (st.errc)
			std::printf("error: %s\n", st.errc.message().c_str());
	}

	// make the files writable again
	if (flags & read_only_files)
	{
		for (std::size_t i = 0; i < file_sizes.size(); ++i)
		{
			char name[1024];
			std::snprintf(name, sizeof(name), "test%d", int(i));
			char dirname[200];
			std::snprintf(dirname, sizeof(dirname), "test_dir%d", int(i) / 5);

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

} // anonymous namespace

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

TORRENT_TEST(extended)
{
	test_checking(extended_files);
}

TORRENT_TEST(corrupt)
{
	test_checking(corrupt_files);
}

TORRENT_TEST(force_recheck)
{
	test_checking(force_recheck);
}

TORRENT_TEST(checking_v2)
{
	test_checking(v2);
}

TORRENT_TEST(read_only_corrupt_v2)
{
	test_checking(read_only_files | corrupt_files | v2);
}

TORRENT_TEST(read_only_v2)
{
	test_checking(read_only_files | v2);
}

TORRENT_TEST(incomplete_v2)
{
	test_checking(incomplete_files | v2);
}

TORRENT_TEST(corrupt_v2)
{
	test_checking(corrupt_files | v2);
}

TORRENT_TEST(single_file_v2)
{
	test_checking(v2 | single_file);
}

TORRENT_TEST(single_file_corrupt_v2)
{
	test_checking(corrupt_files | v2 | single_file);
}

TORRENT_TEST(single_file_incomplete_v2)
{
	test_checking(incomplete_files | v2 | single_file);
}

TORRENT_TEST(force_recheck_v2)
{
	test_checking(force_recheck | v2);
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
	static std::array<int const, 2> const file_sizes{{ 9 * megabyte, 3 * megabyte }};

	file_storage fs;
	create_random_files("test_torrent_dir", file_sizes, &fs);
	TEST_EQUAL(fs.num_files(), 2);

	lt::create_torrent t(fs, piece_size);
	set_piece_hashes(t, ".", ec);
	if (ec) printf("ERROR: set_piece_hashes: (%d) %s\n", ec.value(), ec.message().c_str());

	std::vector<char> buf;
	bencode(std::back_inserter(buf), t.generate());
	auto ti = std::make_shared<torrent_info>(buf, ec, from_span);
	printf("generated torrent: %s test_torrent_dir result: %s\n"
		, aux::to_hex(ti->info_hashes().v1.to_string()).c_str()
		, ec.message().c_str());

	TEST_CHECK(ti->is_valid());

	// we have two files, but there are also two padfiles now
	TEST_EQUAL(ti->num_files(), 4);

	{
		session ses1(settings());
		add_torrent_params p;
		p.file_priorities.resize(std::size_t(ti->num_files()));
		p.file_priorities[0] = 1_pri;
		p.save_path = ".";
		p.ti = ti;
		torrent_handle tor1 = ses1.add_torrent(p, ec);
		// change the priority of a file while checking and make sure it doesn't interrupt the checking.
		std::vector<download_priority_t> prio(std::size_t(ti->num_files()), 0_pri);
		prio[2] = 1_pri;
		tor1.prioritize_files(prio);
		TEST_CHECK(wait_for_alert(ses1, torrent_checked_alert::alert_type
			, "torrent checked", pop_alerts::pop_all, seconds(50)));
		TEST_CHECK(tor1.status({}).is_seeding);
	}
	remove_all("test_torrent_dir", ec);
	if (ec) fprintf(stdout, "ERROR: removing test_torrent_dir: (%d) %s\n", ec.value(), ec.message().c_str());
}
