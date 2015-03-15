/*

Copyright (c) 2014, Arvid Norberg
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
#include "libtorrent/alert_types.hpp"
#include "libtorrent/create_torrent.hpp"
#include "setup_transfer.hpp"
#include "test.hpp"

#include <fstream>
#include <boost/tuple/tuple.hpp>

using namespace libtorrent;
namespace lt = libtorrent;
using boost::tuples::ignore;

template <class T>
boost::shared_ptr<T> clone_ptr(boost::shared_ptr<T> const& ptr)
{
	return boost::shared_ptr<T>(new T(*ptr));
}

int peer_disconnects = 0;

bool on_alert(alert* a)
{
	if (alert_cast<peer_disconnected_alert>(a))
		++peer_disconnects;

	return false;
}

void test_remap_files_gather(storage_mode_t storage_mode = storage_mode_sparse)
{
	// in case the previous run was terminated
	error_code ec;

	int const alert_mask = alert::all_categories
		& ~alert::progress_notification
		& ~alert::stats_notification;

	session_proxy p1;
	session_proxy p2;

	lt::session ses1(fingerprint("LT", 0, 1, 0, 0), std::make_pair(48075, 49000)
		, "0.0.0.0", 0, alert_mask);
	lt::session ses2(fingerprint("LT", 0, 1, 0, 0), std::make_pair(49075, 50000)
		, "0.0.0.0", 0, alert_mask);

	torrent_handle tor1;
	torrent_handle tor2;

	create_directory("tmp1_remap", ec);
	create_directory(combine_path("tmp1_remap", "test_torrent_dir"), ec);
	if (ec)
	{
		fprintf(stderr, "error creating directory: %s\n"
			, ec.message().c_str());
		TEST_CHECK(false);
		return;
	}

	static const int file_sizes[] =
	{ 50, 16000-50, 16000, 1700, 100, 8000, 8000, 1,1,10,10,10,1000,10,10,10,10,1000,10,10,10,1,1,1
		,10,1000,1000,1000,10,1000,130,65000,340,750,20,300,400,5000,23000,900,43000,4000,43000,60, 40};

	create_random_files(combine_path("tmp1_remap", "test_torrent_dir")
		, file_sizes, sizeof(file_sizes)/sizeof(file_sizes[0]));
	file_storage fs;

	// generate a torrent with pad files to make sure they
	// are not requested web seeds
	add_files(fs, combine_path("tmp1_remap", "test_torrent_dir"));
	libtorrent::create_torrent ct(fs, 0x8000, 0x4000);
	set_piece_hashes(ct, "tmp1_remap", ec);
	if (ec)
	{
		fprintf(stderr, "error creating hashes for test torrent: %s\n"
			, ec.message().c_str());
		TEST_CHECK(false);
		return;
	}
	std::vector<char> buf;
	bencode(std::back_inserter(buf), ct.generate());
	boost::shared_ptr<torrent_info> t(new torrent_info(&buf[0], buf.size(), ec));
	boost::shared_ptr<torrent_info> t2(new torrent_info(&buf[0], buf.size(), ec));

	// remap the files to a single one
	file_storage st;
	st.add_file("single_file", t->total_size());
	t2->remap_files(st);

	add_torrent_params params;
	params.storage_mode = storage_mode;
	params.flags &= ~add_torrent_params::flag_paused;
	params.flags &= ~add_torrent_params::flag_auto_managed;

	wait_for_listen(ses1, "ses1");
	wait_for_listen(ses2, "ses2");

	peer_disconnects = 0;

	// test using piece sizes smaller than 16kB
	boost::tie(tor1, tor2, ignore) = setup_transfer(&ses1, &ses2, 0
		, true, false, true, "_remap", 8 * 1024, &t, false, &params
		, true, false, &t2);

	fprintf(stderr, "\ntesting remap gather\n\n");

	for (int i = 0; i < 50; ++i)
	{
		print_alerts(ses1, "ses1", true, true, true, on_alert);
		print_alerts(ses2, "ses2", true, true, true, on_alert);

		torrent_status st1 = tor1.status();
		torrent_status st2 = tor2.status();

		if (i % 10 == 0)
		{
			print_ses_rate(i / 10.f, &st1, &st2);
		}

		if (st2.is_finished) break;

		if (st2.state != torrent_status::downloading)
		{
			static char const* state_str[] =	
				{"checking (q)", "checking", "dl metadata"
				, "downloading", "finished", "seeding", "allocating", "checking (r)"};
			std::cerr << "st2 state: " << state_str[st2.state] << std::endl;
		}

		TEST_CHECK(st1.state == torrent_status::seeding
			|| st1.state == torrent_status::checking_files);
		TEST_CHECK(st2.state == torrent_status::downloading
			|| st2.state == torrent_status::checking_resume_data);

		if (peer_disconnects >= 2) break;

		test_sleep(100);
	}

	torrent_status st2 = tor2.status();
	TEST_CHECK(st2.is_seeding);

	if (!st2.is_seeding) return;

	fprintf(stderr, "\ntesting force recheck\n\n");

	// test force rechecking a seeding torrent with remapped files
	tor2.force_recheck();

	for (int i = 0; i < 50; ++i)
	{
		print_alerts(ses2, "ses2", true, true, true, on_alert);

		torrent_status st2 = tor2.status();

		if (i % 10 == 0)
		{
			print_ses_rate(i / 10.f, NULL, &st2);
		}

		if (st2.state != torrent_status::checking_files)
		{
			static char const* state_str[] =	
				{"checking (q)", "checking", "dl metadata"
				, "downloading", "finished", "seeding", "allocating", "checking (r)"};
			std::cerr << "st2 state: " << state_str[st2.state] << std::endl;
		}

		if (st2.progress == 1.0) break;

		test_sleep(100);
	}

	st2 = tor2.status();
	TEST_CHECK(st2.is_seeding);

	p1 = ses1.abort();
	p2 = ses2.abort();
}

void test_remap_files_scatter(storage_mode_t storage_mode = storage_mode_sparse)
{
	int num_files = 10;

	// in case the previous run was terminated
	error_code ec;

	int const alert_mask = alert::all_categories
		& ~alert::progress_notification
		& ~alert::stats_notification;

	session_proxy p1;
	session_proxy p2;

	lt::session ses1(fingerprint("LT", 0, 1, 0, 0), std::make_pair(48075, 49000)
		, "0.0.0.0", 0, alert_mask);
	lt::session ses2(fingerprint("LT", 0, 1, 0, 0), std::make_pair(49075, 50000)
		, "0.0.0.0", 0, alert_mask);

	torrent_handle tor1;
	torrent_handle tor2;

	create_directory("tmp1_remap2", ec);
	std::ofstream file("tmp1_remap2/temporary");
	boost::shared_ptr<torrent_info> t = ::create_torrent(&file, 32 * 1024, 7);
	file.close();

	file_storage fs;
	for (int i = 0; i < num_files-1; ++i)
	{
		char name[100];
		snprintf(name, sizeof(name), "multifile/file%d.txt", i);
		fs.add_file(name, t->total_size() / 10);
	}
	char name[100];
	snprintf(name, sizeof(name), "multifile/file%d.txt", num_files);
	// the last file has to be a special case to make the size
	// add up exactly (in case the total size is not divisible by 10).
	fs.add_file(name, t->total_size() - fs.total_size());

	boost::shared_ptr<torrent_info> t2 = clone_ptr(t);

	t2->remap_files(fs);

	add_torrent_params params;
	params.storage_mode = storage_mode;
	params.flags &= ~add_torrent_params::flag_paused;
	params.flags &= ~add_torrent_params::flag_auto_managed;

	wait_for_listen(ses1, "ses1");
	wait_for_listen(ses2, "ses2");

	peer_disconnects = 0;

	// test using piece sizes smaller than 16kB
	boost::tie(tor1, tor2, ignore) = setup_transfer(&ses1, &ses2, 0
		, true, false, true, "_remap2", 8 * 1024, &t, false, &params
		, true, false, &t2);

	fprintf(stderr, "\ntesting remap scatter\n\n");

	for (int i = 0; i < 50; ++i)
	{
		print_alerts(ses1, "ses1", true, true, true, on_alert);
		print_alerts(ses2, "ses2", true, true, true, on_alert);

		torrent_status st1 = tor1.status();
		torrent_status st2 = tor2.status();

		if (i % 10 == 0)
		{
			print_ses_rate(i / 10.f, &st1, &st2);
		}

		if (st2.is_finished) break;

		if (st2.state != torrent_status::downloading)
		{
			static char const* state_str[] =	
				{"checking (q)", "checking", "dl metadata"
				, "downloading", "finished", "seeding", "allocating", "checking (r)"};
			std::cerr << "st2 state: " << state_str[st2.state] << std::endl;
		}

		TEST_CHECK(st1.state == torrent_status::seeding
			|| st1.state == torrent_status::checking_files);
		TEST_CHECK(st2.state == torrent_status::downloading
			|| st2.state == torrent_status::checking_resume_data);

		if (peer_disconnects >= 2) break;

		test_sleep(100);
	}

	torrent_status st2 = tor2.status();
	TEST_CHECK(st2.is_seeding);

	if (!st2.is_seeding) return;

	fprintf(stderr, "\ntesting force recheck\n\n");

	// test force rechecking a seeding torrent with remapped files
	tor2.force_recheck();

	for (int i = 0; i < 50; ++i)
	{
		print_alerts(ses2, "ses2", true, true, true, on_alert);

		torrent_status st2 = tor2.status();

		if (i % 10 == 0)
		{
			print_ses_rate(i / 10.f, NULL, &st2);
		}

		if (st2.state != torrent_status::checking_files)
		{
			static char const* state_str[] =	
				{"checking (q)", "checking", "dl metadata"
				, "downloading", "finished", "seeding", "allocating", "checking (r)"};
			std::cerr << "st2 state: " << state_str[st2.state] << std::endl;
		}

		if (st2.progress == 1.0) break;

		test_sleep(100);
	}

	st2 = tor2.status();
	TEST_CHECK(st2.is_seeding);

	p1 = ses1.abort();
	p2 = ses2.abort();
}

void test_remap_files_prio(storage_mode_t storage_mode = storage_mode_sparse)
{
	// in case the previous run was terminated
	error_code ec;

	int const alert_mask = alert::all_categories
		& ~alert::progress_notification
		& ~alert::stats_notification;

	session_proxy p1;
	session_proxy p2;

	lt::session ses1(fingerprint("LT", 0, 1, 0, 0), std::make_pair(48075, 49000)
		, "0.0.0.0", 0, alert_mask);
	lt::session ses2(fingerprint("LT", 0, 1, 0, 0), std::make_pair(49075, 50000)
		, "0.0.0.0", 0, alert_mask);

	torrent_handle tor1;
	torrent_handle tor2;

	create_directory("tmp1_remap3", ec);
	create_directory(combine_path("tmp1_remap3", "test_torrent_dir"), ec);

	// create a torrent with 2 files, remap them into 3 files and make sure
	// the file priorities don't break things
	static const int file_sizes[] = {100000, 100000};
	const int num_files = sizeof(file_sizes)/sizeof(file_sizes[0]);

	create_random_files(combine_path("tmp1_remap3", "test_torrent_dir")
		, file_sizes, num_files);

	file_storage fs1;
	const int piece_size = 0x4000;

	add_files(fs1, combine_path("tmp1_remap3", "test_torrent_dir"));
	libtorrent::create_torrent ct(fs1, piece_size, 0x4000
		, libtorrent::create_torrent::optimize);

	// calculate the hash for all pieces
	set_piece_hashes(ct, "tmp1_remap3", ec);
	if (ec) fprintf(stderr, "ERROR: set_piece_hashes: (%d) %s\n"
		, ec.value(), ec.message().c_str());

	std::vector<char> buf;
	bencode(std::back_inserter(buf), ct.generate());
	boost::shared_ptr<torrent_info> t(new torrent_info(&buf[0], buf.size(), ec));

	int num_new_files = 3;

	file_storage fs;
	for (int i = 0; i < num_new_files-1; ++i)
	{
		char name[100];
		snprintf(name, sizeof(name), "multifile/file%d.txt", i);
		fs.add_file(name, t->total_size() / 10);
	}
	char name[100];
	snprintf(name, sizeof(name), "multifile/file%d.txt", num_new_files);
	// the last file has to be a special case to make the size
	// add up exactly (in case the total size is not divisible by 10).
	fs.add_file(name, t->total_size() - fs.total_size());

	boost::shared_ptr<torrent_info> t2 = clone_ptr(t);

	t2->remap_files(fs);

	add_torrent_params params;
	params.storage_mode = storage_mode;
	params.flags |= add_torrent_params::flag_paused;
	params.flags &= ~add_torrent_params::flag_auto_managed;

	wait_for_listen(ses1, "ses1");
	wait_for_listen(ses2, "ses2");

	peer_disconnects = 0;

	// test using piece sizes smaller than 16kB
	boost::tie(tor1, tor2, ignore) = setup_transfer(&ses1, &ses2, 0
		, true, false, true, "_remap3", 8 * 1024, &t, false, &params
		, true, false, &t2);

	std::vector<int> file_prio(3, 1);
	file_prio[0] = 0;
	tor2.prioritize_files(file_prio);

	// torrent1 will attempt to connect to torrent2
	// make sure torrent2 is up and running by then
	tor2.resume();
	test_sleep(500);
	tor1.resume();

	fprintf(stderr, "\ntesting remap scatter prio\n\n");

	for (int i = 0; i < 50; ++i)
	{
		print_alerts(ses1, "ses1", true, true, true, on_alert);
		print_alerts(ses2, "ses2", true, true, true, on_alert);

		torrent_status st1 = tor1.status();
		torrent_status st2 = tor2.status();

		if (i % 10 == 0)
		{
			std::cerr
				<< "\033[32m" << int(st1.download_payload_rate / 1000.f) << "kB/s "
				<< "\033[33m" << int(st1.upload_payload_rate / 1000.f) << "kB/s "
				<< "\033[0m" << int(st1.progress * 100) << "% "
				<< st1.num_peers
				<< ": "
				<< "\033[32m" << int(st2.download_payload_rate / 1000.f) << "kB/s "
				<< "\033[31m" << int(st2.upload_payload_rate / 1000.f) << "kB/s "
				<< "\033[0m" << int(st2.progress * 100) << "% "
				<< st2.num_peers
				<< std::endl;
		}

		if (st2.is_finished) break;

		if (st2.state != torrent_status::downloading)
		{
			static char const* state_str[] =	
				{"checking (q)", "checking", "dl metadata"
				, "downloading", "finished", "seeding", "allocating", "checking (r)"};
			std::cerr << "st2 state: " << state_str[st2.state] << std::endl;
		}

		TEST_CHECK(st1.state == torrent_status::seeding);
		TEST_CHECK(st2.state == torrent_status::downloading
			|| st2.state == torrent_status::checking_resume_data);

		if (peer_disconnects >= 2) break;

		test_sleep(100);
	}

	torrent_status st2 = tor2.status();
	TEST_CHECK(st2.is_finished);

	p1 = ses1.abort();
	p2 = ses2.abort();
}

int test_main()
{
	using namespace libtorrent;

	error_code ec;

	remove_all("tmp1_remap", ec);
	remove_all("tmp2_remap", ec);

	test_remap_files_gather();
	
	remove_all("tmp1_remap", ec);
	remove_all("tmp2_remap", ec);
	remove_all("tmp1_remap2", ec);
	remove_all("tmp2_remap2", ec);

	test_remap_files_scatter();

	remove_all("tmp1_remap", ec);
	remove_all("tmp2_remap", ec);
	remove_all("tmp1_remap2", ec);
	remove_all("tmp2_remap2", ec);
	remove_all("tmp1_remap3", ec);
	remove_all("tmp2_remap3", ec);

	test_remap_files_prio();
	
	remove_all("tmp1_remap", ec);
	remove_all("tmp2_remap", ec);
	remove_all("tmp1_remap2", ec);
	remove_all("tmp2_remap2", ec);
	remove_all("tmp1_remap3", ec);
	remove_all("tmp2_remap3", ec);

	return 0;
}

