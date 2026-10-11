/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "simulator/simulator.hpp"
#include "libtorrent/aux_/disk_tick_thread.hpp"
#include "libtorrent/time.hpp"

#include <chrono>
#include <vector>

using namespace std::chrono_literals;
using lt::aux::steady_clock;

// the simulated clock only advances when the simulation runs, so a task
// firing at its deadline proves the timer drives it
TORRENT_TEST(oneshots_run_at_simulated_deadlines)
{
	sim::default_config cfg;
	sim::simulation sim{cfg};
	sim::asio::io_context ios(sim);
	lt::aux::disk_tick_thread t(ios);

	std::vector<int> order;
	std::vector<steady_clock::time_point> fired;
	auto const start = steady_clock::now();
	for (int const i : {3, 1, 2})
	{
		t.schedule(start + i * 1min, [&, i] {
			order.push_back(i);
			fired.push_back(steady_clock::now());
		});
	}
	sim.run();

	TEST_CHECK((order == std::vector<int>{1, 2, 3}));
	TEST_EQUAL(fired.size(), 3);
	for (int i = 0; i < int(fired.size()); ++i)
		TEST_CHECK(fired[i] == start + (i + 1) * 1min);
}

TORRENT_TEST(periodic_task_follows_simulated_clock)
{
	sim::default_config cfg;
	sim::simulation sim{cfg};
	sim::asio::io_context ios(sim);
	lt::aux::disk_tick_thread t(ios);

	std::vector<steady_clock::time_point> runs;
	auto const start = steady_clock::now();
	t.add([&](steady_clock::time_point const now) {
		runs.push_back(now);
		// the simulation only ends once nothing is pending, so the task has
		// to retire itself
		if (runs.size() == 5)
			return steady_clock::time_point::max();
		return now + 10min;
	});
	sim.run();

	TEST_EQUAL(runs.size(), 5);
	TEST_CHECK(runs[0] >= start);
	for (int i = 1; i < int(runs.size()); ++i)
		TEST_CHECK(runs[i] == runs[0] + i * 10min);
}

TORRENT_TEST(abort_drops_pending_oneshot)
{
	sim::default_config cfg;
	sim::simulation sim{cfg};
	sim::asio::io_context ios(sim);
	lt::aux::disk_tick_thread t(ios);

	bool ran = false;
	t.schedule(steady_clock::now() + 1h, [&] { ran = true; });
	t.abort();
	sim.run();
	TEST_CHECK(!ran);

	// scheduled after abort, never runs
	t.schedule(steady_clock::now(), [&] { ran = true; });
	sim.run();
	TEST_CHECK(!ran);
}
