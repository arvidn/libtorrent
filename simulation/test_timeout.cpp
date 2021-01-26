/*

Copyright (c) 2021, Arvid Norberg
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
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/deadline_timer.hpp"
#include "libtorrent/disabled_disk_io.hpp"
#include "settings.hpp"
#include "fake_peer.hpp"
#include "utils.hpp"
#include "setup_transfer.hpp"
#include "create_torrent.hpp"
#include "simulator/simulator.hpp"
#include "simulator/utils.hpp"
#include "simulator/queue.hpp"

using namespace sim;
using namespace lt;

using disconnects_t = std::vector<std::pair<lt::seconds, lt::error_code>>;

disconnects_t test_timeout(sim::configuration& cfg)
{
	auto const start_time = lt::clock_type::now();
	sim::simulation sim{cfg};
	std::unique_ptr<sim::asio::io_context> ios = make_io_context(sim, 0);
	lt::session_proxy zombie;

	// setup settings pack to use for the session (customization point)
	lt::session_params sp;
	sp.settings = settings();
	sp.settings.set_int(settings_pack::alert_mask, alert_category::all & ~alert_category::stats);
	sp.settings.set_bool(settings_pack::disable_hash_checks, true);
	sp.disk_io_constructor = lt::disabled_disk_io_constructor;

	// create session
	std::shared_ptr<lt::session> ses = std::make_shared<lt::session>(sp, *ios);

	fake_peer p1(sim, "60.0.0.0");

	// add torrent
	lt::add_torrent_params params = ::create_torrent(0, false);
	params.flags &= ~lt::torrent_flags::auto_managed;
	params.flags &= ~lt::torrent_flags::paused;
	params.flags |= lt::torrent_flags::seed_mode;
	lt::sha1_hash info_hash = params.ti->info_hash();
	ses->async_add_torrent(std::move(params));

	disconnects_t disconnects;

	lt::torrent_handle h;
	print_alerts(*ses, [&](lt::session& ses, lt::alert const* a) {
		if (auto* at = lt::alert_cast<add_torrent_alert>(a))
		{
			h = at->handle;

			p1.connect_to(ep("50.0.0.1", 6881), info_hash);
			p1.send_interested();
			p1.send_request(piece_index_t{0}, 0);
		}
		else if (auto* pd = lt::alert_cast<peer_disconnected_alert>(a))
		{
			disconnects.emplace_back(duration_cast<lt::seconds>(pd->timestamp() - start_time), pd->error);
		}
	});

	// set up a timer to fire later, to shut down
	sim::timer t2(sim, lt::seconds(400)
		, [&](boost::system::error_code const&)
	{
		// shut down
		zombie = ses->abort();
		ses.reset();
	});

	sim.run();

	return disconnects;
}

// the inactive timeout is 60 seconds. If we don't receive a request from a peer
// that's interested in us for 60 seconds, we disconnect them.
TORRENT_TEST(inactive_timeout)
{
	sim::default_config network_cfg;
	auto disconnects = test_timeout(network_cfg);
	TEST_CHECK((disconnects == disconnects_t{{lt::seconds{60}, lt::errors::timed_out_no_request}}));
}

struct slow_upload : sim::default_config
{
	sim::route outgoing_route(asio::ip::address ip) override
	{
	// only affect the libtorrent instance, not the fake peer
		if (ip != addr("50.0.0.1")) return sim::default_config::outgoing_route(ip);

		int const rate = 1;

		using duration = sim::chrono::high_resolution_clock::duration;

		auto it = m_outgoing.find(ip);
		if (it != m_outgoing.end()) return sim::route().append(it->second);
		it = m_outgoing.insert(it, std::make_pair(ip, std::make_shared<queue>(
			std::ref(m_sim->get_io_context())
			, rate * 1000
			, lt::duration_cast<duration>(milliseconds(rate / 2))
			, 200 * 1000, "slow upload rate")));
		return sim::route().append(it->second);
	}
};

// if the upload capacity is so low, that we're still trying to respond to the
// last request, we don't trugger the inactivity timeout, we don't expect the
// other peer to keep requesting more pieces before receiving the previous ones
TORRENT_TEST(inactive_timeout_slow_upload)
{
	slow_upload cfg;
	auto disconnects = test_timeout(cfg);
	TEST_CHECK((disconnects == disconnects_t{{lt::seconds{73}, lt::errors::timed_out_no_request}}));
}

