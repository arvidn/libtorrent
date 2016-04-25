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
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/read_resume_data.hpp"

#include <boost/make_shared.hpp>

#include "test.hpp"
#include "settings.hpp"
#include "setup_transfer.hpp"

using namespace libtorrent;
namespace lt = libtorrent;

boost::shared_ptr<torrent_info> generate_torrent()
{
	file_storage fs;
	fs.add_file("test_resume/tmp1", 128 * 1024 * 8);
	fs.add_file("test_resume/tmp2", 128 * 1024);
	fs.add_file("test_resume/tmp3", 128 * 1024);
	lt::create_torrent t(fs, 128 * 1024, 6);

	t.add_tracker("http://torrent_file_tracker.com/announce");
	t.add_url_seed("http://torrent_file_url_seed.com/");

	int num = t.num_pieces();
	TEST_CHECK(num > 0);
	for (int i = 0; i < num; ++i)
	{
		sha1_hash ph;
		for (int k = 0; k < 20; ++k) ph[k] = lt::random();
		t.set_hash(i, ph);
	}

	std::vector<char> buf;
	bencode(std::back_inserter(buf), t.generate());
	return boost::make_shared<torrent_info>(&buf[0], buf.size());
}

std::vector<char> generate_resume_data(torrent_info* ti
	, char const* file_priorities = "")
{
	entry rd;

	rd["file-format"] = "libtorrent resume file";
	rd["file-version"] = 1;
	rd["info-hash"] = ti->info_hash().to_string();
	rd["blocks per piece"] = (std::max)(1, ti->piece_length() / 0x4000);
	rd["pieces"] = std::string(ti->num_pieces(), '\x01');

	rd["total_uploaded"] = 1337;
	rd["total_downloaded"] = 1338;
	rd["active_time"] = 1339;
	rd["seeding_time"] = 1340;
	rd["upload_rate_limit"] = 1343;
	rd["download_rate_limit"] = 1344;
	rd["max_connections"] = 1345;
	rd["max_uploads"] = 1346;
	rd["seed_mode"] = 0;
	rd["super_seeding"] = 0;
	rd["added_time"] = 1347;
	rd["completed_time"] = 1348;
	rd["finished_time"] = 1352;
	if (file_priorities && file_priorities[0])
	{
		entry::list_type& file_prio = rd["file_priority"].list();
		for (int i = 0; file_priorities[i]; ++i)
			file_prio.push_back(entry(file_priorities[i] - '0'));
	}

	rd["piece_priority"] = std::string(ti->num_pieces(), '\x01');
	rd["auto_managed"] = 0;
	rd["sequential_download"] = 0;
	rd["paused"] = 0;
	entry::list_type& trackers = rd["trackers"].list();
	trackers.push_back(entry(entry::list_t));
	trackers.back().list().push_back(entry("http://resume_data_tracker.com/announce"));
	entry::list_type& url_list = rd["url-list"].list();
	url_list.push_back(entry("http://resume_data_url_seed.com"));

	entry::list_type& httpseeds = rd["httpseeds"].list();
	httpseeds.push_back(entry("http://resume_data_http_seed.com"));

#ifdef TORRENT_WINDOWS
	rd["save_path"] = "c:\\resume_data save_path";
#else
	rd["save_path"] = "/resume_data save_path";
#endif

	std::vector<char> ret;
	bencode(back_inserter(ret), rd);

	fprintf(stderr, "%s\n", rd.to_string().c_str());

	return ret;
}

torrent_handle test_resume_flags(lt::session& ses, int flags
	, char const* file_priorities = "1111", char const* resume_file_prio = ""
	, bool const test_deprecated = false)
{
	boost::shared_ptr<torrent_info> ti = generate_torrent();

	add_torrent_params p;
	std::vector<char> rd = generate_resume_data(ti.get(), resume_file_prio);

#ifndef TORRENT_NO_DEPRECATE
	if (test_deprecated)
	{
		p.resume_data.swap(rd);

		p.trackers.push_back("http://add_torrent_params_tracker.com/announce");
		p.url_seeds.push_back("http://add_torrent_params_url_seed.com");

		p.max_uploads = 1;
		p.max_connections = 2;
		p.upload_limit = 3;
		p.download_limit = 4;
	}
	else
#endif
	{
		error_code ec;
		p = read_resume_data(&rd[0], int(rd.size()), ec);
		TEST_CHECK(!ec);
	}

	p.ti = ti;
	p.flags = flags;
#ifdef TORRENT_WINDOWS
	p.save_path = "c:\\add_torrent_params save_path";
#else
	p.save_path = "/add_torrent_params save_path";
#endif

	if (file_priorities[0])
	{
		std::vector<boost::uint8_t> priorities_vector;
		for (int i = 0; file_priorities[i]; ++i)
			priorities_vector.push_back(file_priorities[i] - '0');

		p.file_priorities = priorities_vector;
	}

	torrent_handle h = ses.add_torrent(p);
	torrent_status s = h.status();
	TEST_EQUAL(s.info_hash, ti->info_hash());
	return h;
}

void default_tests(torrent_status const& s)
{
	// allow some slack in the time stamps since they are reported as
	// relative times. If the computer is busy while running the unit test
	// or running under valgrind it may take several seconds
	TEST_CHECK(s.active_time >= 1339);
	TEST_CHECK(s.active_time < 1339 + 10);

	TEST_EQUAL(s.finished_time, 1352);
	TEST_EQUAL(s.seeding_time, 1340);
	TEST_EQUAL(s.added_time, 1347);
	TEST_EQUAL(s.completed_time, 1348);
}

void test_piece_priorities(bool test_deprecated = false)
{
	settings_pack sett = settings();
	lt::session ses(sett);
	boost::shared_ptr<torrent_info> ti = generate_torrent();
	add_torrent_params p;
	p.ti = ti;
	p.save_path = ".";
	torrent_handle h = ses.add_torrent(p);

	h.piece_priority(0, 0);
	h.piece_priority(ti->num_pieces()-1, 0);

	h.save_resume_data();
	alert const* a = wait_for_alert(ses, save_resume_data_alert::alert_type);

	TEST_CHECK(a);
	if (save_resume_data_alert const* ra = alert_cast<save_resume_data_alert>(a))
	{
		fprintf(stderr, "%s\n", ra->resume_data->to_string().c_str());
		entry::string_type prios = (*ra->resume_data)["piece_priority"].string();
		TEST_EQUAL(int(prios.size()), ti->num_pieces());
		TEST_EQUAL(prios[0], '\0');
		TEST_EQUAL(prios[1], '\x04');
		TEST_EQUAL(prios[ti->num_pieces()-1], '\0');

		std::vector<char> resume_data;
		bencode(std::back_inserter(resume_data), *ra->resume_data);

#ifndef TORRENT_NO_DEPRECATE
		if (test_deprecated)
		{
			p.resume_data = resume_data;
		}
		else
#endif
		{
			error_code ec;
			p = read_resume_data(&resume_data[0], int(resume_data.size()), ec);
			TEST_CHECK(!ec);
			p.ti = ti;
			p.save_path = ".";
		}
	}

	ses.remove_torrent(h);

	// now, make sure the piece priorities are loaded correctly
	h = ses.add_torrent(p);

	TEST_EQUAL(h.piece_priority(0), 0);
	TEST_EQUAL(h.piece_priority(1), 4);
	TEST_EQUAL(h.piece_priority(ti->num_pieces()-1), 0);
}

#ifndef TORRENT_NO_DEPRECATE
TORRENT_TEST(piece_priorities_deprecated)
{
	test_piece_priorities(true);
}
#endif

TORRENT_TEST(piece_priorities)
{
	test_piece_priorities();
}


// TODO: test what happens when loading a resume file with both piece priorities
// and file priorities (file prio should take presedence)

// TODO: make sure a resume file only ever contain file priorities OR piece
// priorities. Never both.

// TODO: generally save

#ifndef TORRENT_NO_DEPRECATE
TORRENT_TEST(file_priorities_default_deprecated)
{
	lt::session ses;
	std::vector<int> file_priorities = test_resume_flags(ses, 0, "", "", true).file_priorities();

	TEST_EQUAL(file_priorities.size(), 3);
	TEST_EQUAL(file_priorities[0], 4);
	TEST_EQUAL(file_priorities[1], 4);
	TEST_EQUAL(file_priorities[2], 4);
}

TORRENT_TEST(file_priorities_resume_seed_mode_deprecated)
{
	// in share mode file priorities should always be 0
	lt::session ses;
	std::vector<int> file_priorities = test_resume_flags(ses,
		add_torrent_params::flag_share_mode, "", "123", true).file_priorities();

	TEST_EQUAL(file_priorities.size(), 3);
	TEST_EQUAL(file_priorities[0], 0);
	TEST_EQUAL(file_priorities[1], 0);
	TEST_EQUAL(file_priorities[2], 0);
}

TORRENT_TEST(file_priorities_seed_mode_deprecated)
{
	// in share mode file priorities should always be 0
	lt::session ses;
	std::vector<int> file_priorities = test_resume_flags(ses,
		add_torrent_params::flag_share_mode, "123", "", true).file_priorities();

	TEST_EQUAL(file_priorities.size(), 3);
	TEST_EQUAL(file_priorities[0], 0);
	TEST_EQUAL(file_priorities[1], 0);
	TEST_EQUAL(file_priorities[2], 0);
}

TORRENT_TEST(resume_save_load_deprecated)
{
	settings_pack sett = settings();
	lt::session ses(sett);
	torrent_handle h = test_resume_flags(ses, 0, "123", "", true);

	h.save_resume_data();

	save_resume_data_alert const* a = alert_cast<save_resume_data_alert>(
		wait_for_alert(ses, save_resume_data_alert::alert_type
		, "resume_save_load"));

	TEST_CHECK(a);
	if (a == NULL) return;

	TEST_CHECK(a->resume_data);

	entry& e = *a->resume_data.get();
	entry::list_type& l = e["file_priority"].list();
	entry::list_type::iterator i = l.begin();

	TEST_EQUAL(l.size(), 3);
	TEST_EQUAL(*i++, 1);
	TEST_EQUAL(*i++, 2);
	TEST_EQUAL(*i++, 3);
}

TORRENT_TEST(resume_save_load_resume_deprecated)
{
	settings_pack sett = settings();
	lt::session ses(sett);
	torrent_handle h = test_resume_flags(ses, 0, "", "123", true);

	h.save_resume_data();

	save_resume_data_alert const* a = alert_cast<save_resume_data_alert>(
		wait_for_alert(ses, save_resume_data_alert::alert_type
		, "resume_save_load"));

	TEST_CHECK(a);
	if (a == NULL) return;

	TEST_CHECK(a->resume_data);

	entry& e = *a->resume_data.get();
	entry::list_type& l = e["file_priority"].list();
	entry::list_type::iterator i = l.begin();

	TEST_EQUAL(l.size(), 3);
	TEST_EQUAL(*i++, 1);
	TEST_EQUAL(*i++, 2);
	TEST_EQUAL(*i++, 3);
}

TORRENT_TEST(file_priorities_resume_override_deprecated)
{
	// make sure that an empty file_priorities vector in add_torrent_params won't
	// override the resume data file priorities, even when override resume data
	// flag is set.
	settings_pack sett = settings();
	lt::session ses(sett);
	std::vector<int> file_priorities = test_resume_flags(ses,
		add_torrent_params::flag_override_resume_data, "", "123", true).file_priorities();

	TEST_EQUAL(file_priorities.size(), 3);
	TEST_EQUAL(file_priorities[0], 1);
	TEST_EQUAL(file_priorities[1], 2);
	TEST_EQUAL(file_priorities[2], 3);
}

TORRENT_TEST(file_priorities_resume_deprecated)
{
	settings_pack sett = settings();
	lt::session ses(sett);
	std::vector<int> file_priorities = test_resume_flags(ses, 0, "", "123", true).file_priorities();

	TEST_EQUAL(file_priorities.size(), 3);
	TEST_EQUAL(file_priorities[0], 1);
	TEST_EQUAL(file_priorities[1], 2);
	TEST_EQUAL(file_priorities[2], 3);
}

TORRENT_TEST(file_priorities1_deprecated)
{
	settings_pack sett = settings();
	lt::session ses(sett);
	std::vector<int> file_priorities = test_resume_flags(ses, 0, "010", "", true).file_priorities();

	TEST_EQUAL(file_priorities.size(), 3);
	TEST_EQUAL(file_priorities[0], 0);
	TEST_EQUAL(file_priorities[1], 1);
	TEST_EQUAL(file_priorities[2], 0);

//#error save resume data and assert the file priorities are preserved
}

TORRENT_TEST(file_priorities2_deprecated)
{
	settings_pack sett = settings();
	lt::session ses(sett);
	std::vector<int> file_priorities = test_resume_flags(ses, 0, "123", "", true).file_priorities();

	TEST_EQUAL(file_priorities.size(), 3);
	TEST_EQUAL(file_priorities[0], 1);
	TEST_EQUAL(file_priorities[1], 2);
	TEST_EQUAL(file_priorities[2], 3);
}

TORRENT_TEST(file_priorities3_deprecated)
{
	settings_pack sett = settings();
	lt::session ses(sett);
	std::vector<int> file_priorities = test_resume_flags(ses, 0, "4321", "", true).file_priorities();

	TEST_EQUAL(file_priorities.size(), 3);
	TEST_EQUAL(file_priorities[0], 4);
	TEST_EQUAL(file_priorities[1], 3);
	TEST_EQUAL(file_priorities[2], 2);
}

TORRENT_TEST(plain_deprecated)
{
	settings_pack sett = settings();
	lt::session ses(sett);

	torrent_status s = test_resume_flags(ses, 0, "", "", true).status();
	default_tests(s);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(s.save_path, "c:\\add_torrent_params save_path");
#else
	TEST_EQUAL(s.save_path, "/add_torrent_params save_path");
#endif
	TEST_EQUAL(s.sequential_download, false);
	TEST_EQUAL(s.paused, false);
	TEST_EQUAL(s.auto_managed, false);
	TEST_EQUAL(s.seed_mode, false);
	TEST_EQUAL(s.super_seeding, false);
	TEST_EQUAL(s.share_mode, false);
	TEST_EQUAL(s.upload_mode, false);
	TEST_EQUAL(s.ip_filter_applies, false);
	TEST_EQUAL(s.connections_limit, 1345);
	TEST_EQUAL(s.uploads_limit, 1346);
}

TORRENT_TEST(use_resume_save_path_deprecated)
{
	settings_pack sett = settings();
	lt::session ses(sett);
	torrent_status s = test_resume_flags(ses, add_torrent_params::flag_use_resume_save_path, "", "", true).status();
	default_tests(s);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(s.save_path, "c:\\resume_data save_path");
#else
	TEST_EQUAL(s.save_path, "/resume_data save_path");
#endif
	TEST_EQUAL(s.sequential_download, false);
	TEST_EQUAL(s.paused, false);
	TEST_EQUAL(s.auto_managed, false);
	TEST_EQUAL(s.seed_mode, false);
	TEST_EQUAL(s.super_seeding, false);
	TEST_EQUAL(s.share_mode, false);
	TEST_EQUAL(s.upload_mode, false);
	TEST_EQUAL(s.ip_filter_applies, false);
	TEST_EQUAL(s.connections_limit, 1345);
	TEST_EQUAL(s.uploads_limit, 1346);
}

TORRENT_TEST(override_resume_data_deprecated)
{
	settings_pack sett = settings();
	lt::session ses(sett);
	torrent_status s = test_resume_flags(ses
		, add_torrent_params::flag_override_resume_data
		| add_torrent_params::flag_paused, "", "", true).status();

	default_tests(s);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(s.save_path, "c:\\add_torrent_params save_path");
#else
	TEST_EQUAL(s.save_path, "/add_torrent_params save_path");
#endif
	TEST_EQUAL(s.sequential_download, false);
	TEST_EQUAL(s.paused, true);
	TEST_EQUAL(s.auto_managed, false);
	TEST_EQUAL(s.seed_mode, false);
	TEST_EQUAL(s.super_seeding, false);
	TEST_EQUAL(s.share_mode, false);
	TEST_EQUAL(s.upload_mode, false);
	TEST_EQUAL(s.ip_filter_applies, false);
	TEST_EQUAL(s.connections_limit, 2);
	TEST_EQUAL(s.uploads_limit, 1);
}

TORRENT_TEST(seed_mode_deprecated)
{
	settings_pack sett = settings();
	lt::session ses(sett);
	torrent_status s = test_resume_flags(ses, add_torrent_params::flag_override_resume_data
		| add_torrent_params::flag_seed_mode, "", "", true).status();
	default_tests(s);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(s.save_path, "c:\\add_torrent_params save_path");
#else
	TEST_EQUAL(s.save_path, "/add_torrent_params save_path");
#endif
	TEST_EQUAL(s.sequential_download, false);
	TEST_EQUAL(s.paused, false);
	TEST_EQUAL(s.auto_managed, false);
	TEST_EQUAL(s.seed_mode, true);
	TEST_EQUAL(s.super_seeding, false);
	TEST_EQUAL(s.share_mode, false);
	TEST_EQUAL(s.upload_mode, false);
	TEST_EQUAL(s.ip_filter_applies, false);
	TEST_EQUAL(s.connections_limit, 2);
	TEST_EQUAL(s.uploads_limit, 1);
}

TORRENT_TEST(upload_mode_deprecated)
{
	settings_pack sett = settings();
	lt::session ses(sett);
	torrent_status s = test_resume_flags(ses, add_torrent_params::flag_upload_mode, "", "", true).status();
	default_tests(s);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(s.save_path, "c:\\add_torrent_params save_path");
#else
	TEST_EQUAL(s.save_path, "/add_torrent_params save_path");
#endif
	TEST_EQUAL(s.sequential_download, false);
	TEST_EQUAL(s.paused, false);
	TEST_EQUAL(s.auto_managed, false);
	TEST_EQUAL(s.seed_mode, false);
	TEST_EQUAL(s.super_seeding, false);
	TEST_EQUAL(s.share_mode, false);
	TEST_EQUAL(s.upload_mode, true);
	TEST_EQUAL(s.ip_filter_applies, false);
	TEST_EQUAL(s.connections_limit, 1345);
	TEST_EQUAL(s.uploads_limit, 1346);
}

TORRENT_TEST(share_mode_deprecated)
{
	settings_pack sett = settings();
	lt::session ses(sett);
	torrent_status s = test_resume_flags(ses
		, add_torrent_params::flag_override_resume_data
		| add_torrent_params::flag_share_mode, "", "", true).status();
	default_tests(s);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(s.save_path, "c:\\add_torrent_params save_path");
#else
	TEST_EQUAL(s.save_path, "/add_torrent_params save_path");
#endif
	TEST_EQUAL(s.sequential_download, false);
	TEST_EQUAL(s.paused, false);
	TEST_EQUAL(s.auto_managed, false);
	TEST_EQUAL(s.seed_mode, false);
	TEST_EQUAL(s.super_seeding, false);
	TEST_EQUAL(s.share_mode, true);
	TEST_EQUAL(s.upload_mode, false);
	TEST_EQUAL(s.ip_filter_applies, false);
	TEST_EQUAL(s.connections_limit, 2);
	TEST_EQUAL(s.uploads_limit, 1);
}

TORRENT_TEST(auto_managed_deprecated)
{
	settings_pack sett = settings();
	lt::session ses(sett);
	// resume data overrides the auto-managed flag
	torrent_status s = test_resume_flags(ses, add_torrent_params::flag_auto_managed, "", "", true).status();
	default_tests(s);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(s.save_path, "c:\\add_torrent_params save_path");
#else
	TEST_EQUAL(s.save_path, "/add_torrent_params save_path");
#endif
	TEST_EQUAL(s.sequential_download, false);
	TEST_EQUAL(s.paused, false);
	TEST_EQUAL(s.auto_managed, false);
	TEST_EQUAL(s.seed_mode, false);
	TEST_EQUAL(s.super_seeding, false);
	TEST_EQUAL(s.share_mode, false);
	TEST_EQUAL(s.upload_mode, false);
	TEST_EQUAL(s.ip_filter_applies, false);
	TEST_EQUAL(s.connections_limit, 1345);
	TEST_EQUAL(s.uploads_limit, 1346);
}

TORRENT_TEST(paused_deprecated)
{
	settings_pack sett = settings();
	lt::session ses(sett);
	// resume data overrides the paused flag
	torrent_status s = test_resume_flags(ses, add_torrent_params::flag_paused, "", "", true).status();
	default_tests(s);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(s.save_path, "c:\\add_torrent_params save_path");
#else
	TEST_EQUAL(s.save_path, "/add_torrent_params save_path");
#endif
	TEST_EQUAL(s.sequential_download, false);
	TEST_EQUAL(s.paused, false);
	TEST_EQUAL(s.auto_managed, false);
	TEST_EQUAL(s.seed_mode, false);
	TEST_EQUAL(s.super_seeding, false);
	TEST_EQUAL(s.share_mode, false);
	TEST_EQUAL(s.upload_mode, false);
	TEST_EQUAL(s.ip_filter_applies, false);
	TEST_EQUAL(s.connections_limit, 1345);
	TEST_EQUAL(s.uploads_limit, 1346);

	// TODO: test all other resume flags here too. This would require returning
	// more than just the torrent_status from test_resume_flags. Also http seeds
	// and trackers for instance
}

TORRENT_TEST(url_seed_resume_data_deprecated)
{
	// merge url seeds with resume data
	fprintf(stderr, "flags: merge_resume_http_seeds\n");
	settings_pack sett = settings();
	lt::session ses(sett);
	torrent_handle h = test_resume_flags(ses,
		add_torrent_params::flag_merge_resume_http_seeds, "", "", true);
	std::set<std::string> us = h.url_seeds();
	std::set<std::string> ws = h.http_seeds();

	TEST_EQUAL(us.size(), 3);
	TEST_EQUAL(std::count(us.begin(), us.end()
		, "http://add_torrent_params_url_seed.com/"), 1);
	TEST_EQUAL(std::count(us.begin(), us.end()
		, "http://torrent_file_url_seed.com/"), 1);
	TEST_EQUAL(std::count(us.begin(), us.end()
		, "http://resume_data_url_seed.com/"), 1);

	TEST_EQUAL(ws.size(), 1);
	TEST_EQUAL(std::count(ws.begin(), ws.end()
		, "http://resume_data_http_seed.com"), 1);
}

TORRENT_TEST(resume_override_torrent_deprecated)
{
	// resume data overrides the .torrent_file
	fprintf(stderr, "flags: no merge_resume_http_seed\n");
	settings_pack sett = settings();
	lt::session ses(sett);
	torrent_handle h = test_resume_flags(ses,
		add_torrent_params::flag_merge_resume_trackers, "", "", true);
	std::set<std::string> us = h.url_seeds();
	std::set<std::string> ws = h.http_seeds();

	TEST_EQUAL(ws.size(), 1);
	TEST_EQUAL(std::count(ws.begin(), ws.end()
		, "http://resume_data_http_seed.com"), 1);

	TEST_EQUAL(us.size(), 1);
	TEST_EQUAL(std::count(us.begin(), us.end()
		, "http://resume_data_url_seed.com/"), 1);
}
#endif

TORRENT_TEST(file_priorities_default)
{
	lt::session ses;
	std::vector<int> file_priorities = test_resume_flags(ses, 0, "", "").file_priorities();

	TEST_EQUAL(file_priorities.size(), 3);
	TEST_EQUAL(file_priorities[0], 4);
	TEST_EQUAL(file_priorities[1], 4);
	TEST_EQUAL(file_priorities[2], 4);
}

TORRENT_TEST(file_priorities_resume_seed_mode)
{
	// in share mode file priorities should always be 0
	lt::session ses;
	std::vector<int> file_priorities = test_resume_flags(ses,
		add_torrent_params::flag_share_mode, "", "123").file_priorities();

	TEST_EQUAL(file_priorities.size(), 3);
	TEST_EQUAL(file_priorities[0], 0);
	TEST_EQUAL(file_priorities[1], 0);
	TEST_EQUAL(file_priorities[2], 0);
}

TORRENT_TEST(file_priorities_seed_mode)
{
	// in share mode file priorities should always be 0
	lt::session ses;
	std::vector<int> file_priorities = test_resume_flags(ses,
		add_torrent_params::flag_share_mode, "123", "").file_priorities();

	TEST_EQUAL(file_priorities.size(), 3);
	TEST_EQUAL(file_priorities[0], 0);
	TEST_EQUAL(file_priorities[1], 0);
	TEST_EQUAL(file_priorities[2], 0);
}

void test_zero_file_prio(bool test_deprecated = false)
{
	fprintf(stderr, "test_file_prio\n");

	lt::session ses;
	boost::shared_ptr<torrent_info> ti = generate_torrent();
	add_torrent_params p;
	p.ti = ti;
	p.save_path = ".";

	entry rd;

	rd["file-format"] = "libtorrent resume file";
	rd["file-version"] = 1;
	rd["info-hash"] = ti->info_hash().to_string();
	rd["blocks per piece"] = (std::max)(1, ti->piece_length() / 0x4000);

	entry::list_type& file_prio = rd["file_priority"].list();
	for (int i = 0; i < 100; ++i)
	{
		file_prio.push_back(entry(0));
	}

	std::string pieces(ti->num_pieces(), '\x01');
	rd["pieces"] = pieces;

	std::string pieces_prio(ti->num_pieces(), '\x01');
	rd["piece_priority"] = pieces_prio;

	std::vector<char> resume_data;
	bencode(back_inserter(resume_data), rd);

#ifndef TORRENT_NO_DEPRECATE
	if (test_deprecated)
	{
		p.resume_data = resume_data;
	}
	else
#endif
	{
		error_code ec;
		p = read_resume_data(&resume_data[0], int(resume_data.size()), ec);
		TEST_CHECK(!ec);
		p.ti = ti;
		p.save_path = ".";
	}

	torrent_handle h = ses.add_torrent(p);

	torrent_status s = h.status();
	TEST_EQUAL(s.total_wanted, 0);
}

#ifndef TORRENT_NO_DEPRECATE
TORRENT_TEST(zero_file_prio_deprecated)
{
	test_zero_file_prio(true);
}
#endif

TORRENT_TEST(zero_file_prio)
{
	test_zero_file_prio();
}

void test_seed_mode(bool const file_prio, bool const pieces_have, bool const piece_prio
	, bool const all_files_zero = false, bool const test_deprecated = false)
{
	fprintf(stderr, "test_seed_mode file_prio: %d pieces_have: %d piece_prio: %d\n"
		, file_prio, pieces_have, piece_prio);

	settings_pack sett = settings();
	lt::session ses(sett);
	boost::shared_ptr<torrent_info> ti = generate_torrent();
	add_torrent_params p;
	p.ti = ti;
	p.save_path = ".";

	entry rd;

	rd["file-format"] = "libtorrent resume file";
	rd["file-version"] = 1;
	rd["info-hash"] = ti->info_hash().to_string();
	rd["blocks per piece"] = (std::max)(1, ti->piece_length() / 0x4000);

	if (file_prio)
	{
		// this should take it out of seed_mode
		entry::list_type& file_prio = rd["file_priority"].list();
		file_prio.push_back(entry(0));
		if (all_files_zero)
		{
			for (int i = 0; i < 100; ++i)
			{
				file_prio.push_back(entry(0));
			}
		}
	}

	std::string pieces(ti->num_pieces(), '\x01');
	if (pieces_have)
	{
		pieces[0] = '\0';
	}
	rd["pieces"] = pieces;

	std::string pieces_prio(ti->num_pieces(), '\x01');
	if (piece_prio)
	{
		pieces_prio[0] = '\0';
	}
	rd["piece_priority"] = pieces_prio;

	rd["seed_mode"] = 1;

	std::vector<char> resume_data;
	bencode(back_inserter(resume_data), rd);

#ifndef TORRENT_NO_DEPRECATE
	if (test_deprecated)
	{
		p.resume_data = resume_data;
	}
	else
#endif
	{
		error_code ec;
		p = read_resume_data(&resume_data[0], int(resume_data.size()), ec);
		TEST_CHECK(!ec);
		p.ti = ti;
		p.save_path = ".";
	}

	torrent_handle h = ses.add_torrent(p);

	torrent_status s = h.status();
	if (file_prio || piece_prio || pieces_have)
	{
		TEST_EQUAL(s.seed_mode, false);
	}
	else
	{
		TEST_EQUAL(s.seed_mode, true);
	}
}
#ifndef TORRENT_NO_DEPRECATE
TORRENT_TEST(seed_mode_file_prio_deprecated)
{
	test_seed_mode(true, false, false, true);
}

TORRENT_TEST(seed_mode_piece_prio_deprecated)
{
	test_seed_mode(false, true, false, true);
}

TORRENT_TEST(seed_mode_piece_have_deprecated)
{
	test_seed_mode(false, false, true, true);
}

TORRENT_TEST(seed_mode_preserve_deprecated)
{
	test_seed_mode(false, false, false, true);
}
#endif

TORRENT_TEST(seed_mode_file_prio)
{
	test_seed_mode(true, false, false);
}

TORRENT_TEST(seed_mode_piece_prio)
{
	test_seed_mode(false, true, false);
}

TORRENT_TEST(seed_mode_piece_have)
{
	test_seed_mode(false, false, true);
}

TORRENT_TEST(seed_mode_preserve)
{
	test_seed_mode(false, false, false);
}

TORRENT_TEST(resume_save_load)
{
	settings_pack sett = settings();
	lt::session ses(sett);
	torrent_handle h = test_resume_flags(ses, 0, "123", "");

	h.save_resume_data();

	save_resume_data_alert const* a = alert_cast<save_resume_data_alert>(
		wait_for_alert(ses, save_resume_data_alert::alert_type
		, "resume_save_load"));

	TEST_CHECK(a);
	if (a == NULL) return;

	TEST_CHECK(a->resume_data);

	entry& e = *a->resume_data.get();
	entry::list_type& l = e["file_priority"].list();
	entry::list_type::iterator i = l.begin();

	TEST_EQUAL(l.size(), 3);
	TEST_EQUAL(*i++, 1);
	TEST_EQUAL(*i++, 2);
	TEST_EQUAL(*i++, 3);
}

TORRENT_TEST(resume_save_load_resume)
{
	settings_pack sett = settings();
	lt::session ses(sett);
	torrent_handle h = test_resume_flags(ses, 0, "", "123");

	h.save_resume_data();

	save_resume_data_alert const* a = alert_cast<save_resume_data_alert>(
		wait_for_alert(ses, save_resume_data_alert::alert_type
		, "resume_save_load"));

	TEST_CHECK(a);
	if (a == NULL) return;

	TEST_CHECK(a->resume_data);

	entry& e = *a->resume_data.get();
	entry::list_type& l = e["file_priority"].list();
	entry::list_type::iterator i = l.begin();

	fprintf(stderr, "%s\n", e.to_string().c_str());

	TEST_EQUAL(l.size(), 3);
	TEST_EQUAL(*i++, 1);
	TEST_EQUAL(*i++, 2);
	TEST_EQUAL(*i++, 3);
}

TORRENT_TEST(file_priorities_resume)
{
	settings_pack sett = settings();
	lt::session ses(sett);
	std::vector<int> file_priorities = test_resume_flags(ses, 0, "", "123").file_priorities();

	TEST_EQUAL(file_priorities.size(), 3);
	TEST_EQUAL(file_priorities[0], 1);
	TEST_EQUAL(file_priorities[1], 2);
	TEST_EQUAL(file_priorities[2], 3);
}

TORRENT_TEST(file_priorities1)
{
	settings_pack sett = settings();
	lt::session ses(sett);
	std::vector<int> file_priorities = test_resume_flags(ses, 0, "010").file_priorities();

	TEST_EQUAL(file_priorities.size(), 3);
	TEST_EQUAL(file_priorities[0], 0);
	TEST_EQUAL(file_priorities[1], 1);
	TEST_EQUAL(file_priorities[2], 0);

//#error save resume data and assert the file priorities are preserved
}

TORRENT_TEST(file_priorities2)
{
	settings_pack sett = settings();
	lt::session ses(sett);
	std::vector<int> file_priorities = test_resume_flags(ses, 0, "123").file_priorities();

	TEST_EQUAL(file_priorities.size(), 3);
	TEST_EQUAL(file_priorities[0], 1);
	TEST_EQUAL(file_priorities[1], 2);
	TEST_EQUAL(file_priorities[2], 3);
}

TORRENT_TEST(file_priorities3)
{
	settings_pack sett = settings();
	lt::session ses(sett);
	std::vector<int> file_priorities = test_resume_flags(ses, 0, "4321").file_priorities();

	TEST_EQUAL(file_priorities.size(), 3);
	TEST_EQUAL(file_priorities[0], 4);
	TEST_EQUAL(file_priorities[1], 3);
	TEST_EQUAL(file_priorities[2], 2);
}

TORRENT_TEST(plain)
{
	settings_pack sett = settings();
	lt::session ses(sett);

	torrent_status s = test_resume_flags(ses, 0).status();
	default_tests(s);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(s.save_path, "c:\\add_torrent_params save_path");
#else
	TEST_EQUAL(s.save_path, "/add_torrent_params save_path");
#endif
	TEST_EQUAL(s.sequential_download, false);
	TEST_EQUAL(s.paused, false);
	TEST_EQUAL(s.auto_managed, false);
	TEST_EQUAL(s.seed_mode, false);
	TEST_EQUAL(s.super_seeding, false);
	TEST_EQUAL(s.share_mode, false);
	TEST_EQUAL(s.upload_mode, false);
	TEST_EQUAL(s.ip_filter_applies, false);
	TEST_EQUAL(s.connections_limit, 1345);
	TEST_EQUAL(s.uploads_limit, 1346);
}

TORRENT_TEST(seed_mode)
{
	settings_pack sett = settings();
	lt::session ses(sett);
	torrent_status s = test_resume_flags(ses
		, add_torrent_params::flag_seed_mode).status();
	default_tests(s);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(s.save_path, "c:\\add_torrent_params save_path");
#else
	TEST_EQUAL(s.save_path, "/add_torrent_params save_path");
#endif
	TEST_EQUAL(s.sequential_download, false);
	TEST_EQUAL(s.paused, false);
	TEST_EQUAL(s.auto_managed, false);
	TEST_EQUAL(s.seed_mode, true);
	TEST_EQUAL(s.super_seeding, false);
	TEST_EQUAL(s.share_mode, false);
	TEST_EQUAL(s.upload_mode, false);
	TEST_EQUAL(s.ip_filter_applies, false);
	TEST_EQUAL(s.connections_limit, 1345);
	TEST_EQUAL(s.uploads_limit, 1346);
}

TORRENT_TEST(upload_mode)
{
	settings_pack sett = settings();
	lt::session ses(sett);
	torrent_status s = test_resume_flags(ses, add_torrent_params::flag_upload_mode).status();
	default_tests(s);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(s.save_path, "c:\\add_torrent_params save_path");
#else
	TEST_EQUAL(s.save_path, "/add_torrent_params save_path");
#endif
	TEST_EQUAL(s.sequential_download, false);
	TEST_EQUAL(s.paused, false);
	TEST_EQUAL(s.auto_managed, false);
	TEST_EQUAL(s.seed_mode, false);
	TEST_EQUAL(s.super_seeding, false);
	TEST_EQUAL(s.share_mode, false);
	TEST_EQUAL(s.upload_mode, true);
	TEST_EQUAL(s.ip_filter_applies, false);
	TEST_EQUAL(s.connections_limit, 1345);
	TEST_EQUAL(s.uploads_limit, 1346);
}

TORRENT_TEST(share_mode)
{
	settings_pack sett = settings();
	lt::session ses(sett);
	torrent_status s = test_resume_flags(ses
		, add_torrent_params::flag_share_mode).status();
	default_tests(s);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(s.save_path, "c:\\add_torrent_params save_path");
#else
	TEST_EQUAL(s.save_path, "/add_torrent_params save_path");
#endif
	TEST_EQUAL(s.sequential_download, false);
	TEST_EQUAL(s.paused, false);
	TEST_EQUAL(s.auto_managed, false);
	TEST_EQUAL(s.seed_mode, false);
	TEST_EQUAL(s.super_seeding, false);
	TEST_EQUAL(s.share_mode, true);
	TEST_EQUAL(s.upload_mode, false);
	TEST_EQUAL(s.ip_filter_applies, false);
	TEST_EQUAL(s.connections_limit, 1345);
	TEST_EQUAL(s.uploads_limit, 1346);
}

TORRENT_TEST(auto_managed)
{
	settings_pack sett = settings();
	lt::session ses(sett);
	// resume data overrides the auto-managed flag
	torrent_status s = test_resume_flags(ses, add_torrent_params::flag_auto_managed).status();
	default_tests(s);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(s.save_path, "c:\\add_torrent_params save_path");
#else
	TEST_EQUAL(s.save_path, "/add_torrent_params save_path");
#endif
	TEST_EQUAL(s.sequential_download, false);
	TEST_EQUAL(s.paused, false);
	TEST_EQUAL(s.auto_managed, true);
	TEST_EQUAL(s.seed_mode, false);
	TEST_EQUAL(s.super_seeding, false);
	TEST_EQUAL(s.share_mode, false);
	TEST_EQUAL(s.upload_mode, false);
	TEST_EQUAL(s.ip_filter_applies, false);
	TEST_EQUAL(s.connections_limit, 1345);
	TEST_EQUAL(s.uploads_limit, 1346);
}

TORRENT_TEST(paused)
{
	settings_pack sett = settings();
	lt::session ses(sett);
	// resume data overrides the paused flag
	torrent_status s = test_resume_flags(ses, add_torrent_params::flag_paused).status();
	default_tests(s);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(s.save_path, "c:\\add_torrent_params save_path");
#else
	TEST_EQUAL(s.save_path, "/add_torrent_params save_path");
#endif
	TEST_EQUAL(s.sequential_download, false);
	TEST_EQUAL(s.paused, true);
	TEST_EQUAL(s.auto_managed, false);
	TEST_EQUAL(s.seed_mode, false);
	TEST_EQUAL(s.super_seeding, false);
	TEST_EQUAL(s.share_mode, false);
	TEST_EQUAL(s.upload_mode, false);
	TEST_EQUAL(s.ip_filter_applies, false);
	TEST_EQUAL(s.connections_limit, 1345);
	TEST_EQUAL(s.uploads_limit, 1346);

	// TODO: test all other resume flags here too. This would require returning
	// more than just the torrent_status from test_resume_flags. Also http seeds
	// and trackers for instance
}

