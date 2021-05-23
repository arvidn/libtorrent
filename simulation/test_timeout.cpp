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

#include <functional>

#include "libtorrent/session.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/deadline_timer.hpp"
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
	sim::simulation sim{cfg};
	auto const start_time = lt::clock_type::now();
	std::unique_ptr<sim::asio::io_service> ios = make_io_service(sim, 0);
	lt::session_proxy zombie;

	lt::session_params sp;
	sp.settings = settings();
	sp.settings.set_int(settings_pack::alert_mask, alert_category::all & ~alert_category::stats);
	sp.settings.set_bool(settings_pack::disable_hash_checks, true);

	// create session
	std::shared_ptr<lt::session> ses = std::make_shared<lt::session>(sp, *ios);

	fake_peer p1(sim, "60.0.0.0");

	// add torrent
	lt::add_torrent_params params = ::create_torrent(0, false);
	params.storage = disabled_storage_constructor;
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
TORRENT_TEST(no_request_timeout)
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
			std::ref(m_sim->get_io_service())
			, rate * 1000
			, lt::duration_cast<duration>(milliseconds(rate / 2))
			, 200 * 1000, "slow upload rate")));
		return sim::route().append(it->second);
	}
};

// if the upload capacity is so low, that we're still trying to respond to the
// last request, we don't trigger the inactivity timeout, we don't expect the
// other peer to keep requesting more pieces before receiving the previous ones
TORRENT_TEST(no_request_timeout_slow_upload)
{
	slow_upload cfg;
	auto disconnects = test_timeout(cfg);
	TEST_CHECK((disconnects == disconnects_t{{lt::seconds{73}, lt::errors::timed_out_no_request}}));
}

disconnects_t test_no_interest_timeout(int const num_peers
	, lt::session_params sp
	, bool const redundant_no_interest)
{
	sim::default_config cfg;
	sim::simulation sim{cfg};
	auto const start_time = lt::clock_type::now();
	std::unique_ptr<sim::asio::io_service> ios = make_io_service(sim, 0);
	lt::session_proxy zombie;

	sp.settings.set_int(settings_pack::alert_mask, alert_category::all & ~alert_category::stats);

	// create session
	std::shared_ptr<lt::session> ses = std::make_shared<lt::session>(sp, *ios);

	std::vector<std::unique_ptr<fake_peer>> peers;
	for (int i = 0; i < num_peers; ++i)
	{
		char ip[50];
		std::snprintf(ip, sizeof(ip), "60.0.0.%d", i + 1);
		peers.emplace_back(new fake_peer(sim, ip));
	}

	// add torrent
	lt::add_torrent_params params = ::create_torrent(0, false);
	params.storage = disabled_storage_constructor;
	params.flags &= ~lt::torrent_flags::auto_managed;
	params.flags &= ~lt::torrent_flags::paused;
	lt::sha1_hash info_hash = params.ti->info_hash();
	ses->async_add_torrent(std::move(params));

	disconnects_t disconnects;

	lt::torrent_handle h;
	print_alerts(*ses, [&](lt::session& ses, lt::alert const* a) {
		if (auto* at = lt::alert_cast<add_torrent_alert>(a))
		{
			h = at->handle;
			for (auto& p : peers)
				p->connect_to(ep("50.0.0.1", 6881), info_hash);
		}
		else if (auto* pd = lt::alert_cast<peer_disconnected_alert>(a))
		{
			disconnects.emplace_back(duration_cast<lt::seconds>(pd->timestamp() - start_time), pd->error);
		}
	});

	std::function<void(boost::system::error_code const&)> keep_alive
		= [&](boost::system::error_code const&)
	{
		for (auto& p : peers)
			p->send_keepalive();
	};

	std::function<void(boost::system::error_code const&)> send_not_interested
		= [&](boost::system::error_code const&)
	{
		for (auto& p : peers)
			p->send_not_interested();
	};

	auto const& tick = redundant_no_interest ? send_not_interested : keep_alive;

	sim::timer t3(sim, lt::seconds(100), tick);
	sim::timer t4(sim, lt::seconds(200), tick);
	sim::timer t5(sim, lt::seconds(300), tick);
	sim::timer t6(sim, lt::seconds(400), tick);
	sim::timer t7(sim, lt::seconds(500), tick);
	sim::timer t8(sim, lt::seconds(599), tick);

	// set up a timer to fire later, to shut down
	sim::timer t2(sim, lt::seconds(700)
		, [&](boost::system::error_code const&)
	{
		// shut down
		zombie = ses->abort();
		ses.reset();
	});

	sim.run();

	return disconnects;
}

// if a peer is not interested in us, and we're not interested in it for long
// enoguh, we disconenct it, but only if we are close to peer connection capacity
TORRENT_TEST(no_interest_timeout)
{
	// with 10 peers, we're close enough to the connection limit to enable
	// inactivity timeout

	lt::session_params sp;
	sp.settings = settings();
	sp.settings.set_int(settings_pack::connections_limit, 15);
	auto disconnects = test_no_interest_timeout(10, std::move(sp), false);
	TEST_EQUAL(disconnects.size(), 10);
	for (auto const& e : disconnects)
	{
		TEST_CHECK(e.first == lt::seconds{600});
		TEST_CHECK(e.second == lt::errors::timed_out_no_interest);
	}
}

TORRENT_TEST(no_interest_timeout_redundant_not_interested)
{
	// even though the peers keep sending not-interested, our clock should not
	// restart
	lt::session_params sp;
	sp.settings = settings();
	sp.settings.set_int(settings_pack::connections_limit, 15);
	auto disconnects = test_no_interest_timeout(10, std::move(sp), true);
	TEST_EQUAL(disconnects.size(), 10);
	for (auto const& e : disconnects)
	{
		TEST_CHECK(e.first == lt::seconds{600});
		TEST_CHECK(e.second == lt::errors::timed_out_no_interest);
	}
}

TORRENT_TEST(no_interest_timeout_zero)
{
	// if we set inactivity_timeout to 0, all peers should be disconnected
	// immediately
	lt::session_params sp;
	sp.settings = settings();
	sp.settings.set_int(settings_pack::connections_limit, 15);
	sp.settings.set_int(settings_pack::inactivity_timeout, 0);
	auto disconnects = test_no_interest_timeout(10, std::move(sp), false);
	TEST_EQUAL(disconnects.size(), 10);
	for (auto const& e : disconnects)
	{
		TEST_CHECK(e.first == lt::seconds{0});
		TEST_CHECK(e.second == lt::errors::timed_out_no_interest);
	}
}

TORRENT_TEST(no_interest_timeout_few_peers)
{
	// with a higher connections limit we're not close enough to enable
	// inactivity timeout
	lt::session_params sp;
	sp.settings = settings();
	sp.settings.set_int(settings_pack::connections_limit, 20);
	auto disconnects = test_no_interest_timeout(10, std::move(sp), false);
	TEST_CHECK(disconnects == disconnects_t{});
}
