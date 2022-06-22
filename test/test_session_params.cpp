/*

Copyright (c) 2016, Alden Torres
Copyright (c) 2016-2017, 2019-2020, Arvid Norberg
Copyright (c) 2017, Steven Siloti
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

#include "libtorrent/config.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/extensions.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/bdecode.hpp"
#include "libtorrent/hex.hpp"
#include "setup_transfer.hpp" // for addr6
#include "settings.hpp"
#include "libtorrent/session_params.hpp"
#include "libtorrent/extensions.hpp"

#include "test.hpp"
#include "setup_transfer.hpp"

#include <fstream>

using namespace lt;
using dht::dht_storage_interface;
using dht::dht_state;

namespace
{
#ifndef TORRENT_DISABLE_DHT
	bool g_storage_constructor_invoked = false;

	std::unique_ptr<dht_storage_interface> dht_custom_storage_constructor(
		settings_interface const& settings)
	{
		g_storage_constructor_invoked = true;
		return dht::dht_default_storage_constructor(settings);
	}
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
	bool g_plugin_added_invoked = false;

	struct custom_plugin : plugin
	{
		void added(session_handle const& h) override
		{
			TORRENT_UNUSED(h);
			g_plugin_added_invoked = true;
		}
	};
#endif
}

TORRENT_TEST(default_plugins)
{
	session_params p1;
#ifndef TORRENT_DISABLE_EXTENSIONS
	TEST_EQUAL(int(p1.extensions.size()), 3);
#else
	TEST_EQUAL(int(p1.extensions.size()), 0);
#endif

	std::vector<std::shared_ptr<plugin>> exts;
	session_params p2(settings_pack(), exts);
	TEST_EQUAL(int(p2.extensions.size()), 0);
}

#ifndef TORRENT_DISABLE_DHT
TORRENT_TEST(custom_dht_storage)
{
	g_storage_constructor_invoked = false;
	settings_pack p = settings();
	p.set_bool(settings_pack::enable_dht, true);
	session_params params(p);
	params.dht_storage_constructor = dht_custom_storage_constructor;
	lt::session ses(params);

	TEST_CHECK(ses.is_dht_running() == true);
	TEST_EQUAL(g_storage_constructor_invoked, true);
}

TORRENT_TEST(dht_state)
{
	settings_pack p = settings();
	p.set_bool(settings_pack::enable_dht, true);
	p.set_int(settings_pack::dht_max_dht_items, 10000);
	p.set_int(settings_pack::dht_max_peers, 20000);

	dht_state s;
	s.nids.emplace_back(addr4("0.0.0.0"), to_hash("0000000000000000000000000000000000000001"));
	s.nodes.push_back(uep("1.1.1.1", 1));
	s.nodes.push_back(uep("2.2.2.2", 2));
	// not important that IPv6 is disabled here
	s.nids.emplace_back(addr6("::"), to_hash("0000000000000000000000000000000000000002"));

	session_params params(p);
	params.dht_state = s;

	params.settings.set_str(settings_pack::listen_interfaces, "127.0.0.1:6881");

	lt::session ses1(params);
	TEST_CHECK(ses1.is_dht_running() == true);
	session_params const params1 = ses1.session_state();
	TEST_EQUAL(params1.settings.get_int(settings_pack::dht_max_dht_items), 10000);
	TEST_EQUAL(params1.settings.get_int(settings_pack::dht_max_peers), 20000);
	entry const e = write_session_params(params1);

	std::vector<char> tmp;
	bencode(std::back_inserter(tmp), e);

	session_params params2 = read_session_params(tmp);
	TEST_EQUAL(params2.settings.get_int(settings_pack::dht_max_dht_items), 10000);
	TEST_EQUAL(params2.settings.get_int(settings_pack::dht_max_peers), 20000);

	TEST_EQUAL(params2.dht_state.nids.size(), 1);

	if (params2.dht_state.nids.size() >= 1)
	{
		// not a chance the nid will be the fake initial ones
		TEST_CHECK(params2.dht_state.nids[0].second != s.nids[0].second);
	}
}
#endif

namespace {

settings_pack test_pack()
{
	settings_pack ret;
	for (std::uint16_t i = 0; i < settings_pack::num_string_settings; ++i)
	{
		std::uint16_t const n = i | settings_pack::string_type_base;
		if (name_for_setting(n) == std::string()) continue;
		ret.set_str(n , std::to_string(i) + "__");
	}
	for (std::uint16_t i = 0; i < settings_pack::num_int_settings; ++i)
	{
		std::uint16_t const n = i | settings_pack::int_type_base;
		if (name_for_setting(n) == std::string()) continue;
		ret.set_int(n, 1000000 + i);
	}
	for (std::uint16_t i = 0; i < settings_pack::num_bool_settings; ++i)
	{
		std::uint16_t const n = i | settings_pack::bool_type_base;
		if (name_for_setting(n) == std::string()) continue;
		ret.set_bool(n, i & 1);
	}
	return ret;
}

dht::dht_state test_state()
{
	dht::dht_state ret;
	auto a1 = make_address("1.2.3.4");
	auto a2 = make_address("1234:abcd:ef01::1");
	ret.nids = dht::node_ids_t{{a1, dht::generate_id(a1)}, {a2, dht::generate_id(a2)}};
	for (int i = 0; i < 50; ++i)
		ret.nodes.push_back(rand_udp_ep(rand_v4));
	for (int i = 0; i < 50; ++i)
		ret.nodes.push_back(rand_udp_ep(rand_v6));
	return ret;
}

ip_filter test_ip_filter()
{
	ip_filter ret;
	ret.add_rule(make_address("fe80::"), make_address("fe81::"), 1);
	ret.add_rule(make_address("127.0.0.1"), make_address("127.255.255.255"), 1);
	return ret;
}

session_params test_params()
{
	session_params ret;
	ret.settings = test_pack();
	ret.dht_state = test_state();
	for (int i = 0; i < 100; ++i)
		ret.ext_state[std::to_string(i)] = std::string(std::to_string(i));
	ret.ip_filter = test_ip_filter();
	return ret;
}

bool operator==(dht::dht_state const& lhs, dht::dht_state const& rhs)
{
	return lhs.nids == rhs.nids
		&& lhs.nodes == rhs.nodes
		&& lhs.nodes6 == rhs.nodes6
		;
}

bool operator==(lt::settings_pack const& lhs, lt::settings_pack const& rhs)
{
	for (std::uint16_t i = 0; i < settings_pack::num_string_settings; ++i)
		if (lhs.get_str(i | settings_pack::string_type_base) != rhs.get_str(i | settings_pack::string_type_base)) return false;
	for (std::uint16_t i = 0; i < settings_pack::num_int_settings; ++i)
		if (lhs.get_int(i | settings_pack::int_type_base) != rhs.get_int(i | settings_pack::int_type_base)) return false;
	for (std::uint16_t i = 0; i < settings_pack::num_bool_settings; ++i)
		if (lhs.get_bool(i | settings_pack::bool_type_base) != rhs.get_bool(i | settings_pack::bool_type_base)) return false;
	return true;
}

void test_ip_filter(ip_filter const& f)
{
	TEST_EQUAL(f.access(make_address("fe7f::1")), 0);
	TEST_EQUAL(f.access(make_address("fe80::1")), 1);
	TEST_EQUAL(f.access(make_address("fe81::1")), 0);
	TEST_EQUAL(f.access(make_address("127.0.0.0")), 0);
	TEST_EQUAL(f.access(make_address("127.0.0.1")), 1);
	TEST_EQUAL(f.access(make_address("127.255.0.1")), 1);
	TEST_EQUAL(f.access(make_address("128.0.0.0")), 0);
}

} // anonymous namespace

TORRENT_TEST(session_params_ip_filter)
{
	session_params input;
	input.ip_filter = test_ip_filter();

	test_ip_filter(input.ip_filter);

	std::vector<char> const buf = write_session_params_buf(input);
	{
		std::ofstream f("../session_state.test");
		f.write(buf.data(), static_cast<std::streamsize>(buf.size()));
	}
	session_params const output = read_session_params(buf);

	test_ip_filter(output.ip_filter);
}

TORRENT_TEST(session_params_round_trip)
{
	session_params const input = test_params();

	std::vector<char> const buf = write_session_params_buf(input);
	{
		std::ofstream f("../session_state.test");
		f.write(buf.data(), static_cast<std::streamsize>(buf.size()));
	}
	session_params const output = read_session_params(buf);

	TEST_CHECK(input.settings == output.settings);
	TEST_CHECK(input.dht_state == output.dht_state);
	TEST_CHECK(input.ext_state == output.ext_state);
	TEST_CHECK(input.ip_filter.export_filter() == output.ip_filter.export_filter());
}

#ifndef TORRENT_DISABLE_EXTENSIONS
TORRENT_TEST(add_plugin)
{
	g_plugin_added_invoked = false;
	session_params params(settings());
	params.extensions.push_back(std::make_shared<custom_plugin>());
	lt::session ses(params);

	TEST_EQUAL(g_plugin_added_invoked, true);
}
#endif
