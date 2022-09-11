/*

Copyright (c) 2022, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "setup_transfer.hpp" // for load_file
#include "test_utils.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/flags.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/aux_/random.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/bencode.hpp"

namespace {

using lt::operator""_bit;
using similar_test_t = lt::flags::bitfield_flag<std::uint32_t, struct similar_test_type_tag>;

namespace st
{
	constexpr similar_test_t no_files = 0_bit;
	constexpr similar_test_t seed_mode = 1_bit;
	constexpr similar_test_t alt_b = 2_bit;
	constexpr similar_test_t alt_a = 3_bit;
	constexpr similar_test_t magnet = 4_bit;
	constexpr similar_test_t collection = 5_bit;
}

std::array<bool, 2> test(
	 similar_test_t const sflags
	, lt::create_flags_t const cflags1
	, lt::create_flags_t const cflags2)
{
	lt::error_code ec;
	lt::create_directories("./test-torrent-1", ec);
	TORRENT_ASSERT(!ec);

	std::vector<char> A(0x8000);
	std::vector<char> B(0x5000);
	lt::aux::random_bytes(A);
	lt::aux::random_bytes(B);

	std::vector<char> A_alt(0x8000);
	lt::aux::random_bytes(A_alt);
	std::vector<char> B_alt(0x5000);
	lt::aux::random_bytes(B_alt);

	{
		ofstream f("./test-torrent-1/A");
		f.write(A.data(), int(A.size()));
	}

	{
		ofstream f("./test-torrent-1/B");
		f.write(B.data(), int(B.size()));
	}

	auto t1 = [&] {
		std::vector<lt::create_file_entry> fs;
		fs.emplace_back("test-torrent-1/A", std::int64_t(A.size()));
		fs.emplace_back("test-torrent-1/B", std::int64_t(B.size()));
		lt::create_torrent t(std::move(fs), 0, cflags1);
		lt::set_piece_hashes(t, ".");
		if (sflags & st::collection)
			t.add_collection("test collection");
		std::vector<char> torrent;
		lt::bencode(back_inserter(torrent), t.generate());
		return std::make_shared<lt::torrent_info>(torrent, lt::from_span);
	}();

	lt::create_directories("test-torrent-2", ec);
	TORRENT_ASSERT(!ec);

	if (sflags & st::alt_a)
	{
		ofstream f("test-torrent-2/A");
		f.write(A_alt.data(), int(A_alt.size()));
	}
	else
	{
		ofstream f("test-torrent-2/A");
		f.write(A.data(), int(A.size()));
	}

	if (sflags & st::alt_b)
	{
		ofstream f("test-torrent-2/B");
		f.write(B_alt.data(), int(B_alt.size()));
	}
	else
	{
		ofstream f("test-torrent-2/B");
		f.write(B.data(), int(B.size()));
	}

	auto t2 = [&] {
		std::vector<lt::create_file_entry> fs;
		fs.emplace_back("test-torrent-2/A", std::int64_t(A.size()));
		fs.emplace_back("test-torrent-2/B", std::int64_t(B.size()));
		lt::create_torrent t(std::move(fs), 0, cflags2);
		lt::set_piece_hashes(t, ".");
		if (sflags & st::collection)
			t.add_collection("test collection");
		else
			t.add_similar_torrent(t1->info_hash());
		std::vector<char> torrent;
		lt::bencode(back_inserter(torrent), t.generate());
		return std::make_shared<lt::torrent_info>(torrent, lt::from_span);
	}();

	if (sflags & st::no_files)
	{
		lt::remove_all("test-torrent-1", ec);
		TORRENT_ASSERT(!ec);
	}

	lt::remove_all("test-torrent-2", ec);
	TORRENT_ASSERT(!ec);

	lt::settings_pack pack;
	pack.set_bool(lt::settings_pack::enable_dht, false);
	pack.set_bool(lt::settings_pack::enable_lsd, false);
	pack.set_bool(lt::settings_pack::enable_natpmp, false);
	pack.set_bool(lt::settings_pack::enable_upnp, false);
	pack.set_int(lt::settings_pack::alert_mask, lt::alert::all_categories);
	lt::session ses(pack);

	lt::add_torrent_params atp;
	atp.flags &= ~lt::torrent_flags::auto_managed;
	atp.flags &= ~lt::torrent_flags::paused;

	if (sflags & st::seed_mode)
		atp.flags |= lt::torrent_flags::seed_mode;

	atp.ti = t1;
	atp.save_path = ".";
	lt::torrent_handle h1 = ses.add_torrent(atp);

	wait_for_seeding(ses, "1");

	if (sflags & st::magnet)
	{
		atp.ti.reset();
		atp.info_hashes = t2->info_hashes();
	}
	else
	{
		atp.ti = t2;
	}
	atp.flags &= ~lt::torrent_flags::seed_mode;
	lt::torrent_handle h2 = ses.add_torrent(atp);

	if (sflags & st::magnet)
	{
		h2.set_metadata(t2->info_section());
	}

	std::array<bool, 2> completed_files{{false, false}};

	// wait for torrent 2 to either start downloading or finish. While waiting,
	// record which files it completes
	auto const start_time = lt::clock_type::now();
	for (;;)
	{
		auto const done = print_alerts(ses, "2", false, false
			, [&](lt::alert const* al)
			{
				if (auto const* sc = lt::alert_cast<lt::state_changed_alert>(al))
				{
					return sc->handle == h2
						&& (sc->state == lt::torrent_status::seeding
						|| sc->state == lt::torrent_status::finished
						|| sc->state == lt::torrent_status::downloading);
				}
				if (auto const* fc = lt::alert_cast<lt::file_completed_alert>(al))
				{
					if (fc->handle == h2)
						completed_files[std::size_t(static_cast<int>(fc->index))] = true;
				}
				return false;
			}, false);
		if (done) break;

		if (lt::clock_type::now() - start_time > lt::seconds(5))
		{
			TEST_ERROR("timeout");
			break;
		}
		ses.wait_for_alert(lt::seconds(5));
	}

	return completed_files;
}

}

using bools = std::array<bool, 2>;

auto const v1 = lt::create_torrent::v1_only;
auto const v2 = lt::create_torrent::v2_only;
auto const canon = lt::create_torrent::canonical_files;

TORRENT_TEST(shared_files_v1_no_pad)
{
	// the first file will be aligned, and since its size is an even multiple of
	// the piece size, the second file will too
	TEST_CHECK(test({}, v1, v1) == bools({true, true}));
}

TORRENT_TEST(shared_files_v1)
{
	// with canonical files, all files are aligned
	TEST_CHECK(test({}, v1 | canon, v1 | canon) == bools({true, true}));
}

TORRENT_TEST(shared_files_v1_collection)
{
	TEST_CHECK(test(st::collection, v1 | canon, v1 | canon) == bools({true, true}));
}

TORRENT_TEST(shared_files_v1_magnet)
{
	TEST_CHECK(test(st::magnet, v1 | canon, v1 | canon) == bools({true, true}));
}

TORRENT_TEST(shared_files_v1_magnet_collection)
{
	TEST_CHECK(test(st::magnet | st::collection, v1 | canon, v1 | canon) == bools({true, true}));
}

TORRENT_TEST(shared_files_v2_magnet)
{
	TEST_CHECK(test(st::magnet, v2, v2) == bools({true, true}));
}

TORRENT_TEST(shared_files_v2_magnet_collection)
{
	TEST_CHECK(test(st::magnet | st::collection, v2, v2) == bools({true, true}));
}

TORRENT_TEST(shared_files_hybrid_magnet)
{
	TEST_CHECK(test(st::magnet, {}, {}) == bools({true, true}));
}

TORRENT_TEST(shared_files_v1_v2_magnet)
{
	TEST_CHECK(test(st::magnet, v1 | canon, v2) == bools({false, false}));
}

TORRENT_TEST(shared_files_seed_mode_v1)
{
	TEST_CHECK(test(st::seed_mode, v1 | canon, v1 | canon) == bools({true, true}));
}

TORRENT_TEST(shared_files_seed_mode_v1_no_files)
{
	// no files on disk, just an (incorrect) promise of beeing in seed mode
	// creating the hard links will fail
	TEST_CHECK(test(st::no_files | st::seed_mode, v1 | canon, v1 | canon) == bools({false, false}));
}

TORRENT_TEST(single_shared_files_v1_b)
{
	TEST_CHECK(test(st::alt_b, v1 | canon, v1 | canon) == bools({true, false}));
}

TORRENT_TEST(single_shared_files_v1_a)
{
	TEST_CHECK(test(st::alt_a, v1 | canon, v1 | canon) == bools({false, true}));
}

TORRENT_TEST(shared_files_v1_v2)
{
	// v1 piece hashes cannot be compared to the v2 merkle roots
	TEST_CHECK(test({}, v1 | canon, v2) == bools({false, false}));
}

TORRENT_TEST(shared_files_v2)
{
	TEST_CHECK(test({}, v2, v2) == bools({true, true}));
}

TORRENT_TEST(shared_files_v2_collection)
{
	TEST_CHECK(test(st::collection, v2, v2) == bools({true, true}));
}

TORRENT_TEST(shared_files_seed_mode_v2)
{
	TEST_CHECK(test(st::seed_mode, v2, v2) == bools({true, true}));
}

TORRENT_TEST(shared_files_seed_mode_v2_no_files)
{
	TEST_CHECK(test(st::no_files | st::seed_mode, v2, v2) == bools({false, false}));
}

TORRENT_TEST(single_shared_files_v2_b)
{
	TEST_CHECK(test(st::alt_b, v2, v2) == bools({true, false}));
}

TORRENT_TEST(single_shared_files_v2_a)
{
	TEST_CHECK(test(st::alt_a, v2, v2) == bools({false, true}));
}

TORRENT_TEST(shared_files_hybrid)
{
	TEST_CHECK(test({}, {}, {}) == bools({true, true}));
}

TORRENT_TEST(shared_files_hybrid_v2)
{
	TEST_CHECK(test({}, {}, v2) == bools({true, true}));
}
