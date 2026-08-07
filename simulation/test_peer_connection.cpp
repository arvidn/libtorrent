/*

Copyright (c) 2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <functional>

#include "libtorrent/session.hpp"
#include "libtorrent/session_params.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/disabled_disk_io.hpp"
#include "libtorrent/aux_/random.hpp"
#include "libtorrent/torrent_flags.hpp"
#include "libtorrent/create_torrent.hpp"
#include "settings.hpp"
#include "fake_peer.hpp"
#include "utils.hpp"
#include "test_utils.hpp"
#include "setup_transfer.hpp"
#include "create_torrent.hpp"
#include "disk_io.hpp"
#include "simulator/simulator.hpp"
#include "simulator/utils.hpp"
#include "simulator/queue.hpp"

template <typename PeerFun, typename TestFun>
void test_peer(lt::torrent_flags_t const flags
	, PeerFun&& peer_fun
	, TestFun&& test)
{
	sim::default_config cfg;
	sim::simulation sim{cfg};
	auto ios = std::make_unique<sim::asio::io_context>(sim, lt::make_address_v4("50.0.0.1"));
	lt::session_proxy zombie;

	lt::session_params sp;
	sp.settings = settings();
	sp.settings.set_int(lt::settings_pack::alert_mask, lt::alert_category::all & ~lt::alert_category::stats);
	if (!(flags & lt::torrent_flags::seed_mode))
		sp.disk_io_constructor = lt::disabled_disk_io_constructor;

	// create session
	std::shared_ptr<lt::session> ses = std::make_shared<lt::session>(sp, *ios);

	auto peer = std::make_unique<fake_peer>(sim, "60.0.0.1");

	// add torrent
	lt::add_torrent_params params
		= (flags & lt::torrent_flags::seed_mode)
		? ::create_torrent(0, true) : ::create_torrent(0, false);
	int const num_pieces = params.ti->num_pieces();
	params.flags &= ~lt::torrent_flags::auto_managed;
	params.flags &= ~lt::torrent_flags::paused;
	params.flags |= flags;
	lt::sha1_hash const info_hash = params.ti->info_hash();
	ses->async_add_torrent(std::move(params));

	lt::torrent_handle h;
	bool connected = false;
	print_alerts(*ses, [&](lt::session& ses, lt::alert const* a) {
		if (auto* at = lt::alert_cast<lt::add_torrent_alert>(a))
		{
			h = at->handle;

			TORRENT_ASSERT(!connected);
			peer->connect_to(ep("50.0.0.1", 6881), info_hash);
			peer_fun(*peer.get(), num_pieces);
			connected = true;
		}
		if (connected)
			test(a);
	});

	// set up a timer to fire later, to shut down
	sim::timer t2(sim, lt::seconds(700)
		, [&](boost::system::error_code const&)
	{
		// shut down
		zombie = ses->abort();
		ses.reset();
	});

	sim.run();
}

struct peer_errors
{
	void operator()(lt::alert const* a)
	{
		auto* pe = lt::alert_cast<lt::peer_error_alert>(a);
		if (!pe) return;
		alerts.push_back(pe->error);
	}

	std::vector<lt::error_code> alerts;
};

struct peer_disconnects
{
	void operator()(lt::alert const* a)
	{
		// when we're expecting an orderly disconnect, make sure we don't also
		// get a peer-error.
		TEST_CHECK(lt::alert_cast<lt::peer_error_alert>(a) == nullptr);

		auto* pd = lt::alert_cast<lt::peer_disconnected_alert>(a);
		if (!pd) return;
		alerts.push_back(pd->error);
	}

	std::vector<lt::error_code> alerts;
};

struct invalid_requests
{
	void operator()(lt::alert const* a)
	{
		// we don't expect a peer error
		TEST_CHECK(lt::alert_cast<lt::peer_error_alert>(a) == nullptr);

		auto* ir = lt::alert_cast<lt::invalid_request_alert>(a);
		if (!ir) return;
		alerts.push_back(ir->request);
	}

	std::vector<lt::peer_request> alerts;
};

using vec = std::vector<lt::error_code>;
using reqs = std::vector<lt::peer_request>;

TORRENT_TEST(alternate_have_all_have_none)
{
	peer_disconnects d;
	test_peer({}, [](fake_peer& p, int)
		{
			p.send_have_all();
			p.send_have_none();
			p.send_have_all();
			p.send_have_none();
		}
		, d);
	TEST_CHECK(d.alerts == vec{lt::errors::timed_out_inactivity});
}

TORRENT_TEST(alternate_have_all_have_none_seed)
{
	peer_disconnects d;
	test_peer(lt::torrent_flags::seed_mode, [](fake_peer& p, int)
		{
			p.send_have_all();
			p.send_have_none();
			p.send_have_all();
			p.send_have_none();
		}
		, d);
	TEST_CHECK(d.alerts == vec{lt::errors::upload_upload_connection});
}

TORRENT_TEST(bitfield_and_have_none)
{
	peer_disconnects d;
	test_peer({}, [](fake_peer& p, int const num_pieces)
		{
			std::vector<bool> bitfield(num_pieces, false);
			bitfield[lt::aux::random(num_pieces)] = true;
			p.send_bitfield(bitfield);
			p.send_have_none();
		}
		, d);
	TEST_CHECK(d.alerts == vec{lt::errors::timed_out_inactivity});
}

TORRENT_TEST(bitfield_and_have_all)
{
	peer_disconnects d;
	test_peer({}, [](fake_peer& p, int const num_pieces)
		{
			std::vector<bool> bitfield(num_pieces, false);
			bitfield[lt::aux::random(num_pieces)] = true;
			p.send_bitfield(bitfield);
			p.send_have_all();
		}
		, d);
	TEST_CHECK(d.alerts == vec{lt::errors::timed_out_inactivity});
}

TORRENT_TEST(full_bitfield_and_have_all)
{
	peer_disconnects d;
	test_peer({}, [](fake_peer& p, int const num_pieces)
		{
			std::vector<bool> bitfield(num_pieces, true);
			p.send_bitfield(bitfield);
			p.send_have_all();
		}
		, d);
	TEST_CHECK(d.alerts == vec{lt::errors::timed_out_inactivity});
}

TORRENT_TEST(full_bitfield_and_have_none)
{
	peer_disconnects d;
	test_peer({}, [](fake_peer& p, int const num_pieces)
		{
			std::vector<bool> bitfield(num_pieces, true);
			p.send_bitfield(bitfield);
			p.send_have_none();
		}
		, d);
	TEST_CHECK(d.alerts == vec{lt::errors::timed_out_inactivity});
}

TORRENT_TEST(invalid_request)
{
	invalid_requests e;
	test_peer({}, [](fake_peer& p, int const num_pieces)
		{
			p.send_interested();
			p.send_request(1_piece, 0);
		}
		, e);
	TEST_CHECK((e.alerts == reqs{lt::peer_request{1_piece, 0, lt::default_block_size}}));
}

TORRENT_TEST(large_message)
{
	peer_errors e;
	test_peer({}, [](fake_peer& p, int const num_pieces)
		{
			p.send_large_message();
		}
		, e);
	TEST_CHECK(e.alerts == vec{lt::errors::packet_too_large});
}

TORRENT_TEST(have_all_invalid_msg)
{
	peer_errors e;
	test_peer({}, [](fake_peer& p, int const num_pieces)
		{
			p.send_have_all();
			p.send_invalid_message();
		}
		, e);
	TEST_CHECK(e.alerts == vec{lt::errors::invalid_message});
}

TORRENT_TEST(invalid_message)
{
	peer_errors e;
	test_peer({}, [](fake_peer& p, int const num_pieces)
		{
			p.send_invalid_message();
		}
		, e);
	TEST_CHECK(e.alerts == vec{lt::errors::invalid_message});
}

TORRENT_TEST(short_bitfield)
{
	peer_errors e;
	test_peer({}, [](fake_peer& p, int const num_pieces)
		{
			std::vector<bool> bitfield(num_pieces - 1, true);
			p.send_bitfield(bitfield);
		}
		, e);
	TEST_CHECK(e.alerts == vec{lt::errors::invalid_bitfield_size});
}

TORRENT_TEST(long_bitfield)
{
	peer_errors e;
	test_peer({}, [](fake_peer& p, int const num_pieces)
		{
			std::vector<bool> bitfield(num_pieces + 9, true);
			p.send_bitfield(bitfield);
		}
		, e);
	TEST_CHECK(e.alerts == vec{lt::errors::invalid_bitfield_size});
}

// verify that a peer_disconnected_alert is posted even when the connection
// is torn down before being associated with a torrent (i.e. before the
// handshake completes successfully). Here the peer sends an info-hash that
// doesn't match any torrent in the session.
TORRENT_TEST(disconnect_unknown_info_hash)
{
	sim::default_config cfg;
	sim::simulation sim{cfg};
	auto ios = std::make_unique<sim::asio::io_context>(sim, lt::make_address_v4("50.0.0.1"));
	lt::session_proxy zombie;

	lt::session_params sp;
	sp.settings = settings();
	sp.settings.set_int(lt::settings_pack::alert_mask
		, lt::alert_category::all & ~lt::alert_category::stats);
	sp.disk_io_constructor = lt::disabled_disk_io_constructor;

	std::shared_ptr<lt::session> ses = std::make_shared<lt::session>(sp, *ios);

	auto peer = std::make_unique<fake_peer>(sim, "60.0.0.1");

	// add a torrent so we can use add_torrent_alert to know the session is up
	lt::add_torrent_params params = ::create_torrent(0, false);
	params.flags &= ~lt::torrent_flags::auto_managed;
	params.flags &= ~lt::torrent_flags::paused;
	ses->async_add_torrent(std::move(params));

	peer_disconnects d;
	print_alerts(*ses, [&](lt::session&, lt::alert const* a)
	{
		if (lt::alert_cast<lt::add_torrent_alert>(a))
		{
			// connect with an info-hash that doesn't match any torrent, so
			// the connection is rejected before being attached to a torrent.
			peer->connect_to(ep("50.0.0.1", 6881)
				, lt::sha1_hash("01010101010101010101"));
		}
		d(a);
	});

	sim::timer t2(sim, lt::seconds(60)
		, [&](boost::system::error_code const&)
	{
		zombie = ses->abort();
		ses.reset();
	});

	sim.run();

	TEST_CHECK(d.alerts == vec{lt::errors::invalid_info_hash});
}

// Regression test for the seed_mode + force_recheck() race in
// torrent::force_recheck()/hash_job_completed(). While a deferred
// force_recheck() (m_pending_force_recheck) waits for outstanding hash
// jobs to drain, m_files_checked must stay false for the whole wait, not
// just once the recheck itself starts, since are_files_checked() is what
// gates new block requests (request_blocks.cpp). Otherwise a
// newly-connecting peer could complete a piece via verify_piece() while
// an on-demand seed-mode hash job (verifying()) is still outstanding for
// it, colliding at the disk_cache layer.
//
// Uses test_disk (deterministic, FIFO-ordered async_hash(), see
// disk_io.hpp/.cpp) instead of TORRENT_SIMULATE_SLOW_HASH, so dispatch
// order alone makes the race deterministic.
TORRENT_TEST(seed_mode_recheck_defer_blocks_new_downloads)
{
	sim::default_config cfg;
	sim::simulation sim{cfg};
	auto ios_seed = std::make_unique<sim::asio::io_context>(sim, lt::make_address_v4("50.0.0.1"));
	auto ios_peer3 = std::make_unique<sim::asio::io_context>(sim, lt::make_address_v4("50.0.0.2"));
	lt::session_proxy zombie_seed;
	lt::session_proxy zombie_peer3;

	lt::piece_index_t const bad_piece(0);
	lt::piece_index_t const good_piece(1);

	lt::add_torrent_params atp =
		::create_test_torrent(lt::default_block_size, 4, lt::create_torrent::v1_only);
	atp.save_path = ".";
	atp.flags &= ~lt::torrent_flags::auto_managed;
	atp.flags &= ~lt::torrent_flags::paused;
	lt::sha1_hash const info_hash = atp.ti->info_hash();

	// the seed under test: seed_mode, with "bad_piece" corrupted so its
	// on-demand check fails once requested.
	lt::session_params sp_seed;
	sp_seed.settings = settings();
	sp_seed.settings.set_int(
		lt::settings_pack::alert_mask, lt::alert_category::all & ~lt::alert_category::stats);
	sp_seed.settings.set_str(lt::settings_pack::listen_interfaces, "50.0.0.1:6881");
	// disk_seek() treats sequential access as free (0 delay), and piece 0,
	// being the very first disk operation on a fresh test_disk_io instance,
	// always lands exactly on the expected next offset. Use hash_time
	// instead, which is added per block unconditionally, to reliably widen
	// the window regardless of that optimization.
	test_disk seed_disk = test_disk().set_seed(true).corrupt_piece(bad_piece);
	// wide enough to comfortably outlast torrent::connect_peer()'s normal
	// per-tick outgoing-connection scheduling latency (observed to take a
	// few real seconds), so peer3's connection has time to land and be
	// evaluated for requests while the wait is still genuinely ongoing.
	seed_disk.hash_time = lt::seconds(5);
	sp_seed.disk_io_constructor = seed_disk;
	auto ses_seed = std::make_shared<lt::session>(sp_seed, *ios_seed);

	// peer3: an ordinary, fully-seeded peer with the *correct* data for
	// every piece, including "good_piece". Connected mid-wait, to see
	// whether the seed illegitimately downloads "good_piece" from it before
	// that piece's own on-demand job has completed.
	lt::session_params sp_peer3;
	sp_peer3.settings = settings();
	sp_peer3.settings.set_int(
		lt::settings_pack::alert_mask, lt::alert_category::all & ~lt::alert_category::stats);
	sp_peer3.settings.set_str(lt::settings_pack::listen_interfaces, "50.0.0.2:6881");
	sp_peer3.disk_io_constructor = test_disk().set_seed(true);
	auto ses_peer3 = std::make_shared<lt::session>(sp_peer3, *ios_peer3);

	lt::add_torrent_params atp_seed = atp;
	atp_seed.flags |= lt::torrent_flags::seed_mode;
	ses_seed->async_add_torrent(atp_seed);
	ses_peer3->async_add_torrent(atp);

	auto const peer1 = std::make_unique<fake_peer>(sim, "60.0.0.1");
	auto const peer2 = std::make_unique<fake_peer>(sim, "60.0.0.2");

	lt::torrent_handle seed_handle;
	bool hash_failed_seen = false;
	bool recheck_started = false;
	bool good_piece_finished_before_recheck = false;

	print_alerts(*ses_seed, [&](lt::session&, lt::alert const* a) {
		if (auto* at = lt::alert_cast<lt::add_torrent_alert>(a))
		{
			seed_handle = at->handle;
			// request the corrupt piece first, so its hash job is queued
			// (and thus resolves) ahead of the good piece's.
			peer1->connect_to(ep("50.0.0.1", 6881), info_hash);
			peer1->send_interested();
			peer1->send_request(bad_piece, 0);
		}
		if (auto* hf = lt::alert_cast<lt::hash_failed_alert>(a))
		{
			if (hf->piece_index == bad_piece)
			{
				hash_failed_seen = true;
				// force_recheck() should now be deferred, waiting for
				// good_piece's on-demand job (still outstanding: it was
				// only just requested, and test_disk serializes hash jobs
				// FIFO). Connect a new peer offering every piece, including
				// good_piece, right now. Connecting any earlier would just
				// get torn down immediately by force_recheck()'s own
				// disconnect_all() (which runs regardless of whether it
				// ends up deferring); torrent::connect_peer() is also
				// subject to the normal per-tick outgoing-connection
				// schedule rather than connecting instantly, which is why
				// the wait below is generous.
				seed_handle.connect_peer(ep("50.0.0.2", 6881));
			}
		}
		if (auto* sc = lt::alert_cast<lt::state_changed_alert>(a))
		{
			// this is when the *deferred* force_recheck() actually runs (the
			// wait is over): the earlier, immediate checking_resume_data
			// transition inside leave_seed_mode()/force_recheck() itself
			// only happens once m_num_outstanding_hash_jobs has drained, so
			// this can only fire once good_piece's own on-demand job has
			// resolved (whether normally, or via an illegitimate download
			// from peer3).
			if (sc->state == lt::torrent_status::checking_files
				|| sc->state == lt::torrent_status::checking_resume_data)
				recheck_started = true;
		}
		if (auto* pf = lt::alert_cast<lt::piece_finished_alert>(a))
		{
			// piece_finished_alert legitimately fires for good_piece once
			// the recheck (started above) gets to it, that's expected.
			// It firing *before* the recheck starts can only mean it was
			// (re-)downloaded from peer3 while force_recheck() was still
			// supposed to be deferred.
			if (pf->piece_index == good_piece && !recheck_started)
				good_piece_finished_before_recheck = true;
		}
	});

	// request the good piece shortly after the corrupt one, so both hash
	// jobs are outstanding together, but strictly after piece 0's job so it
	// resolves (and fails) first.
	sim::timer t1(sim, lt::milliseconds(50), [&](boost::system::error_code const&) {
		peer2->connect_to(ep("50.0.0.1", 6881), info_hash);
		peer2->send_interested();
		peer2->send_request(good_piece, 0);
	});

	sim::timer t2(sim, lt::seconds(90), [&](boost::system::error_code const&) {
		zombie_seed = ses_seed->abort();
		zombie_peer3 = ses_peer3->abort();
		ses_seed.reset();
		ses_peer3.reset();
	});

	sim.run();

	TEST_CHECK(hash_failed_seen);
	TEST_CHECK(recheck_started);
	TEST_CHECK(!good_piece_finished_before_recheck);
}
