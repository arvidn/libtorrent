/*

Copyright (c) 2014-2022, Arvid Norberg
Copyright (c) 2016, 2018, 2021, Alden Torres
Copyright (c) 2017, Steven Siloti
Copyright (c) 2020, AllSeeingEyeTolledEweSew
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/session.hpp"
#include "libtorrent/session_params.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/aux_/random.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/read_resume_data.hpp"
#include "libtorrent/write_resume_data.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/aux_/file.hpp"
#include "libtorrent/alert_types.hpp"
#include "setup_transfer.hpp"
#include "test_utils.hpp"

#include "test.hpp"
#include "test_utils.hpp"
#include "settings.hpp"
#include "setup_transfer.hpp"
#include "settings.hpp"

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
	rd["info-hash"] = ti->info_hashes().v1.to_string();
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
	rd["last_seen_complete"] = 1353;
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
	bool const with_files = (flags & torrent_flags::seed_mode) && !(flags & torrent_flags::no_verify_files);
	std::shared_ptr<torrent_info> ti = generate_torrent(with_files);

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

	if (with_files)
	{
		p.save_path = ".";
	}
	else
	{
#ifdef TORRENT_WINDOWS
		p.save_path = "c:\\add_torrent_params save_path";
#else
		p.save_path = "/add_torrent_params save_path";
#endif
	}

	if (file_priorities[0])
	{
		aux::vector<download_priority_t, file_index_t> priorities_vector;
		for (int i = 0; file_priorities[i]; ++i)
			priorities_vector.push_back(download_priority_t(aux::numeric_cast<std::uint8_t>(file_priorities[i] - '0')));

		p.file_priorities = priorities_vector;
	}

	torrent_handle h = ses.add_torrent(p);
	torrent_status s = h.status();
	TEST_EQUAL(s.info_hashes, ti->info_hashes());
	return h;
}

void default_tests(torrent_status const& s, lt::time_point const time_now)
{
	TORRENT_UNUSED(time_now);
	// allow some slack in the time stamps since they are reported as
	// relative times. If the computer is busy while running the unit test
	// or running under valgrind it may take several seconds
#if TORRENT_ABI_VERSION == 1
	TEST_CHECK(s.active_time >= 1339);
	TEST_CHECK(s.active_time < 1339 + 10);

	auto const now = duration_cast<seconds>(time_now.time_since_epoch()).count();
	TEST_CHECK(s.time_since_download >= now - 2);
	TEST_CHECK(s.time_since_upload >= now - 3);

	TEST_CHECK(s.time_since_download < now - 2 + 10);
	TEST_CHECK(s.time_since_upload < now - 3 + 10);

	TEST_CHECK(s.finished_time < 1352 + 2);
	TEST_CHECK(s.finished_time >= 1352);
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

	TEST_EQUAL(s.last_seen_complete, 1353);
}

void test_piece_priorities(bool test_deprecated = false)
{
	lt::session ses(settings());
	std::shared_ptr<torrent_info> ti = generate_torrent();
	add_torrent_params p;
	p.ti = ti;
	p.save_path = ".";
	torrent_handle h = ses.add_torrent(p);

	h.piece_priority(0_piece, 0_pri);
	h.piece_priority(ti->last_piece(), 0_pri);

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

	TEST_EQUAL(h.piece_priority(0_piece), 0_pri);
	TEST_EQUAL(h.piece_priority(1_piece), 4_pri);
	TEST_EQUAL(h.piece_priority(ti->last_piece()), 0_pri);
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
	TEST_EQUAL(h.trackers().at(0).url, "http://torrent_file_tracker2.com/announce");
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
		ofstream("add_torrent_params_test" SEP "test_resume" SEP "tmp1").write(a.data(), std::streamsize(a.size()));
		ofstream("add_torrent_params_test" SEP "test_resume" SEP "tmp2").write(b.data(), std::streamsize(b.size()));
		ofstream("add_torrent_params_test" SEP "test_resume" SEP "tmp3").write(b.data(), std::streamsize(b.size()));
	}

	add_torrent_params p;
	p.ti = ti;
	p.save_path = "add_torrent_params_test";

	p.have_pieces.resize(2);
	p.have_pieces.set_bit(0_piece);
	p.have_pieces.set_bit(1_piece);

	lt::session ses(settings());
	torrent_handle h = ses.add_torrent(p);

	wait_for_alert(ses, torrent_checked_alert::alert_type, "piece_slots");

	torrent_status s = h.status();
	print_alerts(ses, "ses");
	TEST_EQUAL(s.info_hashes, ti->info_hashes());
	TEST_EQUAL(s.pieces.size(), ti->num_pieces());
	TEST_CHECK(s.pieces.size() >= 4);
	TEST_EQUAL(s.pieces[0_piece], true);
	TEST_EQUAL(s.pieces[1_piece], true);
	TEST_EQUAL(s.pieces[2_piece], false);
	TEST_EQUAL(s.pieces[3_piece], false);

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

		TEST_EQUAL(pieces[0_piece], true);
		TEST_EQUAL(pieces[1_piece], true);
		TEST_EQUAL(pieces[2_piece], false);
		TEST_EQUAL(pieces[3_piece], false);
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
		ofstream("add_torrent_params_test" SEP "test_resume" SEP "tmp1").write(a.data(), std::streamsize(a.size()));
		ofstream("add_torrent_params_test" SEP "test_resume" SEP "tmp2").write(b.data(), std::streamsize(b.size()));
		ofstream("add_torrent_params_test" SEP "test_resume" SEP "tmp3").write(b.data(), std::streamsize(b.size()));
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
	TEST_EQUAL(s.info_hashes, ti->info_hashes());
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

	auto const now = lt::clock_type::now();
	torrent_status s = test_resume_flags(ses, {}, "", "", true).status();
	default_tests(s, now);
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
	auto const now = lt::clock_type::now();
	torrent_status s = test_resume_flags(ses
		, torrent_flags::use_resume_save_path, "", "", true).status();
	default_tests(s, now);
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
	auto const now = lt::clock_type::now();
	torrent_status s = test_resume_flags(ses
		, torrent_flags::override_resume_data
		| torrent_flags::paused, "", "", true).status();

	default_tests(s, now);
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
	auto const now = lt::clock_type::now();
	torrent_status s = test_resume_flags(ses, torrent_flags::override_resume_data
		| torrent_flags::seed_mode, "", "", true).status();
	default_tests(s, now);
	TEST_EQUAL(s.flags & flags_mask, torrent_flags::seed_mode);
	TEST_EQUAL(s.connections_limit, 2);
	TEST_EQUAL(s.uploads_limit, 1);
}

TORRENT_TEST(upload_mode_deprecated)
{
	lt::session ses(settings());
	auto const now = lt::clock_type::now();
	torrent_status s = test_resume_flags(ses
		, torrent_flags::upload_mode, "", "", true).status();
	default_tests(s, now);
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
	auto const now = lt::clock_type::now();
	torrent_status s = test_resume_flags(ses
		, torrent_flags::override_resume_data
		| torrent_flags::share_mode, "", "", true).status();
	default_tests(s, now);
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
	auto const now = lt::clock_type::now();
	torrent_status s = test_resume_flags(ses
		, torrent_flags::auto_managed, "", "", true).status();
	default_tests(s, now);
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
	auto const now = lt::clock_type::now();
	torrent_status s = test_resume_flags(ses, torrent_flags::paused, "", "", true).status();
	default_tests(s, now);
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

	TEST_EQUAL(us.size(), 3);
	TEST_EQUAL(std::count(us.begin(), us.end()
		, "http://add_torrent_params_url_seed.com/"), 1);
	TEST_EQUAL(std::count(us.begin(), us.end()
		, "http://torrent_file_url_seed.com/"), 1);
	TEST_EQUAL(std::count(us.begin(), us.end()
		, "http://resume_data_url_seed.com/"), 1);
}

TORRENT_TEST(resume_override_torrent_deprecated)
{
	// resume data overrides the .torrent_file
	std::printf("flags: no merge_resume_http_seed\n");
	lt::session ses(settings());
	torrent_handle h = test_resume_flags(ses,
		torrent_flags::merge_resume_trackers, "", "", true);
	std::set<std::string> us = h.url_seeds();

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
	rd["info-hash"] = ti->info_hashes().v1.to_string();
	rd["blocks per piece"] = std::max(1, ti->piece_length() / 0x4000);

	// set file priorities to 0
	rd["file_priority"] = entry::list_type(100, entry(0));

	if (mix_prios)
		rd["pieces"] = std::string(std::size_t(ti->num_pieces()), '\x01');
	else
		rd["pieces"] = std::string(std::size_t(ti->num_pieces()), '\x00');

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

	lt::torrent_status s = h.status();
	for (int i = 0; i < 40; ++i)
	{
		if (s.state != torrent_status::checking_files
			&& s.state != torrent_status::checking_resume_data)
			break;
		std::this_thread::sleep_for(lt::milliseconds(100));
		s = h.status();
	}

	// Once a torrent becomes seed, any piece- and file priorities are
	// forgotten and all bytes are considered "wanted". So, whether we "want"
	// all bytes here or not, depends on whether we're a seed
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
	rd["info-hash"] = ti->info_hashes().v1;
	rd["info"] = bdecode(ti->info_section());
	std::vector<char> resume_data;
	bencode(back_inserter(resume_data), rd);

	add_torrent_params atp;
	atp.resume_data = std::move(resume_data);
	atp.save_path = ".";

	session ses;
	torrent_handle h = ses.add_torrent(atp);
	auto torrent = h.torrent_file();
	TEST_CHECK(torrent->info_hashes() == ti->info_hashes());
	torrent_status s = h.status();
}
#endif

TORRENT_TEST(merkle_trees)
{
	lt::add_torrent_params p;
	p.ti = generate_torrent();
	p.save_path = ".";

	std::vector<std::vector<char>> piece_layers;

	for (file_index_t const i : p.ti->files().file_range())
	{
		auto const pspan = p.ti->piece_layer(i);
		piece_layers.emplace_back(pspan.begin(), pspan.end());
	}

	lt::session ses(settings());
	auto h = ses.add_torrent(p);

	h.save_resume_data();

	save_resume_data_alert const* a = alert_cast<save_resume_data_alert>(
		wait_for_alert(ses, save_resume_data_alert::alert_type
		, "merkle_trees"));

	TEST_CHECK(a);
	if (a == nullptr) return;

	TEST_EQUAL(a->params.merkle_trees.size(), 3);
	TEST_EQUAL(a->params.merkle_tree_mask.size(), 3);

	auto const pl = h.piece_layers();
	TEST_CHECK(pl.size() == piece_layers.size());

	for (file_index_t const i : p.ti->files().file_range())
	{
		auto const& one_layer = pl[std::size_t(int(i))];
		TEST_CHECK(one_layer.size() == piece_layers[std::size_t(int(i))].size() / lt::sha256_hash::size());
		for (int piece = 0; piece < int(one_layer.size()); ++piece)
			TEST_CHECK(one_layer[std::size_t(piece)] == lt::sha256_hash(piece_layers[std::size_t(int(i))].data() + piece * lt::sha256_hash::size()));
		auto const& m = a->params.merkle_tree_mask[i];
		TEST_CHECK(std::count(m.begin(), m.end(), true) == int(a->params.merkle_trees[i].size()));
	}
}

TORRENT_TEST(resume_info_dict)
{
	// make sure the "info" dictionary is picked up correctly from the
	// resume data

	std::shared_ptr<torrent_info> ti = generate_torrent();
	entry rd;
	rd["file-format"] = "libtorrent resume file";
	rd["name"] = ti->name();
	rd["info-hash"] = ti->info_hashes().v1;
	rd["info"] = bdecode(ti->info_section());
	std::vector<char> resume_data;
	bencode(back_inserter(resume_data), rd);

	error_code ec;
	add_torrent_params atp = read_resume_data(resume_data, ec);
	TEST_CHECK(atp.ti->info_hashes() == ti->info_hashes());
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
	constexpr test_mode_t missing_files = 5_bit;
	constexpr test_mode_t pieces_have_all = 6_bit;
	constexpr test_mode_t missing_all_files = 7_bit;
	constexpr test_mode_t extended_files = 8_bit;
}

namespace {

void test_seed_mode(test_mode_t const flags)
{
	lt::session ses(settings());
	std::shared_ptr<torrent_info> ti = generate_torrent(true);
	add_torrent_params p;
	p.ti = ti;
	p.save_path = ".";

	if (flags & test_mode::missing_files)
	{
		TEST_CHECK(::remove(combine_path("test_resume", "tmp2").c_str()) == 0);
	}

	if (flags & test_mode::missing_all_files)
	{
		lt::error_code ec;
		lt::remove_all("test_resume", ec);
		TEST_CHECK(!ec);
	}

	if (flags & test_mode::extended_files)
	{
		int const ret = ::truncate("test_resume" SEP "tmp2", 128 * 1024 + 10);
		TEST_EQUAL(ret, 0);
	}

	entry rd;

	rd["file-format"] = "libtorrent resume file";
	rd["file-version"] = 1;
	rd["info-hash"] = ti->info_hashes().v1.to_string();
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

	if (flags & test_mode::pieces_have_all)
	{
		rd["pieces"] = std::string(std::size_t(ti->num_pieces()), '\x01');
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

	// if we have all pieces, any zero-priority file is not checked against the
	// disk. But if we have any zero-priority file, the seed-mode flag is cleared
	if ((flags & (test_mode::piece_prio
		| test_mode::missing_files
		| test_mode::missing_all_files
		| test_mode::pieces_have))
		|| (flags & (test_mode::pieces_have_all | test_mode::file_prio)) == test_mode::file_prio
		)
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
				else if (alert_cast<torrent_finished_alert>(a))
				{
					// the torrent is not finished!
					TEST_CHECK(false);
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
		bool finished = false;
		auto const start_time = lt::clock_type::now();
		while (!done || !finished)
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
				else if (alert_cast<torrent_finished_alert>(a))
				{
					finished = true;
				}
			}
			if (lt::clock_type::now() - start_time > seconds(5)) break;
		}
		TEST_CHECK(done);
		TEST_CHECK(finished);
		torrent_status const s = h.status();
		if (flags & test_mode::file_prio)
			TEST_CHECK(!(s.flags & torrent_flags::seed_mode));
		else
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

TORRENT_TEST(seed_mode_missing_files_deprecated)
{
	test_seed_mode(test_mode::missing_files | test_mode::deprecated);
}

TORRENT_TEST(seed_mode_missing_all_files_deprecated)
{
	test_seed_mode(test_mode::missing_all_files | test_mode::deprecated);
}

TORRENT_TEST(seed_mode_missing_files_with_pieces_deprecated)
{
	test_seed_mode(test_mode::missing_files | test_mode::pieces_have | test_mode::deprecated);
}

TORRENT_TEST(seed_mode_missing_files_with_all_pieces_deprecated)
{
	test_seed_mode(test_mode::missing_files | test_mode::pieces_have_all | test_mode::deprecated);
}
#endif

TORRENT_TEST(seed_mode_file_prio)
{
	test_seed_mode(test_mode::file_prio);
}

TORRENT_TEST(seed_mode_extended_files)
{
	test_seed_mode(test_mode::extended_files);
}

TORRENT_TEST(seed_mode_have_file_prio)
{
	test_seed_mode(test_mode::pieces_have_all | test_mode::file_prio);
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

TORRENT_TEST(seed_mode_missing_files)
{
	test_seed_mode(test_mode::missing_files);
}

TORRENT_TEST(seed_mode_missing_all_files)
{
	test_seed_mode(test_mode::missing_all_files);
}

TORRENT_TEST(seed_mode_missing_files_with_pieces)
{
	test_seed_mode(test_mode::missing_files | test_mode::pieces_have);
}

TORRENT_TEST(seed_mode_missing_files_with_all_pieces)
{
	test_seed_mode(test_mode::missing_files | test_mode::pieces_have_all);
}

TORRENT_TEST(seed_mode_load_peers)
{
	lt::session ses(settings());
	std::shared_ptr<torrent_info> ti = generate_torrent();
	add_torrent_params p;
	p.ti = ti;
	p.save_path = ".";
	p.flags |= torrent_flags::seed_mode;
	p.peers.push_back(tcp::endpoint(make_address("1.2.3.4"), 12345));

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
	TEST_CHECK(peers[0] == tcp::endpoint(make_address("1.2.3.4"), 12345));
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

	auto const now = lt::clock_type::now();
	torrent_status s = test_resume_flags(ses).status();
	default_tests(s, now);
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
	auto const now = lt::clock_type::now();
	torrent_status s = test_resume_flags(ses
		, torrent_flags::seed_mode).status();
	default_tests(s, now);
	TEST_EQUAL(s.flags & flags_mask, torrent_flags::seed_mode);
	TEST_EQUAL(s.connections_limit, 1345);
	TEST_EQUAL(s.uploads_limit, 1346);
}

TORRENT_TEST(seed_mode_no_verify_files)
{
	lt::session ses(settings());
	auto const now = lt::clock_type::now();
	torrent_status s = test_resume_flags(ses
		, torrent_flags::seed_mode | torrent_flags::no_verify_files).status();
	default_tests(s, now);
	// note that torrent_flags::no_verify_files is NOT set here
	TEST_EQUAL(s.flags & flags_mask, torrent_flags::seed_mode);
	TEST_EQUAL(s.connections_limit, 1345);
	TEST_EQUAL(s.uploads_limit, 1346);
}

TORRENT_TEST(upload_mode)
{
	lt::session ses(settings());
	auto const now = lt::clock_type::now();
	torrent_status s = test_resume_flags(ses, torrent_flags::upload_mode).status();
	default_tests(s, now);
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
	auto const now = lt::clock_type::now();
	torrent_status s = test_resume_flags(ses
		, torrent_flags::share_mode).status();
	default_tests(s, now);
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
	auto const now = lt::clock_type::now();
	torrent_status s = test_resume_flags(ses, torrent_flags::auto_managed).status();
	default_tests(s, now);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(s.save_path, "c:\\add_torrent_params save_path");
#else
	TEST_EQUAL(s.save_path, "/add_torrent_params save_path");
#endif
	// auto managed torrents may have been paused by the time we get here, so
	// filter out that flag
	TEST_EQUAL((s.flags & flags_mask) & ~torrent_flags::paused, torrent_flags::auto_managed);
	TEST_EQUAL(s.connections_limit, 1345);
	TEST_EQUAL(s.uploads_limit, 1346);
}

TORRENT_TEST(paused)
{
	lt::session ses(settings());
	// resume data overrides the paused flag
	auto const now = lt::clock_type::now();
	torrent_status s = test_resume_flags(ses, torrent_flags::paused).status();
	default_tests(s, now);
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

TORRENT_TEST(no_metadata)
{
	lt::session ses(settings());

	add_torrent_params p;
	p.info_hashes.v1 = sha1_hash("abababababababababab");
	p.save_path = ".";
	p.name = "foobar";
	torrent_handle h = ses.add_torrent(p);
	h.save_resume_data(torrent_handle::save_info_dict);
	alert const* a = wait_for_alert(ses, save_resume_data_alert::alert_type);
	TEST_CHECK(a);
	save_resume_data_alert const* ra = alert_cast<save_resume_data_alert>(a);
	TEST_CHECK(ra);
	if (ra)
	{
		auto const& atp = ra->params;
		TEST_EQUAL(atp.info_hashes, p.info_hashes);
		TEST_EQUAL(atp.name, "foobar");
	}
}

template <typename Fun>
void test_unfinished_pieces(Fun f)
{
	// create a torrent and complete files
	std::shared_ptr<torrent_info> ti = generate_torrent(true, true);

	add_torrent_params p;
	p.info_hashes = ti->info_hashes();
	p.have_pieces.resize(ti->num_pieces(), true);
	p.ti = ti;
	p.save_path = ".";

	f(*ti, p);

	lt::session ses(settings());
	torrent_handle h = ses.add_torrent(p);
	torrent_status s = h.status();
	TEST_EQUAL(s.info_hashes, ti->info_hashes());

	if (s.state == torrent_status::seeding) return;

	print_alerts(ses, "ses");

	for (int i = 0; i < 30; ++i)
	{
		std::this_thread::sleep_for(lt::milliseconds(100));
		s = h.status();
		print_alerts(ses, "ses");
		if (s.state == torrent_status::seeding) return;
	}

	TEST_EQUAL(s.state, torrent_status::seeding);
}

TORRENT_TEST(unfinished_pieces_pure_seed)
{
	test_unfinished_pieces([](torrent_info const&, add_torrent_params&){});
}

TORRENT_TEST(unfinished_pieces_check_all)
{
	test_unfinished_pieces([](torrent_info const&, add_torrent_params& atp)
	{
		atp.have_pieces.clear();
	});
}

TORRENT_TEST(unfinished_pieces_finished)
{
	// make sure that a piece that isn't maked as "have", but whose blocks are
	// all downloaded gets checked and turn into "have".
	test_unfinished_pieces([](torrent_info const& ti, add_torrent_params& atp)
	{
		atp.have_pieces.clear_bit(0_piece);
		atp.unfinished_pieces[0_piece].resize(ti.piece_length() / 0x4000, true);
	});
}

TORRENT_TEST(unfinished_pieces_all_finished)
{
	// make sure that a piece that isn't maked as "have", but whose blocks are
	// all downloaded gets checked and turn into "have".
	test_unfinished_pieces([](torrent_info const& ti, add_torrent_params& atp)
	{
		// we have none of the pieces
		atp.have_pieces.clear_all();
		int const blocks_per_piece = ti.piece_length() / 0x4000;

		// but all pieces are downloaded
		for (piece_index_t p : ti.piece_range())
			atp.unfinished_pieces[p].resize(blocks_per_piece, true);
	});
}

#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE
// this test relies on (and tests) the disk I/O being asynchronous. Since the
// posix_disk_io isn't, this test won't pass
TORRENT_TEST(resume_data_have_pieces)
{
	if (sizeof(void*) < 8)
	{
		// disable this test when disk I/O is not async
		return;
	}

	std::vector<lt::create_file_entry> fs;
	fs.emplace_back("tmp1", 128 * 1024 * 8);
	lt::create_torrent t(std::move(fs), 128 * 1024);

	TEST_CHECK(t.num_pieces() > 0);

	std::vector<char> piece_data(std::size_t(t.piece_length()), 0);
	aux::random_bytes(piece_data);

	sha1_hash const ph = lt::hasher(piece_data).final();
	for (auto const i : t.piece_range())
		t.set_hash(i, ph);

	std::vector<char> buf;
	bencode(std::back_inserter(buf), t.generate());
	auto ti = std::make_shared<torrent_info>(buf, from_span);

	lt::session ses(settings());
	lt::add_torrent_params atp;
	atp.ti = ti;
	atp.flags &= ~torrent_flags::paused;
	atp.save_path = ".";
	auto h = ses.add_torrent(atp);
	wait_for_downloading(ses, "");
	h.add_piece(0_piece, std::move(piece_data));

	h.save_resume_data();
	ses.pause();

	auto const* rs = static_cast<save_resume_data_alert const*>(
		wait_for_alert(ses, save_resume_data_alert::alert_type));
	TEST_CHECK(rs != nullptr);
	TEST_EQUAL(rs->params.unfinished_pieces.size(), 1);
}
#endif

// See https://github.com/arvidn/libtorrent/issues/5174
TORRENT_TEST(removed)
{
	lt::session ses(settings());
	std::shared_ptr<torrent_info> ti = generate_torrent();
	add_torrent_params p;
	p.ti = ti;
	p.save_path = ".";
	// we're _likely_ to trigger the condition, but not guaranteed. loop
	// until we do.
	bool triggered = false;
	for (int i = 0; i < 10; i++) {
		torrent_handle h = ses.add_torrent(p);
		// this is asynchronous
		ses.remove_torrent(h);
		try {
			h.save_resume_data();
			triggered = true;
		} catch (std::exception const&) {
			std::printf("failed to trigger condition, retrying\n");
		}
	}
	TEST_CHECK(triggered);
	if (!triggered) return;
	alert const* a = wait_for_alert(ses, save_resume_data_failed_alert::alert_type);
	TEST_CHECK(a != nullptr);
}
