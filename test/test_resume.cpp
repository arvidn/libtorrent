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
#include "libtorrent/write_resume_data.hpp"
#include "setup_transfer.hpp"

#include "test.hpp"
#include "test_utils.hpp"
#include "settings.hpp"
#include "setup_transfer.hpp"
#include "settings.hpp"

#include <fstream>

#ifdef TORRENT_WINDOWS
#define SEP "\\"
#else
#define SEP "/"
#endif
using namespace lt;

namespace {

torrent_flags_t const flags_mask
	= torrent_flags::sequential_download
	| torrent_flags::paused
	| torrent_flags::auto_managed
	| torrent_flags::seed_mode
	| torrent_flags::super_seeding
	| torrent_flags::share_mode
	| torrent_flags::upload_mode
	| torrent_flags::apply_ip_filter;

std::vector<char> generate_resume_data(torrent_info* ti
	, char const* file_priorities = "")
{
	entry rd;

	rd["file-format"] = "libtorrent resume file";
	rd["file-version"] = 1;
	rd["info-hash"] = ti->info_hash().to_string();
	rd["blocks per piece"] = std::max(1, ti->piece_length() / 0x4000);
	rd["pieces"] = std::string(std::size_t(ti->num_pieces()), '\x01');

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
	rd["last_download"] = 2;
	rd["last_upload"] = 3;
	rd["finished_time"] = 1352;
	if (file_priorities && file_priorities[0])
	{
		entry::list_type& file_prio = rd["file_priority"].list();
		for (int i = 0; file_priorities[i]; ++i)
			file_prio.push_back(entry(file_priorities[i] - '0'));
	}

	rd["piece_priority"] = std::string(std::size_t(ti->num_pieces()), '\x01');
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

	std::printf("%s\n", rd.to_string().c_str());

	return ret;
}

torrent_handle test_resume_flags(lt::session& ses
	, torrent_flags_t const flags = torrent_flags_t{}
	, char const* file_priorities = "1111", char const* resume_file_prio = ""
	, bool const test_deprecated = false)
{
	std::shared_ptr<torrent_info> ti = generate_torrent();

	add_torrent_params p;
	std::vector<char> rd = generate_resume_data(ti.get(), resume_file_prio);
	TORRENT_UNUSED(test_deprecated);
#if TORRENT_ABI_VERSION == 1
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
		p = read_resume_data(rd);
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
		aux::vector<download_priority_t, file_index_t> priorities_vector;
		for (int i = 0; file_priorities[i]; ++i)
			priorities_vector.push_back(download_priority_t(aux::numeric_cast<std::uint8_t>(file_priorities[i] - '0')));

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
#if TORRENT_ABI_VERSION == 1
	TEST_CHECK(s.active_time >= 1339);
	TEST_CHECK(s.active_time < 1339 + 10);

	auto const now = duration_cast<seconds>(clock_type::now().time_since_epoch()).count();
	TEST_CHECK(s.time_since_download >= now - 2);
	TEST_CHECK(s.time_since_upload >= now - 3);

	TEST_CHECK(s.time_since_download < now - 2 + 10);
	TEST_CHECK(s.time_since_upload < now - 3 + 10);
#endif

	using lt::seconds;
	TEST_CHECK(s.finished_duration< seconds(1352 + 2));
	TEST_CHECK(s.seeding_duration < seconds(1340 + 2));
	TEST_CHECK(s.active_duration >= seconds(1339));
	TEST_CHECK(s.active_duration < seconds(1339 + 10));

	TEST_CHECK(s.added_time < 1347 + 2);
	TEST_CHECK(s.added_time >= 1347);
	TEST_CHECK(s.completed_time < 1348 + 2);
	TEST_CHECK(s.completed_time >= 1348);
}

void test_piece_priorities(bool test_deprecated = false)
{
	lt::session ses(settings());
	std::shared_ptr<torrent_info> ti = generate_torrent();
	add_torrent_params p;
	p.ti = ti;
	p.save_path = ".";
	torrent_handle h = ses.add_torrent(p);

	h.piece_priority(piece_index_t(0), 0_pri);
	h.piece_priority(piece_index_t(ti->num_pieces()-1), 0_pri);

	h.save_resume_data();
	alert const* a = wait_for_alert(ses, save_resume_data_alert::alert_type);

	TEST_CHECK(a);
	save_resume_data_alert const* ra = alert_cast<save_resume_data_alert>(a);
	TEST_CHECK(ra);
	if (ra)
	{
		auto const prios = ra->params.piece_priorities;
		TEST_EQUAL(int(prios.size()), ti->num_pieces());
		TEST_EQUAL(prios[0], 0_pri);
		TEST_EQUAL(prios[1], 4_pri);
		TEST_EQUAL(prios[std::size_t(ti->num_pieces() - 1)], 0_pri);

		std::vector<char> resume_data = write_resume_data_buf(ra->params);
		TORRENT_UNUSED(test_deprecated);
#if TORRENT_ABI_VERSION == 1
		if (test_deprecated)
		{
			p.resume_data = resume_data;
		}
		else
#endif
		{
			p = read_resume_data(resume_data);
			p.ti = ti;
			p.save_path = ".";
		}
	}

	ses.remove_torrent(h);

	// now, make sure the piece priorities are loaded correctly
	h = ses.add_torrent(p);

	TEST_EQUAL(h.piece_priority(piece_index_t(0)), 0_pri);
	TEST_EQUAL(h.piece_priority(piece_index_t(1)), 4_pri);
	TEST_EQUAL(h.piece_priority(piece_index_t(ti->num_pieces()-1)), 0_pri);
}

} // anonymous namespace

#if TORRENT_ABI_VERSION == 1
TORRENT_TEST(piece_priorities_deprecated)
{
	test_piece_priorities(true);
}
#endif

TORRENT_TEST(piece_priorities)
{
	test_piece_priorities();
}

TORRENT_TEST(test_non_metadata)
{
	lt::session ses(settings());
	// this test torrent contain a tracker:
	// http://torrent_file_tracker.com/announce

	// and a URL seed:
	// http://torrent_file_url_seed.com

	std::shared_ptr<torrent_info> ti = generate_torrent();
	add_torrent_params p;
	p.ti = ti;
	p.save_path = ".";
	torrent_handle h = ses.add_torrent(p);

	h.replace_trackers(std::vector<lt::announce_entry>{announce_entry{"http://torrent_file_tracker2.com/announce"}});
	h.remove_url_seed("http://torrent_file_url_seed.com/");
	h.add_url_seed("http://torrent.com/");

	TEST_EQUAL(ti->comment(), "test comment");
	TEST_EQUAL(ti->creator(), "libtorrent test");
	auto const creation_date = ti->creation_date();

	h.save_resume_data(torrent_handle::save_info_dict);
	alert const* a = wait_for_alert(ses, save_resume_data_alert::alert_type);

	TEST_CHECK(a);
	save_resume_data_alert const* ra = alert_cast<save_resume_data_alert>(a);
	TEST_CHECK(ra);
	if (ra)
	{
		auto const& atp = ra->params;
		TEST_CHECK(atp.trackers == std::vector<std::string>{"http://torrent_file_tracker2.com/announce"});
		TEST_CHECK(atp.url_seeds == std::vector<std::string>{"http://torrent.com/"});
		TEST_CHECK(atp.ti);
		TEST_EQUAL(atp.ti->comment(), "test comment");
		TEST_EQUAL(atp.ti->creator(), "libtorrent test");
		TEST_EQUAL(atp.ti->creation_date(), creation_date);

		std::vector<char> resume_data = write_resume_data_buf(atp);
		p = read_resume_data(resume_data);
		p.ti = ti;
		p.save_path = ".";
	}

	ses.remove_torrent(h);

	// now, make sure the fields are restored correctly
	h = ses.add_torrent(p);

	TEST_EQUAL(h.trackers().size(), 1);
	TEST_CHECK(h.trackers().at(0).url == "http://torrent_file_tracker2.com/announce");
	TEST_CHECK(h.url_seeds() == std::set<std::string>{"http://torrent.com/"});
	auto t = h.status().torrent_file.lock();
	TEST_EQUAL(ti->comment(), "test comment");
	TEST_EQUAL(ti->creator(), "libtorrent test");
	TEST_EQUAL(ti->creation_date(), creation_date);
}

TORRENT_TEST(test_remove_trackers)
{
	lt::session ses(settings());
	// this test torrent contain a tracker:
	// http://torrent_file_tracker.com/announce

	std::shared_ptr<torrent_info> ti = generate_torrent();
	add_torrent_params p;
	p.ti = ti;
	p.save_path = ".";
	torrent_handle h = ses.add_torrent(p);

	h.replace_trackers(std::vector<lt::announce_entry>{});

	h.save_resume_data(torrent_handle::save_info_dict);
	alert const* a = wait_for_alert(ses, save_resume_data_alert::alert_type);

	TEST_CHECK(a);
	save_resume_data_alert const* ra = alert_cast<save_resume_data_alert>(a);
	TEST_CHECK(ra);
	if (ra)
	{
		auto const& atp = ra->params;
		TEST_EQUAL(atp.trackers.size(), 0);

		std::vector<char> resume_data = write_resume_data_buf(atp);
		p = read_resume_data(resume_data);
		p.ti = ti;
		p.save_path = ".";
	}

	ses.remove_torrent(h);

	// now, make sure the fields are restored correctly
	h = ses.add_torrent(p);

	TEST_EQUAL(h.trackers().size(), 0);
}

TORRENT_TEST(test_remove_web_seed)
{
	lt::session ses(settings());
	// this test torrent contain a URL seed:
	// http://torrent_file_url_seed.com

	std::shared_ptr<torrent_info> ti = generate_torrent();
	add_torrent_params p;
	p.ti = ti;
	p.save_path = ".";
	torrent_handle h = ses.add_torrent(p);

	h.remove_url_seed("http://torrent_file_url_seed.com/");

	h.save_resume_data(torrent_handle::save_info_dict);
	alert const* a = wait_for_alert(ses, save_resume_data_alert::alert_type);

	TEST_CHECK(a);
	save_resume_data_alert const* ra = alert_cast<save_resume_data_alert>(a);
	TEST_CHECK(ra);
	if (ra)
	{
		auto const& atp = ra->params;
		TEST_CHECK(atp.url_seeds.size() == 0);

		std::vector<char> resume_data = write_resume_data_buf(atp);
		p = read_resume_data(resume_data);
		p.ti = ti;
		p.save_path = ".";
	}

	ses.remove_torrent(h);

	// now, make sure the fields are restored correctly
	h = ses.add_torrent(p);

	TEST_EQUAL(h.url_seeds().size(), 0);
}

TORRENT_TEST(piece_slots)
{
	// make sure the "pieces" field is correctly accepted from resume data
	std::shared_ptr<torrent_info> ti = generate_torrent();

	error_code ec;
	create_directories("add_torrent_params_test" SEP "test_resume", ec);
	{
		std::vector<char> a(128 * 1024 * 8);
		std::vector<char> b(128 * 1024);
		std::ofstream("add_torrent_params_test" SEP "test_resume" SEP "tmp1").write(a.data(), std::streamsize(a.size()));
		std::ofstream("add_torrent_params_test" SEP "test_resume" SEP "tmp2").write(b.data(), std::streamsize(b.size()));
		std::ofstream("add_torrent_params_test" SEP "test_resume" SEP "tmp3").write(b.data(), std::streamsize(b.size()));
	}

	add_torrent_params p;
	p.ti = ti;
	p.save_path = "add_torrent_params_test";

	p.have_pieces.resize(2);
	p.have_pieces.set_bit(piece_index_t{0});
	p.have_pieces.set_bit(piece_index_t{1});

	lt::session ses(settings());
	torrent_handle h = ses.add_torrent(p);

	wait_for_alert(ses, torrent_checked_alert::alert_type, "piece_slots");

	torrent_status s = h.status();
	print_alerts(ses, "ses");
	TEST_EQUAL(s.info_hash, ti->info_hash());
	TEST_EQUAL(s.pieces.size(), ti->num_pieces());
	TEST_CHECK(s.pieces.size() >= 4);
	TEST_EQUAL(s.pieces[piece_index_t{0}], true);
	TEST_EQUAL(s.pieces[piece_index_t{1}], true);
	TEST_EQUAL(s.pieces[piece_index_t{2}], false);
	TEST_EQUAL(s.pieces[piece_index_t{3}], false);

	// now save resume data and make sure the pieces are preserved correctly
	h.save_resume_data();
	alert const* a = wait_for_alert(ses, save_resume_data_alert::alert_type);

	TEST_CHECK(a);
	save_resume_data_alert const* ra = alert_cast<save_resume_data_alert>(a);
	TEST_CHECK(ra);
	if (ra)
	{
		auto const& pieces = ra->params.have_pieces;
		TEST_EQUAL(int(pieces.size()), ti->num_pieces());

		TEST_EQUAL(pieces[piece_index_t{0}], true);
		TEST_EQUAL(pieces[piece_index_t{1}], true);
		TEST_EQUAL(pieces[piece_index_t{2}], false);
		TEST_EQUAL(pieces[piece_index_t{3}], false);
	}
}

namespace {

void test_piece_slots_seed(settings_pack const& sett)
{
	// make sure the "pieces" field is correctly accepted from resume data
	std::shared_ptr<torrent_info> ti = generate_torrent();

	error_code ec;
	create_directories(combine_path("add_torrent_params_test", "test_resume"), ec);
	{
		std::vector<char> a(128 * 1024 * 8);
		std::vector<char> b(128 * 1024);
		std::ofstream("add_torrent_params_test" SEP "test_resume" SEP "tmp1").write(a.data(), std::streamsize(a.size()));
		std::ofstream("add_torrent_params_test" SEP "test_resume" SEP "tmp2").write(b.data(), std::streamsize(b.size()));
		std::ofstream("add_torrent_params_test" SEP "test_resume" SEP "tmp3").write(b.data(), std::streamsize(b.size()));
	}

	add_torrent_params p;
	p.ti = ti;
	p.save_path = "add_torrent_params_test";

	p.have_pieces.resize(ti->num_pieces(), true);

	lt::session ses(sett);
	torrent_handle h = ses.add_torrent(p);

	wait_for_alert(ses, torrent_checked_alert::alert_type, "piece_slots");

	torrent_status s = h.status();
	print_alerts(ses, "ses");
	TEST_EQUAL(s.info_hash, ti->info_hash());
	TEST_EQUAL(s.pieces.size(), ti->num_pieces());
	for (auto const i : ti->piece_range())
	{
		TEST_EQUAL(s.pieces[i], true);
	}

	TEST_EQUAL(s.is_seeding, true);

	// now save resume data and make sure it reflects that we're a seed
	h.save_resume_data();
	alert const* a = wait_for_alert(ses, save_resume_data_alert::alert_type);

	TEST_CHECK(a);
	save_resume_data_alert const* ra = alert_cast<save_resume_data_alert>(a);
	TEST_CHECK(ra);
	if (ra)
	{
		auto const& pieces = ra->params.have_pieces;
		TEST_EQUAL(int(pieces.size()), ti->num_pieces());

		for (auto const i : ti->piece_range())
		{
			TEST_EQUAL(pieces[i], true);
		}
	}
}

} // anonymous namespace

TORRENT_TEST(piece_slots_seed)
{
	test_piece_slots_seed(settings());
}

TORRENT_TEST(piece_slots_seed_suggest_cache)
{
	settings_pack sett = settings();
	sett.set_int(settings_pack::suggest_mode, settings_pack::suggest_read_cache);
	test_piece_slots_seed(sett);
}

// TODO: test what happens when loading a resume file with both piece priorities
// and file priorities (file prio should take precedence)

// TODO: make sure a resume file only ever contain file priorities OR piece
// priorities. Never both.

// TODO: generally save

#if TORRENT_ABI_VERSION == 1
TORRENT_TEST(file_priorities_default_deprecated)
{
	lt::session ses(settings());
	std::vector<download_priority_t> file_priorities = test_resume_flags(ses
		, {}, "", "", true).get_file_priorities();

	TEST_EQUAL(file_priorities.size(), 3);
	TEST_EQUAL(file_priorities[0], 4);
	TEST_EQUAL(file_priorities[1], 4);
	TEST_EQUAL(file_priorities[2], 4);
}

// As long as the add_torrent_params priorities are empty, the file_priorities
// from the resume data should take effect
TORRENT_TEST(file_priorities_in_resume_deprecated)
{
	lt::session ses(settings());
	std::vector<download_priority_t> file_priorities = test_resume_flags(ses, {}, "", "123").get_file_priorities();

	TEST_EQUAL(file_priorities.size(), 3);
	TEST_EQUAL(file_priorities[0], 1);
	TEST_EQUAL(file_priorities[1], 2);
	TEST_EQUAL(file_priorities[2], 3);
}

// if both resume data and add_torrent_params has file_priorities, the
// add_torrent_params one take precedence
TORRENT_TEST(file_priorities_in_resume_and_params_deprecated)
{
	lt::session ses(settings());
	std::vector<download_priority_t> file_priorities = test_resume_flags(ses, {}, "456", "123").get_file_priorities();

	TEST_EQUAL(file_priorities.size(), 3);
	TEST_EQUAL(file_priorities[0], 4);
	TEST_EQUAL(file_priorities[1], 5);
	TEST_EQUAL(file_priorities[2], 6);
}

// if we set flag_override_resume_data, it should no affect file priorities
TORRENT_TEST(file_priorities_override_resume_deprecated)
{
	lt::session ses(settings());
	std::vector<download_priority_t> file_priorities = test_resume_flags(ses
		, add_torrent_params::flag_override_resume_data, "", "123").get_file_priorities();

	TEST_EQUAL(file_priorities.size(), 3);
	TEST_EQUAL(file_priorities[0], 1);
	TEST_EQUAL(file_priorities[1], 2);
	TEST_EQUAL(file_priorities[2], 3);
}

#ifndef TORRENT_DISABLE_SHARE_MODE
TORRENT_TEST(file_priorities_resume_share_mode_deprecated)
{
	// in share mode file priorities should always be 0
	lt::session ses(settings());
	std::vector<download_priority_t> file_priorities = test_resume_flags(ses,
		torrent_flags::share_mode, "", "123", true).get_file_priorities();

	TEST_EQUAL(file_priorities.size(), 3);
	TEST_EQUAL(file_priorities[0], 0);
	TEST_EQUAL(file_priorities[1], 0);
	TEST_EQUAL(file_priorities[2], 0);
}

TORRENT_TEST(file_priorities_share_mode_deprecated)
{
	// in share mode file priorities should always be 0
	lt::session ses(settings());
	std::vector<download_priority_t> file_priorities = test_resume_flags(ses,
		torrent_flags::share_mode, "123", "", true).get_file_priorities();

	TEST_EQUAL(file_priorities.size(), 3);
	TEST_EQUAL(file_priorities[0], 0);
	TEST_EQUAL(file_priorities[1], 0);
	TEST_EQUAL(file_priorities[2], 0);
}
#endif

TORRENT_TEST(resume_save_load_deprecated)
{
	lt::session ses(settings());
	torrent_handle h = test_resume_flags(ses, {}, "123", "", true);

	h.save_resume_data();

	save_resume_data_alert const* a = alert_cast<save_resume_data_alert>(
		wait_for_alert(ses, save_resume_data_alert::alert_type
		, "resume_save_load"));

	TEST_CHECK(a);
	if (a == nullptr) return;

	auto const l = a->params.file_priorities;

	TEST_EQUAL(l.size(), 3);
	TEST_EQUAL(l[0], 1);
	TEST_EQUAL(l[1], 2);
	TEST_EQUAL(l[2], 3);
}

TORRENT_TEST(resume_save_load_resume_deprecated)
{
	lt::session ses(settings());
	torrent_handle h = test_resume_flags(ses, {}, "", "123", true);

	h.save_resume_data();

	save_resume_data_alert const* a = alert_cast<save_resume_data_alert>(
		wait_for_alert(ses, save_resume_data_alert::alert_type
		, "resume_save_load"));

	TEST_CHECK(a);
	if (a == nullptr) return;

	auto const l = a->params.file_priorities;

	TEST_EQUAL(l.size(), 3);
	TEST_EQUAL(l[0], 1);
	TEST_EQUAL(l[1], 2);
	TEST_EQUAL(l[2], 3);
}

TORRENT_TEST(file_priorities_resume_override_deprecated)
{
	// make sure that an empty file_priorities vector in add_torrent_params won't
	// override the resume data file priorities, even when override resume data
	// flag is set.
	lt::session ses(settings());
	std::vector<download_priority_t> file_priorities = test_resume_flags(ses,
		torrent_flags::override_resume_data, "", "123", true).get_file_priorities();

	TEST_EQUAL(file_priorities.size(), 3);
	TEST_EQUAL(file_priorities[0], 1);
	TEST_EQUAL(file_priorities[1], 2);
	TEST_EQUAL(file_priorities[2], 3);
}

TORRENT_TEST(file_priorities_resume_deprecated)
{
	lt::session ses(settings());
	std::vector<download_priority_t> file_priorities = test_resume_flags(ses, {}, "", "123", true).get_file_priorities();

	TEST_EQUAL(file_priorities.size(), 3);
	TEST_EQUAL(file_priorities[0], 1);
	TEST_EQUAL(file_priorities[1], 2);
	TEST_EQUAL(file_priorities[2], 3);
}

TORRENT_TEST(file_priorities1_deprecated)
{
	lt::session ses(settings());
	std::vector<download_priority_t> file_priorities = test_resume_flags(ses, {}, "010", "", true).get_file_priorities();

	TEST_EQUAL(file_priorities.size(), 3);
	TEST_EQUAL(file_priorities[0], 0);
	TEST_EQUAL(file_priorities[1], 1);
	TEST_EQUAL(file_priorities[2], 0);

//#error save resume data and assert the file priorities are preserved
}

TORRENT_TEST(file_priorities2_deprecated)
{
	lt::session ses(settings());
	std::vector<download_priority_t> file_priorities = test_resume_flags(ses, {}, "123", "", true).get_file_priorities();

	TEST_EQUAL(file_priorities.size(), 3);
	TEST_EQUAL(file_priorities[0], 1);
	TEST_EQUAL(file_priorities[1], 2);
	TEST_EQUAL(file_priorities[2], 3);
}

TORRENT_TEST(file_priorities3_deprecated)
{
	lt::session ses(settings());
	std::vector<download_priority_t> file_priorities = test_resume_flags(ses, {}, "4321", "", true).get_file_priorities();

	TEST_EQUAL(file_priorities.size(), 3);
	TEST_EQUAL(file_priorities[0], 4);
	TEST_EQUAL(file_priorities[1], 3);
	TEST_EQUAL(file_priorities[2], 2);
}

TORRENT_TEST(plain_deprecated)
{
	lt::session ses(settings());

	torrent_status s = test_resume_flags(ses, {}, "", "", true).status();
	default_tests(s);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(s.save_path, "c:\\add_torrent_params save_path");
#else
	TEST_EQUAL(s.save_path, "/add_torrent_params save_path");
#endif
	TEST_EQUAL(s.flags & flags_mask, torrent_flags_t{});
	TEST_EQUAL(s.connections_limit, 1345);
	TEST_EQUAL(s.uploads_limit, 1346);
}

TORRENT_TEST(use_resume_save_path_deprecated)
{
	lt::session ses(settings());
	torrent_status s = test_resume_flags(ses
		, torrent_flags::use_resume_save_path, "", "", true).status();
	default_tests(s);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(s.save_path, "c:\\resume_data save_path");
#else
	TEST_EQUAL(s.save_path, "/resume_data save_path");
#endif
	TEST_EQUAL(s.flags & flags_mask, torrent_flags_t{});
	TEST_EQUAL(s.connections_limit, 1345);
	TEST_EQUAL(s.uploads_limit, 1346);
}

TORRENT_TEST(override_resume_data_deprecated)
{
	lt::session ses(settings());
	torrent_status s = test_resume_flags(ses
		, torrent_flags::override_resume_data
		| torrent_flags::paused, "", "", true).status();

	default_tests(s);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(s.save_path, "c:\\add_torrent_params save_path");
#else
	TEST_EQUAL(s.save_path, "/add_torrent_params save_path");
#endif
	TEST_EQUAL(s.flags & flags_mask, torrent_flags::paused);
	TEST_EQUAL(s.connections_limit, 2);
	TEST_EQUAL(s.uploads_limit, 1);
}

TORRENT_TEST(seed_mode_deprecated)
{
	lt::session ses(settings());
	torrent_status s = test_resume_flags(ses, torrent_flags::override_resume_data
		| torrent_flags::seed_mode, "", "", true).status();
	default_tests(s);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(s.save_path, "c:\\add_torrent_params save_path");
#else
	TEST_EQUAL(s.save_path, "/add_torrent_params save_path");
#endif
	TEST_EQUAL(s.flags & flags_mask, torrent_flags::seed_mode);
	TEST_EQUAL(s.connections_limit, 2);
	TEST_EQUAL(s.uploads_limit, 1);
}

TORRENT_TEST(upload_mode_deprecated)
{
	lt::session ses(settings());
	torrent_status s = test_resume_flags(ses
		, torrent_flags::upload_mode, "", "", true).status();
	default_tests(s);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(s.save_path, "c:\\add_torrent_params save_path");
#else
	TEST_EQUAL(s.save_path, "/add_torrent_params save_path");
#endif
	TEST_EQUAL(s.flags & flags_mask, torrent_flags::upload_mode);
	TEST_EQUAL(s.connections_limit, 1345);
	TEST_EQUAL(s.uploads_limit, 1346);
}

#ifndef TORRENT_DISABLE_SHARE_MODE
TORRENT_TEST(share_mode_deprecated)
{
	lt::session ses(settings());
	torrent_status s = test_resume_flags(ses
		, torrent_flags::override_resume_data
		| torrent_flags::share_mode, "", "", true).status();
	default_tests(s);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(s.save_path, "c:\\add_torrent_params save_path");
#else
	TEST_EQUAL(s.save_path, "/add_torrent_params save_path");
#endif
	TEST_EQUAL(s.flags & flags_mask, torrent_flags::share_mode);
	TEST_EQUAL(s.connections_limit, 2);
	TEST_EQUAL(s.uploads_limit, 1);
}
#endif

TORRENT_TEST(auto_managed_deprecated)
{
	lt::session ses(settings());
	// resume data overrides the auto-managed flag
	torrent_status s = test_resume_flags(ses
		, torrent_flags::auto_managed, "", "", true).status();
	default_tests(s);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(s.save_path, "c:\\add_torrent_params save_path");
#else
	TEST_EQUAL(s.save_path, "/add_torrent_params save_path");
#endif
	TEST_EQUAL(s.flags & flags_mask, torrent_flags_t{});
	TEST_EQUAL(s.connections_limit, 1345);
	TEST_EQUAL(s.uploads_limit, 1346);
}

TORRENT_TEST(paused_deprecated)
{
	lt::session ses(settings());
	// resume data overrides the paused flag
	torrent_status s = test_resume_flags(ses, torrent_flags::paused, "", "", true).status();
	default_tests(s);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(s.save_path, "c:\\add_torrent_params save_path");
#else
	TEST_EQUAL(s.save_path, "/add_torrent_params save_path");
#endif
	TEST_EQUAL(s.flags & flags_mask, torrent_flags_t{});
	TEST_EQUAL(s.connections_limit, 1345);
	TEST_EQUAL(s.uploads_limit, 1346);

	// TODO: test all other resume flags here too. This would require returning
	// more than just the torrent_status from test_resume_flags. Also http seeds
	// and trackers for instance
}

TORRENT_TEST(url_seed_resume_data_deprecated)
{
	// merge url seeds with resume data
	std::printf("flags: merge_resume_http_seeds\n");
	lt::session ses(settings());
	torrent_handle h = test_resume_flags(ses,
		torrent_flags::merge_resume_http_seeds, "", "", true);
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
	std::printf("flags: no merge_resume_http_seed\n");
	lt::session ses(settings());
	torrent_handle h = test_resume_flags(ses,
		torrent_flags::merge_resume_trackers, "", "", true);
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
	lt::session ses(settings());
	std::vector<download_priority_t> file_priorities
		= test_resume_flags(ses, {}, "", "").get_file_priorities();

	TEST_EQUAL(file_priorities.size(), 3);
	TEST_EQUAL(file_priorities[0], 4_pri);
	TEST_EQUAL(file_priorities[1], 4_pri);
	TEST_EQUAL(file_priorities[2], 4_pri);
}

#ifndef TORRENT_DISABLE_SHARE_MODE
TORRENT_TEST(file_priorities_resume_share_mode)
{
	// in share mode file priorities should always be 0
	lt::session ses(settings());
	std::vector<download_priority_t> file_priorities = test_resume_flags(ses
		, torrent_flags::share_mode, "", "123").get_file_priorities();

	TEST_EQUAL(file_priorities.size(), 3);
	TEST_EQUAL(file_priorities[0], 0_pri);
	TEST_EQUAL(file_priorities[1], 0_pri);
	TEST_EQUAL(file_priorities[2], 0_pri);
}

TORRENT_TEST(file_priorities_share_mode)
{
	// in share mode file priorities should always be 0
	lt::session ses(settings());
	std::vector<download_priority_t> file_priorities = test_resume_flags(ses
		, torrent_flags::share_mode, "123", "").get_file_priorities();

	TEST_EQUAL(file_priorities.size(), 3);
	TEST_EQUAL(file_priorities[0], 0_pri);
	TEST_EQUAL(file_priorities[1], 0_pri);
	TEST_EQUAL(file_priorities[2], 0_pri);
}
#endif

namespace {

void test_zero_file_prio(bool test_deprecated = false, bool mix_prios = false)
{
	std::printf("test_file_prio\n");

	lt::session ses(settings());
	std::shared_ptr<torrent_info> ti = generate_torrent();
	add_torrent_params p;
	p.ti = ti;
	p.save_path = ".";

	entry rd;

	rd["file-format"] = "libtorrent resume file";
	rd["file-version"] = 1;
	rd["info-hash"] = ti->info_hash().to_string();
	rd["blocks per piece"] = std::max(1, ti->piece_length() / 0x4000);

	// set file priorities to 0
	rd["file_priority"] = entry::list_type(100, entry(0));

	rd["pieces"] = std::string(std::size_t(ti->num_pieces()), '\x01');

	// but set the piece priorities to 1. these take precedence
	if (mix_prios)
		rd["piece_priority"] = std::string(std::size_t(ti->num_pieces()), '\x01');

	std::vector<char> resume_data;
	bencode(back_inserter(resume_data), rd);
	TORRENT_UNUSED(test_deprecated);
#if TORRENT_ABI_VERSION == 1
	if (test_deprecated)
	{
		p.resume_data = resume_data;
	}
	else
#endif
	{
		p = read_resume_data(resume_data);
		p.ti = ti;
		p.save_path = ".";
	}

	torrent_handle h = ses.add_torrent(p);

	torrent_status s = h.status();
	if (mix_prios)
	{
		TEST_EQUAL(s.total_wanted, ti->total_size());
	}
	else
	{
		TEST_EQUAL(s.total_wanted, 0);
	}
}

} // anonymous namespace

#if TORRENT_ABI_VERSION == 1
TORRENT_TEST(zero_file_prio_deprecated)
{
	test_zero_file_prio(true);
}

TORRENT_TEST(mixing_file_and_piece_prio_deprecated)
{
	test_zero_file_prio(true, true);
}


TORRENT_TEST(backwards_compatible_resume_info_dict)
{
	// make sure the "info" dictionary is picked up correctly from the
	// resume data in backwards compatible mode

	std::shared_ptr<torrent_info> ti = generate_torrent();
	entry rd;
	rd["file-format"] = "libtorrent resume file";
	rd["name"] = ti->name();
	rd["info-hash"] = ti->info_hash();
	auto metainfo = ti->metadata();
	rd["info"] = bdecode({metainfo.get(), ti->metadata_size()});
	std::vector<char> resume_data;
	bencode(back_inserter(resume_data), rd);

	add_torrent_params atp;
	atp.resume_data = std::move(resume_data);
	atp.save_path = ".";

	session ses;
	torrent_handle h = ses.add_torrent(atp);
	auto torrent = h.torrent_file();
	TEST_CHECK(torrent->info_hash() == ti->info_hash());
	torrent_status s = h.status();
}
#endif

TORRENT_TEST(resume_info_dict)
{
	// make sure the "info" dictionary is picked up correctly from the
	// resume data

	std::shared_ptr<torrent_info> ti = generate_torrent();
	entry rd;
	rd["file-format"] = "libtorrent resume file";
	rd["name"] = ti->name();
	rd["info-hash"] = ti->info_hash();
	auto metainfo = ti->metadata();
	rd["info"] = bdecode({metainfo.get(), ti->metadata_size()});
	std::vector<char> resume_data;
	bencode(back_inserter(resume_data), rd);

	error_code ec;
	add_torrent_params atp = read_resume_data(resume_data, ec);
	TEST_CHECK(atp.ti->info_hash() == ti->info_hash());
}

TORRENT_TEST(zero_file_prio)
{
	test_zero_file_prio();
}

TORRENT_TEST(mixing_file_and_piece_prio)
{
	test_zero_file_prio(false, true);
}

using test_mode_t = flags::bitfield_flag<std::uint8_t, struct test_mode_tag>;

namespace test_mode {
	constexpr test_mode_t file_prio = 0_bit;
	constexpr test_mode_t pieces_have = 1_bit;
	constexpr test_mode_t piece_prio = 2_bit;
	constexpr test_mode_t all_files_zero = 3_bit;
#if TORRENT_ABI_VERSION == 1
	constexpr test_mode_t deprecated = 4_bit;
#endif
}

namespace {

void test_seed_mode(test_mode_t const flags)
{
	lt::session ses(settings());
	std::shared_ptr<torrent_info> ti = generate_torrent(true);
	add_torrent_params p;
	p.ti = ti;
	p.save_path = ".";

	entry rd;

	rd["file-format"] = "libtorrent resume file";
	rd["file-version"] = 1;
	rd["info-hash"] = ti->info_hash().to_string();
	rd["blocks per piece"] = std::max(1, ti->piece_length() / 0x4000);

	if (flags & test_mode::file_prio)
	{
		// this should take it out of seed_mode
		entry::list_type& file_prio = rd["file_priority"].list();
		file_prio.push_back(entry(0));
		if (flags & test_mode::all_files_zero)
		{
			for (int i = 0; i < 100; ++i)
			{
				file_prio.push_back(entry(0));
			}
		}
	}

	if (flags & test_mode::pieces_have)
	{
		std::string pieces(std::size_t(ti->num_pieces()), '\x01');
		pieces[0] = '\0';
		rd["pieces"] = pieces;
	}

	if (flags & test_mode::piece_prio)
	{
		std::string pieces_prio(std::size_t(ti->num_pieces()), '\x01');
		pieces_prio[0] = '\0';
		rd["piece_priority"] = pieces_prio;
	}

	rd["seed_mode"] = 1;

	std::vector<char> resume_data;
	bencode(back_inserter(resume_data), rd);

#if TORRENT_ABI_VERSION == 1
	if (flags & test_mode::deprecated)
	{
		p.resume_data = resume_data;
	}
	else
#endif
	{
		error_code ec;
		p = read_resume_data(resume_data, ec);
		TEST_CHECK(!ec);
		p.ti = ti;
		p.save_path = ".";
	}

	torrent_handle h = ses.add_torrent(p);

	if (flags & (test_mode::file_prio
		| test_mode::piece_prio
		| test_mode::pieces_have))
	{
		std::vector<alert*> alerts;
		bool done = false;
		auto const start_time = lt::clock_type::now();
		while (!done)
		{
			ses.wait_for_alert(seconds(1));
			ses.pop_alerts(&alerts);
			for (auto a : alerts)
			{
				std::printf("%s\n", a->message().c_str());
				if (auto const* sca = alert_cast<state_changed_alert>(a))
				{
					TEST_CHECK(sca->state != torrent_status::seeding);
					if (sca->state == torrent_status::downloading) done = true;
				}
			}
			if (lt::clock_type::now() - start_time > seconds(5)) break;
		}
		TEST_CHECK(done);
		torrent_status const s = h.status();
		TEST_CHECK(!(s.flags & torrent_flags::seed_mode));
	}
	else
	{
		std::vector<alert*> alerts;
		bool done = false;
		auto const start_time = lt::clock_type::now();
		while (!done)
		{
			ses.wait_for_alert(seconds(1));
			ses.pop_alerts(&alerts);
			for (auto a : alerts)
			{
				std::printf("%s\n", a->message().c_str());
				if (auto const* sca = alert_cast<state_changed_alert>(a))
				{
					TEST_CHECK(sca->state != torrent_status::checking_files);
					if (sca->state == torrent_status::seeding) done = true;
				}
			}
			if (lt::clock_type::now() - start_time > seconds(5)) break;
		}
		TEST_CHECK(done);
		torrent_status const s = h.status();
		TEST_CHECK(s.flags & torrent_flags::seed_mode);
	}
}

} // anonymous namespace

#if TORRENT_ABI_VERSION == 1
TORRENT_TEST(seed_mode_file_prio_deprecated)
{
	test_seed_mode(test_mode::file_prio | test_mode::deprecated);
}

TORRENT_TEST(seed_mode_piece_prio_deprecated)
{
	test_seed_mode(test_mode::pieces_have | test_mode::deprecated);
}

TORRENT_TEST(seed_mode_piece_have_deprecated)
{
	test_seed_mode(test_mode::piece_prio | test_mode::deprecated);
}

TORRENT_TEST(seed_mode_preserve_deprecated)
{
	test_seed_mode(test_mode::deprecated);
}
#endif

TORRENT_TEST(seed_mode_file_prio)
{
	test_seed_mode(test_mode::file_prio);
}

TORRENT_TEST(seed_mode_piece_prio)
{
	test_seed_mode(test_mode::pieces_have);
}

TORRENT_TEST(seed_mode_piece_have)
{
	test_seed_mode(test_mode::piece_prio);
}

TORRENT_TEST(seed_mode_preserve)
{
	test_seed_mode(test_mode_t{});
}

TORRENT_TEST(seed_mode_load_peers)
{
	lt::session ses(settings());
	std::shared_ptr<torrent_info> ti = generate_torrent();
	add_torrent_params p;
	p.ti = ti;
	p.save_path = ".";
	p.flags |= torrent_flags::seed_mode;
	p.peers.push_back(tcp::endpoint(address::from_string("1.2.3.4"), 12345));

	torrent_handle h = ses.add_torrent(p);

	wait_for_alert(ses, torrent_checked_alert::alert_type, "seed_mode_load_peers");

	h.save_resume_data();

	save_resume_data_alert const* a = alert_cast<save_resume_data_alert>(
		wait_for_alert(ses, save_resume_data_alert::alert_type
		, "seed_mode_load_peers"));

	TEST_CHECK(a);
	if (a == nullptr) return;

	auto const& peers = a->params.peers;
	TEST_EQUAL(peers.size(), 1);
	TEST_CHECK(peers[0] == tcp::endpoint(address::from_string("1.2.3.4"), 12345));
}

TORRENT_TEST(resume_save_load)
{
	lt::session ses(settings());
	torrent_handle h = test_resume_flags(ses, {}, "123", "");

	h.save_resume_data();

	save_resume_data_alert const* a = alert_cast<save_resume_data_alert>(
		wait_for_alert(ses, save_resume_data_alert::alert_type
		, "resume_save_load"));

	TEST_CHECK(a);
	if (a == nullptr) return;

	auto const l = a->params.file_priorities;

	TEST_EQUAL(l.size(), 3);
	TEST_EQUAL(l[0], 1_pri);
	TEST_EQUAL(l[1], 2_pri);
	TEST_EQUAL(l[2], 3_pri);
}

TORRENT_TEST(resume_save_load_resume)
{
	lt::session ses(settings());
	torrent_handle h = test_resume_flags(ses, {}, "", "123");

	h.save_resume_data();

	save_resume_data_alert const* a = alert_cast<save_resume_data_alert>(
		wait_for_alert(ses, save_resume_data_alert::alert_type
		, "resume_save_load"));

	TEST_CHECK(a);
	if (a == nullptr) return;

	auto const l = a->params.file_priorities;

	TEST_EQUAL(l.size(), 3);
	TEST_EQUAL(l[0], 1_pri);
	TEST_EQUAL(l[1], 2_pri);
	TEST_EQUAL(l[2], 3_pri);
}

TORRENT_TEST(file_priorities_resume)
{
	lt::session ses(settings());
	std::vector<download_priority_t> file_priorities = test_resume_flags(ses, {}, "", "123").get_file_priorities();

	TEST_EQUAL(file_priorities.size(), 3);
	TEST_EQUAL(file_priorities[0], 1_pri);
	TEST_EQUAL(file_priorities[1], 2_pri);
	TEST_EQUAL(file_priorities[2], 3_pri);
}

TORRENT_TEST(file_priorities1)
{
	lt::session ses(settings());
	std::vector<download_priority_t> file_priorities = test_resume_flags(ses, {}, "010").get_file_priorities();

	TEST_EQUAL(file_priorities.size(), 3);
	TEST_EQUAL(file_priorities[0], 0_pri);
	TEST_EQUAL(file_priorities[1], 1_pri);
	TEST_EQUAL(file_priorities[2], 0_pri);

//#error save resume data and assert the file priorities are preserved
}

TORRENT_TEST(file_priorities2)
{
	lt::session ses(settings());
	std::vector<download_priority_t> file_priorities = test_resume_flags(ses, {}, "123").get_file_priorities();

	TEST_EQUAL(file_priorities.size(), 3);
	TEST_EQUAL(file_priorities[0], 1_pri);
	TEST_EQUAL(file_priorities[1], 2_pri);
	TEST_EQUAL(file_priorities[2], 3_pri);
}

TORRENT_TEST(file_priorities3)
{
	lt::session ses(settings());
	std::vector<download_priority_t> file_priorities = test_resume_flags(ses, {}, "4321").get_file_priorities();

	TEST_EQUAL(file_priorities.size(), 3);
	TEST_EQUAL(file_priorities[0], 4_pri);
	TEST_EQUAL(file_priorities[1], 3_pri);
	TEST_EQUAL(file_priorities[2], 2_pri);
}

TORRENT_TEST(plain)
{
	lt::session ses(settings());

	torrent_status s = test_resume_flags(ses).status();
	default_tests(s);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(s.save_path, "c:\\add_torrent_params save_path");
#else
	TEST_EQUAL(s.save_path, "/add_torrent_params save_path");
#endif
	TEST_EQUAL(s.flags & flags_mask, torrent_flags_t{});
	TEST_EQUAL(s.connections_limit, 1345);
	TEST_EQUAL(s.uploads_limit, 1346);
}

TORRENT_TEST(seed_mode)
{
	lt::session ses(settings());
	torrent_status s = test_resume_flags(ses
		, torrent_flags::seed_mode).status();
	default_tests(s);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(s.save_path, "c:\\add_torrent_params save_path");
#else
	TEST_EQUAL(s.save_path, "/add_torrent_params save_path");
#endif
	TEST_EQUAL(s.flags & flags_mask, torrent_flags::seed_mode);
	TEST_EQUAL(s.connections_limit, 1345);
	TEST_EQUAL(s.uploads_limit, 1346);
}

TORRENT_TEST(upload_mode)
{
	lt::session ses(settings());
	torrent_status s = test_resume_flags(ses, torrent_flags::upload_mode).status();
	default_tests(s);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(s.save_path, "c:\\add_torrent_params save_path");
#else
	TEST_EQUAL(s.save_path, "/add_torrent_params save_path");
#endif
	TEST_EQUAL(s.flags & flags_mask, torrent_flags::upload_mode);
	TEST_EQUAL(s.connections_limit, 1345);
	TEST_EQUAL(s.uploads_limit, 1346);
}

#ifndef TORRENT_DISABLE_SHARE_MODE
TORRENT_TEST(share_mode)
{
	lt::session ses(settings());
	torrent_status s = test_resume_flags(ses
		, torrent_flags::share_mode).status();
	default_tests(s);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(s.save_path, "c:\\add_torrent_params save_path");
#else
	TEST_EQUAL(s.save_path, "/add_torrent_params save_path");
#endif
	TEST_EQUAL(s.flags & flags_mask, torrent_flags::share_mode);
	TEST_EQUAL(s.connections_limit, 1345);
	TEST_EQUAL(s.uploads_limit, 1346);
}
#endif

TORRENT_TEST(auto_managed)
{
	lt::session ses(settings());
	// resume data overrides the auto-managed flag
	torrent_status s = test_resume_flags(ses, torrent_flags::auto_managed).status();
	default_tests(s);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(s.save_path, "c:\\add_torrent_params save_path");
#else
	TEST_EQUAL(s.save_path, "/add_torrent_params save_path");
#endif
	TEST_EQUAL(s.flags & flags_mask, torrent_flags::auto_managed);
	TEST_EQUAL(s.connections_limit, 1345);
	TEST_EQUAL(s.uploads_limit, 1346);
}

TORRENT_TEST(paused)
{
	lt::session ses(settings());
	// resume data overrides the paused flag
	torrent_status s = test_resume_flags(ses, torrent_flags::paused).status();
	default_tests(s);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(s.save_path, "c:\\add_torrent_params save_path");
#else
	TEST_EQUAL(s.save_path, "/add_torrent_params save_path");
#endif
	TEST_EQUAL(s.flags & flags_mask, torrent_flags::paused);
	TEST_EQUAL(s.connections_limit, 1345);
	TEST_EQUAL(s.uploads_limit, 1346);

	// TODO: test all other resume flags here too. This would require returning
	// more than just the torrent_status from test_resume_flags. Also http seeds
	// and trackers for instance
}
