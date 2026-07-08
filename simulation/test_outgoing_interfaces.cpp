/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/session.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/aux_/deadline_timer.hpp"
#include "settings.hpp"
#include "create_torrent.hpp"
#include "simulator/simulator.hpp"
#include "simulator/socks_server.hpp"
#include "simulator/utils.hpp"
#include "fake_peer.hpp"
#include "utils.hpp"

using namespace sim;

namespace {

	// under the simulator, enum_net_interfaces() always reports the interface
	// name "eth0" for every address on a node (see enum_net_interfaces() in
	// src/enum_net.cpp).
	constexpr char const* device_name = "eth0";

	template <typename Settings, typename Test>
	void run_test(Settings const& make_settings, Test const& test)
	{
		// setup the simulation
		sim::default_config network_cfg;
		sim::simulation sim{network_cfg};
		sim::asio::io_context ios(sim, asio::ip::make_address_v4("50.0.0.1"));
		lt::session_proxy zombie;

		// a real, working SOCKS5 relay, used by the proxy test below to make sure
		// outgoing connections actually go all the way through and reach
		// verify_bound_address(), instead of failing earlier for unrelated
		// reasons (e.g. a proxy that doesn't exist).
		sim::asio::io_context proxy_ios{sim, asio::ip::make_address_v4("50.50.50.50")};
		sim::socks_server socks5(proxy_ios, 5555, 5);

		lt::settings_pack pack = settings();
		make_settings(pack);
		std::shared_ptr<lt::session> ses = std::make_shared<lt::session>(pack, ios);

		fake_peer p1(sim, "60.0.0.0");

		lt::add_torrent_params params = ::create_torrent(0, false);
		params.flags &= ~lt::torrent_flags::auto_managed;
		params.flags &= ~lt::torrent_flags::paused;
		ses->async_add_torrent(params);

		print_alerts(*ses, [](lt::session&, lt::alert const* a) {
			if (auto at = lt::alert_cast<lt::add_torrent_alert>(a))
			{
				lt::torrent_handle h = at->handle;
				add_fake_peer(h, 0);
			}
		});

		sim::timer t(sim, lt::seconds(60), [&](boost::system::error_code const&) {
			test(p1);

			// shut down
			zombie = ses->abort();
			p1.close();
			ses.reset();
		});

		sim.run();
	}

} // anonymous namespace

// settings_pack::outgoing_interfaces restricts which local device outgoing
// peer connections may bind to. session_impl caches the result of
// enumerating the local network interfaces (m_cached_interfaces) instead of
// asking the OS every time verify_bound_address() is called. This makes sure
// that cache is actually populated and used correctly: when the configured
// device matches the one we're bound to, the connection is allowed through.
TORRENT_TEST(outgoing_interfaces_matching_device_allowed)
{
	run_test(
		[](lt::settings_pack& pack) {
			pack.set_str(lt::settings_pack::outgoing_interfaces, device_name);
		},
		[](fake_peer& p) { TEST_EQUAL(p.connected(), true); });
}

// the opposite of outgoing_interfaces_matching_device_allowed: when the
// configured device does not match, verify_bound_address() must reject the
// connection and it must never complete a BT handshake.
TORRENT_TEST(outgoing_interfaces_wrong_device_blocked)
{
	run_test(
		[](lt::settings_pack& pack) {
			pack.set_str(lt::settings_pack::outgoing_interfaces, "nonexistent0");
		},
		[](fake_peer& p) { TEST_EQUAL(p.connected(), false); });
}

// same as outgoing_interfaces_matching_device_allowed, but with peer
// connections proxied. reopen_listen_sockets() only refreshes
// m_cached_interfaces unconditionally when it also needs the interface list
// to build listen endpoints; when proxying peer connections it must still
// populate the cache whenever outgoing_interfaces is set, since
// verify_bound_address() depends on it for every connection attempt.
TORRENT_TEST(outgoing_interfaces_matching_device_allowed_with_proxy)
{
	run_test(
		[](lt::settings_pack& pack) {
			pack.set_str(lt::settings_pack::outgoing_interfaces, device_name);
			pack.set_int(lt::settings_pack::proxy_type, lt::settings_pack::socks5);
			pack.set_str(lt::settings_pack::proxy_hostname, "50.50.50.50");
			pack.set_int(lt::settings_pack::proxy_port, 5555);
			pack.set_bool(lt::settings_pack::proxy_peer_connections, true);
		},
		[](fake_peer& p) { TEST_EQUAL(p.connected(), true); });
}
