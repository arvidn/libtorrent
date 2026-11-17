/*

Copyright (c) 2015-2016, 2018-2019, 2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "transfer_sim.hpp"
#include "fake_peer.hpp"
#include "libtorrent/load_torrent.hpp"
#include "libtorrent/posix_disk_io.hpp"

#include <filesystem>
#include <functional>
#include <set>
#include <string_view>
#include <variant>

using namespace sim;
using namespace lt;
using namespace std::chrono_literals;

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

// a seed that encrypts its send buffer in place has to copy a non-exclusive
// disk buffer before queueing it. The transfer succeeds either way, so only
// the seed's own buffers show the difference. This asserts the non-exclusive
// path was reached at all, which no other test does
TORRENT_TEST(encryption_non_exclusive_read_buffers)
{
	auto const seed_disk = test_disk();
	run_test(
		[](lt::session& ses0, lt::session& ses1) {
			enable_enc(ses0);
			enable_enc(ses1);
		},
		[](lt::session&, lt::alert const*) {},
		expect_seed(true),
		{},
		test_disk(),
		seed_disk);
	TEST_CHECK(*seed_disk.non_exclusive_reads > 0);
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

#if TORRENT_USE_I2P
namespace {

void run_i2p_test(test_transfer_flags_t const flags,
	std::function<void(lt::session&, lt::session&)> setup = no_init,
	std::function<void(lt::session&, lt::alert const*)> handler = {},
	test_disk const seeder_disk = test_disk())
{
	bool i2p_peer = false;
	bool ip_peer = false;
	run_test(
		[&](lt::session& ses0, lt::session& ses1) {
			filter_ips(ses1);
			setup(ses0, ses1);
		},
		[&](lt::session& ses, lt::alert const* a) {
			if (auto const* pc = lt::alert_cast<lt::peer_connect_alert>(a))
			{
				if (std::holds_alternative<lt::sha256_hash>(pc->ep))
					i2p_peer = true;
				else
					ip_peer = true;
			}
			if (handler)
				handler(ses, a);
		},
		[&](std::shared_ptr<lt::session> ses[2]) {
			expect_seed(!(flags & tx::corruption))(ses);
			TEST_CHECK(i2p_peer);
			TEST_CHECK(!ip_peer);
		},
		flags | tx::i2p,
		test_disk(),
		seeder_disk,
		// SAM session setup alone takes several high latency round trips
		120s);
}

}

TORRENT_TEST(i2p) { run_i2p_test({}); }

TORRENT_TEST(i2p_v2_only) { run_i2p_test(tx::v2_only); }

TORRENT_TEST(i2p_v1_only) { run_i2p_test(tx::v1_only); }

TORRENT_TEST(i2p_magnet) { run_i2p_test(tx::magnet_download); }

TORRENT_TEST(i2p_resume_restart)
{
	run_i2p_test(tx::resume_restart, no_init, restore_from_resume());
}

#ifndef TORRENT_DISABLE_LOGGING
// an i2p peer has no IP address, make sure it can still be banned for
// sending corrupt data
TORRENT_TEST(i2p_corruption)
{
	test_transfer_flags_t const flags = tx::corruption;
	bool banned_peer = false;
	run_i2p_test(
		flags,
		no_init,
		[&](lt::session&, lt::alert const* a) {
			if (auto const* log = lt::alert_cast<lt::torrent_log_alert>(a))
			{
				std::string_view const msg = log->log_message();
				if (msg.find("Too many corrupt pieces") != std::string_view::npos
					|| msg.find("BANNING PEER") != std::string_view::npos)
					banned_peer = true;
			}
		},
		test_disk().send_corrupt_data(num_pieces(flags) / 4 * blocks_per_piece(flags)));
	TEST_CHECK(banned_peer);
}
#endif

TORRENT_TEST(i2p_encryption)
{
	run_i2p_test({}, [](lt::session& ses0, lt::session& ses1) {
		enable_enc(ses0);
		enable_enc(ses1);
	});
}
#endif

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

// When the downloader has the flag, v1 validation is skipped and the transfer
// succeeds. Without it, the downloader detects the hash mismatch and aborts.
void run_disable_v1_hashes_test(bool const enable_on_downloader)
{
	bool got_inconsistent_error = false;
	run_test(
		no_init,
		[&](lt::session&, lt::alert const* a)
		{
			if (auto e = lt::alert_cast<lt::torrent_error_alert>(a))
				if (e->error == lt::errors::torrent_inconsistent_hashes)
					got_inconsistent_error = true;
		},
		[&](std::shared_ptr<lt::session> ses[2])
		{
			TEST_EQUAL(is_seed(*ses[0]), enable_on_downloader);
			TEST_EQUAL(got_inconsistent_error, !enable_on_downloader);
		},
		tx::bad_v1_hashes | (enable_on_downloader ? tx::disable_v1_hashes : test_transfer_flags_t{})
	);
}

// setting ON + bad v1 hashes -> should succeed, v1 validation is skipped
TORRENT_TEST(disable_v1_hashes_bad_v1_enabled)
{ run_disable_v1_hashes_test(true); }

// setting OFF + bad v1 hashes -> should fail with torrent_inconsistent_hashes
TORRENT_TEST(disable_v1_hashes_bad_v1_disabled)
{ run_disable_v1_hashes_test(false); }

TORRENT_TEST(is_finished)
{
	bool prioritized = false;
	run_test(
		no_init,
		[&prioritized](lt::session& ses, lt::alert const* a) {
			if (!alert_cast<piece_finished_alert>(a))
				return;

			// further pieces may already be in flight and complete before
			// file priorities take effect below, so only assert on the first one
			if (prioritized)
				return;
			prioritized = true;

			TEST_EQUAL(is_finished(ses), false);
			std::vector<download_priority_t> prio(4, dont_download);
			ses.get_torrents()[0].prioritize_files(prio);
			// applying the priorities is asynchronous. the torrent may not
			// finish immediately
		},
		[](std::shared_ptr<lt::session> ses[2]) {
			TEST_EQUAL(is_finished(*ses[0]), true);
			TEST_EQUAL(is_finished(*ses[1]), true);
		});
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
void run_torrent_test(lt::add_torrent_params atp)
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

	atp.save_path = ".";

	atp.flags &= ~lt::torrent_flags::auto_managed;
	atp.flags &= ~lt::torrent_flags::paused;

	ses[1]->async_add_torrent(atp);

	ses[0]->async_add_torrent(atp);

	sim::timer t(sim, lt::seconds(10), [&](boost::system::error_code const&)
	{
		auto h = ses[0]->get_torrents();
#if TORRENT_ABI_VERSION < 4
		auto ti = h[0].torrent_file_with_hashes();

		if (ti->v2())
			TEST_EQUAL(ti->v2_piece_hashes_verified(), true);

		auto downloaded = serialize(*ti);
		auto added = serialize(atp);
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

lt::add_torrent_params test_torrent(std::vector<lt::create_file_entry> fs
	, int const piece_size, lt::create_flags_t const flags)
{
	lt::create_torrent ct(std::move(fs), piece_size, flags);
	lt::settings_pack pack;
	lt::error_code ec;
	lt::set_piece_hashes(ct, "", pack, test_disk().set_files(existing_files_mode::full_valid)
		, [](lt::piece_index_t p) { std::cout << "."; std::cout.flush();}, ec);

	return lt::load_torrent_buffer(lt::bencode(ct.generate()));
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

TORRENT_TEST(unload_file_transfer)
{
	using asio::ip::address;
	address const peer0 = addr("50.0.0.1");
	address const peer1 = addr("50.0.0.2");

	// file 0: two interior pieces + half of the shared boundary piece
	// file 1: the other half of the boundary piece + one interior piece.
	// The seed uses the real disk, so the files exist and the hashes are
	// calculated from them.
	static std::array<int, 2> const unload_sizes{{2 * 0x4000 + 0x2000, 0x4000 + 0x2000}};
	std::filesystem::remove_all("unload_seed");
	std::filesystem::remove_all("unload_downloader");
	std::vector<lt::create_file_entry> const files = create_random_files(
		"unload_seed", unload_sizes);
	lt::create_torrent ct(files, 0x4000, lt::create_torrent::v1_only);
	set_piece_hashes(ct, ".");
	lt::add_torrent_params const atp = lt::load_torrent_buffer(lt::bencode(ct.generate()));

	std::string const downloader_path = "unload_downloader";

	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	sim::asio::io_context ios0 { sim, peer0 };
	sim::asio::io_context ios1 { sim, peer1 };

	lt::session_proxy zombie[2];

	lt::session_params params;
	params.settings = settings();
	params.settings.set_bool(settings_pack::disable_hash_checks, false);
	params.settings.set_bool(settings_pack::enable_outgoing_utp, false);
	params.settings.set_bool(settings_pack::enable_incoming_utp, false);
	params.settings.set_int(settings_pack::in_enc_policy, settings_pack::pe_disabled);
	params.settings.set_int(settings_pack::out_enc_policy, settings_pack::pe_disabled);
	params.settings.set_int(settings_pack::allowed_enc_level, settings_pack::pe_plaintext);
	params.settings.set_str(settings_pack::listen_interfaces, "50.0.0.1:6881");
	params.disk_io_constructor = lt::posix_disk_io_constructor;

	std::shared_ptr<lt::session> ses[2];
	ses[0] = std::make_shared<lt::session>(params, ios0);

	params.settings.set_str(settings_pack::listen_interfaces, "50.0.0.2:6881");
	ses[1] = std::make_shared<lt::session>(params, ios1);

	print_alerts(*ses[0], [&](lt::session&, lt::alert const* a) {
		if (auto const* ta = alert_cast<lt::add_torrent_alert>(a))
			ta->handle.connect_peer(lt::tcp::endpoint(peer1, 6881));
	}, 0);
	print_alerts(*ses[1], [](lt::session&, lt::alert const*) {}, 1);

	lt::add_torrent_params seed_atp = atp;
	seed_atp.save_path = ".";
	seed_atp.flags &= ~lt::torrent_flags::auto_managed;
	seed_atp.flags &= ~lt::torrent_flags::paused;
	ses[1]->async_add_torrent(seed_atp);

	lt::add_torrent_params dl_atp = atp;
	dl_atp.save_path = downloader_path;
	dl_atp.flags &= ~lt::torrent_flags::auto_managed;
	dl_atp.flags &= ~lt::torrent_flags::paused;
	ses[0]->async_add_torrent(dl_atp);

	file_storage const& fs = atp.ti->layout();
	std::string const file0 = combine_path(downloader_path, fs.file_path(0_file));
	std::string const file1 = combine_path(downloader_path, fs.file_path(1_file));

	sim::timer unload_timer(sim, lt::seconds(15), [&](boost::system::error_code const&) {
		auto const torrents = ses[0]->get_torrents();
		TEST_EQUAL(torrents.size(), 1);
		if (torrents.empty()) return;
		TEST_CHECK(torrents[0].status().is_seeding);
		TEST_CHECK(exists(file0));
		TEST_CHECK(exists(file1));
		torrents[0].unload_file(0_file);
	});

	sim::timer check_timer(sim, lt::seconds(20), [&](boost::system::error_code const&) {
		auto const torrents = ses[0]->get_torrents();
		TEST_EQUAL(torrents.size(), 1);
		if (torrents.empty()) return;
		auto const h = torrents[0];

		TEST_CHECK(!exists(file0));
		TEST_CHECK(exists(file1));
		TEST_CHECK(h.get_unloaded_files()[0]);

		// interior pieces of the unloaded file are gone, the shared boundary
		// piece and the other file's interior piece are still seeding
		TEST_CHECK(!h.have_piece(0_piece));
		TEST_CHECK(!h.have_piece(1_piece));
		TEST_CHECK(h.have_piece(2_piece));
		TEST_CHECK(h.have_piece(3_piece));

		h.force_recheck();
	});

	sim::timer recheck_timer(sim, lt::seconds(35), [&](boost::system::error_code const&) {
		auto const torrents = ses[0]->get_torrents();
		TEST_EQUAL(torrents.size(), 1);
		if (torrents.empty()) return;
		auto const h = torrents[0];

		// a recheck must not recreate the file or bring back the dropped
		// interior pieces, nor re-download the boundary bytes
		TEST_CHECK(!exists(file0));
		TEST_CHECK(exists(file1));
		TEST_CHECK(!h.have_piece(0_piece));
		TEST_CHECK(!h.have_piece(1_piece));
		TEST_CHECK(h.have_piece(2_piece));
		TEST_CHECK(h.have_piece(3_piece));

		int idx = 0;
		for (auto& s : ses)
		{
			zombie[idx++] = s->abort();
			s.reset();
		}
	});

	sim::timer shutdown_timer(sim, lt::seconds(60), [&](boost::system::error_code const&) {
		int idx = 0;
		for (auto& s : ses)
		{
			if (!s) continue;
			zombie[idx++] = s->abort();
			s.reset();
		}
	});

	sim.run();
}

// A block for an exclusive piece may still arrive after the file was unloaded
// and the request was cancelled. It must be discarded: no picker entry, no
// have bit and no part-file slot for the dropped piece.
TORRENT_TEST(unload_file_late_block)
{
	using asio::ip::address;
	address const peer = addr("50.0.0.1");

	// file 0: pieces 0 and 1 plus half of the shared piece 2
	// file 1: the other half of piece 2 plus piece 3
	static std::array<int, 2> const sizes{{2 * 0x4000 + 0x2000, 0x4000 + 0x2000}};
	std::filesystem::remove_all("unload_late_seed");
	std::filesystem::remove_all("unload_late_downloader");
	std::vector<lt::create_file_entry> const files = create_random_files(
		"unload_late_seed", sizes);
	lt::create_torrent ct(files, 0x4000, lt::create_torrent::v1_only);
	set_piece_hashes(ct, ".");
	lt::add_torrent_params const atp = lt::load_torrent_buffer(lt::bencode(ct.generate()));
	file_storage const& fs = atp.ti->layout();
	index_range<piece_index_t> const exclusive
		= lt::aux::file_piece_range_exclusive(fs, 0_file);

	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	sim::asio::io_context ios { sim, peer };
	lt::session_proxy zombie;

	lt::session_params params;
	params.settings = settings();
	params.settings.set_str(settings_pack::listen_interfaces, "50.0.0.1:6881");
	params.disk_io_constructor = lt::posix_disk_io_constructor;
	auto ses = std::make_shared<lt::session>(params, ios);

	// the peer advertises the whole torrent but never sends a block until the
	// unload has committed
	fake_peer fake(sim, "60.0.0.1");

	lt::torrent_handle h;
	lt::piece_block late_block;
	bool have_late = false;
	bool unloaded = false;
	std::int64_t part_size = -1;

	auto part_file_size = [&]() -> std::int64_t {
		error_code ec;
		for (auto const& e : std::filesystem::directory_iterator(
			"unload_late_downloader", ec))
		{
			if (e.path().extension() == ".parts")
				return std::filesystem::file_size(e.path(), ec);
		}
		return -1;
	};

	print_alerts(*ses, [&](lt::session&, lt::alert const* a) {
		if (auto const* at = alert_cast<lt::add_torrent_alert>(a))
		{
			h = at->handle;
			fake.send_have_all();
			fake.send_unchoke();
			fake.connect_to(ep("50.0.0.1", 6881), atp.ti->info_hash());
		}
		else if (alert_cast<file_prio_alert>(a) && have_late && !unloaded)
		{
			unloaded = true;
			part_size = part_file_size();
			fake.send_piece(late_block.piece_index, late_block.block_index * 0x4000);
			fake.flush_send_buffer();
		}
	});

	lt::add_torrent_params dl_atp = atp;
	dl_atp.save_path = "unload_late_downloader";
	dl_atp.flags &= ~lt::torrent_flags::auto_managed;
	dl_atp.flags &= ~lt::torrent_flags::paused;
	ses->async_add_torrent(dl_atp);

	std::function<void(boost::system::error_code const&)> try_unload
		= [&](boost::system::error_code const&)
	{
		if (have_late || !h.is_valid()) return;
		for (auto const& entry : h.get_download_queue())
		{
			if (entry.piece_index < exclusive._begin || entry.piece_index >= exclusive._end)
				continue;
			for (int i = 0; i < entry.blocks_in_piece; ++i)
			{
				have_late = true;
				late_block = lt::piece_block(entry.piece_index, i);
				break;
			}
			if (have_late) break;
		}
		if (have_late) h.unload_file(0_file);
	};

	sim::timer unload_timer1(sim, lt::seconds(1), try_unload);
	sim::timer unload_timer2(sim, lt::seconds(2), try_unload);
	sim::timer unload_timer3(sim, lt::seconds(3), try_unload);
	sim::timer unload_timer4(sim, lt::seconds(4), try_unload);

	sim::timer check_timer(sim, lt::seconds(10), [&](boost::system::error_code const&) {
		TEST_CHECK(have_late);
		TEST_CHECK(unloaded);
		if (!have_late || !unloaded) return;

		TEST_CHECK(!exists("unload_late_downloader/unload_late_seed/test_dir0/test0"));
		TEST_CHECK(!h.have_piece(late_block.piece_index));

		for (auto const& entry : h.get_download_queue())
			TEST_CHECK(entry.piece_index != late_block.piece_index);

		// no part file existed when the file was unloaded, and the late block
		// must not have allocated a slot in one
		TEST_CHECK(part_size == -1);
		TEST_EQUAL(part_file_size(), part_size);
	});

	sim::timer shutdown_timer(sim, lt::seconds(20), [&](boost::system::error_code const&) {
		zombie = ses->abort();
		ses.reset();
	});

	sim.run();
}

// the simulation disk I/O does not support unloading. A failed unload must
// not commit the unloaded state, drop any pieces or stop the torrent.
TORRENT_TEST(unload_file_unsupported_backend)
{
	using asio::ip::address;
	address const peer = addr("50.0.0.1");

	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	sim::asio::io_context ios { sim, peer };
	lt::session_proxy zombie;

	lt::session_params params;
	params.settings = settings();
	params.settings.set_str(settings_pack::listen_interfaces, "50.0.0.1:6881");
	params.disk_io_constructor = test_disk().set_seed();
	auto ses = std::make_shared<lt::session>(params, ios);

	lt::add_torrent_params atp = ::create_torrent(0, true);
	atp.flags |= lt::torrent_flags::seed_mode;
	atp.flags &= ~lt::torrent_flags::paused;
	atp.flags &= ~lt::torrent_flags::auto_managed;

	lt::torrent_handle h;
	bool got_error = false;
	print_alerts(*ses, [&](lt::session&, lt::alert const* a) {
		if (auto const* at = alert_cast<lt::add_torrent_alert>(a))
			h = at->handle;
		else if (alert_cast<file_error_alert>(a))
			got_error = true;
	});

	ses->async_add_torrent(atp);

	sim::timer unload_timer(sim, lt::seconds(1), [&](boost::system::error_code const&) {
		TEST_CHECK(h.is_valid());
		if (!h.is_valid()) return;
		TEST_CHECK(h.have_piece(0_piece));
		h.unload_file(0_file);
	});

	sim::timer check_timer(sim, lt::seconds(3), [&](boost::system::error_code const&) {
		TEST_CHECK(got_error);
		TEST_CHECK(h.is_valid());
		if (!h.is_valid()) return;
		auto const unloaded = h.get_unloaded_files();
		TEST_CHECK(unloaded.empty() || !unloaded[0]);
		TEST_CHECK(h.flags() & torrent_flags::seed_mode);
		TEST_CHECK(h.have_piece(0_piece));
		TEST_CHECK(!(h.flags() & torrent_flags::paused));
		TEST_CHECK(!h.status().errc);
		zombie = ses->abort();
		ses.reset();
	});

	sim.run();
}

// a resume save requested while a file-priority job is outstanding is
// deferred until the job commits. Removing the torrent before that must still
// answer the request, or a client waiting for the alert never hears back.
TORRENT_TEST(deferred_resume_save_before_remove)
{
	using asio::ip::address;
	address const peer = addr("50.0.0.1");

	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	sim::asio::io_context ios { sim, peer };
	lt::session_proxy zombie;

	lt::session_params params;
	params.settings = settings();
	params.settings.set_str(settings_pack::listen_interfaces, "50.0.0.1:6881");
	params.disk_io_constructor = test_disk().set_seed();
	auto ses = std::make_shared<lt::session>(params, ios);

	lt::add_torrent_params atp = ::create_torrent(0, true);
	atp.flags |= lt::torrent_flags::seed_mode;
	atp.flags &= ~lt::torrent_flags::paused;
	atp.flags &= ~lt::torrent_flags::auto_managed;

	lt::torrent_handle h;
	bool got_failed = false;
	print_alerts(*ses, [&](lt::session&, lt::alert const* a) {
		if (auto const* at = alert_cast<lt::add_torrent_alert>(a))
			h = at->handle;
		else if (alert_cast<save_resume_data_failed_alert>(a))
			got_failed = true;
	});

	ses->async_add_torrent(atp);

	// all three calls are processed in order, so the save is still deferred
	// when the removal aborts the torrent
	sim::timer remove_timer(sim, lt::seconds(1), [&](boost::system::error_code const&) {
		TEST_CHECK(h.is_valid());
		if (!h.is_valid()) return;
		h.file_priority(0_file, low_priority);
		h.save_resume_data();
		ses->remove_torrent(h);
	});

	sim::timer check_timer(sim, lt::seconds(3), [&](boost::system::error_code const&) {
		TEST_CHECK(got_failed);
		zombie = ses->abort();
		ses.reset();
	});

	sim.run();
}

// A block may still be in a peer's request queue when the picker drops the
// piece (end-game duplicate request). Sending that block after the unload must
// not re-add the piece or allocate a part-file slot for it.
TORRENT_TEST(unload_file_duplicate_late_block)
{
	using asio::ip::address;
	address const peer0 = addr("50.0.0.1");

	std::filesystem::remove_all("unload_dup_dl");

	// one piece of zeroes, so the fake peer can serve it verbatim
	std::vector<lt::create_file_entry> files;
	files.emplace_back("file0.bin", 0x4000);
	lt::create_torrent ct(files, 0x4000, lt::create_torrent::v1_only);
	lt::sha1_hash const piece_hash = lt::hasher(std::vector<char>(0x4000, 0)).final();
	ct.set_hash(0_piece, piece_hash);
	lt::add_torrent_params const atp = lt::load_torrent_buffer(lt::bencode(ct.generate()));

	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	sim::asio::io_context ios { sim, peer0 };
	lt::session_proxy zombie;

	lt::session_params params;
	params.settings = settings();
	params.settings.set_bool(settings_pack::strict_end_game_mode, false);
	params.settings.set_bool(settings_pack::close_redundant_connections, false);
	params.settings.set_str(settings_pack::listen_interfaces, "50.0.0.1:6881");
	params.disk_io_constructor = lt::posix_disk_io_constructor;
	auto ses = std::make_shared<lt::session>(params, ios);

	fake_peer a(sim, "60.0.0.1");
	fake_peer b(sim, "60.0.0.2");

	bool requested_a = false;
	bool requested_b = false;
	auto const on_message = [](bool& requested) {
		return [&requested](std::uint8_t const msg, lt::span<char const> const payload) {
			if (msg != 6 || payload.size() < 12) return;
			char const* ptr = payload.data();
			if (lt::aux::read_uint32(ptr) == 0 && lt::aux::read_uint32(ptr) == 0)
				requested = true;
		};
	};
	a.set_message_handler(on_message(requested_a));
	b.set_message_handler(on_message(requested_b));

	lt::torrent_handle h;
	bool unloaded = false;
	print_alerts(*ses, [&](lt::session&, lt::alert const* al) {
		if (auto const* at = alert_cast<lt::add_torrent_alert>(al))
		{
			h = at->handle;
			for (fake_peer* p : {&a, &b})
			{
				p->send_have_all();
				p->send_unchoke();
				p->connect_to(ep("50.0.0.1", 6881), atp.ti->info_hash());
			}
		}
		else if (alert_cast<file_prio_alert>(al))
		{
			unloaded = true;
		}
	});

	auto part_file_size = []() -> std::int64_t {
		error_code ec;
		for (auto const& e : std::filesystem::directory_iterator("unload_dup_dl", ec))
			if (e.path().extension() == ".parts")
				return std::filesystem::file_size(e.path(), ec);
		return -1;
	};

	lt::add_torrent_params dl_atp = atp;
	dl_atp.save_path = "unload_dup_dl";
	dl_atp.flags &= ~torrent_flags::paused;
	dl_atp.flags &= ~torrent_flags::auto_managed;
	ses->async_add_torrent(dl_atp);

	bool sent_a = false;
	bool sent_b = false;
	std::int64_t part_size = -1;

	sim::timer send_a(sim, lt::seconds(2), [&](boost::system::error_code const&) {
		TEST_CHECK(requested_a);
		TEST_CHECK(requested_b);
		if (!requested_a || !requested_b) return;
		a.send_piece(0_piece, 0);
		a.flush_send_buffer();
		sent_a = true;
	});

	sim::timer unload_timer(sim, lt::seconds(3), [&](boost::system::error_code const&) {
		TEST_CHECK(sent_a);
		TEST_CHECK(h.is_valid());
		if (!sent_a || !h.is_valid()) return;
		TEST_CHECK(h.have_piece(0_piece));
		if (!h.have_piece(0_piece)) return;
		h.unload_file(0_file);
	});

	auto const send_b = [&](boost::system::error_code const&) {
		if (!unloaded || sent_b) return;
		part_size = part_file_size();
		b.send_piece(0_piece, 0);
		b.flush_send_buffer();
		sent_b = true;
	};
	sim::timer send_b1(sim, lt::seconds(4), send_b);
	sim::timer send_b2(sim, lt::seconds(5), send_b);

	sim::timer check_timer(sim, lt::seconds(8), [&](boost::system::error_code const&) {
		TEST_CHECK(unloaded);
		TEST_CHECK(sent_b);
		TEST_CHECK(h.is_valid());
		if (!h.is_valid()) return;
		TEST_CHECK(!h.have_piece(0_piece));
		for (auto const& entry : h.get_download_queue())
			TEST_CHECK(entry.piece_index != 0_piece);
		// no part file existed when the file was unloaded, and the duplicate
		// block must not have allocated a slot in one
		TEST_CHECK(part_size == -1);
		TEST_EQUAL(part_file_size(), part_size);
		zombie = ses->abort();
		ses.reset();
	});

	sim.run();
}

// A piece shared with a file that is still wanted may be partially downloaded
// when the other file is unloaded. Its received blocks were written to the
// file that is deleted, and the storage only preserves complete pieces. The
// piece has to be restarted, or the picker keeps the lost blocks as finished
// and the piece hash fails against the part file once the rest arrives.
TORRENT_TEST(unload_file_partial_boundary_piece)
{
	using asio::ip::address;
	address const peer = addr("50.0.0.1");

	std::filesystem::remove_all("unload_partial_dl");

	// piece 0 lies inside file0. piece 1 (64 KiB) is shared: the last 32 KiB
	// of file0 followed by all of file1. The content is zeroes, so the fake
	// peer can serve any block of piece 1 verbatim.
	std::vector<lt::create_file_entry> files;
	files.emplace_back("unload_partial/file0.bin", 0x18000);
	files.emplace_back("unload_partial/file1.bin", 0x8000);
	lt::create_torrent ct(files, 0x10000, lt::create_torrent::v1_only);
	lt::sha1_hash const piece_hash = lt::hasher(std::vector<char>(0x10000, 0)).final();
	ct.set_hash(0_piece, piece_hash);
	ct.set_hash(1_piece, piece_hash);
	lt::add_torrent_params const atp = lt::load_torrent_buffer(lt::bencode(ct.generate()));

	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	sim::asio::io_context ios { sim, peer };
	lt::session_proxy zombie;

	lt::session_params params;
	params.settings = settings();
	// keep the connection after the wanted file completes
	params.settings.set_bool(settings_pack::close_redundant_connections, false);
	params.settings.set_str(settings_pack::listen_interfaces, "50.0.0.1:6881");
	params.disk_io_constructor = lt::posix_disk_io_constructor;
	auto ses = std::make_shared<lt::session>(params, ios);

	fake_peer fake(sim, "60.0.0.1");

	lt::torrent_handle h;
	bool unload_sent = false;
	bool got_unload = false;
	bool got_error = false;
	bool needs_flush = false;
	// blocks of the shared piece the peer was asked for and has not sent yet
	std::array<bool, 4> wanted{{false, false, false, false}};

	fake.set_message_handler([&](std::uint8_t const msg, lt::span<char const> const payload)
	{
		if (msg != 6 || payload.size() < 12) return;
		char const* ptr = payload.data();
		lt::piece_index_t const piece(lt::aux::read_uint32(ptr));
		int const start = lt::aux::read_uint32(ptr);
		if (piece != 1_piece) return;
		int const block = start / 0x4000;
		if (block < 0 || block > 3) return;
		wanted[std::size_t(block)] = true;
	});

	print_alerts(*ses, [&](lt::session&, lt::alert const* a) {
		if (auto const* at = alert_cast<lt::add_torrent_alert>(a))
		{
			h = at->handle;
			// only piece 1 (the shared one) is served, so the download queue
			// is not filled with requests the test never answers
			fake.send_bitfield({false, true});
			fake.send_unchoke();
			fake.connect_to(ep("50.0.0.1", 6881), atp.ti->info_hash());
		}
		else if (alert_cast<file_prio_alert>(a))
		{
			got_unload = true;
		}
		else if (alert_cast<file_error_alert>(a))
		{
			got_error = true;
		}
	});

	lt::add_torrent_params dl_atp = atp;
	dl_atp.save_path = "unload_partial_dl";
	dl_atp.flags &= ~torrent_flags::paused;
	dl_atp.flags &= ~torrent_flags::auto_managed;
	ses->async_add_torrent(dl_atp);

	std::string const file0 = "unload_partial_dl/unload_partial/file0.bin";
	std::string const file1 = "unload_partial_dl/unload_partial/file1.bin";

	int ticks = 0;
	bool done = false;
	sim::asio::high_resolution_timer tick(ios);
	std::function<void(boost::system::error_code const&)> on_tick
		= [&](boost::system::error_code const&)
	{
		++ticks;
		if (done) return;

		if (h.is_valid())
		{
			// serve the blocks of the shared piece the peer was asked for.
			// The blocks of the other file are held back until the unload
			// committed, so the piece is partially downloaded when the
			// unload runs; they arrive afterwards and are discarded, which
			// makes the torrent ask for the restarted piece again.
			for (int b = 0; b < 4; ++b)
			{
				if (!wanted[std::size_t(b)]) continue;
				if (b >= 2 && !got_unload) continue;
				fake.send_piece(1_piece, b * 0x4000);
				wanted[std::size_t(b)] = false;
				needs_flush = true;
			}

			if (needs_flush && !fake.writing())
			{
				fake.flush_send_buffer();
				needs_flush = false;
			}

			// unload once two blocks of the shared piece are in, so some of
			// its data is on the file being deleted and some is not yet there
			if (!unload_sent)
			{
				for (auto const& p : h.get_download_queue())
				{
					if (p.piece_index != 1_piece) continue;
					if (p.finished + p.writing < 2) continue;
					unload_sent = true;
					h.unload_file(0_file);
					break;
				}
			}
		}

		if (unload_sent && got_unload && h.is_valid() && h.have_piece(1_piece))
		{
			done = true;
			TEST_CHECK(!got_error);
			TEST_CHECK(!h.status().errc);
			TEST_CHECK(!(h.flags() & torrent_flags::paused));
			TEST_CHECK(!h.have_piece(0_piece));
			TEST_CHECK(!exists(file0));
			TEST_CHECK(exists(file1));
			if (exists(file1))
			{
				error_code ec;
				TEST_EQUAL(int(std::filesystem::file_size(file1, ec)), 0x8000);
				TEST_CHECK(!ec);
			}
			zombie = ses->abort();
			ses.reset();
			return;
		}

		// give up after 30 seconds of simulated time
		if (ticks >= 240)
		{
			done = true;
			TEST_CHECK(unload_sent);
			TEST_CHECK(got_unload);
			TEST_CHECK(!got_error);
			TEST_CHECK(h.is_valid());
			if (h.is_valid())
			{
				TEST_CHECK(!h.status().errc);
				TEST_CHECK(!(h.flags() & torrent_flags::paused));
				TEST_CHECK(h.have_piece(1_piece));
			}
			zombie = ses->abort();
			ses.reset();
			return;
		}

		tick.expires_after(125ms);
		tick.async_wait(on_tick);
	};
	tick.expires_after(125ms);
	tick.async_wait(on_tick);

	sim.run();
}

#ifndef TORRENT_DISABLE_STREAMING
// Deadline downloads must still complete with the corrected queue estimates
// and the existing two-second scheduling limit.
TORRENT_TEST(piece_deadlines)
{
	for (int const queue_time : {1, 3, 10})
	{
		std::set<piece_index_t> pieces_read;
		int num_pieces = 0;
		run_test(
			[queue_time](lt::session& downloader, lt::session&) {
				settings_pack pack;
				pack.set_int(settings_pack::request_queue_time, queue_time);
				pack.set_int(settings_pack::download_rate_limit, 32 * 1024);
				downloader.apply_settings(pack);
			},
			[&](lt::session&, lt::alert const* a) {
				if (auto const* at = alert_cast<add_torrent_alert>(a))
				{
					num_pieces = at->params.ti->num_pieces();
					for (piece_index_t const piece : at->params.ti->piece_range())
						at->handle.set_piece_deadline(
							piece, int(piece) * 1000, torrent_handle::alert_when_available);
				}
				else if (auto const* rp = alert_cast<read_piece_alert>(a))
				{
					TEST_CHECK(!rp->error);
					TEST_CHECK(pieces_read.insert(rp->piece).second);
				}
			},
			expect_seed(true));
		TEST_CHECK(num_pieces > 0);
		TEST_EQUAL(int(pieces_read.size()), num_pieces);
	}
}
#endif
