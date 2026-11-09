/*

Copyright (c) 2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "setup_transfer.hpp" // for load_file, make_torrent, wait_for_seeding
#include "settings.hpp" // for settings()

#include "libtorrent/flags.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/load_torrent.hpp"
#include "libtorrent/torrent_flags.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/torrent_status.hpp"

#include <array>
#include <climits>
#include <iostream>

namespace {

using add_torrent_test_flag_t = lt::flags::bitfield_flag<std::uint32_t, struct add_torrent_test_tag>;

using lt::operator""_bit;

#if TORRENT_ABI_VERSION < 3
add_torrent_test_flag_t const set_info_hash = 0_bit;
#endif
add_torrent_test_flag_t const set_info_hashes_v1 = 1_bit;
add_torrent_test_flag_t const set_info_hashes_v2 = 2_bit;
add_torrent_test_flag_t const async_add = 3_bit;
add_torrent_test_flag_t const ec_add = 4_bit;
add_torrent_test_flag_t const magnet_link = 5_bit;
#if TORRENT_ABI_VERSION < 3
add_torrent_test_flag_t const set_invalid_info_hash = 6_bit;
#endif
add_torrent_test_flag_t const set_invalid_info_hash_v1 = 7_bit;
add_torrent_test_flag_t const set_invalid_info_hash_v2 = 8_bit;

std::string test_torrent_path(std::string const& file)
{
	std::string const root_dir = lt::parent_path(lt::current_working_directory());
	return lt::combine_path(lt::combine_path(root_dir, "test_torrents"), file);
}

lt::session_params test_session_params()
{
	lt::session_params p = settings();
	p.settings.set_int(
		lt::settings_pack::alert_mask, lt::alert_category::error | lt::alert_category::status);
	p.settings.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:6881");
	return p;
}

lt::error_code test_add_torrent(std::string file, add_torrent_test_flag_t const flags)
{
	std::string const filename = test_torrent_path(file);

	lt::add_torrent_params atp;
	try
	{
		atp = lt::load_torrent_file(filename);
	}
	catch (lt::system_error const& e)
	{
		return e.code();
	}
	atp.save_path = ".";

#if TORRENT_ABI_VERSION < 3
	if (flags & set_info_hash) atp.info_hash = atp.ti->info_hash();
#endif
	if (!(flags & set_info_hashes_v1)) atp.info_hashes.v1.clear();
	if (!(flags & set_info_hashes_v2)) atp.info_hashes.v2.clear();
#if TORRENT_ABI_VERSION < 3
	if (flags & set_invalid_info_hash) atp.info_hash = lt::sha1_hash("abababababababababab");
#endif
	if (flags & set_invalid_info_hash_v1) atp.info_hashes.v1 = lt::sha1_hash("abababababababababab");
	if (flags & set_invalid_info_hash_v2) atp.info_hashes.v2 = lt::sha256_hash("abababababababababababababababab");

	std::vector<char> info_section;

	if (flags & magnet_link)
	{
		auto const is = atp.ti->info_section();
		info_section.assign(is.begin(), is.end());
		atp.ti.reset();
	}

	lt::session ses(test_session_params());
	try
	{
		if (flags & ec_add)
		{
			lt::error_code ec;
			ses.add_torrent(atp, ec);
			if (ec) return ec;
		}
		else if (flags & async_add)
		{
			ses.async_add_torrent(atp);
		}
		else
		{
			ses.add_torrent(atp);
		}
	}
	catch (lt::system_error const& e)
	{
		return e.code();
	}

	std::vector<lt::alert*> alerts;
	auto const start_time = lt::clock_type::now();
	while (lt::clock_type::now() - start_time < lt::seconds(3))
	{
		ses.wait_for_alert(lt::seconds(1));
		alerts.clear();
		ses.pop_alerts(&alerts);
		for (auto const* a : alerts)
		{
			std::cout << a->message() << '\n';
			if (auto const* te = lt::alert_cast<lt::torrent_error_alert>(a))
			{
				return te->error;
			}

			if (auto const* mf = lt::alert_cast<lt::metadata_failed_alert>(a))
			{
				return mf->error;
			}

			if (auto const* ta = lt::alert_cast<lt::add_torrent_alert>(a))
			{
				if (ta->error) return ta->error;

				if (flags & magnet_link)
				{
					// if this fails, we'll pick up the metadata_failed_alert
					TEST_CHECK(ta->handle.is_valid());
					ta->handle.set_metadata(info_section);
				}
				else
				{
					// success!
					return lt::error_code();
				}
			}

			if (lt::alert_cast<lt::metadata_received_alert>(a))
			{
				// success!
				return lt::error_code();
			}
		}
	}

	return lt::error_code();
}

lt::torrent_status add_torrent_with_scrape(
	lt::session& ses, int const idx, int const complete, int const incomplete, int const downloaded)
{
	// each case needs a distinct torrent, a session cannot hold the same one
	// twice
	std::string const name = "scrape_" + std::to_string(idx);
	lt::add_torrent_params atp = make_torrent({{name, 0x4000}}, 0x4000);
	atp.save_path = ".";
	// seed mode makes the torrent finished right away, which is what makes
	// seed_rank() consult the scrape counters. There are no files on disk, so
	// verifying them would leave seed mode. Not auto managed, so the torrent
	// is never paused by the queuing logic
	atp.flags = lt::torrent_flags::seed_mode | lt::torrent_flags::no_verify_files;
	atp.num_complete = complete;
	atp.num_incomplete = incomplete;
	atp.num_downloaded = downloaded;

	lt::torrent_handle const h = ses.add_torrent(atp);

	// seed_rank() is only meaningful once the resume data check has completed
	wait_for_seeding(ses, name.c_str());
	lt::torrent_status const st = h.status();
	TEST_EQUAL(st.state, lt::torrent_status::seeding);
	return st;
}

struct test_case_t
{
	char const* filename;
	add_torrent_test_flag_t flags;
	lt::error_code expected_error;
};

auto const v2 = "v2.torrent";
auto const hybrid = "v2_hybrid.torrent";
auto const v1 = "base.torrent";

test_case_t const add_torrent_test_cases[] = {
	{v2, {}, {}},
	{v2, set_info_hashes_v1, {}},
	{v2, set_info_hashes_v2, {}},
	{v2, set_info_hashes_v1 | set_info_hashes_v2, {}},
#if TORRENT_ABI_VERSION < 3
	{v2, set_info_hash, {}},
	// the info_hash field is ignored when we have an actual torrent_info object
	{v2, set_invalid_info_hash, {}},
#endif
	{v2, set_invalid_info_hash_v1, lt::errors::mismatching_info_hash},
	{v2, set_invalid_info_hash_v2, lt::errors::mismatching_info_hash},

	{hybrid, {}, {}},
	{hybrid, set_info_hashes_v1, {}},
	{hybrid, set_info_hashes_v2, {}},
	{hybrid, set_info_hashes_v1 | set_info_hashes_v2, {}},
#if TORRENT_ABI_VERSION < 3
	{hybrid, set_info_hash, {}},
	// the info_hash field is ignored when we have an actual torrent_info object
	{hybrid, set_invalid_info_hash, {}},
#endif
	{hybrid, set_invalid_info_hash_v1, lt::errors::mismatching_info_hash},
	{hybrid, set_invalid_info_hash_v2, lt::errors::mismatching_info_hash},

	{v1, {}, {}},
	{v1, set_info_hashes_v1, {}},
#if TORRENT_ABI_VERSION < 3
	{v1, set_info_hash, {}},
	// the info_hash field is ignored when we have an actual torrent_info object
	{v1, set_invalid_info_hash, {}},
#endif

	// magnet links
	{v2, magnet_link, lt::errors::missing_info_hash_in_uri},
	{v2, magnet_link | set_info_hashes_v1, {}},
	{v2, magnet_link | set_info_hashes_v2, {}},
#if TORRENT_ABI_VERSION < 3
	// a v2-only magnet link supports magnet links with a truncated hash
	{v2, magnet_link | set_info_hash, {}},
	{v2, magnet_link | set_invalid_info_hash, lt::errors::mismatching_info_hash},
#endif
	{v2, magnet_link | set_info_hashes_v1 | set_info_hashes_v2, {}},
	{v2, magnet_link | set_invalid_info_hash_v1, lt::errors::mismatching_info_hash},
	{v2, magnet_link | set_invalid_info_hash_v2, lt::errors::mismatching_info_hash},

	{hybrid, magnet_link, lt::errors::missing_info_hash_in_uri},
	{hybrid, magnet_link | set_info_hashes_v1, {}},
	{hybrid, magnet_link | set_info_hashes_v2, {}},
#if TORRENT_ABI_VERSION < 3
	{hybrid, magnet_link | set_info_hash, {}},
	{hybrid, magnet_link | set_invalid_info_hash, lt::errors::mismatching_info_hash},
#endif
	{hybrid, magnet_link | set_info_hashes_v1 | set_info_hashes_v2, {}},
	{hybrid, magnet_link | set_invalid_info_hash_v1, lt::errors::mismatching_info_hash},
	{hybrid, magnet_link | set_invalid_info_hash_v2, lt::errors::mismatching_info_hash},

	{v1, magnet_link, lt::errors::missing_info_hash_in_uri},
#if TORRENT_ABI_VERSION < 3
	{v1, magnet_link | set_info_hash, {}},
	{v1, magnet_link | set_invalid_info_hash, lt::errors::mismatching_info_hash},
#endif
	{v1, magnet_link | set_info_hashes_v1, {}},
	{v1, magnet_link | set_invalid_info_hash_v1, lt::errors::mismatching_info_hash},
	{v1, magnet_link | set_invalid_info_hash_v2, lt::errors::mismatching_info_hash},
};

}

TORRENT_TEST(invalid_file_root)
{
	TEST_CHECK(test_add_torrent("v2_invalid_root_hash.torrent", {}) == lt::error_code(lt::errors::torrent_invalid_piece_layer));
}

TORRENT_TEST(add_torrent)
{
	int i = 0;
	for (auto const& test_case : add_torrent_test_cases)
	{
		std::cerr << "idx: " << i << '\n';
		auto const e = test_add_torrent(test_case.filename, test_case.flags);
		if (e != test_case.expected_error)
		{
			std::cerr << test_case.filename << '\n';
			TEST_ERROR(e.message() + " != " + test_case.expected_error.message());
		}
		++i;
	}
}

TORRENT_TEST(async_add_torrent)
{
	int i = 0;
	for (auto const& test_case : add_torrent_test_cases)
	{
		auto const e = test_add_torrent(test_case.filename, test_case.flags | async_add);
		if (e != test_case.expected_error)
		{
			std::cerr << "idx: " << i << " " << test_case.filename << '\n';
			TEST_ERROR(e.message() + " != " + test_case.expected_error.message());
		}
		++i;
	}
}

TORRENT_TEST(ec_add_torrent)
{
	int i = 0;
	for (auto const& test_case : add_torrent_test_cases)
	{
		auto const e = test_add_torrent(test_case.filename, test_case.flags | ec_add);
		if (e != test_case.expected_error)
		{
			std::cerr << "idx: " << i << " " << test_case.filename << '\n';
			TEST_ERROR(e.message() + " != " + test_case.expected_error.message());
		}
		++i;
	}
}

TORRENT_TEST(add_torrent_scrape_data)
{
	int constexpr no_seeds = 0x10000000;
	int constexpr prio_mask = 0x0fffffff;
	// the scrape counters are stored in 24 bit fields, with the all-ones value
	// reserved for "unknown"
	int constexpr max_scrape = 0xfffffe;

	// the seed_ratio_not_met and recently_started bits depend on timing, so
	// they are not part of the expected ranks
	int constexpr rank_mask = no_seeds | prio_mask;

	struct scrape_test_t
	{
		int complete;
		int incomplete;
		int downloaded;
		int expected_complete;
		int expected_incomplete;
		int expected_rank;
	};

	std::array<scrape_test_t, 8> const tests{{
		{-1, -1, -1, -1, -1, no_seeds},
		// values that don't fit are treated as unknown
		{INT_MAX, INT_MAX, INT_MAX, -1, -1, no_seeds},
		{INT_MIN, INT_MIN, INT_MIN, -1, -1, no_seeds},
		{0xffffff, 0xffffff, 0xffffff, -1, -1, no_seeds},
		{0, 0, 0, 0, 0, no_seeds},
		// we are the one seed, so it's not counted
		{1, max_scrape, max_scrape, 1, max_scrape, no_seeds | max_scrape},
		// the rank saturates rather than overflow
		{2, max_scrape, 0, 2, max_scrape, prio_mask},
		{max_scrape, max_scrape, max_scrape, max_scrape, max_scrape, 1000},
	}};

	lt::session ses(test_session_params());
	int idx = 0;
	for (auto const& t : tests)
	{
		std::cerr << "complete: " << t.complete << " incomplete: " << t.incomplete
				  << " downloaded: " << t.downloaded << '\n';
		lt::torrent_status const st =
			add_torrent_with_scrape(ses, idx++, t.complete, t.incomplete, t.downloaded);
		TEST_EQUAL(st.num_complete, t.expected_complete);
		TEST_EQUAL(st.num_incomplete, t.expected_incomplete);
		TEST_EQUAL(st.seed_rank & rank_mask, t.expected_rank);
		TEST_CHECK(st.seed_rank >= 0);
	}
}
