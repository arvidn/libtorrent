/*

Copyright (c) 2015, Arvid Norberg
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

#include <array>
#include <iostream>
#include "test.hpp"
#include "create_torrent.hpp"
#include "settings.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/session_stats.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/aux_/proxy_settings.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/random.hpp"
#include "simulator/simulator.hpp"
#include "simulator/socks_server.hpp"
#include "simulator/utils.hpp"
#include "setup_swarm.hpp"
#include "utils.hpp"
#include "test_utils.hpp"
#include "setup_transfer.hpp" // for addr()
#include "disk_io.hpp"

using namespace sim;
using namespace lt;

std::string make_ep_string(char const* address, bool const is_v6
	, char const* port)
{
	std::string ret;
	if (is_v6) ret += '[';
	ret += address;
	if (is_v6) ret += ']';
	ret += ':';
	ret += port;
	return ret;
}

template <typename Setup, typename HandleAlerts, typename Test>
void run_test(
	Setup setup
	, HandleAlerts on_alert
	, Test test
	, test_transfer_flags_t flags = {}
	, test_disk const downloader_disk_constructor = test_disk()
	, test_disk const seed_disk_constructor = test_disk()
	, lt::seconds const timeout = lt::seconds(60)
	)
{
	const bool use_ipv6 = bool(flags & tx::ipv6);

	char const* peer0_ip[2] = { "50.0.0.1", "feed:face:baad:f00d::1" };
	char const* peer1_ip[2] = { "50.0.0.2", "feed:face:baad:f00d::2" };

	using asio::ip::address;
	address peer0 = addr(peer0_ip[use_ipv6]);
	address peer1 = addr(peer1_ip[use_ipv6]);
	address proxy = (flags & tx::ipv6) ? addr("2001::2") : addr("50.50.50.50");

	// setup the simulation
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	sim::asio::io_context ios0 { sim, peer0 };
	sim::asio::io_context ios1 { sim, peer1 };

	lt::session_proxy zombie[2];

	sim::asio::io_context proxy_ios{sim, proxy };
	sim::socks_server socks4(proxy_ios, 4444, 4);
	sim::socks_server socks5(proxy_ios, 5555, 5);
	socks5.bind_start_port(3000);

	lt::session_params params;
	// setup settings pack to use for the session (customization point)
	lt::settings_pack& pack = params.settings;
	pack = settings();
	pack.set_bool(settings_pack::disable_hash_checks, false);

	// disable utp by default
	pack.set_bool(settings_pack::enable_outgoing_utp, false);
	pack.set_bool(settings_pack::enable_incoming_utp, false);

	// disable encryption by default
	pack.set_bool(settings_pack::prefer_rc4, false);
	pack.set_int(settings_pack::in_enc_policy, settings_pack::pe_disabled);
	pack.set_int(settings_pack::out_enc_policy, settings_pack::pe_disabled);
	pack.set_int(settings_pack::allowed_enc_level, settings_pack::pe_plaintext);

	pack.set_str(settings_pack::listen_interfaces, make_ep_string(peer0_ip[use_ipv6], use_ipv6, "6881"));

	// create session
	std::shared_ptr<lt::session> ses[2];

	// session 0 is a downloader, session 1 is a seed

	params.disk_io_constructor = downloader_disk_constructor;
	ses[0] = std::make_shared<lt::session>(params, ios0);

	pack.set_str(settings_pack::listen_interfaces, make_ep_string(peer1_ip[use_ipv6], use_ipv6, "6881"));

	params.disk_io_constructor = seed_disk_constructor.set_files(existing_files_mode::full_valid);
	ses[1] = std::make_shared<lt::session>(params, ios1);

	setup(*ses[0], *ses[1]);

	// only monitor alerts for session 0 (the downloader)
	print_alerts(*ses[0], [=](lt::session& ses, lt::alert const* a) {
		if (auto ta = alert_cast<lt::add_torrent_alert>(a))
		{
			if (flags & tx::connect_proxy)
				ta->handle.connect_peer(lt::tcp::endpoint(proxy, 3000));
			else
				ta->handle.connect_peer(lt::tcp::endpoint(peer1, 6881));
		}
		on_alert(ses, a);
	}, 0);

	print_alerts(*ses[1], [](lt::session&, lt::alert const*){}, 1);

	lt::add_torrent_params atp = ::create_test_torrent(10
		, (flags & tx::v2_only) ? create_torrent::v2_only
		: (flags & tx::v1_only) ? create_torrent::v1_only
		: create_flags_t{}
		, (flags & tx::small_pieces) ? 1 : (flags & tx::large_pieces) ? 4 : 2
		, (flags & tx::multiple_files) ? 3 : 1
		);
	atp.flags &= ~lt::torrent_flags::auto_managed;
	atp.flags &= ~lt::torrent_flags::paused;

	ses[1]->async_add_torrent(atp);
	auto torrent = atp.ti;

	atp.save_path = save_path(0);
	if (flags & tx::magnet_download)
	{
		atp.info_hashes = atp.ti->info_hashes();
		atp.ti.reset();
	}
	ses[0]->async_add_torrent(atp);

	sim::timer t(sim, timeout, [&](boost::system::error_code const&)
	{
		auto h = ses[0]->get_torrents();
		auto ti = h[0].torrent_file_with_hashes();

		// if we're a seed, we should definitely have the torrent info. If we're
		// note a seed, we may still have the torrent_info in case it's a v1
		// torrent
		if (is_seed(*ses[0])) TEST_CHECK(ti);

		if (ti)
		{
			if (ti->v2())
				TEST_EQUAL(ti->v2_piece_hashes_verified(), true);

			auto downloaded = serialize(*ti);
			auto added = serialize(*torrent);
			TEST_CHECK(downloaded == added);
		}

		test(ses);

		// shut down
		int idx = 0;
		for (auto& s : ses)
		{
			zombie[idx++] = s->abort();
			s.reset();
		}
	});

	sim.run();
}

namespace {

void no_init(lt::session& ses0, lt::session& ses1) {}

struct record_finished_pieces
 {
	record_finished_pieces(std::set<lt::piece_index_t>& p)
		: m_passed(&p)
	{}

	void operator()(lt::session&, lt::alert const* a) const
	{
		if (auto const* pf = lt::alert_cast<lt::piece_finished_alert>(a))
			m_passed->insert(pf->piece_index);
	}

	std::set<lt::piece_index_t>* m_passed;
};


struct expect_seed
{
	expect_seed(bool e) : m_expect(e) {}
	void operator()(std::shared_ptr<lt::session> ses[2]) const
	{
		TEST_EQUAL(is_seed(*ses[0]), m_expect);
	}
	bool m_expect;
};

int blocks_per_piece(test_transfer_flags_t const flags)
{
	if (flags & tx::small_pieces) return 1;
	if (flags & tx::large_pieces) return 4;
	return 2;
}

int num_pieces(test_transfer_flags_t const flags)
{
	if (flags & tx::multiple_files)
	{
		// since v1 torrents don't pad files by default, there will be fewer
		// pieces on those torrents
		if (flags & tx::v1_only)
			return 31;
		else
			return 33;
	}
	return 11;
}

std::ostream& operator<<(std::ostream& os, existing_files_mode const mode)
{
	switch (mode)
	{
		case existing_files_mode::no_files: return os << "no_files";
		case existing_files_mode::full_invalid: return os << "full_invalid";
		case existing_files_mode::partial_valid: return os << "partial_valid";
		case existing_files_mode::full_valid: return os << "full_valid";
	}
	return os << "<unknown file mode>";
}

}

TORRENT_TEST(socks4_tcp)
{
	run_test(
		[](lt::session& ses0, lt::session& ses1)
		{
			set_proxy(ses0, settings_pack::socks4);
			filter_ips(ses1);
		},
		[](lt::session&, lt::alert const*) {},
		expect_seed(true)
	);
}

TORRENT_TEST(socks5_tcp_connect)
{
	run_test(
		[](lt::session& ses0, lt::session& ses1)
		{
			set_proxy(ses0, settings_pack::socks5);
			filter_ips(ses1);
		},
		[](lt::session&, lt::alert const*) {},
		expect_seed(true)
	);
}

TORRENT_TEST(encryption_tcp)
{
	run_test(
		[](lt::session& ses0, lt::session& ses1)
		{ enable_enc(ses0); enable_enc(ses1); },
		[](lt::session&, lt::alert const*) {},
		expect_seed(true)
	);
}

TORRENT_TEST(no_proxy_tcp_ipv6)
{
	run_test(
		no_init,
		[](lt::session&, lt::alert const*) {},
		expect_seed(true),
		tx::ipv6
	);
}

TORRENT_TEST(no_proxy_utp_ipv6)
{
	run_test(
		[](lt::session& ses0, lt::session& ses1)
		{ utp_only(ses0); utp_only(ses1); },
		[](lt::session&, lt::alert const*) {},
		expect_seed(true),
		tx::ipv6
	);
}

// TODO: the socks server does not support IPv6 addresses yet
/*
TORRENT_TEST(socks5_tcp_ipv6)
{
	run_test(
		[](lt::session& ses0, lt::session& ses1)
		{
			set_proxy(ses0, settings_pack::socks5);
			filter_ips(ses1);
		},
		[](lt::session&, lt::alert const*) {},
		[](std::shared_ptr<lt::session> ses[2]) {
			TEST_EQUAL(is_seed(*ses[0]), true);
		},
		tx::ipv6
	);
}
*/

TORRENT_TEST(no_proxy_tcp)
{
	run_test(
		no_init,
		[](lt::session&, lt::alert const*) {},
		expect_seed(true)
	);
}

TORRENT_TEST(no_proxy_utp)
{
	run_test(
		[](lt::session& ses0, lt::session& ses1)
		{ utp_only(ses0); utp_only(ses1); },
		[](lt::session&, lt::alert const*) {},
		expect_seed(true)
	);
}

TORRENT_TEST(encryption_utp)
{
	run_test(
		[](lt::session& ses0, lt::session& ses1)
		{
			enable_enc(ses0);
			enable_enc(ses1);
			utp_only(ses0);
			utp_only(ses1);
		},
		[](lt::session&, lt::alert const*) {},
		expect_seed(true)
	);
}

TORRENT_TEST(socks5_utp)
{
	run_test(
		[](lt::session& ses0, lt::session& ses1)
		{
			set_proxy(ses0, settings_pack::socks5);
			utp_only(ses0);
			filter_ips(ses1);
			utp_only(ses1);
		},
		[](lt::session&, lt::alert const*) {},
		expect_seed(true)
	);
}

TORRENT_TEST(socks5_utp_incoming)
{
	run_test(
		[](lt::session& ses0, lt::session& ses1)
		{
			set_proxy(ses1, settings_pack::socks5);
			utp_only(ses0);
			utp_only(ses1);
			filter_ips(ses0);
		},
		[](lt::session&, lt::alert const*) {},
		expect_seed(true),
		tx::connect_proxy
	);
}

TORRENT_TEST(socks5_utp_circumvent_proxy_reject)
{
	run_test(
		[](lt::session& ses0, lt::session& ses1)
		{
			set_proxy(ses1, settings_pack::socks5);
			utp_only(ses0);
			utp_only(ses1);
		},
		[](lt::session&, lt::alert const*) {},
		expect_seed(false)
	);
}

// if we're not proxying peer connections, it's OK to accept incoming
// connections
TORRENT_TEST(socks5_utp_circumvent_proxy_ok)
{
	run_test(
		[](lt::session& ses0, lt::session& ses1)
		{
			set_proxy(ses1, settings_pack::socks5, {}, false);
			utp_only(ses0);
			utp_only(ses1);
		},
		[](lt::session&, lt::alert const*) {},

		// the UDP socket socks5 proxy support doesn't allow accepting direct
		// connections, circumventing the proxy, so this transfer will fail,
		// even though it would be reasonable for it to pass as well
		expect_seed(false)
	);
}

TORRENT_TEST(http_tcp_circumvent_proxy_reject)
{
	run_test(
		[](lt::session& ses0, lt::session& ses1)
		{
			set_proxy(ses1, settings_pack::http);
		},
		[](lt::session&, lt::alert const*) {},
		expect_seed(false)
	);
}

// if we're not proxying peer connections, it's OK to accept incoming
// connections
TORRENT_TEST(http_tcp_circumvent_proxy_ok)
{
	run_test(
		[](lt::session& ses0, lt::session& ses1)
		{
			set_proxy(ses1, settings_pack::http, {}, false);
		},
		[](lt::session&, lt::alert const*) {},
		expect_seed(true)
	);
}

// the purpose of these tests is to make sure that the sessions can't actually
// talk directly to each other. i.e. they are negative tests. If they can talk
// directly to each other, all other tests in here may be broken.
TORRENT_TEST(no_proxy_tcp_banned)
{
	run_test(
		[](lt::session&, lt::session& ses1) { filter_ips(ses1); },
		[](lt::session&, lt::alert const*) {},
		expect_seed(false)
	);
}

TORRENT_TEST(no_proxy_utp_banned)
{
	run_test(
		[](lt::session& ses0, lt::session& ses1)
		{ utp_only(ses0); utp_only(ses1); filter_ips(ses1); },
		[](lt::session&, lt::alert const*) {},
		expect_seed(false)
	);
}

namespace {

void run_matrix_test(test_transfer_flags_t const flags, existing_files_mode const files, bool const corruption)
{
	std::cout << "\n\nTEST CASE: "
		<< ((flags & tx::small_pieces) ? "small-pieces" : (flags & tx::large_pieces) ? "large-pieces" : "normal-pieces")
		<< "-" << (corruption ? "corruption" : "valid")
		<< "-" << ((flags & tx::v2_only) ? "v2_only" : (flags & tx::v1_only) ? "v1_only" : "hybrid")
		<< "-" << ((flags & tx::magnet_download) ? "magnet" : "torrent")
		<< "-" << ((flags & tx::multiple_files) ? "multi_file" : "single_file")
		<< "-" << files
		<< "\n\n";

	auto downloader_disk = test_disk().set_files(files);
	auto seeder_disk = test_disk();
	if (corruption) seeder_disk = seeder_disk.send_corrupt_data(num_pieces(flags) / 4 * blocks_per_piece(flags));
	std::set<piece_index_t> passed;
	run_test(no_init
		, record_finished_pieces(passed)
		, expect_seed(!corruption)
		, flags
		, downloader_disk
		, seeder_disk
		);

	int const expected_pieces = num_pieces(flags);

	// we we send some corrupt pieces, it's not straight-forward to predict
	// exactly how many will pass the hash check, since a failure will cause
	// a re-request and also a request of the block hashes (for v2 torrents)
	if (corruption)
	{
		TEST_CHECK(int(passed.size()) < expected_pieces);
	}
	else
	{
		TEST_EQUAL(int(passed.size()), expected_pieces);
	}
}

}

TORRENT_TEST(transfer_matrix)
{
	using fm = existing_files_mode;

	for (test_transfer_flags_t piece_size : {test_transfer_flags_t{}, tx::small_pieces, tx::large_pieces})
		for (bool corruption : {false, true})
			for (test_transfer_flags_t bt_version : {test_transfer_flags_t{}, tx::v2_only, tx::v1_only})
				for (test_transfer_flags_t magnet : {test_transfer_flags_t{}, tx::magnet_download})
					for (test_transfer_flags_t multi_file : {test_transfer_flags_t{}, tx::multiple_files})
						for (fm files : {fm::no_files, fm::full_invalid, fm::partial_valid})
						{
							// this will clear the history of all output we've printed so far.
							// if we encounter an error from now on, we'll only print the relevant
							// iteration
							reset_output();

							// re-seed the random engine each iteration, to make the runs
							// deterministic
							lt::aux::random_engine().seed(0x23563a7f);

							run_matrix_test(piece_size | bt_version | magnet | multi_file, files, corruption);
							if (_g_test_failures > 0) return;
						}
}

TORRENT_TEST(piece_extent_affinity)
{
	run_test(
		[](lt::session& ses0, lt::session& ses1)
		{
			settings_pack p;
			p.set_bool(settings_pack::piece_extent_affinity, true);
			ses0.apply_settings(p);
			ses1.apply_settings(p);
		},
		[](lt::session&, lt::alert const*) {},
		expect_seed(true)
	);
}

TORRENT_TEST(is_finished)
{
	run_test(no_init
		, [](lt::session& ses, lt::alert const* a) {
			if (alert_cast<piece_finished_alert>(a))
			{
				TEST_EQUAL(is_finished(ses), false);
				std::vector<download_priority_t> prio(4, dont_download);
				ses.get_torrents()[0].prioritize_files(prio);
				// applying the priorities is asynchronous. the torrent may not
				// finish immediately
			}
		},
		[](std::shared_ptr<lt::session> ses[2]) {
				TEST_EQUAL(is_finished(*ses[0]), true);
				TEST_EQUAL(is_finished(*ses[1]), true);
		}
	);
}

TORRENT_TEST(v1_only_magnet)
{
	std::set<piece_index_t> passed;
	run_test(no_init
		, record_finished_pieces(passed)
		, expect_seed(true)
		, tx::v1_only | tx::magnet_download
	);
	TEST_EQUAL(passed.size(), 11);
}

TORRENT_TEST(disk_full)
{
	run_test(no_init
		, [](lt::session&, lt::alert const*) {}
		// the disk filled up, we failed to complete the download
		, expect_seed(false)
		, {}
		, test_disk().set_space_left(5 * lt::default_block_size)
	);
}

TORRENT_TEST(disk_full_recover)
{
	run_test(
		[](lt::session& ses0, lt::session&)
		{
			settings_pack p;
			p.set_int(settings_pack::optimistic_disk_retry, 30);
			ses0.apply_settings(p);
		},
		[](lt::session&, lt::alert const* a) {
			if (auto ta = alert_cast<lt::add_torrent_alert>(a))
			{
				// the torrent has to be auto-managed in order to automatically
				// leave upload mode after it hits disk-full
				ta->handle.set_flags(torrent_flags::auto_managed);
			}
		}
		// the disk filled up, we failed to complete the download, but then the
		// disk recovered and we completed it
		, expect_seed(true)
		, {}
		, test_disk().set_space_left(10 * lt::default_block_size).set_recover_full_disk()
		, test_disk()
		, lt::seconds(65)
	);
}

TORRENT_TEST(disk_full_recover_large_pieces)
{
	run_test(
		[](lt::session& ses0, lt::session&)
		{
			settings_pack p;
			p.set_int(settings_pack::optimistic_disk_retry, 30);
			ses0.apply_settings(p);
		},
		[](lt::session&, lt::alert const* a) {
			if (auto ta = alert_cast<lt::add_torrent_alert>(a))
			{
				// the torrent has to be auto-managed in order to automatically
				// leave upload mode after it hits disk-full
				ta->handle.set_flags(torrent_flags::auto_managed);
			}
		}
		// the disk filled up, we failed to complete the download, but then the
		// disk recovered and we completed it
		, expect_seed(true)
		, tx::large_pieces
		, test_disk().set_space_left(10 * lt::default_block_size).set_recover_full_disk()
		, test_disk()
		, lt::seconds(70)
	);
}

// Below is a series of tests to transfer torrents with varying pad-file related
// traits
void run_torrent_test(std::shared_ptr<lt::torrent_info> ti)
{
	using asio::ip::address;
	address peer0 = addr("50.0.0.1");
	address peer1 = addr("50.0.0.2");

	// setup the simulation
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	sim::asio::io_context ios0 { sim, peer0 };
	sim::asio::io_context ios1 { sim, peer1 };

	lt::session_proxy zombie[2];

	lt::session_params params;
	// setup settings pack to use for the session (customization point)
	lt::settings_pack& pack = params.settings;
	pack = settings();
	pack.set_bool(settings_pack::disable_hash_checks, false);

	// disable utp by default
	pack.set_bool(settings_pack::enable_outgoing_utp, false);
	pack.set_bool(settings_pack::enable_incoming_utp, false);

	// disable encryption by default
	pack.set_bool(settings_pack::prefer_rc4, false);
	pack.set_int(settings_pack::in_enc_policy, settings_pack::pe_disabled);
	pack.set_int(settings_pack::out_enc_policy, settings_pack::pe_disabled);
	pack.set_int(settings_pack::allowed_enc_level, settings_pack::pe_plaintext);

	pack.set_str(settings_pack::listen_interfaces, "50.0.0.1:6881");

	// create session
	std::shared_ptr<lt::session> ses[2];

	// session 0 is a downloader, session 1 is a seed

	params.disk_io_constructor = test_disk();
	ses[0] = std::make_shared<lt::session>(params, ios0);

	pack.set_str(settings_pack::listen_interfaces, "50.0.0.2:6881");

	params.disk_io_constructor = test_disk().set_files(existing_files_mode::full_valid);
	ses[1] = std::make_shared<lt::session>(params, ios1);

	// only monitor alerts for session 0 (the downloader)
	print_alerts(*ses[0], [=](lt::session& ses, lt::alert const* a) {
		if (auto ta = alert_cast<lt::add_torrent_alert>(a))
		{
			ta->handle.connect_peer(lt::tcp::endpoint(peer1, 6881));
		}
	}, 0);

	print_alerts(*ses[1], [](lt::session&, lt::alert const*){}, 1);

	lt::add_torrent_params atp;
	atp.ti = ti;
	atp.save_path = ".";

	atp.flags &= ~lt::torrent_flags::auto_managed;
	atp.flags &= ~lt::torrent_flags::paused;

	ses[1]->async_add_torrent(atp);
	auto torrent = atp.ti;

	ses[0]->async_add_torrent(atp);

	sim::timer t(sim, lt::seconds(10), [&](boost::system::error_code const&)
	{
		auto h = ses[0]->get_torrents();
		auto ti = h[0].torrent_file_with_hashes();

		if (ti->v2())
			TEST_EQUAL(ti->v2_piece_hashes_verified(), true);

		auto downloaded = serialize(*ti);
		auto added = serialize(*torrent);
		TEST_CHECK(downloaded == added);

		TEST_CHECK(is_seed(*ses[0]));
		TEST_CHECK(is_seed(*ses[1]));

		h[0].force_recheck();
	});

	sim::timer t2(sim, lt::minutes(1), [&](boost::system::error_code const&)
	{
		// shut down
		int idx = 0;
		for (auto& s : ses)
		{
			zombie[idx++] = s->abort();
			s.reset();
		}
	});

	sim.run();
}

namespace {

std::shared_ptr<lt::torrent_info> test_torrent(lt::file_storage fs, lt::create_flags_t const flags)
{
	lt::create_torrent ct(fs, fs.piece_length(), flags);
	lt::settings_pack pack;
	lt::error_code ec;
	lt::set_piece_hashes(ct, "", pack, test_disk().set_files(existing_files_mode::full_valid)
		, [](lt::piece_index_t p) { std::cout << "."; std::cout.flush();}, ec);

	auto e = ct.generate();
	return std::make_shared<lt::torrent_info>(e);
}

}

TORRENT_TEST(simple_torrent)
{
	run_torrent_test(test_torrent(make_files(
		{{0x3ff0, false}, {0x10, true}}, 0x4000), {}));
}

TORRENT_TEST(odd_last_pad_file)
{
	run_torrent_test(test_torrent(make_files(
		{{0x4100, false}, {0x10, true}}, 0x4000), {}));
}

TORRENT_TEST(small_piece_size)
{
	run_torrent_test(test_torrent(make_files(
		{{0x3ff0, false}, {0x10, true}}, 0x2000), {}));
}

TORRENT_TEST(odd_piece_size)
{
	run_torrent_test(test_torrent(make_files(
		{{0x1ffe, false}, {0x1, true}}, 0x1fff), {}));
}

TORRENT_TEST(large_pad_file)
{
	run_torrent_test(test_torrent(make_files(
		{{0x5000, false}, {0x100000000 - 0x5000, true}}, 0x100000), {}));
}

TORRENT_TEST(unaligned_pad_file)
{
	run_torrent_test(test_torrent(make_files(
		{{0x3fff, false}, {0x10, true}}, 0x4000), {}));
}

TORRENT_TEST(piece_size_pad_file)
{
	run_torrent_test(test_torrent(make_files(
		{{0x8000, false}, {0x8000, true}}, 0x8000), {}));
}

TORRENT_TEST(block_size_pad_file)
{
	run_torrent_test(test_torrent(make_files(
		{{0x4000, false}, {0x4000, true}}, 0x4000), {}));
}

TORRENT_TEST(back_to_back_pad_file)
{
	run_torrent_test(test_torrent(make_files(
		{{0x3000, false}, {0x800, true}, {0x800, true}}, 0x4000), {}));
}

TORRENT_TEST(small_file_large_piece)
{
	run_torrent_test(test_torrent(make_files(
		{{0x833ed, false}, {0x7cc13, true}, {0x3d, false}, {0x7ffc3, true}, {0x14000, false}}, 0x80000), {}));
}
