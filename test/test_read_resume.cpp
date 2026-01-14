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
#include <iostream>

#include "libtorrent/entry.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/aux_/random.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/read_resume_data.hpp"
#include "libtorrent/write_resume_data.hpp"
#include "libtorrent/load_torrent.hpp"

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
	rd["i2p"] = 0;
	rd["super_seeding"] = 0;
	rd["added_time"] = 1347;
	rd["completed_time"] = 1348;
	rd["finished_time"] = 1352;
	rd["last_seen_complete"] = 1353;

	rd["piece_priority"] = "\x01\x02\x03\x04\x05\x06";
	rd["auto_managed"] = 0;
	rd["sequential_download"] = 0;
	rd["paused"] = 0;

	add_torrent_params atp = read_resume_data(bencode(rd));

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
		| torrent_flags::i2p_torrent
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

	error_code ec;
	add_torrent_params atp = read_resume_data(bencode(rd), ec);
	TEST_EQUAL(ec, error_code(errors::missing_info_hash));
}

TORRENT_TEST(read_resume_info_hash2)
{
	entry rd;

	rd["file-format"] = "libtorrent resume file";
	rd["file-version"] = 1;
	// it's OK to *only* have a v2 hash
	rd["info-hash2"] = "01234567890123456789012345678901";

	error_code ec;
	add_torrent_params atp = read_resume_data(bencode(rd), ec);
	TEST_EQUAL(ec, error_code());
}

TORRENT_TEST(read_resume_missing_file_format)
{
	entry rd;

	// missing file-format
	rd["file-version"] = 1;
	rd["info-hash"] = "abcdefghijklmnopqrst";

	error_code ec;
	add_torrent_params atp = read_resume_data(bencode(rd), ec);
	TEST_EQUAL(ec, error_code(errors::invalid_file_tag));
}

namespace {
add_torrent_params generate_torrent()
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

	return load_torrent_buffer(bencode(t.generate()));
}
} // anonymous namespace

TORRENT_TEST(read_resume_torrent)
{
	add_torrent_params p = generate_torrent();

	entry rd;
	rd["file-format"] = "libtorrent resume file";
	rd["file-version"] = 1;
	rd["info-hash"] = p.ti->info_hashes().v1.to_string();
	rd["info"] = bdecode(p.ti->info_section());

	// the info-hash field does not match the torrent in the "info" field, so it
	// will be ignored
	add_torrent_params atp = read_resume_data(bencode(rd));
	TEST_CHECK(atp.ti);

	TEST_EQUAL(atp.ti->info_hashes(), p.ti->info_hashes());
	TEST_EQUAL(atp.ti->name(), p.ti->name());
}

TORRENT_TEST(mismatching_v1_hash)
{
	add_torrent_params p = generate_torrent();

	entry rd;
	rd["file-format"] = "libtorrent resume file";
	rd["file-version"] = 1;
	rd["info-hash"] = "abababababababababab";
	rd["info-hash2"] = p.ti->info_hashes().v2;
	rd["info"] = bdecode(p.ti->info_section());

	std::vector<char> resume_data;
	bencode(std::back_inserter(resume_data), rd);

	// the info-hash field does not match the torrent in the "info" field, so it
	// will be ignored
	error_code ec;
	add_torrent_params atp = read_resume_data(resume_data, ec);
	TEST_CHECK(ec == errors::mismatching_info_hash);
}

TORRENT_TEST(mismatching_v2_hash)
{
	add_torrent_params p = generate_torrent();

	entry rd;
	rd["file-format"] = "libtorrent resume file";
	rd["file-version"] = 1;
	rd["info-hash"] = p.ti->info_hashes().v1;
	rd["info-hash2"] = "abababababababababababababababab";
	rd["info"] = bdecode(p.ti->info_section());

	std::vector<char> resume_data;
	bencode(std::back_inserter(resume_data), rd);

	// the info-hash field does not match the torrent in the "info" field, so it
	// will be ignored
	error_code ec;
	add_torrent_params atp = read_resume_data(resume_data, ec);
	TEST_CHECK(ec == errors::mismatching_info_hash);
}

namespace {

void test_roundtrip(add_torrent_params input)
{
	// in order to accept that certain bitfields round up to even 8 (bytes)
	// we round up the input bitfields
	input.have_pieces.resize(input.have_pieces.num_bytes() * 8);
	input.verified_pieces.resize(input.verified_pieces.num_bytes() * 8);
	for (auto& [p, b]: input.unfinished_pieces)
		b.resize(b.num_bytes() * 8);
	for (auto& b: input.merkle_tree_mask)
		b.resize(b.num_bytes() * 8);
	for (auto& b: input.verified_leaf_hashes)
		b.resize(b.num_bytes() * 8);

	auto b = write_resume_data_buf(input);
	error_code ec;
	auto const output = read_resume_data(b, ec);

	TEST_CHECK(input.verified_leaf_hashes == output.verified_leaf_hashes);
	TEST_CHECK(input.merkle_tree_mask == output.merkle_tree_mask);
	TEST_CHECK(input.file_priorities == output.file_priorities);
	TEST_CHECK(input.save_path == output.save_path);
	TEST_CHECK(input.part_file_dir == output.part_file_dir);
	TEST_CHECK(input.name == output.name);
	TEST_CHECK(input.trackers == output.trackers);
	TEST_CHECK(input.tracker_tiers == output.tracker_tiers);
	TEST_CHECK(input.info_hashes == output.info_hashes);
	TEST_CHECK(input.url_seeds == output.url_seeds);
	TEST_CHECK(input.unfinished_pieces == output.unfinished_pieces);
	TEST_CHECK(input.verified_pieces == output.verified_pieces);
	TEST_CHECK(input.piece_priorities == output.piece_priorities);
	TEST_CHECK(input.merkle_trees == output.merkle_trees);
	TEST_CHECK(input.renamed_files == output.renamed_files);
	TEST_CHECK(input.comment == output.comment);
	TEST_CHECK(input.created_by == output.created_by);

	auto const compare = write_resume_data_buf(output);
	TEST_CHECK(compare == b);
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
	b.resize(190);
	b.set_bit(2);
	b.set_bit(6);
	b.set_bit(12);
	b.set_bit(100);
	b.set_bit(103);
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

TORRENT_TEST(round_trip_save_path)
{
	add_torrent_params atp;
	atp.save_path = "abc";
	test_roundtrip(atp);
}

TORRENT_TEST(round_trip_part_file_dir)
{
	add_torrent_params atp;
	atp.part_file_dir = "def";
	test_roundtrip(atp);
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
	atp.flags |= torrent_flags::deprecated_override_trackers;
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
#if TORRENT_ABI_VERSION < 4
		torrent_flags::override_trackers,
		torrent_flags::override_web_seeds,
#endif
		torrent_flags::need_save_resume,
		torrent_flags::disable_dht,
		torrent_flags::disable_lsd,
		torrent_flags::disable_pex,
#if TORRENT_USE_I2P
		torrent_flags::i2p_torrent,
#endif
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

namespace {

bitfield make_bitfield(std::initializer_list<bool> init)
{
	bitfield ret(int(init.size()));
	int idx = 0;
	for (auto v : init)
	{
		if (v) ret.set_bit(idx);
		++idx;
	}
	return ret;
}

}

TORRENT_TEST(round_trip_merkle_tree_mask)
{
	add_torrent_params atp;
	atp.merkle_trees = aux::vector<std::vector<sha256_hash>, file_index_t>{
		{sha256_hash{"01010101010101010101010101010101"}, sha256_hash{"21212121212121212121212121212121"}}
		, {sha256_hash{"23232323232323232323232323232323"}, sha256_hash{"43434343434343434343434343434343"}}
		};
	atp.merkle_tree_mask = aux::vector<bitfield, file_index_t>{
		make_bitfield({false, false, false, true, true, true, true})};
	test_roundtrip(atp);
}

TORRENT_TEST(round_trip_verified_leaf_hashes)
{
	add_torrent_params atp;
	atp.merkle_trees = aux::vector<std::vector<sha256_hash>, file_index_t>{
		{sha256_hash{"01010101010101010101010101010101"}},
		{sha256_hash{"12121212121212121212121212121212"}}};
	atp.verified_leaf_hashes = aux::vector<bitfield, file_index_t>{
		make_bitfield({true, true, false, false})
		, make_bitfield({false, true, false, true})};
	test_roundtrip(atp);
}

TORRENT_TEST(invalid_resume_version)
{
	entry ret;
	ret["file-format"] = "libtorrent resume file";
	ret["file-version"] = 0;
	ret["info-hash"] = "                    ";
	TEST_THROW(read_resume_data(bencode(ret)));

	ret["file-version"] = 3;
	TEST_THROW(read_resume_data(bencode(ret)));

	ret["file-version"] = 42;
	TEST_THROW(read_resume_data(bencode(ret)));
}

TORRENT_TEST(deprecated_pieces_field)
{
	entry ret;
	ret["file-format"] = "libtorrent resume file";
	ret["file-version"] = 1;
	ret["info-hash"] = "                    ";
	ret["pieces"] = std::string("\x02\x02\x00\x00\x00\x03\x02\x01\x03\x01", 10);
	add_torrent_params const atp = read_resume_data(bencode(ret));

	TEST_EQUAL(atp.have_pieces.get_bit(0_piece), false);
	TEST_EQUAL(atp.have_pieces.get_bit(1_piece), false);
	TEST_EQUAL(atp.have_pieces.get_bit(2_piece), false);
	TEST_EQUAL(atp.have_pieces.get_bit(3_piece), false);
	TEST_EQUAL(atp.have_pieces.get_bit(4_piece), false);
	TEST_EQUAL(atp.have_pieces.get_bit(5_piece), true);
	TEST_EQUAL(atp.have_pieces.get_bit(6_piece), false);
	TEST_EQUAL(atp.have_pieces.get_bit(7_piece), true);
	TEST_EQUAL(atp.have_pieces.get_bit(8_piece), true);
	TEST_EQUAL(atp.have_pieces.get_bit(9_piece), true);

	TEST_EQUAL(atp.verified_pieces.get_bit(0_piece), true);
	TEST_EQUAL(atp.verified_pieces.get_bit(1_piece), true);
	TEST_EQUAL(atp.verified_pieces.get_bit(2_piece), false);
	TEST_EQUAL(atp.verified_pieces.get_bit(3_piece), false);
	TEST_EQUAL(atp.verified_pieces.get_bit(4_piece), false);
	TEST_EQUAL(atp.verified_pieces.get_bit(5_piece), true);
	TEST_EQUAL(atp.verified_pieces.get_bit(6_piece), true);
	TEST_EQUAL(atp.verified_pieces.get_bit(7_piece), false);
	TEST_EQUAL(atp.verified_pieces.get_bit(8_piece), true);
	TEST_EQUAL(atp.verified_pieces.get_bit(9_piece), false);
}

TORRENT_TEST(deprecated_trees_fields)
{
	entry ret;
	ret["file-format"] = "libtorrent resume file";
	ret["file-version"] = 1;
	ret["info-hash"] = "                    ";
	auto& tree = ret["trees"].list();

	tree.emplace_back(entry::dictionary_t);
	auto& file = tree.back().dict();

	file["hashes"] = std::string();
	file["mask"] = "0001101010111";
	file["verified"] = "1110010101111";

	add_torrent_params const atp = read_resume_data(bencode(ret));

	TEST_CHECK(atp.merkle_tree_mask.at(0) == make_bitfield(
		{false, false, false, true, true, false, true, false, true, false, true, true, true}));

	TEST_CHECK(atp.verified_leaf_hashes.at(0) == make_bitfield(
		{true, true, true, false, false, true, false, true, false, true, true, true, true}));
}
