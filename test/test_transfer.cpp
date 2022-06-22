/*

Copyright (c) 2008-2010, 2012-2022, Arvid Norberg
Copyright (c) 2016, 2018, Alden Torres
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

#include "libtorrent/session.hpp"
#include "libtorrent/session_settings.hpp"
#include "libtorrent/session_params.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/torrent_info.hpp"

#include "test.hpp"
#include "setup_transfer.hpp"
#include "settings.hpp"
#include "test_utils.hpp"

#include <tuple>
#include <functional>

#include <fstream>
#include <iostream>

using namespace lt;

using std::ignore;

namespace {

int peer_disconnects = 0;

bool on_alert(alert const* a)
{
	auto const* const pd = alert_cast<peer_disconnected_alert>(a);
	if (pd && pd->error != make_error_code(errors::self_connection))
		++peer_disconnects;
	else if (alert_cast<peer_error_alert>(a))
		++peer_disconnects;

	return false;
}

struct transfer_tag;
using transfer_flags_t = lt::flags::bitfield_flag<std::uint8_t, transfer_tag>;

constexpr transfer_flags_t delete_files = 2_bit;
constexpr transfer_flags_t move_storage = 3_bit;

void test_transfer(int proxy_type, settings_pack const& sett
	, transfer_flags_t flags = {}
	, storage_mode_t storage_mode = storage_mode_sparse)
{
	char const* test_name[] = {"no", "SOCKS4", "SOCKS5", "SOCKS5 password", "HTTP", "HTTP password"};

	std::printf("\n\n  ==== TESTING %s proxy ==== move-storage: %s\n\n\n"
		, test_name[proxy_type]
		, (flags & move_storage) ? "true": "false"
	);

	// in case the previous run was terminated
	error_code ec;
	remove_all("tmp1_transfer", ec);
	remove_all("tmp2_transfer", ec);
	remove_all("tmp1_transfer_moved", ec);
	remove_all("tmp2_transfer_moved", ec);

	// these are declared before the session objects
	// so that they are destructed last. This enables
	// the sessions to destruct in parallel
	session_proxy p1;
	session_proxy p2;

	settings_pack pack = settings();
	pack.set_str(settings_pack::listen_interfaces, test_listen_interface());

	pack.set_bool(settings_pack::enable_upnp, false);
	pack.set_bool(settings_pack::enable_natpmp, false);
	pack.set_bool(settings_pack::enable_lsd, false);
	pack.set_bool(settings_pack::enable_dht, false);
#if TORRENT_ABI_VERSION == 1
	pack.set_bool(settings_pack::rate_limit_utp, true);
#endif

	lt::session ses1(pack);

	pack.set_str(settings_pack::listen_interfaces, test_listen_interface());
	lt::session ses2(pack);

	int proxy_port = 0;
	if (proxy_type)
	{
		proxy_port = start_proxy(proxy_type);

		settings_pack pack_p;
		pack_p.set_str(settings_pack::proxy_username, "testuser");
		pack_p.set_str(settings_pack::proxy_password, "testpass");
		pack_p.set_int(settings_pack::proxy_type, proxy_type);
		pack_p.set_int(settings_pack::proxy_port, proxy_port);

		// test resetting the proxy in quick succession.
		// specifically the udp_socket connecting to a new
		// socks5 proxy while having one connection attempt
		// in progress.
		pack_p.set_str(settings_pack::proxy_hostname, "5.6.7.8");
		ses1.apply_settings(pack_p);
		pack_p.set_str(settings_pack::proxy_hostname, "127.0.0.1");
		ses1.apply_settings(pack_p);
	}

	pack = sett;

	// we need a short reconnect time since we
	// finish the torrent and then restart it
	// immediately to complete the second half.
	// using a reconnect time > 0 will just add
	// to the time it will take to complete the test
	pack.set_int(settings_pack::min_reconnect_time, 0);
	pack.set_int(settings_pack::stop_tracker_timeout, 1);
	pack.set_bool(settings_pack::announce_to_all_trackers, true);
	pack.set_bool(settings_pack::announce_to_all_tiers, true);

	// make sure we announce to both http and udp trackers
	pack.set_bool(settings_pack::prefer_udp_trackers, false);
	pack.set_bool(settings_pack::enable_outgoing_utp, false);
	pack.set_bool(settings_pack::enable_incoming_utp, false);
	pack.set_bool(settings_pack::enable_lsd, false);
	pack.set_bool(settings_pack::enable_natpmp, false);
	pack.set_bool(settings_pack::enable_upnp, false);
	pack.set_bool(settings_pack::enable_dht, false);

	pack.set_int(settings_pack::out_enc_policy, settings_pack::pe_disabled);
	pack.set_int(settings_pack::in_enc_policy, settings_pack::pe_disabled);

	pack.set_bool(settings_pack::allow_multiple_connections_per_ip, false);

	// TODO: these settings_pack tests belong in their own test
	pack.set_int(settings_pack::unchoke_slots_limit, 0);
	ses1.apply_settings(pack);
	TEST_CHECK(ses1.get_settings().get_int(settings_pack::unchoke_slots_limit) == 0);

	pack.set_int(settings_pack::unchoke_slots_limit, -1);
	ses1.apply_settings(pack);
	TEST_CHECK(ses1.get_settings().get_int(settings_pack::unchoke_slots_limit) == -1);

	pack.set_int(settings_pack::unchoke_slots_limit, 8);
	ses1.apply_settings(pack);
	TEST_CHECK(ses1.get_settings().get_int(settings_pack::unchoke_slots_limit) == 8);

	ses2.apply_settings(pack);

	torrent_handle tor1;
	torrent_handle tor2;

	create_directory("tmp1_transfer", ec);
	std::ofstream file("tmp1_transfer/temporary");
	std::shared_ptr<torrent_info> t = ::create_torrent(&file, "temporary", 32 * 1024, 13, false);
	file.close();

	TEST_CHECK(exists(combine_path("tmp1_transfer", "temporary")));

	add_torrent_params params;
	params.storage_mode = storage_mode;
	params.flags &= ~torrent_flags::paused;
	params.flags &= ~torrent_flags::auto_managed;

	wait_for_listen(ses1, "ses1");
	wait_for_listen(ses2, "ses2");

	peer_disconnects = 0;

	// test using piece sizes smaller than 16kB
	std::tie(tor1, tor2, ignore) = setup_transfer(&ses1, &ses2, nullptr
		, true, false, true, "_transfer", 1024 * 1024, &t, false, &params);

	int num_pieces = tor2.torrent_file()->num_pieces();
	std::vector<int> priorities(std::size_t(num_pieces), 1);

	lt::time_point const start_time = lt::clock_type::now();

	static char const* state_str[] =
		{"checking (q)", "checking", "dl metadata"
		, "downloading", "finished", "seeding", "allocating", "checking (r)"};

	for (int i = 0; i < 20000; ++i)
	{
		if (lt::clock_type::now() - start_time > seconds(10))
		{
			std::cout << "timeout\n";
			break;
		}
		// sleep a bit
		ses2.wait_for_alert(lt::milliseconds(100));

		torrent_status const st1 = tor1.status();
		torrent_status const st2 = tor2.status();

		print_alerts(ses1, "ses1", true, true, &on_alert);
		print_alerts(ses2, "ses2", true, true, &on_alert);

		if (i % 10 == 0)
		{
			print_ses_rate(i / 10.f, &st1, &st2);
		}

		std::cout << "st1-progress: " << (st1.progress * 100.f) << "% state: " << state_str[st1.state] << "\n";
		std::cout << "st2-progress: " << (st2.progress * 100.f) << "% state: " << state_str[st2.state] << "\n";
		if ((flags & move_storage) && st2.progress > 0.1f)
		{
			flags &= ~move_storage;
			tor1.move_storage("tmp1_transfer_moved");
			tor2.move_storage("tmp2_transfer_moved");
			std::cout << "moving storage" << std::endl;
		}

		if ((flags & delete_files) && st2.progress > 0.1f)
		{
			ses1.remove_torrent(tor1, session::delete_files);
			std::cout << "deleting files" << std::endl;

			std::this_thread::sleep_for(lt::seconds(1));
			break;
		}

		// wait 10 loops before we restart the torrent. This lets
		// us catch all events that failed (and would put the torrent
		// back into upload mode) before we restart it.

		if (st2.is_seeding) break;

		TEST_CHECK(st1.state == torrent_status::seeding
			|| st1.state == torrent_status::checking_files
			|| st1.state == torrent_status::checking_resume_data);
		TEST_CHECK(st2.state == torrent_status::downloading
			|| st2.state == torrent_status::checking_resume_data);

		if (peer_disconnects >= 2) break;

		std::this_thread::sleep_for(lt::milliseconds(100));
	}

	if (!(flags & delete_files))
	{
		TEST_CHECK(tor2.status().is_seeding);
	}

	// this allows shutting down the sessions in parallel
	p1 = ses1.abort();
	p2 = ses2.abort();

	if (proxy_type) stop_proxy(proxy_port);
}

void cleanup()
{
	error_code ec;
	remove_all("tmp1_transfer", ec);
	remove_all("tmp2_transfer", ec);
	remove_all("tmp1_transfer_moved", ec);
	remove_all("tmp2_transfer_moved", ec);
}

} // anonymous namespace

#if TORRENT_ABI_VERSION == 1
TORRENT_TEST(no_contiguous_buffers)
{
	using namespace lt;

	// test no contiguous_recv_buffers
	settings_pack p;
	p.set_bool(settings_pack::contiguous_recv_buffer, false);
	test_transfer(0, p);

	cleanup();
}
#endif

	// test with all kinds of proxies
TORRENT_TEST(socks5_pw)
{
	using namespace lt;
	test_transfer(settings_pack::socks5_pw, settings_pack());
	cleanup();
}

TORRENT_TEST(http)
{
	using namespace lt;
	test_transfer(settings_pack::http, settings_pack());
	cleanup();
}

TORRENT_TEST(http_pw)
{
	using namespace lt;
	test_transfer(settings_pack::http_pw, settings_pack());
	cleanup();
}
/*
TORRENT_TEST(i2p)
{
	using namespace lt;
	test_transfer(settings_pack::i2p_proxy, settings_pack());
	cleanup();
}
*/
TORRENT_TEST(move_storage)
{
	using namespace lt;
	test_transfer(0, settings_pack(), move_storage);
	cleanup();
}

TORRENT_TEST(delete_files)
{
	using namespace lt;
	settings_pack p = settings_pack();
	p.set_int(settings_pack::aio_threads, 10);
	test_transfer(0, p, delete_files);
	cleanup();
}

TORRENT_TEST(allow_fast)
{
	using namespace lt;
	// test allowed fast
	settings_pack p;
	p.set_int(settings_pack::allowed_fast_set_size, 2000);
	test_transfer(0, p);

	cleanup();
}

TORRENT_TEST(allocate)
{
	using namespace lt;
	// test storage_mode_allocate
	std::printf("full allocation mode\n");
	test_transfer(0, settings_pack(), {}, storage_mode_allocate);

	cleanup();
}

TORRENT_TEST(suggest)
{
	using namespace lt;
	settings_pack p;
	p.set_int(settings_pack::suggest_mode, settings_pack::suggest_read_cache);
	test_transfer(0, p);

	cleanup();
}

TORRENT_TEST(disable_os_cache)
{
	using namespace lt;
	settings_pack p;
	p.set_int(settings_pack::disk_io_write_mode, settings_pack::disable_os_cache);
	test_transfer(0, p, {}, storage_mode_allocate);

	cleanup();
}


TORRENT_TEST(write_through)
{
	using namespace lt;
	settings_pack p;
	p.set_int(settings_pack::disk_io_write_mode, settings_pack::write_through);
	test_transfer(0, p, {}, storage_mode_allocate);

	cleanup();
}
