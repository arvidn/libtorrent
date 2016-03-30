/*

Copyright (c) 2008, Arvid Norberg
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
#include "libtorrent/hasher.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/thread.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/file.hpp"
#include "libtorrent/torrent_info.hpp"

#include "test.hpp"
#include "setup_transfer.hpp"
#include "test_utils.hpp"

#include <boost/tuple/tuple.hpp>
#include <boost/bind.hpp>

#include <fstream>
#include <iostream>

using namespace libtorrent;
namespace lt = libtorrent;

using boost::tuples::ignore;

const int mask = alert::all_categories & ~(alert::performance_warning | alert::stats_notification);

int peer_disconnects = 0;

bool on_alert(alert const* a)
{
	if (alert_cast<peer_disconnected_alert>(a))
		++peer_disconnects;
	else if (alert_cast<peer_error_alert>(a))
		++peer_disconnects;

	return false;
}

// simulate a full disk
struct test_storage : default_storage
{
	test_storage(storage_params const& params)
		: default_storage(params)
		, m_written(0)
		, m_limit(16 * 1024 * 2)
	{}

	virtual void set_file_priority(std::vector<boost::uint8_t> const& p
		, storage_error& ec) {}

	void set_limit(int lim)
	{
		mutex::scoped_lock l(m_mutex);
		m_limit = lim;
	}

	int writev(
		file::iovec_t const* bufs
		, int num_bufs
		, int piece_index
		, int offset
		, int flags
		, storage_error& se)
	{
		mutex::scoped_lock l(m_mutex);
		if (m_written >= m_limit)
		{
			std::cerr << "storage written: " << m_written << " limit: " << m_limit << std::endl;
			error_code ec;
			ec = error_code(boost::system::errc::no_space_on_device, generic_category());
			se.ec = ec;
			return 0;
		}

		for (int i = 0; i < num_bufs; ++i)
			m_written += bufs[i].iov_len;
		l.unlock();
		return default_storage::writev(bufs, num_bufs, piece_index, offset, flags, se);
	}

	virtual ~test_storage() {}

	int m_written;
	int m_limit;
	mutex m_mutex;
};

storage_interface* test_storage_constructor(storage_params const& params)
{
	return new test_storage(params);
}

void test_transfer(int proxy_type, settings_pack const& sett
	, bool test_disk_full = false
	, storage_mode_t storage_mode = storage_mode_sparse)
{
	char const* test_name[] = {"no", "SOCKS4", "SOCKS5", "SOCKS5 password", "HTTP", "HTTP password"};

	fprintf(stderr, "\n\n  ==== TESTING %s proxy ==== disk-full: %s\n\n\n"
		, test_name[proxy_type], test_disk_full ? "true": "false");

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

	settings_pack pack;
	pack.set_str(settings_pack::listen_interfaces, "0.0.0.0:48075");
	pack.set_int(settings_pack::alert_mask, mask);

	pack.set_bool(settings_pack::enable_upnp, false);
	pack.set_bool(settings_pack::enable_natpmp, false);
	pack.set_bool(settings_pack::enable_lsd, false);
	pack.set_bool(settings_pack::enable_dht, false);

	lt::session ses1(pack);

	pack.set_str(settings_pack::listen_interfaces, "0.0.0.0:49075");
	lt::session ses2(pack);

	int proxy_port = 0;
	if (proxy_type)
	{
		proxy_port = start_proxy(proxy_type);

		settings_pack pack;
		pack.set_str(settings_pack::proxy_username, "testuser");
		pack.set_str(settings_pack::proxy_password, "testpass");
		pack.set_int(settings_pack::proxy_type, proxy_type);
		pack.set_int(settings_pack::proxy_port, proxy_port);
		pack.set_bool(settings_pack::force_proxy, true);

		// test resetting the proxy in quick succession.
		// specifically the udp_socket connecting to a new
		// socks5 proxy while having one connection attempt
		// in progress.
		pack.set_str(settings_pack::proxy_hostname, "5.6.7.8");
		ses1.apply_settings(pack);
		pack.set_str(settings_pack::proxy_hostname, "127.0.0.1");
		ses1.apply_settings(pack);
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
	pack.set_int(settings_pack::alert_mask, mask);

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
	boost::shared_ptr<torrent_info> t = ::create_torrent(&file, "temporary", 32 * 1024, 13, false);
	file.close();

	TEST_CHECK(exists(combine_path("tmp1_transfer", "temporary")));

	add_torrent_params addp(&test_storage_constructor);
	addp.flags &= ~add_torrent_params::flag_paused;
	addp.flags &= ~add_torrent_params::flag_auto_managed;

	add_torrent_params params;
	params.storage_mode = storage_mode;
	params.flags &= ~add_torrent_params::flag_paused;
	params.flags &= ~add_torrent_params::flag_auto_managed;

	wait_for_listen(ses1, "ses1");
	wait_for_listen(ses2, "ses2");

	peer_disconnects = 0;

	// test using piece sizes smaller than 16kB
	boost::tie(tor1, tor2, ignore) = setup_transfer(&ses1, &ses2, 0
		, true, false, true, "_transfer", 8 * 1024, &t, false, test_disk_full?&addp:&params);

	int num_pieces = tor2.torrent_file()->num_pieces();
	std::vector<int> priorities(num_pieces, 1);

	// also test to move the storage of the downloader and the uploader
	// to make sure it can handle switching paths
	bool test_move_storage = false;

	int upload_mode_timer = 0;

	wait_for_downloading(ses2, "ses2");

	for (int i = 0; i < 200; ++i)
	{
		torrent_status st1 = tor1.status();
		torrent_status st2 = tor2.status();

		print_alerts(ses1, "ses1", true, true, true, &on_alert);
		print_alerts(ses2, "ses2", true, true, true, &on_alert);

		if (i % 10 == 0)
		{
			print_ses_rate(i / 10.f, &st1, &st2);
		}

		if (!test_move_storage && st2.progress > 0.25f)
		{
			test_move_storage = true;
			tor1.move_storage("tmp1_transfer_moved");
			tor2.move_storage("tmp2_transfer_moved");
			std::cerr << "moving storage" << std::endl;
		}

		// wait 10 loops before we restart the torrent. This lets
		// us catch all events that failed (and would put the torrent
		// back into upload mode) before we restart it.

		// TODO: factor out the disk-full test into its own unit test
		if (test_disk_full && st2.upload_mode && ++upload_mode_timer > 10)
		{
			test_disk_full = false;
			((test_storage*)tor2.get_storage_impl())->set_limit(16 * 1024 * 1024);

			// if we reset the upload mode too soon, there may be more disk
			// jobs failing right after, putting us back in upload mode. So,
			// give the disk some time to fail all disk jobs before resetting
			// upload mode to false
			test_sleep(500);

			// then we need to drain the alert queue, so the peer_disconnects
			// counter doesn't get incremented by old alerts
			print_alerts(ses1, "ses1", true, true, true, &on_alert);
			print_alerts(ses2, "ses2", true, true, true, &on_alert);

			lt::error_code err = tor2.status().errc;
			fprintf(stderr, "error: \"%s\"\n", err.message().c_str());
			TEST_CHECK(!err);
			tor2.set_upload_mode(false);

			// at this point we probably disconnected the seed
			// so we need to reconnect as well
			fprintf(stderr, "%s: reconnecting peer\n", time_now_string());
			error_code ec;
			tor2.connect_peer(tcp::endpoint(address::from_string("127.0.0.1", ec)
				, ses1.listen_port()));

			TEST_CHECK(tor2.status().is_finished == false);
			fprintf(stderr, "disconnects: %d\n", peer_disconnects);
			TEST_CHECK(peer_disconnects >= 2);
			fprintf(stderr, "%s: discovered disk full mode. Raise limit and disable upload-mode\n", time_now_string());
			peer_disconnects = 0;
			continue;
		}

		if (!test_disk_full && st2.is_seeding) break;

		TEST_CHECK(st1.state == torrent_status::seeding
			|| st1.state == torrent_status::checking_files);
		TEST_CHECK(st2.state == torrent_status::downloading
			|| st2.state == torrent_status::checking_resume_data
			|| (test_disk_full && st2.errc));

		if (!test_disk_full && peer_disconnects >= 2) break;

		// if nothing is being transferred after 2 seconds, we're failing the test
//		if (!test_disk_full && st1.upload_payload_rate == 0 && i > 20) break;

		test_sleep(100);
	}

	TEST_CHECK(tor2.status().is_seeding);

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

TORRENT_TEST(no_contiguous_buffers)
{
	using namespace libtorrent;

	// test no contiguous_recv_buffers
	settings_pack p;
	p.set_bool(settings_pack::contiguous_recv_buffer, false);
	test_transfer(0, p);

	cleanup();
}

	// test with all kinds of proxies
TORRENT_TEST(socks5_pw)
{
	using namespace libtorrent;
	test_transfer(settings_pack::socks5_pw, settings_pack());
	cleanup();
}

TORRENT_TEST(http)
{
	using namespace libtorrent;
	test_transfer(settings_pack::http, settings_pack());
	cleanup();
}

TORRENT_TEST(http_pw)
{
	using namespace libtorrent;
	test_transfer(settings_pack::http_pw, settings_pack());
	cleanup();
}
/*
TORRENT_TEST(i2p)
{
	using namespace libtorrent;
	test_transfer(settings_pack::i2p_proxy, settings_pack());
	cleanup();
}

// this test is too flaky. Move it to a sim
TORRENT_TEST(disk_full)
{
	using namespace libtorrent;
	// test with a (simulated) full disk
	test_transfer(0, settings_pack(), true);

	cleanup();
}
*/

TORRENT_TEST(allow_fast)
{
	using namespace libtorrent;
	// test allowed fast
	settings_pack p;
	p.set_int(settings_pack::allowed_fast_set_size, 2000);
	test_transfer(0, p, false);

	cleanup();
}

TORRENT_TEST(coalesce_reads)
{
	using namespace libtorrent;
	// test allowed fast
	settings_pack p;
	p.set_int(settings_pack::read_cache_line_size, 16);
	p.set_bool(settings_pack::coalesce_reads, true);
	test_transfer(0, p, false);

	cleanup();
}

TORRENT_TEST(coalesce_writes)
{
	using namespace libtorrent;
	// test allowed fast
	settings_pack p;
	p.set_bool(settings_pack::coalesce_writes, true);
	test_transfer(0, p, false);

	cleanup();
}


TORRENT_TEST(allocate)
{
	using namespace libtorrent;
	// test storage_mode_allocate
	fprintf(stderr, "full allocation mode\n");
	test_transfer(0, settings_pack(), false, storage_mode_allocate);

	cleanup();
}

