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
#include <boost/tuple/tuple.hpp>
#include <boost/bind.hpp>

#include "test.hpp"
#include "setup_transfer.hpp"
#include <fstream>
#include <iostream>

using namespace libtorrent;
using boost::tuples::ignore;

int const alert_mask = alert::all_categories
& ~alert::progress_notification
& ~alert::stats_notification;

int peer_disconnects = 0;

int tracker_responses = 0;

bool on_alert(alert* a)
{
	if (alert_cast<tracker_reply_alert>(a))
		++tracker_responses;
	else if (alert_cast<peer_disconnected_alert>(a))
		++peer_disconnects;
	else if (alert_cast<peer_error_alert>(a))
		++peer_disconnects;

	return false;
}

// simulate a full disk
struct test_storage : storage_interface
{
	test_storage(file_storage const& fs, std::string const& p, file_pool& fp)
		: m_lower_layer(default_storage_constructor(fs, 0, p, fp, std::vector<boost::uint8_t>()))
  		, m_written(0)
		, m_limit(16 * 1024 * 2)
	{}
	virtual void set_file_priority(std::vector<boost::uint8_t> const& p) {}

	virtual bool initialize(bool allocate_files)
	{ return m_lower_layer->initialize(allocate_files); }

	virtual bool has_any_file()
	{ return m_lower_layer->has_any_file(); }

	virtual int readv(file::iovec_t const* bufs, int slot, int offset, int num_bufs, int flags)
	{ return m_lower_layer->readv(bufs, slot, offset, num_bufs, flags); }

	virtual int writev(file::iovec_t const* bufs, int slot, int offset, int num_bufs, int flags)
	{
		int ret = m_lower_layer->writev(bufs, slot, offset, num_bufs, flags);
		if (ret > 0) m_written += ret;
		if (m_written > m_limit)
		{
#if BOOST_VERSION == 103500
			set_error("", error_code(boost::system::posix_error::no_space_on_device, get_posix_category()));
#elif BOOST_VERSION > 103500
			set_error("", error_code(boost::system::errc::no_space_on_device, get_posix_category()));
#else
			set_error("", error_code(ENOSPC, get_posix_category()));
#endif
			return -1;
		}
		return ret;
	}

	virtual size_type physical_offset(int piece_index, int offset)
	{ return m_lower_layer->physical_offset(piece_index, offset); }

	virtual int read(char* buf, int slot, int offset, int size)
	{ return m_lower_layer->read(buf, slot, offset, size); }

	virtual int write(const char* buf, int slot, int offset, int size)
	{
		int ret = m_lower_layer->write(buf, slot, offset, size);
		if (ret > 0) m_written += ret;
		if (m_written > m_limit)
		{
#if BOOST_VERSION == 103500
			set_error("", error_code(boost::system::posix_error::no_space_on_device, get_posix_category()));
#elif BOOST_VERSION > 103500
			set_error("", error_code(boost::system::errc::no_space_on_device, get_posix_category()));
#else
			set_error("", error_code(ENOSPC, get_posix_category()));
#endif
			return -1;
		}
		return ret;
	}

	virtual int sparse_end(int start) const
	{ return m_lower_layer->sparse_end(start); }

	virtual int move_storage(std::string const& save_path, int flags)
	{ return m_lower_layer->move_storage(save_path, flags); }

	virtual bool verify_resume_data(lazy_entry const& rd, error_code& error)
	{ return m_lower_layer->verify_resume_data(rd, error); }

	virtual bool write_resume_data(entry& rd) const
	{ return m_lower_layer->write_resume_data(rd); }

	virtual bool move_slot(int src_slot, int dst_slot)
	{ return m_lower_layer->move_slot(src_slot, dst_slot); }

	virtual bool swap_slots(int slot1, int slot2)
	{ return m_lower_layer->swap_slots(slot1, slot2); }

	virtual bool swap_slots3(int slot1, int slot2, int slot3)
	{ return m_lower_layer->swap_slots3(slot1, slot2, slot3); }

	virtual bool release_files() { return m_lower_layer->release_files(); }

	virtual bool rename_file(int index, std::string const& new_filename)
	{ return m_lower_layer->rename_file(index, new_filename); }

	virtual bool delete_files() { return m_lower_layer->delete_files(); }

	virtual ~test_storage() {}

	boost::scoped_ptr<storage_interface> m_lower_layer;
	int m_written;
	int m_limit;
};

storage_interface* test_storage_constructor(file_storage const& fs
	, file_storage const*, std::string const& path, file_pool& fp, std::vector<boost::uint8_t> const&)
{
	return new test_storage(fs, path, fp);
}

void test_transfer(int proxy_type, bool test_disk_full = false
	, bool test_allowed_fast = false
	, storage_mode_t storage_mode = storage_mode_sparse)
{
	static int listen_port = 0;

	char const* test_name[] = {"no", "SOCKS4", "SOCKS5", "SOCKS5 password", "HTTP", "HTTP password"};

	fprintf(stderr, "\n\n  ==== TESTING %s proxy ==== disk-full: %s allow-fast: %s\n\n\n"
		, test_name[proxy_type], test_disk_full ? "true": "false", test_allowed_fast ? "true" : "false");

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

	session ses1(fingerprint("LT", 0, 1, 0, 0)
		, std::make_pair(48075 + listen_port, 49000), "0.0.0.0", 0, alert_mask);
	session ses2(fingerprint("LT", 0, 1, 0, 0)
		, std::make_pair(49075 + listen_port, 50000), "0.0.0.0", 0, alert_mask);
	listen_port += 10;

	proxy_settings ps;
	if (proxy_type)
	{
		ps.port = start_proxy(proxy_type);
		ps.username = "testuser";
		ps.password = "testpass";
		ps.type = (proxy_settings::proxy_type)proxy_type;

		// test resetting the proxy in quick succession.
		// specifically the udp_socket connecting to a new
		// socks5 proxy while having one connection attempt
		// in progress.
		ps.hostname = "5.6.7.8";
		ses1.set_proxy(ps);

		ps.hostname = "127.0.0.1";
		ses1.set_proxy(ps);
		ses2.set_proxy(ps);
	}

	session_settings sett;
	sett.allow_multiple_connections_per_ip = false;
	sett.ignore_limits_on_local_network = false;

	if (test_allowed_fast)
	{
		sett.allowed_fast_set_size = 2000;
		sett.unchoke_slots_limit = 0;
	}

	sett.unchoke_slots_limit = 0;
	ses1.set_settings(sett);
	TEST_CHECK(ses1.settings().unchoke_slots_limit == 0);
	sett.unchoke_slots_limit = -1;
	ses1.set_settings(sett);
	TEST_CHECK(ses1.settings().unchoke_slots_limit == -1);
	sett.unchoke_slots_limit = 8;
	ses1.set_settings(sett);
	TEST_CHECK(ses1.settings().unchoke_slots_limit == 8);

	// we need a short reconnect time since we
	// finish the torrent and then restart it
	// immediately to complete the second half.
	// using a reconnect time > 0 will just add
	// to the time it will take to complete the test
	sett.min_reconnect_time = 0;
	sett.stop_tracker_timeout = 1;
	sett.announce_to_all_trackers = true;
	sett.announce_to_all_tiers = true;
	// make sure we announce to both http and udp trackers
	sett.prefer_udp_trackers = false;
	sett.enable_outgoing_utp = false;
	sett.enable_incoming_utp = false;

	ses1.set_settings(sett);
	ses2.set_settings(sett);

#ifndef TORRENT_DISABLE_ENCRYPTION
	pe_settings pes;
	pes.out_enc_policy = pe_settings::disabled;
	pes.in_enc_policy = pe_settings::disabled;
	ses1.set_pe_settings(pes);
	ses2.set_pe_settings(pes);
#endif

	torrent_handle tor1;
	torrent_handle tor2;

	create_directory("tmp1_transfer", ec);
	std::ofstream file("tmp1_transfer/temporary");
	boost::intrusive_ptr<torrent_info> t = ::create_torrent(&file, 16 * 1024, 13, false);
	file.close();

	TEST_CHECK(exists(combine_path("tmp1_transfer", "temporary")));

	add_torrent_params addp(&test_storage_constructor);
	addp.flags &= ~add_torrent_params::flag_paused;
	addp.flags &= ~add_torrent_params::flag_auto_managed;

	add_torrent_params params;
	params.storage_mode = storage_mode;

	wait_for_listen(ses1, "ses1");
	wait_for_listen(ses2, "ses1");

	peer_disconnects = 0;

	// test using piece sizes smaller than 16kB
	boost::tie(tor1, tor2, ignore) = setup_transfer(&ses1, &ses2, 0
		, true, false, true, "_transfer", 8 * 1024, &t, false, test_disk_full?&addp:&params);

	int num_pieces = tor2.torrent_file()->num_pieces();
	std::vector<int> priorities(num_pieces, 1);

	// also test to move the storage of the downloader and the uploader
	// to make sure it can handle switching paths
	bool test_move_storage = false;

	tracker_responses = 0;

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

		// TODO: 3 factor out the disk-full test into its own unit test
		if (test_disk_full && st2.upload_mode)
		{
			test_disk_full = false;
			((test_storage*)tor2.get_storage_impl())->m_limit = 16 * 1024 * 1024;

			// if we reset the upload mode too soon, there may be more disk
			// jobs failing right after, putting us back in upload mode. So,
			// give the disk some time to fail all disk jobs before resetting
			// upload mode to false
			test_sleep(500);

			// then we need to drain the alert queue, so the peer_disconnects
			// counter doesn't get incremented by old alerts
			print_alerts(ses1, "ses1", true, true, true, &on_alert);
			print_alerts(ses2, "ses2", true, true, true, &on_alert);

			tor2.set_upload_mode(false);
			TEST_CHECK(tor2.status().is_finished == false);
			TEST_EQUAL(peer_disconnects, 2);
			fprintf(stderr, "%s: discovered disk full mode. Raise limit and disable upload-mode\n", time_now_string());
			peer_disconnects = 0;
			test_sleep(100);
			continue;
		}

		if (!test_disk_full && st2.is_finished) break;

		if (st2.state != torrent_status::downloading)
		{
			static char const* state_str[] =
				{"checking (q)", "checking", "dl metadata"
				, "downloading", "finished", "seeding", "allocating", "checking (r)"};
			std::cerr << "st2 state: " << state_str[st2.state] << std::endl;
		}

		TEST_CHECK(st1.state == torrent_status::seeding
			|| st1.state == torrent_status::checking_files);
		TEST_CHECK(st2.state == torrent_status::downloading
			|| st2.state == torrent_status::checking_resume_data
			|| (test_disk_full && !st2.error.empty()));

		if (!test_disk_full && peer_disconnects >= 2) break;

		// if nothing is being transferred after 2 seconds, we're failing the test
//		if (!test_disk_full && st1.upload_payload_rate == 0 && i > 20) break;

		test_sleep(100);
	}

	TEST_CHECK(tor2.status().is_seeding);

	// this allows shutting down the sessions in parallel
	p1 = ses1.abort();
	p2 = ses2.abort();

	if (proxy_type) stop_proxy(ps.port);
}

int test_main()
{
	using namespace libtorrent;

	// test with all kinds of proxies
	for (int i = 0; i < 6; ++i)
		test_transfer(i);

	// test with a (simulated) full disk
	test_transfer(0, true, true);

	// test allowed fast
	test_transfer(0, false, true);

	// test storage_mode_allocate
	fprintf(stderr, "full allocation mode\n");
	test_transfer(0, false, false, storage_mode_allocate);

#ifndef TORRENT_NO_DEPRECATE
	fprintf(stderr, "compact mode\n");
	test_transfer(0, false, false, storage_mode_compact);
#endif

	error_code ec;
	remove_all("tmp1_transfer", ec);
	remove_all("tmp2_transfer", ec);
	remove_all("tmp1_transfer_moved", ec);
	remove_all("tmp2_transfer_moved", ec);

	return 0;
}

