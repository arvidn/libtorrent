/*

Copyright (c) 2016, Arvid Norberg
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

#include "test.hpp"
#include "test_utils.hpp"

#include <vector>

#include "libtorrent/entry.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/random.hpp"
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

	rd["piece_priority"] = "\x01\x02\x03\x04\x05\x06";
	rd["auto_managed"] = 0;
	rd["sequential_download"] = 0;
	rd["paused"] = 0;

	std::vector<char> resume_data;
	bencode(std::back_inserter(resume_data), rd);

	add_torrent_params atp = read_resume_data(resume_data);

	TEST_EQUAL(atp.info_hash, sha1_hash("abcdefghijklmnopqrst"));
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
	file_storage fs;
	fs.add_file("test_resume/tmp1", 128 * 1024 * 8);
	fs.add_file("test_resume/tmp2", 128 * 1024);
	fs.add_file("test_resume/tmp3", 128 * 1024);
	lt::create_torrent t(fs, 128 * 1024, 6);

	t.add_tracker("http://torrent_file_tracker.com/announce");
	t.add_url_seed("http://torrent_file_url_seed.com/");

	int num = t.num_pieces();
	TEST_CHECK(num > 0);
	for (auto const i : fs.piece_range())
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
	rd["info-hash"] = ti->info_hash().to_string();
	rd["info"] = bdecode({ti->metadata().get(), ti->metadata_size()});

	std::vector<char> resume_data;
	bencode(std::back_inserter(resume_data), rd);

	// the info-hash field does not match the torrent in the "info" field, so it
	// will be ignored
	add_torrent_params atp = read_resume_data(resume_data);
	TEST_CHECK(atp.ti);

	TEST_EQUAL(atp.ti->info_hash(), ti->info_hash());
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
	atp.unfinished_pieces = std::map<piece_index_t, bitfield>{{piece_index_t{42}, bits()}};
	test_roundtrip(atp);
}

TORRENT_TEST(round_trip_trackers)
{
	add_torrent_params atp;
	atp.flags |= torrent_flags::override_trackers;
	test_roundtrip(atp);
}

