/*

Copyright (c) 2022, Arvid Norberg
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

#include "simulator/simulator.hpp"
#include "simulator/utils.hpp"

#include "test.hpp"
#include "create_torrent.hpp"
#include "settings.hpp"
#include "setup_swarm.hpp"
#include "utils.hpp"
#include "test_utils.hpp"
#include "setup_transfer.hpp" // for addr()
#include "disk_io.hpp"

#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/alert_types.hpp"

namespace {

template <typename Setup, typename HandleAlerts>
void run_test(
	Setup setup
	, HandleAlerts on_alert
	, test_disk const downloader_disk_constructor = test_disk()
	, test_disk const seed_disk_constructor = test_disk()
	)
{
	char const* peer0_ip = "50.0.0.1";
	char const* peer1_ip = "50.0.0.2";

	lt::address peer0 = addr(peer0_ip);
	lt::address peer1 = addr(peer1_ip);

	// setup the simulation
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	sim::asio::io_context ios0 { sim, peer0 };
	sim::asio::io_context ios1 { sim, peer1 };

	lt::session_proxy zombie[2];

	lt::session_params params;
	// setup settings pack to use for the session (customization point)
	lt::settings_pack& pack = params.settings;
	pack = settings();

	pack.set_str(lt::settings_pack::listen_interfaces, make_ep_string(peer0_ip, false, "6881"));

	// create session
	std::shared_ptr<lt::session> ses[2];

	// session 0 is a downloader, session 1 is a seed

	params.disk_io_constructor = downloader_disk_constructor;
	ses[0] = std::make_shared<lt::session>(params, ios0);

	pack.set_str(lt::settings_pack::listen_interfaces, make_ep_string(peer1_ip, false, "6881"));

	params.disk_io_constructor = seed_disk_constructor.set_files(existing_files_mode::full_valid);
	ses[1] = std::make_shared<lt::session>(params, ios1);

	setup(*ses[0], *ses[1]);

	// only monitor alerts for session 0 (the downloader)
	print_alerts(*ses[0], [=](lt::session& ses, lt::alert const* a) {
		if (auto ta = lt::alert_cast<lt::add_torrent_alert>(a))
			ta->handle.connect_peer(lt::tcp::endpoint(peer1, 6881));
		on_alert(ses, a);
	}, 0);

	print_alerts(*ses[1], [](lt::session&, lt::alert const*){}, 1);

	// the min reconnect time defaults to 60 seconds
	sim::timer t(sim, lt::seconds(70), [&](boost::system::error_code const&)
	{
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

lt::info_hash_t setup_conflict(lt::session& seed, lt::session& downloader)
{
	lt::add_torrent_params atp = ::create_test_torrent(10, lt::create_flags_t{}, 2);
	atp.flags &= ~lt::torrent_flags::auto_managed;
	atp.flags &= ~lt::torrent_flags::paused;

	// add the complete torrent to the seed
	seed.async_add_torrent(atp);

	lt::info_hash_t const ih = atp.ti->info_hashes();

	// add v1-only magnet link
	atp.ti.reset();
	atp.info_hashes.v1 = ih.v1;
	downloader.async_add_torrent(atp);

	// add v2-only magnet link
	atp.info_hashes.v1.clear();
	atp.info_hashes.v2 = ih.v2;
	downloader.async_add_torrent(atp);
	return ih;
}

} // anonymous namespace

// This adds the same hybrid torrent twice, once via the v1 info-hash and once
// via the v2 info-hash. Once the conflict is detected, both torrents should
// fail with the duplicate_torrent error state.
TORRENT_TEST(hybrid_torrent_conflict)
{
	std::vector<lt::torrent_handle> handles;

	int errors = 0;
	int conflict = 0;
	lt::info_hash_t added_ih;
	run_test([&](lt::session& ses0, lt::session& ses1) {
		added_ih = setup_conflict(ses1, ses0);
	},
	[&](lt::session& ses, lt::alert const* a) {
		if (auto const* ta = lt::alert_cast<lt::add_torrent_alert>(a))
		{
			handles.push_back(ta->handle);
		}
		else if (lt::alert_cast<lt::torrent_removed_alert>(a))
		{
			TEST_ERROR("a torrent was removed");
		}
		else if (auto const* te = lt::alert_cast<lt::torrent_error_alert>(a))
		{
			++errors;
			// both handles are expected to fail with duplicate torrent error
			TEST_EQUAL(te->error, lt::error_code(lt::errors::duplicate_torrent));
		}
		else if (auto const* tc = lt::alert_cast<lt::torrent_conflict_alert>(a))
		{
			++conflict;
			TEST_EQUAL(std::count(handles.begin(), handles.end(), tc->handle), 1);
			TEST_EQUAL(std::count(handles.begin(), handles.end(), tc->conflicting_torrent), 1);
			TEST_CHECK(tc->handle != tc->conflicting_torrent);

			TEST_CHECK(added_ih == tc->metadata->info_hashes());
		}

		for (auto& h : handles)
			TEST_CHECK(h.is_valid());

		auto torrents = ses.get_torrents();
		if (handles.size() == 2)
		{
			TEST_EQUAL(torrents.size(), 2);
		}
	}
	);

	TEST_EQUAL(errors, 2);
	TEST_EQUAL(conflict, 1);
}

// try to resume the torrents after failing with a conflict. Ensure they both
// fail again with the same error
TORRENT_TEST(resume_conflict)
{
	std::vector<lt::torrent_handle> handles;

	int errors = 0;
	int resume = 0;

	run_test([](lt::session& ses0, lt::session& ses1) {
		setup_conflict(ses1, ses0);
	},
	[&](lt::session& ses, lt::alert const* a) {
		if (auto const* ta = lt::alert_cast<lt::add_torrent_alert>(a))
		{
			handles.push_back(ta->handle);
		}
		else if (lt::alert_cast<lt::torrent_removed_alert>(a))
		{
			TEST_ERROR("a torrent was removed");
		}
		else if (auto const* te = lt::alert_cast<lt::torrent_error_alert>(a))
		{
			++errors;
			// both handles are expected to fail with duplicate torrent error
			TEST_EQUAL(te->error, lt::error_code(lt::errors::duplicate_torrent));
			if (resume < 2)
			{
				te->handle.clear_error();
				te->handle.resume();
				++resume;
			}
		}
		for (auto& h : handles)
			TEST_CHECK(h.is_valid());

		auto torrents = ses.get_torrents();
		if (handles.size() == 2)
		{
			TEST_EQUAL(torrents.size(), 2);
		}
	}
	);

	TEST_EQUAL(errors, 4);
	TEST_EQUAL(resume, 2);
}

TORRENT_TEST(resolve_conflict)
{
	int errors = 0;
	int finished = 0;
	int removed = 0;

	run_test([](lt::session& ses0, lt::session& ses1) {
		setup_conflict(ses1, ses0);
	},
	[&](lt::session& ses, lt::alert const* a) {
		if (lt::alert_cast<lt::torrent_removed_alert>(a))
		{
			++removed;
		}
		else if (auto const* te = lt::alert_cast<lt::torrent_error_alert>(a))
		{
			++errors;
			// both handles are expected to fail with duplicate torrent error
			TEST_EQUAL(te->error, lt::error_code(lt::errors::duplicate_torrent));
			if (errors == 1)
			{
				ses.remove_torrent(te->handle);
			}
			else if (errors == 2)
			{
				te->handle.clear_error();
				te->handle.resume();
			}
		}
		else if (lt::alert_cast<lt::torrent_finished_alert>(a))
		{
			++finished;
		}

		if (errors == 2)
		{
			auto torrents = ses.get_torrents();
			TEST_EQUAL(torrents.size(), 1);
		}
	});

	TEST_EQUAL(errors, 2);
	TEST_EQUAL(finished, 1);
	TEST_EQUAL(removed, 1);
}

TORRENT_TEST(conflict_readd)
{
	std::vector<lt::torrent_handle> handles;
	int errors = 0;
	int finished = 0;
	int removed = 0;
	int conflict = 0;

	run_test([](lt::session& ses0, lt::session& ses1) {
		setup_conflict(ses1, ses0);
	},
	[&](lt::session& ses, lt::alert const* a) {
		if (auto const* ta = lt::alert_cast<lt::add_torrent_alert>(a))
		{
			handles.push_back(ta->handle);
		}
		else if (lt::alert_cast<lt::torrent_removed_alert>(a))
		{
			++removed;
		}
		else if (auto const* te = lt::alert_cast<lt::torrent_error_alert>(a))
		{
			++errors;
			// both handles are expected to fail with duplicate torrent error
			TEST_EQUAL(te->error, lt::error_code(lt::errors::duplicate_torrent));
		}
		else if (auto const* tf = lt::alert_cast<lt::torrent_finished_alert>(a))
		{
			++finished;
			TEST_EQUAL(handles.size(), 1);
			TEST_CHECK(handles[0] == tf->handle);
		}
		else if (auto const* tc = lt::alert_cast<lt::torrent_conflict_alert>(a))
		{
			++conflict;
			ses.remove_torrent(tc->handle);
			ses.remove_torrent(tc->conflicting_torrent);
			handles.clear();

			lt::add_torrent_params atp;
			atp.ti = std::move(tc->metadata);
			atp.save_path = ".";
			ses.async_add_torrent(std::move(atp));
		}

		for (auto& h : handles)
			TEST_CHECK(h.is_valid());
	});

	TEST_EQUAL(errors, 2);
	TEST_EQUAL(finished, 1);
	TEST_EQUAL(removed, 2);
	TEST_EQUAL(conflict, 1);
}
