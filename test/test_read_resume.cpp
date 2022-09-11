/*

Copyright (c) 2016, 2018, 2021, Alden Torres
Copyright (c) 2016-2022, Arvid Norberg
Copyright (c) 2017, Steven Siloti
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "test_utils.hpp"

#include <vector>

#include "libtorrent/entry.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/aux_/random.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/read_resume_data.hpp"
#include "libtorrent/write_resume_data.hpp"

using namespace lt;

TORRENT_TEST(read_resume)
{
	entry rd;

	rd["file-format"] = "libtorrent resume file";
	rd["file-version"] = 1;
	rd["info-hash"] = "abcdefghijklmnopqrst";
	rd["pieces"] = "\x01\x01\x01\x01\x01\x01";

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
	rd["last_seen_complete"] = 1353;

	rd["piece_priority"] = "\x01\x02\x03\x04\x05\x06";
	rd["auto_managed"] = 0;
	rd["sequential_download"] = 0;
	rd["paused"] = 0;

	std::vector<char> resume_data;
	bencode(std::back_inserter(resume_data), rd);

	add_torrent_params atp = read_resume_data(resume_data);

	TEST_EQUAL(atp.info_hashes.v1, sha1_hash("abcdefghijklmnopqrst"));
	TEST_EQUAL(atp.have_pieces.size(), 6);
	TEST_EQUAL(atp.have_pieces.count(), 6);

	TEST_EQUAL(atp.total_uploaded, 1337);
	TEST_EQUAL(atp.total_downloaded, 1338);
	TEST_EQUAL(atp.active_time, 1339);
	TEST_EQUAL(atp.seeding_time, 1340);
	TEST_EQUAL(atp.upload_limit, 1343);
	TEST_EQUAL(atp.download_limit, 1344);
	TEST_EQUAL(atp.max_connections, 1345);
	TEST_EQUAL(atp.max_uploads, 1346);

	torrent_flags_t const flags_mask
		= torrent_flags::seed_mode
		| torrent_flags::super_seeding
		| torrent_flags::auto_managed
		| torrent_flags::paused
		| torrent_flags::sequential_download;

	TEST_CHECK(!(atp.flags & flags_mask));
	TEST_EQUAL(atp.added_time, 1347);
	TEST_EQUAL(atp.completed_time, 1348);
	TEST_EQUAL(atp.finished_time, 1352);
	TEST_EQUAL(atp.last_seen_complete, 1353);

	TEST_EQUAL(atp.piece_priorities.size(), 6);
	TEST_EQUAL(atp.piece_priorities[0], 1_pri);
	TEST_EQUAL(atp.piece_priorities[1], 2_pri);
	TEST_EQUAL(atp.piece_priorities[2], 3_pri);
	TEST_EQUAL(atp.piece_priorities[3], 4_pri);
	TEST_EQUAL(atp.piece_priorities[4], 5_pri);
	TEST_EQUAL(atp.piece_priorities[5], 6_pri);
}

TORRENT_TEST(read_resume_missing_info_hash)
{
	entry rd;

	rd["file-format"] = "libtorrent resume file";
	rd["file-version"] = 1;
	// missing info-hash

	std::vector<char> resume_data;
	bencode(std::back_inserter(resume_data), rd);

	error_code ec;
	add_torrent_params atp = read_resume_data(resume_data, ec);
	TEST_EQUAL(ec, error_code(errors::missing_info_hash));
}

TORRENT_TEST(read_resume_info_hash2)
{
	entry rd;

	rd["file-format"] = "libtorrent resume file";
	rd["file-version"] = 1;
	// it's OK to *only* have a v2 hash
	rd["info-hash2"] = "01234567890123456789012345678901";

	std::vector<char> resume_data;
	bencode(std::back_inserter(resume_data), rd);

	error_code ec;
	add_torrent_params atp = read_resume_data(resume_data, ec);
	TEST_EQUAL(ec, error_code());
}

TORRENT_TEST(read_resume_missing_file_format)
{
	entry rd;

	// missing file-format
	rd["file-version"] = 1;
	rd["info-hash"] = "abcdefghijklmnopqrst";

	std::vector<char> resume_data;
	bencode(std::back_inserter(resume_data), rd);

	error_code ec;
	add_torrent_params atp = read_resume_data(resume_data, ec);
	TEST_EQUAL(ec, error_code(errors::invalid_file_tag));
}

TORRENT_TEST(read_resume_mismatching_torrent)
{
	entry rd;

	rd["file-format"] = "libtorrent resume file";
	rd["file-version"] = 1;
	rd["info-hash"] = "abcdefghijklmnopqrst";
	entry& info = rd["info"];
	info["piece length"] = 16384 * 16;
	info["name"] = "test";


	std::vector<char> resume_data;
	bencode(std::back_inserter(resume_data), rd);

	// the info-hash field does not match the torrent in the "info" field, so it
	// will be ignored
	add_torrent_params atp = read_resume_data(resume_data);
	TEST_CHECK(!atp.ti);
}

namespace {
std::shared_ptr<torrent_info> generate_torrent()
{
	std::vector<lt::create_file_entry> fs;
	fs.emplace_back("test_resume/tmp1", 128 * 1024 * 8);
	fs.emplace_back("test_resume/tmp2", 128 * 1024);
	fs.emplace_back("test_resume/tmp3", 128 * 1024);
	lt::create_torrent t(std::move(fs), 128 * 1024);

	t.add_tracker("http://torrent_file_tracker.com/announce");
	t.add_url_seed("http://torrent_file_url_seed.com/");

	int num = t.num_pieces();
	TEST_CHECK(num > 0);
	for (auto const i : t.piece_range())
	{
		sha1_hash ph;
		aux::random_bytes(ph);
		t.set_hash(i, ph);
	}

	std::vector<char> buf;
	bencode(std::back_inserter(buf), t.generate());
	return std::make_shared<torrent_info>(buf, from_span);
}
} // anonymous namespace

TORRENT_TEST(read_resume_torrent)
{
	std::shared_ptr<torrent_info> ti = generate_torrent();

	entry rd;
	rd["file-format"] = "libtorrent resume file";
	rd["file-version"] = 1;
	rd["info-hash"] = ti->info_hashes().v1.to_string();
	rd["info"] = bdecode(ti->info_section());

	std::vector<char> resume_data;
	bencode(std::back_inserter(resume_data), rd);

	// the info-hash field does not match the torrent in the "info" field, so it
	// will be ignored
	add_torrent_params atp = read_resume_data(resume_data);
	TEST_CHECK(atp.ti);

	TEST_EQUAL(atp.ti->info_hashes(), ti->info_hashes());
	TEST_EQUAL(atp.ti->name(), ti->name());
}

namespace {

void test_roundtrip(add_torrent_params const& input)
{
	auto b = write_resume_data_buf(input);
	error_code ec;
	auto output = read_resume_data(b, ec);
	TEST_CHECK(write_resume_data_buf(output) == b);
}

template <typename T>
lt::typed_bitfield<T> bits()
{
	lt::typed_bitfield<T> b;
	b.resize(19);
	b.set_bit(T(2));
	b.set_bit(T(6));
	b.set_bit(T(12));
	return b;
}

lt::bitfield bits()
{
	lt::bitfield b;
	b.resize(19);
	b.set_bit(2);
	b.set_bit(6);
	b.set_bit(12);
	return b;
}

template <typename T>
std::vector<T> vec()
{
	std::vector<T> ret;
	ret.resize(10);
	ret[0] = T(1);
	ret[1] = T(2);
	ret[5] = T(3);
	ret[7] = T(4);
	return ret;
}
}

TORRENT_TEST(round_trip_have_pieces)
{
	add_torrent_params atp;
	atp.have_pieces = bits<piece_index_t>();
	test_roundtrip(atp);
}

TORRENT_TEST(round_trip_last_seen_complete)
{
	add_torrent_params atp;
	atp.last_seen_complete = 42;
	test_roundtrip(atp);
}

TORRENT_TEST(round_trip_verified_pieces)
{
	add_torrent_params atp;
	atp.verified_pieces = bits<piece_index_t>();
	test_roundtrip(atp);
}

TORRENT_TEST(round_trip_prios)
{
	add_torrent_params atp;
	atp.piece_priorities = vec<download_priority_t>();
	test_roundtrip(atp);
}

TORRENT_TEST(round_trip_unfinished)
{
	add_torrent_params atp;
	atp.unfinished_pieces = std::map<piece_index_t, bitfield>{{42_piece, bits()}};
	test_roundtrip(atp);
}

TORRENT_TEST(round_trip_trackers)
{
	add_torrent_params atp;
	atp.flags |= torrent_flags::override_trackers;
	test_roundtrip(atp);
}

TORRENT_TEST(round_trip_name)
{
	add_torrent_params atp;
	atp.name = "foobar";
	test_roundtrip(atp);
}

TORRENT_TEST(round_trip_flags)
{
	torrent_flags_t const flags[] = {
		torrent_flags::seed_mode,
		torrent_flags::upload_mode,
		torrent_flags::share_mode,
		torrent_flags::apply_ip_filter,
		torrent_flags::paused,
		torrent_flags::auto_managed,
		torrent_flags::duplicate_is_error,
		torrent_flags::update_subscribe,
		torrent_flags::super_seeding,
		torrent_flags::sequential_download,
		torrent_flags::stop_when_ready,
		torrent_flags::override_trackers,
		torrent_flags::override_web_seeds,
		torrent_flags::need_save_resume,
		torrent_flags::disable_dht,
		torrent_flags::disable_lsd,
		torrent_flags::disable_pex,
	};

	for (auto const& f : flags)
	{
		add_torrent_params atp;
		atp.flags = f;
		test_roundtrip(atp);
	}
}

TORRENT_TEST(round_trip_info_hash)
{
	add_torrent_params atp;
	atp.info_hashes.v2 = sha256_hash{"21212121212121212121212121212121"};
	test_roundtrip(atp);
	entry e = write_resume_data(atp);
	TEST_CHECK(e["info-hash2"] == "21212121212121212121212121212121");
}

TORRENT_TEST(round_trip_merkle_trees)
{
	add_torrent_params atp;
	atp.merkle_trees = aux::vector<std::vector<sha256_hash>, file_index_t>{
		{sha256_hash{"01010101010101010101010101010101"}, sha256_hash{"21212121212121212121212121212121"}}
		, {sha256_hash{"23232323232323232323232323232323"}, sha256_hash{"43434343434343434343434343434343"}}
		};
	test_roundtrip(atp);
}

TORRENT_TEST(round_trip_merkle_tree_mask)
{
	add_torrent_params atp;
	atp.merkle_trees = aux::vector<std::vector<sha256_hash>, file_index_t>{
		{sha256_hash{"01010101010101010101010101010101"}, sha256_hash{"21212121212121212121212121212121"}}
		, {sha256_hash{"23232323232323232323232323232323"}, sha256_hash{"43434343434343434343434343434343"}}
		};
	atp.merkle_tree_mask = aux::vector<std::vector<bool>, file_index_t>{{false, false, false, true, true, true, true}};
	test_roundtrip(atp);
}

TORRENT_TEST(round_trip_verified_leaf_hashes)
{
	add_torrent_params atp;
	atp.verified_leaf_hashes = aux::vector<std::vector<bool>, file_index_t>{
		{true, true, false, false}, {false, true, false, true}};
	test_roundtrip(atp);
}
