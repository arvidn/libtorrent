/*

Copyright (c) 2015-2016, 2018-2019, 2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "transfer_sim.hpp"

using namespace sim;
using namespace lt;

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

#if TORRENT_ABI_VERSION < 4
		auto downloaded = serialize(*ti);
		auto added = serialize(*torrent);
		TEST_CHECK(downloaded == added);
#endif

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

std::shared_ptr<lt::torrent_info> test_torrent(std::vector<lt::create_file_entry> fs
	, int const piece_size, lt::create_flags_t const flags)
{
	lt::create_torrent ct(std::move(fs), piece_size, flags);
	lt::settings_pack pack;
	lt::error_code ec;
	lt::set_piece_hashes(ct, "", pack, test_disk().set_files(existing_files_mode::full_valid)
		, [](lt::piece_index_t p) { std::cout << "."; std::cout.flush();}, ec);

	auto e = ct.generate();
	std::vector<char> buf;
	lt::bencode(std::back_inserter(buf), e);
	return std::make_shared<lt::torrent_info>(buf, lt::from_span);
}

}

TORRENT_TEST(simple_torrent)
{
	run_torrent_test(test_torrent(make_files(
		{{0x3ff0, false}, {0x10, true}}), 0x4000, {}));
}

TORRENT_TEST(odd_last_pad_file)
{
	run_torrent_test(test_torrent(make_files(
		{{0x4100, false}, {0x10, true}}), 0x4000, {}));
}

TORRENT_TEST(small_piece_size)
{
	run_torrent_test(test_torrent(make_files(
		{{0x3ff0, false}, {0x10, true}}), 0x2000, {}));
}

TORRENT_TEST(odd_piece_size)
{
	run_torrent_test(test_torrent(make_files(
		{{0x1ffe, false}, {0x1, true}}), 0x1fff, {}));
}

TORRENT_TEST(large_pad_file)
{
	run_torrent_test(test_torrent(make_files(
		{{0x5000, false}, {0x100000000 - 0x5000, true}}), 0x100000, {}));
}

TORRENT_TEST(unaligned_pad_file)
{
	run_torrent_test(test_torrent(make_files(
		{{0x3fff, false}, {0x10, true}}), 0x4000, {}));
}

TORRENT_TEST(piece_size_pad_file)
{
	run_torrent_test(test_torrent(make_files(
		{{0x8000, false}, {0x8000, true}}), 0x8000, {}));
}

TORRENT_TEST(block_size_pad_file)
{
	run_torrent_test(test_torrent(make_files(
		{{0x4000, false}, {0x4000, true}}), 0x4000, {}));
}

TORRENT_TEST(back_to_back_pad_file)
{
	run_torrent_test(test_torrent(make_files(
		{{0x3000, false}, {0x800, true}, {0x800, true}}), 0x4000, {}));
}

TORRENT_TEST(small_file_large_piece)
{
	run_torrent_test(test_torrent(make_files(
		{{0x833ed, false}, {0x7cc13, true}, {0x3d, false}, {0x7ffc3, true}, {0x14000, false}}), 0x80000, {}));
}

TORRENT_TEST(empty_file)
{
	run_torrent_test(test_torrent(make_files(
		{{0x3000, false}, {0, false}, {0x8000, false}}), 0x4000, {}));
}
