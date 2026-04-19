/*

Copyright (c) 2026, Arvid Norberg
Copyright (c) 2026, lzhzh1
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/session.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/disabled_disk_io.hpp"
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

struct test_result
{
	std::vector<lt::error_code> disconnects;
	std::vector<lt::tcp::endpoint> connects;
};

test_result test_allow_multiple_connections_per_pid(bool allow
	, lt::peer_id const& pid
	, char const* peer1_ip
	, char const* peer2_ip)
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
	sp.settings.set_bool(lt::settings_pack::allow_multiple_connections_per_pid, allow);
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
	auto peer1 = std::make_unique<fake_peer>(sim, peer1_ip);
	auto peer2 = std::make_unique<fake_peer>(sim, peer2_ip);
	peer1->set_peer_id(pid);
	peer2->set_peer_id(pid);

	test_result result;

	// set up a timer to fire later, to shut down
	sim::timer t2(sim, lt::seconds(5)
		, [&](boost::system::error_code const&)
	{
		zombie = ses->abort();
		ses.reset();
	});

	print_alerts(*ses, [&](lt::session&, lt::alert const* a)
	{
		auto* pd = lt::alert_cast<lt::peer_disconnected_alert>(a);
		if (pd) result.disconnects.push_back(pd->error);

		auto* pa = lt::alert_cast<lt::peer_connect_alert>(a);
		if (pa) result.connects.push_back(std::get<lt::peer_alert::ip_endpoint>(pa->ep));

		if (lt::alert_cast<lt::add_torrent_alert>(a))
		{
			// both peers connect immediately after the torrent is added
			peer1->connect_to(ep("50.0.0.1", 6881), info_hash);
			peer2->connect_to(ep("50.0.0.1", 6881), info_hash);
		}
	});

	sim.run();

	return result;
}

bool has_duplicate_peer_id_error(std::vector<lt::error_code> const& errors)
{
	for (auto const& err : errors)
		if (err == lt::errors::duplicate_peer_id)
			return true;
	return false;
}

bool has_connected(std::vector<lt::tcp::endpoint> const& endpoints
	, lt::address_v4 const& addr)
{
	for (auto const& ep : endpoints)
		if (ep.address() == addr)
			return true;
	return false;
}

} // anonymous namespace

// allow_multiple_connections_per_pid = false:
// a second connection from a different IP but the same peer-id should be rejected
TORRENT_TEST(allow_multiple_connections_per_pid_false)
{
	lt::peer_id pid;
	std::fill(pid.data(), pid.data() + 20, char(0xAA));

	auto result = test_allow_multiple_connections_per_pid(false, pid
		, "60.0.0.1", "60.0.0.2");

	// verify that a duplicate_peer_id disconnect occurred
	TEST_CHECK(has_duplicate_peer_id_error(result.disconnects));
}

// allow_multiple_connections_per_pid = true:
// a second connection from a different IP but the same peer-id should be allowed
TORRENT_TEST(allow_multiple_connections_per_pid_true)
{
	lt::peer_id pid;
	std::fill(pid.data(), pid.data() + 20, char(0xBB));

	auto result = test_allow_multiple_connections_per_pid(true, pid
		, "60.0.0.3", "60.0.0.4");

	// verify no duplicate_peer_id error occurred
	TEST_CHECK(!has_duplicate_peer_id_error(result.disconnects));

	// verify that the second peer connected successfully
	TEST_CHECK(has_connected(result.connects, lt::make_address_v4("60.0.0.4")));
}
