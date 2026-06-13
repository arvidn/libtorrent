/*

Copyright (c) 2013-2017, 2020-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "libtorrent/session_params.hpp" // for disk_io_constructor_type
#include "libtorrent/flags.hpp"

#include <cstdint>

using web_seed_flag_t = lt::flags::bitfield_flag<std::uint32_t, struct web_seed_flag_tag>;

namespace web_seed {
	using lt::operator""_bit;
	constexpr web_seed_flag_t chunked_encoding = 0_bit;
	constexpr web_seed_flag_t test_ban = 1_bit;
	// keepalive defaults on; this flag disables it.
	constexpr web_seed_flag_t no_keepalive = 2_bit;
	constexpr web_seed_flag_t test_rename = 3_bit;
	// proxy_peers defaults on; this flag disables it.
	constexpr web_seed_flag_t no_proxy_peers = 4_bit;
	constexpr web_seed_flag_t expect_host_header = 5_bit;
}

int EXPORT run_http_suite(int proxy,
	char const* protocol,
	lt::disk_io_constructor_type disk_io,
	web_seed_flag_t flags = {});

void EXPORT test_transfer(lt::session& ses,
	lt::add_torrent_params atp,
	int proxy = 0,
	char const* protocol = "http",
	web_seed_flag_t flags = {});
