/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/config.hpp"

#include "test.hpp"

#if TORRENT_USE_RTC

#include "libtorrent/session.hpp"
#include "libtorrent/session_params.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/aux_/torrent.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/aux_/path.hpp"

#include "setup_transfer.hpp"
#include "settings.hpp"
#include "test_utils.hpp"

#include <fstream>

using namespace lt;
using namespace std::chrono_literals;

// posts "fun" onto ses's network thread and runs it against the torrent's
// internal object. Mirrors post_torrent() in test_fast_extension.cpp.
// native_handle() is only safe to touch from libtorrent's own thread.
template <typename Fun>
static void post_torrent(lt::session& ses, torrent_handle const& th, Fun fun)
{
	auto const tor = th.native_handle();
	TEST_CHECK(tor);
	if (!tor) return;
	post(ses.native_handle()->get_context(), [tor, fun] { fun(*tor); });
}

// Stands in for a WebTorrent tracker: relays a single SDP offer/answer pair
// directly between the two torrents' rtc_signaling objects, in-process,
// with no signaling server involved. This exercises the full rtc_signaling
// / rtc_stream / peer_connection / piece-picker path exactly the way a real
// WebSocket tracker relay would, minus the wire protocol itself (which is
// covered separately by the websocket_tracker_connection parsing tests).
static void connect_rtc_peers(
	lt::session& ses1, torrent_handle const& tor1, lt::session& ses2, torrent_handle const& tor2)
{
	using aux::request_callback;
	using aux::rtc_answer;
	using aux::rtc_offer;

	// on_rtc_offer() asserts the receiving torrent already has its own
	// rtc_signaling object, so make sure tor2 has one before we hand it
	// tor1's offer. The offer this generates is never used.
	post_torrent(ses2, tor2, [](aux::torrent& t2) {
		static_cast<request_callback&>(t2).generate_rtc_offers(
			1, [](error_code const&, std::vector<rtc_offer> const&) {});
	});

	post_torrent(ses1, tor1, [&ses1, &ses2, tor1, tor2](aux::torrent& t1) {
		static_cast<request_callback&>(t1).generate_rtc_offers(1,
			[&ses1, &ses2, tor1, tor2](error_code const& ec, std::vector<rtc_offer> const& offers) {
				TEST_CHECK(!ec);
				TEST_CHECK(!offers.empty());
				if (ec || offers.empty()) return;

				rtc_offer offer = offers[0];
				offer.answer_callback = [&ses1, tor1](peer_id const&, rtc_answer const& answer) {
					post_torrent(ses1, tor1, [answer](aux::torrent& t1b) {
						static_cast<request_callback&>(t1b).on_rtc_answer(answer);
					});
				};

				post_torrent(ses2, tor2, [offer](aux::torrent& t2) {
					static_cast<request_callback&>(t2).on_rtc_offer(offer);
				});
			});
	});
}

// use_metadata_transfer selects one of the two bools setup_transfer() takes:
// false gives tor2 the full torrent_info up front, true adds it as a magnet
// (info hash only), requiring the ut_metadata extension to fetch it over the
// same WebRTC connection before the piece transfer can start.
static void test_webtorrent_transfer(bool const use_metadata_transfer)
{
	std::string const suffix = use_metadata_transfer ? "_webtorrent_metadata" : "_webtorrent";

	error_code ec;
	remove_all("tmp1" + suffix, ec);
	remove_all("tmp2" + suffix, ec);

	// declared before the sessions so they're destructed last, letting the
	// sessions shut down in parallel
	session_proxy p1;
	session_proxy p2;

	settings_pack pack = settings();
	session_params sp(pack);
	lt::session ses1(sp);
	lt::session ses2(sp);

	int const piece_size = 16 * 1024;

	create_directory("tmp1" + suffix, ec);
	std::ofstream file(combine_path("tmp1" + suffix, "temporary"));
	add_torrent_params atp = ::create_torrent(&file, "temporary", piece_size, 9, false);
	file.close();

	atp.flags &= ~torrent_flags::paused;
	atp.flags &= ~torrent_flags::auto_managed;

	torrent_handle tor1;
	torrent_handle tor2;
	std::tie(tor1, tor2, std::ignore) = setup_transfer(
		&ses1, &ses2, nullptr, true, use_metadata_transfer, false, suffix, piece_size, &atp);

	// connect the two torrents over WebRTC instead of TCP/uTP, using an
	// in-process stand-in for the tracker's signaling relay. This has to
	// happen before waiting on anything else: with use_metadata_transfer,
	// tor2 has no metadata yet and can only reach the "downloading" state
	// once a peer (the one we're about to connect) has supplied it.
	connect_rtc_peers(ses1, tor1, ses2, tor2);

	// setup_transfer() never calls connect_peer(), no tracker is configured
	// (create_torrent's add_tracker is false), and settings() disables
	// DHT/LSD/uPnP/NAT-PMP, so the two sessions have no way to learn about
	// each other except through connect_rtc_peers() above. That rules out a
	// TCP/uTP connection by construction, but verify it positively too: every
	// peer_connect_alert must report socket_type_t::rtc.
	int rtc_connects = 0;
	auto const check_socket_type = [&](alert const* a) {
		if (auto const* pc = alert_cast<peer_connect_alert>(a))
		{
			TEST_CHECK(pc->socket_type == socket_type_t::rtc);
			++rtc_connects;
		}
		return false;
	};

	auto const start_time = lt::clock_type::now();
	while (lt::clock_type::now() - start_time < 30s)
	{
		ses2.wait_for_alert(200ms);

		print_alerts(ses1, "ses1", false, false, check_socket_type);
		print_alerts(ses2, "ses2", false, false, check_socket_type);

		if (tor2.status().is_seeding) break;
	}

	TEST_CHECK(tor2.status().is_seeding);
	// one peer_connect_alert from each session's point of view
	TEST_CHECK(rtc_connects == 2);

	p1 = ses1.abort();
	p2 = ses2.abort();
}

TORRENT_TEST(webtorrent_transfer) { test_webtorrent_transfer(false); }

TORRENT_TEST(webtorrent_transfer_metadata) { test_webtorrent_transfer(true); }

#else

TORRENT_TEST(disabled) {}

#endif // TORRENT_USE_RTC
