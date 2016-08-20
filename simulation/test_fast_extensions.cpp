/*

Copyright (c) 2016, Arvid Norberg
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

#include "test.hpp"
#include "utils.hpp"
#include "libtorrent/alert.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "create_torrent.hpp"
#include "settings.hpp"
#include "fake_peer.hpp"
#include "setup_transfer.hpp" // for ep()
#include "simulator/utils.hpp"

namespace lt = libtorrent;

template <typename Sett, typename Alert>
void run_fake_peer_test(
	lt::add_torrent_params params
	, Sett const& sett
	, Alert const& alert)
{
	sim::default_config cfg;
	sim::simulation sim{cfg};

	sim::asio::io_service ios(sim, lt::address_v4::from_string("50.0.0.1"));
	lt::session_proxy zombie;

	// setup settings pack to use for the session (customization point)
	lt::settings_pack pack = settings();
	pack.set_str(lt::settings_pack::listen_interfaces, "0.0.0.0:6881");
	sett(pack);
	// create session
	std::shared_ptr<lt::session> ses = std::make_shared<lt::session>(pack, ios);

	fake_peer p1(sim, "60.0.0.0");

	params.flags &= ~lt::add_torrent_params::flag_auto_managed;
	params.flags &= ~lt::add_torrent_params::flag_paused;
	ses->async_add_torrent(params);

	// the alert notification function is called from within libtorrent's
	// context. It's not OK to talk to libtorrent in there, post it back out and
	// then ask for alerts.
	print_alerts(*ses, [&](lt::session& ses, lt::alert const* a) {
		alert(ses, a, p1);
	});

	sim::timer t(sim, lt::seconds(1)
		, [&](boost::system::error_code const& ec)
	{
		// shut down
		zombie = ses->abort();

		p1.close();

		ses.reset();
	});

	sim.run();
}

// make sure we consistently send the same allow-fast pieces, regardless
// of which pieces the peer has.
TORRENT_TEST(allow_fast)
{
	std::set<int> allowed_fast;

	int const num_pieces = 50;
	lt::add_torrent_params params = create_torrent(0, false, num_pieces);
	std::vector<bool> bitfield(num_pieces, false);

	for (int i = 0; i < num_pieces + 1; ++i)
	{
		// just for this one session, to check for duplicates
		std::set<int> local_allowed_fast;

		run_fake_peer_test(params, [] (lt::settings_pack& pack) {
			pack.set_int(lt::settings_pack::allowed_fast_set_size, 13);
		}
		, [&] (lt::session& ses, lt::alert const* a, fake_peer& p1)
		{
			if (auto at = lt::alert_cast<lt::add_torrent_alert>(a))
			{
				lt::torrent_handle h = at->handle;
				p1.connect_to(ep("50.0.0.1", 6881)
					, h.torrent_file()->info_hash());
				p1.send_bitfield(bitfield);
				p1.send_interested();
			}
			else if (auto l = lt::alert_cast<lt::peer_log_alert>(a))
			{
				if (strcmp(l->event_type, "ALLOWED_FAST") != 0) return;

				int const piece = atoi(l->msg());
				// make sure we don't get the same allowed piece more than once
				TEST_EQUAL(local_allowed_fast.count(piece), 0);

				// build the union of all allow-fast pieces we've received, across
				// simulations.
				allowed_fast.insert(piece);
				local_allowed_fast.insert(piece);

				// make sure this is a valid piece
				TEST_CHECK(piece < num_pieces);
				TEST_CHECK(piece >= 0);
				// and make sure it's not one of the pieces we have
				// because that would be redundant
				TEST_EQUAL(bitfield[piece], false);
			}
		});

		// i goes from [0, mum_pieces + 1) to cover the have-none and have-all
		// cases. After the last iteration, we can't add another piece.
		if (i < int(bitfield.size()))
			bitfield[i] = true;
	}

	// we should never have sent any other pieces than the 13 designated for this
	// peer's IP.
	TEST_EQUAL(int(allowed_fast.size()), 13);
}

// This tests a worst case scenario of allow-fast configuration where we must
// verify that libtorrent correctly aborts before satisfying the settings
// (because doing so would be too expensive)
//
// we have a torrent with a lot of pieces, and we want to send that many minus
// one allow-fast pieces. The way allow-fast pieces are computed is by hashing
// the peer's IP modulus the number of pieces. To actually compute which pieces
// to send (or which one piece _not_ to send) we would have to work hard through
// a lot of duplicates. This test makes sure we don't, and abort well before
// then
TORRENT_TEST(allow_fast_stress)
{
	std::set<int> allowed_fast;

	int const num_pieces = 50000;
	lt::add_torrent_params params = create_torrent(0, false, num_pieces);

	run_fake_peer_test(params, [&] (lt::settings_pack& pack) {
		pack.set_int(lt::settings_pack::allowed_fast_set_size, num_pieces - 1);
	}
	, [&] (lt::session& ses, lt::alert const* a, fake_peer& p1)
	{
		if (auto at = lt::alert_cast<lt::add_torrent_alert>(a))
		{
			lt::torrent_handle h = at->handle;
			p1.connect_to(ep("50.0.0.1", 6881)
				, h.torrent_file()->info_hash());
			p1.send_interested();
		}
		else if (auto l = lt::alert_cast<lt::peer_log_alert>(a))
		{
			if (strcmp(l->event_type, "ALLOWED_FAST") != 0) return;

			int const piece = atoi(l->msg());

			// make sure we don't get the same allowed piece more than once
			TEST_EQUAL(allowed_fast.count(piece), 0);

			// build the union of all allow-fast pieces we've received, across
			// simulations.
			allowed_fast.insert(piece);

			// make sure this is a valid piece
			TEST_CHECK(piece < num_pieces);
			TEST_CHECK(piece >= 0);
		}
	});

	std::printf("received %d allowed fast, out of %d configured ones\n"
		, int(allowed_fast.size()), num_pieces - 1);
	TEST_CHECK(int(allowed_fast.size()) < num_pieces / 80);
}
