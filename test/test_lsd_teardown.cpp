/*

Copyright (c) 2026, KSEGIT
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/session.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/magnet_uri.hpp"
#include "libtorrent/error_code.hpp"

#include "libtorrent/aux_/scope_end.hpp"

#include "test.hpp"
#include "test_utils.hpp" // for test_listen_interface()

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>

// Regression guard for the macOS shutdown freeze (qbittorrent/qBittorrent#24353)
// and the libtorrent session-destructor hang (#4510).
//
// With Local Service Discovery enabled and an active torrent, the single
// io_context thread periodically runs on_lsd_announce -> announce_lsd ->
// lsd::announce_impl. Historically that performed a *synchronous*, uncancellable
// multicast send_to(). When the outbound interface stalls (e.g. a VPN tunnel
// that vanishes across sleep/wake) the send blocks in-kernel forever, the io
// thread never returns to its run loop, abort() (dispatched onto that same
// thread) never runs, and session_proxy::~session_proxy()'s std::thread::join()
// deadlocks. Downstream this is the macOS "must force quit" freeze.
//
// This test pins the portable invariant the fix protects: with LSD active,
// aborting the session and destroying the session_proxy completes within a
// bounded time. The bound is deliberately generous (30s vs the few
// milliseconds a healthy teardown takes) so the test is not timing-flaky; a
// true deadlock blows well past it. Note: on a healthy CI interface a
// blocking send also returns quickly, so this test cannot simulate the
// stalled-interface trigger itself (nor does it exercise the uTP teardown
// path - no peer connections exist). On hosts where no interface can join
// the LSD multicast group the announce is skipped entirely and only the
// bounded-teardown invariant is exercised. The test guards that invariant
// and, via the watchdog below, makes any future teardown deadlock fail fast
// with attribution instead of hanging the test binary.
TORRENT_TEST(lsd_teardown_is_bounded)
{
	using namespace lt;
	using namespace std::chrono;

	// if the deadlock this test guards against regresses, the session_proxy
	// destructor below never returns and the TEST_CHECK at the end is never
	// reached. This watchdog converts that indefinite hang into a bounded,
	// clearly-attributed failure instead of an opaque CI job timeout.
	std::mutex done_mutex;
	std::condition_variable done_cv;
	bool done = false;
	std::thread watchdog([&] {
		std::unique_lock<std::mutex> l(done_mutex);
		if (done_cv.wait_for(l, seconds(90), [&] { return done; })) return;
		std::fprintf(stderr, "test_lsd_teardown: TIMEOUT - session teardown "
			"appears deadlocked (io thread not joinable)\n");
		std::abort();
	});
	// join via scope guard: if anything below throws, unwinding through a
	// joinable std::thread would call std::terminate
	auto join_watchdog = aux::scope_end([&] {
		{
			std::lock_guard<std::mutex> l(done_mutex);
			done = true;
		}
		done_cv.notify_one();
		watchdog.join();
	});

	steady_clock::time_point start;
	{
		// declared first so it is destroyed last (joins the io thread)
		session_proxy proxy;

		settings_pack pack;
		pack.set_int(settings_pack::alert_mask
			, alert_category::error | alert_category::status);
		pack.set_bool(settings_pack::enable_dht, false);
		pack.set_bool(settings_pack::enable_lsd, true);
		pack.set_bool(settings_pack::enable_upnp, false);
		pack.set_bool(settings_pack::enable_natpmp, false);
		pack.set_str(settings_pack::listen_interfaces, test_listen_interface());

		lt::session ses(pack);

		// add an (active) torrent so the LSD announce path actually fires
		error_code ec;
		add_torrent_params atp = parse_magnet_uri(
			"magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567", ec);
		TEST_CHECK(!ec);
		atp.save_path = ".";
		atp.flags &= ~torrent_flags::paused;
		torrent_handle th = ses.add_torrent(atp, ec);
		TEST_CHECK(!ec);

		// deterministically drive the LSD announce path rather than waiting
		// for the periodic announce timer
		th.force_lsd_announce();
		// give the io thread time to process the announce
		std::this_thread::sleep_for(milliseconds(500));

		start = steady_clock::now();
		proxy = ses.abort();
		// end of scope: ses is destroyed, then proxy's dtor joins the io thread
	}
	auto const elapsed = duration_cast<milliseconds>(steady_clock::now() - start);

	std::printf("LSD session teardown took %d ms\n", int(elapsed.count()));

	// If the io thread had wedged in a synchronous LSD send, the join above
	// would never return and we would never reach this line within the bound.
	TEST_CHECK(elapsed < milliseconds(30000));
}
