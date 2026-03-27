/*




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
#include "libtorrent/disabled_disk_io.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/torrent_flags.hpp"
#include "settings.hpp"
#include "fake_peer.hpp"
#include "utils.hpp"
#include "test_utils.hpp"
#include "setup_transfer.hpp"
#include "create_torrent.hpp"
#include "simulator/simulator.hpp"
#include "simulator/utils.hpp"

namespace {

struct peer_disconnects
{
	void operator()(lt::alert const* a)
	{
		auto* pd = lt::alert_cast<lt::peer_disconnected_alert>(a);
		if (!pd) return;
		alerts.push_back(pd->error);
	}

	std::vector<lt::error_code> alerts;
};

struct peer_connects
{
	void operator()(lt::alert const* a)
	{
		auto* pa = lt::alert_cast<lt::peer_connect_alert>(a);
		if (!pa) return;
		alerts.push_back(pa->endpoint);
	}

	std::vector<lt::tcp::endpoint> alerts;
};

} // anonymous namespace

// allow_multiple_connections_per_pid = false:
// a second connection from a different IP but the same peer-id should be rejected
TORRENT_TEST(allow_multiple_connections_per_pid_false)
{
	// setup the simulation
	sim::default_config cfg;
	sim::simulation sim{cfg};
	auto ios = std::make_unique<sim::asio::io_context>(sim, lt::make_address_v4("50.0.0.1"));
	lt::session_proxy zombie;

	// setup settings pack
	lt::session_params sp;
	sp.settings = settings();
	sp.settings.set_int(lt::settings_pack::alert_mask
		, lt::alert_category::all & ~lt::alert_category::stats);
	sp.settings.set_bool(lt::settings_pack::allow_multiple_connections_per_pid, false);
	sp.disk_io_constructor = lt::disabled_disk_io_constructor;

	// create session
	std::shared_ptr<lt::session> ses = std::make_shared<lt::session>(sp, *ios);

	// add torrent
	lt::add_torrent_params params = ::create_torrent(0, false);
	lt::sha1_hash const info_hash = params.ti->info_hash();
	params.flags &= ~lt::torrent_flags::auto_managed;
	params.flags &= ~lt::torrent_flags::paused;
	ses->async_add_torrent(std::move(params));

	// create two fake peers with the same peer-id but different IPs
	lt::peer_id fixed_pid;
	std::fill(fixed_pid.data(), fixed_pid.data() + 20, 0xAA);

	auto peer1 = std::make_unique<fake_peer>(sim, "60.0.0.1");
	auto peer2 = std::make_unique<fake_peer>(sim, "60.0.0.2");
	peer1->set_peer_id(fixed_pid);
	peer2->set_peer_id(fixed_pid);

	peer_disconnects d;

	// set up a timer to fire later, to shut down
	sim::timer t2(sim, lt::seconds(5)
		, [&](boost::system::error_code const&)
	{
		zombie = ses->abort();
		ses.reset();
	});

	print_alerts(*ses, [&](lt::session&, lt::alert const* a)
	{
		d(a);

		if (lt::alert_cast<lt::add_torrent_alert>(a))
		{
			// both peers connect immediately after the torrent is added
			peer1->connect_to(ep("50.0.0.1", 6881), info_hash);
			peer2->connect_to(ep("50.0.0.1", 6881), info_hash);
		}
	});

	sim.run();

	// verify that a duplicate_peer_id disconnect occurred
	bool has_duplicate_error = false;
	for (auto const& err : d.alerts)
		if (err == lt::errors::duplicate_peer_id)
			has_duplicate_error = true;

	TEST_CHECK(has_duplicate_error);
}

// allow_multiple_connections_per_pid = true:
// a second connection from a different IP but the same peer-id should be allowed
TORRENT_TEST(allow_multiple_connections_per_pid_true)
{
	// setup the simulation
	sim::default_config cfg;
	sim::simulation sim{cfg};
	auto ios = std::make_unique<sim::asio::io_context>(sim, lt::make_address_v4("50.0.0.1"));
	lt::session_proxy zombie;

	// setup settings pack
	lt::session_params sp;
	sp.settings = settings();
	sp.settings.set_int(lt::settings_pack::alert_mask
		, lt::alert_category::all & ~lt::alert_category::stats);
	sp.settings.set_bool(lt::settings_pack::allow_multiple_connections_per_pid, true);
	sp.disk_io_constructor = lt::disabled_disk_io_constructor;

	// create session
	std::shared_ptr<lt::session> ses = std::make_shared<lt::session>(sp, *ios);

	// add torrent
	lt::add_torrent_params params = ::create_torrent(0, false);
	lt::sha1_hash const info_hash = params.ti->info_hash();
	params.flags &= ~lt::torrent_flags::auto_managed;
	params.flags &= ~lt::torrent_flags::paused;
	ses->async_add_torrent(std::move(params));

	// create two fake peers with the same peer-id but different IPs
	lt::peer_id fixed_pid;
	std::fill(fixed_pid.data(), fixed_pid.data() + 20, 0xBB);

	auto peer1 = std::make_unique<fake_peer>(sim, "60.0.0.3");
	auto peer2 = std::make_unique<fake_peer>(sim, "60.0.0.4");
	peer1->set_peer_id(fixed_pid);
	peer2->set_peer_id(fixed_pid);

	peer_disconnects d;
	peer_connects c;

	// set up a timer to fire later, to shut down
	sim::timer t2(sim, lt::seconds(5)
		, [&](boost::system::error_code const&)
	{
		zombie = ses->abort();
		ses.reset();
	});

	print_alerts(*ses, [&](lt::session&, lt::alert const* a)
	{
		d(a);
		c(a);

		if (lt::alert_cast<lt::add_torrent_alert>(a))
		{
			// both peers connect immediately after the torrent is added
			peer1->connect_to(ep("50.0.0.1", 6881), info_hash);
			peer2->connect_to(ep("50.0.0.1", 6881), info_hash);
		}
	});

	sim.run();

	// verify that the second peer (60.0.0.4) connected successfully
	bool second_connected = false;
	for (auto const& ep : c.alerts)
		if (ep.address() == lt::make_address_v4("60.0.0.4"))
			second_connected = true;

	// verify no duplicate_peer_id error occurred
	bool has_duplicate_error = false;
	for (auto const& err : d.alerts)
		if (err == lt::errors::duplicate_peer_id)
			has_duplicate_error = true;

	TEST_CHECK(second_connected);
	TEST_CHECK(!has_duplicate_error);
}
