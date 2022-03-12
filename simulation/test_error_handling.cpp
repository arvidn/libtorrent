/*

Copyright (c) 2016-2019, 2021, Arvid Norberg
Copyright (c) 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <array>
#include "test.hpp"
#include "create_torrent.hpp"
#include "settings.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/session_stats.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/aux_/proxy_settings.hpp"
#include "libtorrent/aux_/random.hpp"
#include "libtorrent/settings_pack.hpp"
#include "simulator/simulator.hpp"
#include "simulator/socks_server.hpp"
#include "simulator/utils.hpp"
#include "setup_swarm.hpp"
#include "utils.hpp"
#include "setup_transfer.hpp" // for addr()

using namespace sim;

#if defined _MSC_VER && _ITERATOR_DEBUG_LEVEL > 0
// https://developercommunity.visualstudio.com/content/problem/140200/c-stl-stdvector-constructor-declared-with-noexcept.html
#error "msvc's standard library does not support bad_alloc with debug iterators. This test only works with debug iterators disabled on msvc. _ITERATOR_DEBUG_LEVEL=0"
#endif

int g_alloc_counter = 1000000;

template <typename HandleAlerts, typename Test>
void run_test(int const round, HandleAlerts const& on_alert, Test const& test)
{
	using namespace lt;

	using asio::ip::address;
	address const peer0 = addr("50.0.0.1");
	address const peer1 = addr("50.0.0.2");

	// setup the simulation
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	sim::asio::io_context ios0 { sim, peer0 };
	sim::asio::io_context ios1 { sim, peer1 };

	lt::session_proxy zombie[2];

	// setup settings pack to use for the session (customization point)
	lt::settings_pack pack = settings();

	// disable utp by default
	pack.set_bool(settings_pack::enable_outgoing_utp, false);
	pack.set_bool(settings_pack::enable_incoming_utp, false);

	// disable encryption by default
	pack.set_bool(settings_pack::prefer_rc4, false);
	pack.set_int(settings_pack::in_enc_policy, settings_pack::pe_disabled);
	pack.set_int(settings_pack::out_enc_policy, settings_pack::pe_disabled);
	pack.set_int(settings_pack::allowed_enc_level, settings_pack::pe_plaintext);

	pack.set_str(settings_pack::listen_interfaces, peer0.to_string() + ":6881");

	std::shared_ptr<lt::session> ses[2];

	// create session
	ses[0] = std::make_shared<lt::session>(pack, ios0);

	pack.set_str(settings_pack::listen_interfaces, peer1.to_string() + ":6881");
	ses[1] = std::make_shared<lt::session>(pack, ios1);

	// only monitor alerts for session 0 (the downloader)
	print_alerts(*ses[0], [=](lt::session& ses, lt::alert const* a) {
		if (auto ta = alert_cast<lt::add_torrent_alert>(a))
		{
			ta->handle.connect_peer(lt::tcp::endpoint(peer1, 6881));
		}
		on_alert(ses, a);
	});

	print_alerts(*ses[1]);

	// the first peer is a downloader, the second peer is a seed
	lt::add_torrent_params params = ::create_torrent(1);
	params.flags &= ~lt::torrent_flags::auto_managed;
	params.flags &= ~lt::torrent_flags::paused;

	params.save_path = save_path(0);
	ses[0]->async_add_torrent(params);

	params.save_path = save_path(1);
	ses[1]->async_add_torrent(params);

	sim::timer t(sim, lt::seconds(60), [&](boost::system::error_code const&)
	{
		test(ses);

		// shut down
		int idx = 0;
		for (auto& s : ses)
		{
			zombie[idx++] = s->abort();
			s.reset();
		}
	});

	// we're only interested in allocation failures after construction has
	// completed
	g_alloc_counter = round;
	sim.run();
}

#ifdef _MSC_VER
namespace libtorrent {
namespace aux {
	// this is unfortunate. Some MSVC standard containers really don't move
	// with noexcept, by actually allocating memory (i.e. it's not just a matter
	// of accidentally missing noexcept specifiers). The heterogeneous queue used
	// by the alert manager requires alerts to be noexcept move constructable and
	// move assignable, which they claim to be, even though on MSVC some of them
	// aren't. Things will improve in C++17 and it doesn't seem worth the trouble
	// to make the heterogeneous queue support throwing moves, nor to replace all
	// standard types with variants that can move noexcept.
	// this thread local variable is set to true whenever such throwing
	// container is wrapped, to make it pretend that it cannot throw. This
	// signals to the test that it can't exercise that failure path.
	// this is defined in simulation/utils.cpp
	// TODO: in C++17, make this inline
	extern thread_local int g_must_not_fail;
}
}
#endif

void* operator new(std::size_t sz)
{
	if (--g_alloc_counter == 0)
	{
		char stack[10000];
		libtorrent::print_backtrace(stack, sizeof(stack), 40, nullptr);
#ifdef _MSC_VER
		if (libtorrent::aux::g_must_not_fail)
		{
			++g_alloc_counter;
			return std::malloc(sz);
		}
#endif
		std::printf("\n\nthrowing bad_alloc (as part of test)\n%s\n\n\n", stack);
		throw std::bad_alloc();
	}
	return std::malloc(sz);
}

void operator delete(void* ptr) noexcept
{
	std::free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept
{
	std::free(ptr);
}

TORRENT_TEST(error_handling)
{
	for (int i = 0; i < 8000; ++i)
	{
		// this will clear the history of all output we've printed so far.
		// if we encounter an error from now on, we'll only print the relevant
		// iteration
		unit_test::reset_output();

		// re-seed the random engine each iteration, to make the runs
		// deterministic
		lt::aux::random_engine().seed(0x82daf973);

		std::printf("\n\n === ROUND %d ===\n\n", i);
		try
		{
			using namespace lt;
			run_test(i,
				[](lt::session&, lt::alert const*) {},
				[](std::shared_ptr<lt::session>[2]) {}
			);
		}
		catch (std::bad_alloc const&)
		{
			// this is kind of expected
		}
		catch (boost::system::system_error const& err)
		{
			TEST_ERROR("session constructor terminated with unexpected exception. \""
				+ err.code().message() + "\" round: "
				+ std::to_string(i));
			break;
		}
		catch (std::exception const& err)
		{
			TEST_ERROR("session constructor terminated with unexpected exception. \""
				+ std::string(err.what()) + "\" round: "
				+ std::to_string(i));
			break;
		}
		catch (...)
		{
			TEST_ERROR("session constructor terminated with unexpected exception. round: "
				+ std::to_string(i));
			break;
		}
		// if we didn't fail any allocations this run, there's no need to
		// continue, we won't exercise any new code paths
		if (g_alloc_counter > 0) break;
	}

	// if this fails, we need to raise the limit in the loop above
	TEST_CHECK(g_alloc_counter > 0);

	// we don't want any part of the actual test framework to suffer from failed
	// allocations, so bump the counter
	g_alloc_counter = 1000000;
}

