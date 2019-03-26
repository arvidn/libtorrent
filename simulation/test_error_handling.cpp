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
#include "libtorrent/random.hpp"
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

std::string make_ep_string(char const* address, bool const is_v6
	, char const* port)
{
	std::string ret;
	if (is_v6) ret += '[';
	ret += address;
	if (is_v6) ret += ']';
	ret += ':';
	ret += port;
	return ret;
}

template <typename HandleAlerts, typename Test>
void run_test(HandleAlerts const& on_alert, Test const& test)
{
	using namespace lt;

	using asio::ip::address;
	address const peer0 = addr("50.0.0.1");
	address const peer1 = addr("50.0.0.2");

	// setup the simulation
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	sim::asio::io_service ios0 { sim, peer0 };
	sim::asio::io_service ios1 { sim, peer1 };

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

	sim.run();
}

int g_alloc_counter = 1000000;

void* operator new(std::size_t sz)
{
	if (--g_alloc_counter == 0)
	{
		char stack[10000];
		libtorrent::print_backtrace(stack, sizeof(stack), 40, nullptr);
#ifdef _MSC_VER
		// this is a bit unfortunate. Some MSVC standard containers really don't move
		// with noexcept, by actually allocating memory (i.e. it's not just a matter
		// of accidentally missing noexcept specifiers). The heterogeneous queue used
		// by the alert manager requires alerts to be noexcept move constructable and
		// move assignable, which they claim to be, even though on MSVC some of them
		// aren't. Things will improve in C++17 and it doesn't seem worth the trouble
		// to make the heterogeneous queue support throwing moves, nor to replace all
		// standard types with variants that can move noexcept.
		if (std::strstr(stack, " libtorrent::entry::operator= ") != nullptr
			|| std::strstr(stack, " libtorrent::aux::noexcept_movable<std::map<") != nullptr)
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

#ifdef __cpp_sized_deallocation
void operator delete(void* ptr, std::size_t) noexcept
{
	std::free(ptr);
}
#endif

TORRENT_TEST(error_handling)
{
	for (int i = 0; i < 8000; ++i)
	{
		// this will clear the history of all output we've printed so far.
		// if we encounter an error from now on, we'll only print the relevant
		// iteration
		reset_output();

		// re-seed the random engine each iteration, to make the runs
		// deterministic
		lt::aux::random_engine().seed(0x82daf973);

		std::printf("\n\n === ROUND %d ===\n\n", i);
		try
		{
			g_alloc_counter = i;
			using namespace lt;
			run_test(
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

