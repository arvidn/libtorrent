/*

Copyright (c) 2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "setup_transfer.hpp" // for load_file
#include "settings.hpp" // for settings()

#include "libtorrent/flags.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/path.hpp"

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

lt::error_code test_add_torrent(std::string file, add_torrent_test_flag_t const flags)
{
	std::string const root_dir = lt::parent_path(lt::current_working_directory());
	std::string const filename = lt::combine_path(lt::combine_path(root_dir, "test_torrents"), file);

	lt::error_code ec;
	std::vector<char> data;
	TEST_CHECK(load_file(filename, data, ec) == 0);

	auto ti = std::make_shared<lt::torrent_info>(data, ec, lt::from_span);
	TEST_CHECK(!ec);
	if (ec) std::printf(" loading(\"%s\") -> failed %s\n", filename.c_str()
		, ec.message().c_str());

	lt::add_torrent_params atp;
	atp.ti = ti;
	atp.save_path = ".";

#if TORRENT_ABI_VERSION < 3
	if (flags & set_info_hash) atp.info_hash = atp.ti->info_hash();
#endif
	if (flags & set_info_hashes_v1) atp.info_hashes.v1 = atp.ti->info_hashes().v1;
	if (flags & set_info_hashes_v2) atp.info_hashes.v2 = atp.ti->info_hashes().v2;
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

	lt::session_params p = settings();
	p.settings.set_int(lt::settings_pack::alert_mask, lt::alert_category::error | lt::alert_category::status);
	p.settings.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:6881");
	lt::session ses(p);
	try
	{
		if (flags & ec_add)
		{
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
