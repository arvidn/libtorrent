/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/session.hpp"
#include "libtorrent/session_params.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/disk_interface.hpp"

#include "simulator/simulator.hpp"
#include "simulator/utils.hpp"

#include "test.hpp"
#include "settings.hpp"
#include "setup_transfer.hpp" // for addr()
#include "utils.hpp"
#include "disk_io.hpp"

using namespace lt;
using namespace std::chrono_literals;

namespace {

// test_disk_io::async_set_file_priority() unconditionally fails with
// operation_not_supported, exercising on_file_priority()'s error path: a
// deferred priority update must still apply once the failing job completes.
template <typename Setup, typename Check>
void run_test(int const num_files, Setup const& setup, Check const& check)
{
	lt::address const peer0 = addr("50.0.0.1");

	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	sim::asio::io_context ios0{sim, peer0};

	lt::session_proxy zombie;

	lt::session_params params;
	params.settings = settings();
	params.disk_io_constructor = test_disk().set_seed();

	auto ses = std::make_shared<lt::session>(params, ios0);

	// v1_only keeps the file count exactly num_files (no pad files), so
	// priorities can be compared against a plain vector
	lt::add_torrent_params atp =
		::create_test_torrent(lt::default_block_size, 4, lt::create_torrent::v1_only, num_files);
	atp.save_path = ".";
	atp.flags &= ~lt::torrent_flags::paused;
	atp.flags &= ~lt::torrent_flags::auto_managed;

	torrent_handle h;
	bool got_file_error_alert = false;
	print_alerts(*ses, [&](lt::session&, lt::alert const* a) {
		if (auto const* ta = alert_cast<add_torrent_alert>(a))
			h = ta->handle;
		else if (alert_cast<file_error_alert>(a))
			got_file_error_alert = true;
	});

	ses->async_add_torrent(atp);

	sim::timer t0(sim, 1s, [&](boost::system::error_code const&) { setup(h); });

	sim::timer t1(sim, 3s, [&](boost::system::error_code const&) {
		// confirms the disk-error path in on_file_priority() actually ran
		TEST_CHECK(got_file_error_alert);

		check(h);

		zombie = ses->abort();
		ses.reset();
	});

	sim.run();
}

} // anonymous namespace

// a prioritize_files() call issued while one is already outstanding is
// deferred rather than dispatched as a second concurrent disk job, and
// must still apply once the outstanding job completes, even on error.
TORRENT_TEST(prioritize_files_disk_error)
{
	int const num_files = 4;
	std::vector<download_priority_t> const first(std::size_t(num_files), lt::dont_download);
	std::vector<download_priority_t> const last(std::size_t(num_files), lt::top_priority);

	run_test(
		num_files,
		[&](torrent_handle h) {
			// the second call, issued before the first job's completion
			// handler runs, merges into m_deferred_file_priorities
			h.prioritize_files(first);
			h.prioritize_files(last);
		},
		[&](torrent_handle h) { TEST_CHECK(h.get_file_priorities() == last); });
}

// same as above, exercised through set_file_priority(), which shares
// on_file_priority() with prioritize_files()
TORRENT_TEST(set_file_priority_disk_error)
{
	int const num_files = 4;

	run_test(
		num_files,
		[](torrent_handle h) {
			h.file_priority(file_index_t(0), lt::dont_download);
			h.file_priority(file_index_t(0), lt::top_priority);
		},
		[](torrent_handle h) { TEST_EQUAL(h.file_priority(file_index_t(0)), lt::top_priority); });
}
