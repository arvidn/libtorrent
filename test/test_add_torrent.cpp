/*

Copyright (c) 2021, Arvid Norberg
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
#include "setup_transfer.hpp" // for load_file

#include "libtorrent/flags.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/hasher.hpp"

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
// these set the info-hash to the specified hash of the info-section, regardless
// of whether the torrent has an info-hash of that kind
add_torrent_test_flag_t const set_sha1_info_hash = 9_bit;
add_torrent_test_flag_t const set_sha256_info_hash = 10_bit;
add_torrent_test_flag_t const set_truncated_sha256_info_hash = 11_bit;
add_torrent_test_flag_t const trailing_garbage = 12_bit;

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

	auto const is = atp.ti->info_section();
	std::vector<char> info_section(is.begin(), is.end());

	if (flags & trailing_garbage) info_section.push_back('e');
	if (flags & set_sha1_info_hash)
		atp.info_hashes.v1 = lt::hasher(info_section).final();
	if (flags & set_sha256_info_hash)
		atp.info_hashes.v2 = lt::hasher256(info_section).final();
	if (flags & set_truncated_sha256_info_hash)
		atp.info_hashes.v1 = lt::sha1_hash(lt::hasher256(info_section).final().data());

	if (flags & magnet_link) atp.ti.reset();

	lt::session_params p;
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

	// success must be signalled by an alert, not by the absence of an error
	return boost::asio::error::timed_out;
}

struct test_case_t
{
	char const* filename;
	add_torrent_test_flag_t flags;
	lt::error_code expected_error;
};

auto const v2 = "v2.torrent";
auto const v2_only = "v2_only.torrent";
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

	// the info-hashes in add_torrent_params are checked against the
	// torrent_info the same way as against metadata received for a magnet link
	{v1, set_sha256_info_hash, lt::errors::mismatching_info_hash},
	{v1, set_truncated_sha256_info_hash, lt::errors::mismatching_info_hash},
	{v2_only, set_sha1_info_hash, lt::errors::mismatching_info_hash},
	{v2_only, set_sha256_info_hash, {}},
	{v2_only, set_truncated_sha256_info_hash, {}},
	{v2_only, set_truncated_sha256_info_hash | set_sha256_info_hash, {}},
	{hybrid, set_truncated_sha256_info_hash, {}},
	{hybrid, set_truncated_sha256_info_hash | set_sha256_info_hash, {}},
	{hybrid, set_sha1_info_hash | set_sha256_info_hash, {}},
	{v2_only, trailing_garbage | set_sha256_info_hash, lt::errors::mismatching_info_hash},

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

	// the metadata must have the info-hash the magnet link was added with. A
	// hash of the right bytes, but of a kind the torrent doesn't have, is not
	// its info-hash
	{v1, magnet_link | set_sha1_info_hash, {}},
	{v1, magnet_link | set_sha256_info_hash, lt::errors::mismatching_info_hash},
	{v1, magnet_link | set_truncated_sha256_info_hash, lt::errors::mismatching_info_hash},
	{v2_only, magnet_link | set_sha1_info_hash, lt::errors::mismatching_info_hash},
	{v2_only, magnet_link | set_sha256_info_hash, {}},
	{v2_only, magnet_link | set_truncated_sha256_info_hash, {}},
	{hybrid, magnet_link | set_sha1_info_hash, {}},
	{hybrid, magnet_link | set_sha256_info_hash, {}},
	{hybrid, magnet_link | set_truncated_sha256_info_hash, {}},
	{v1, magnet_link | set_sha1_info_hash | set_sha256_info_hash, lt::errors::mismatching_info_hash},
	{v2_only, magnet_link | set_sha1_info_hash | set_sha256_info_hash, lt::errors::mismatching_info_hash},
	{hybrid, magnet_link | set_sha1_info_hash | set_sha256_info_hash, {}},
	// some clients put the truncated v2 hash in btih alongside btmh
	{v2_only, magnet_link | set_truncated_sha256_info_hash | set_sha256_info_hash, {}},
	{hybrid, magnet_link | set_truncated_sha256_info_hash | set_sha256_info_hash, {}},

	// the info-hash only covers the info-dictionary itself
	{v1, magnet_link | trailing_garbage | set_sha1_info_hash, lt::errors::mismatching_info_hash},
	{v2_only, magnet_link | trailing_garbage | set_sha256_info_hash, lt::errors::mismatching_info_hash},
	{v2_only, magnet_link | trailing_garbage | set_truncated_sha256_info_hash, lt::errors::mismatching_info_hash},
	{hybrid, magnet_link | trailing_garbage | set_sha1_info_hash | set_sha256_info_hash, lt::errors::mismatching_info_hash},
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
