/*

Copyright (c) 2015-2021, Arvid Norberg
Copyright (c) 2018, Steven Siloti
Copyright (c) 2021, Alden Torres
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
#include "utils.hpp"
#include "create_torrent.hpp"
#include "fake_peer.hpp"
#include "simulator/simulator.hpp"
#include "simulator/utils.hpp" // for timer
#include <iostream>

using namespace sim;
using namespace lt;

const int num_torrents = 10;

using sim::asio::ip::address_v4;

// this is the general template for these tests. create the session with custom
// settings (Settings), set up the test, by adding torrents with certain
// arguments (Setup), run the test and verify the end state (Test)
template <typename Settings, typename Setup, typename Test>
void run_test(Settings const& sett, Setup const& setup, Test const& test)
{
	// setup the simulation
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	std::unique_ptr<sim::asio::io_context> ios = make_io_context(sim, 0);
	lt::session_proxy zombie;

	// setup settings pack to use for the session (customization point)
	lt::settings_pack pack = settings();
	sett(pack);

	// create session
	std::shared_ptr<lt::session> ses = std::make_shared<lt::session>(pack, *ios);

	// set up test, like adding torrents (customization point)
	setup(*ses);

	// set up a timer to fire later, to verify everything we expected to happen
	// happened
	sim::timer t(sim, lt::seconds((num_torrents + 1) * 60)
		, [&](boost::system::error_code const&)
	{
		test(*ses);

		// shut down
		zombie = ses->abort();
		ses.reset();
	});

	sim.run();
}

TORRENT_TEST(dont_count_slow_torrents)
{
	run_test(
		[](lt::settings_pack& sett) {
			// session settings
			sett.set_bool(lt::settings_pack::dont_count_slow_torrents, true);
			sett.set_int(lt::settings_pack::active_downloads, 1);
			sett.set_int(lt::settings_pack::active_seeds, 1);
		},

		[](lt::session& ses) {
			// add torrents
			for (int i = 0; i < num_torrents; ++i)
			{
				lt::add_torrent_params params = ::create_torrent(i, false);
				params.flags |= lt::torrent_flags::auto_managed;
				params.flags |= lt::torrent_flags::paused;
				ses.async_add_torrent(params);
			}
		},

		[](lt::session& ses) {
			// verify result

			std::vector<lt::alert*> alerts;
			ses.pop_alerts(&alerts);

			lt::time_point last = lt::time_point::min();
			lt::time_point start_time = alerts[0]->timestamp();

			int num_started = 0;
			for (alert* a : alerts)
			{
				std::printf("%-3d %s\n", int(duration_cast<lt::seconds>(a->timestamp()
						- start_time).count()), a->message().c_str());
				if (alert_cast<torrent_resumed_alert>(a) == nullptr) continue;

				lt::time_point t = a->timestamp();
				if (last != lt::time_point::min())
				{
					// expect starting of new torrents to be spaced by 60 seconds
					// the division by 2 is to allow some slack (it's integer
					// division)
					TEST_EQUAL(duration_cast<lt::seconds>(t - last).count() / 2, 60 / 2);
				}
				last = t;
				++num_started;
			}

			TEST_EQUAL(num_started, num_torrents);

			for (torrent_handle const& h : ses.get_torrents())
			{
				TEST_CHECK(h.status().flags & torrent_flags::auto_managed);
				TEST_CHECK(!(h.status().flags & torrent_flags::paused));
			}
		});
}

TORRENT_TEST(count_slow_torrents)
{
	run_test(
		[](settings_pack& sett) {
			// session settings
			sett.set_bool(settings_pack::dont_count_slow_torrents, false);
			sett.set_int(settings_pack::active_downloads, 1);
			sett.set_int(settings_pack::active_seeds, 1);
		},

		[](lt::session& ses) {
			// add torrents
			for (int i = 0; i < num_torrents; ++i)
			{
				lt::add_torrent_params params = ::create_torrent(i, false);
				params.flags |= torrent_flags::auto_managed;
				params.flags |= torrent_flags::paused;
				ses.async_add_torrent(params);
			}
		},

		[](lt::session& ses) {
			// verify result (only one should have been started, even though
			// they're all idle)

			std::vector<lt::alert*> alerts;
			ses.pop_alerts(&alerts);

			lt::time_point start_time = alerts[0]->timestamp();

			int num_started = 0;
			for (alert* a : alerts)
			{
				std::printf("%-3d %s\n", int(duration_cast<lt::seconds>(a->timestamp()
						- start_time).count()), a->message().c_str());
				if (alert_cast<torrent_resumed_alert>(a) == nullptr) continue;
				++num_started;
			}

			TEST_EQUAL(num_started, 1);

			num_started = 0;
			for (torrent_handle const& h : ses.get_torrents())
			{
				TEST_CHECK(h.status().flags & torrent_flags::auto_managed);
				num_started += !(h.status().flags & torrent_flags::paused);
			}
			TEST_EQUAL(num_started, 1);
		});
}

TORRENT_TEST(force_stopped_download)
{
	run_test(
		[](settings_pack& sett) {
			// session settings
			sett.set_bool(settings_pack::dont_count_slow_torrents, true);
			sett.set_int(settings_pack::active_downloads, 10);
			sett.set_int(settings_pack::active_seeds, 10);
		},

		[](lt::session& ses) {
			// add torrents
			for (int i = 0; i < num_torrents; ++i)
			{
				lt::add_torrent_params params = ::create_torrent(i, false);
				// torrents are paused and not auto-managed
				params.flags &= ~torrent_flags::auto_managed;
				params.flags |= torrent_flags::paused;
				ses.async_add_torrent(params);
			}
		},

		[](lt::session& ses) {
			// verify result (none should have been started)

			std::vector<lt::alert*> alerts;
			ses.pop_alerts(&alerts);

			lt::time_point start_time = alerts[0]->timestamp();

			for (alert* a : alerts)
			{
				std::printf("%-3d %s\n", int(duration_cast<lt::seconds>(a->timestamp()
						- start_time).count()), a->message().c_str());
				// we don't expect any torrents being started or stopped, since
				// they're all force stopped
				TEST_CHECK(alert_cast<torrent_resumed_alert>(a) == nullptr);
				TEST_CHECK(alert_cast<torrent_paused_alert>(a) == nullptr);
			}

			for (torrent_handle const& h : ses.get_torrents())
			{
				TEST_CHECK(!(h.status().flags & torrent_flags::auto_managed));
				TEST_CHECK(h.status().flags & torrent_flags::paused);
			}
		});
}

TORRENT_TEST(force_started)
{
	run_test(
		[](settings_pack& sett) {
			// session settings
			sett.set_bool(settings_pack::dont_count_slow_torrents, false);
			sett.set_int(settings_pack::active_downloads, 1);
			sett.set_int(settings_pack::active_seeds, 1);
		},

		[](lt::session& ses) {
			// add torrents
			for (int i = 0; i < num_torrents; ++i)
			{
				lt::add_torrent_params params = ::create_torrent(i, false);
				// torrents are started and not auto-managed
				params.flags &= ~torrent_flags::auto_managed;
				params.flags &= ~torrent_flags::paused;
				ses.async_add_torrent(params);
			}
		},

		[](lt::session& ses) {
			// verify result (none should have been started)

			std::vector<lt::alert*> alerts;
			ses.pop_alerts(&alerts);

			lt::time_point start_time = alerts[0]->timestamp();

			for (alert* a : alerts)
			{
				std::printf("%-3d %s\n", int(duration_cast<lt::seconds>(a->timestamp()
						- start_time).count()), a->message().c_str());
				// we don't expect any torrents being started or stopped, since
				// they're all force started
				TEST_CHECK(alert_cast<torrent_resumed_alert>(a) == nullptr);
				TEST_CHECK(alert_cast<torrent_paused_alert>(a) == nullptr);
			}

			for (torrent_handle const& h : ses.get_torrents())
			{
				TEST_CHECK(!(h.status().flags & torrent_flags::auto_managed));
				TEST_CHECK(!(h.status().flags & torrent_flags::paused));
			}
		});
}

TORRENT_TEST(seed_limit)
{
	run_test(
		[](settings_pack& sett) {
			// session settings
			// set the seed limit to 3
			sett.set_bool(settings_pack::dont_count_slow_torrents, false);
			sett.set_int(settings_pack::active_checking, 1);
			sett.set_int(settings_pack::active_seeds, 3);
		},

		[](lt::session& ses) {
			// add torrents
			// add 5 seeds
			for (int i = 0; i < num_torrents; ++i)
			{
				lt::add_torrent_params params = ::create_torrent(i, true);
				// torrents are paused and auto-managed
				params.flags |= torrent_flags::auto_managed;
				params.flags |= torrent_flags::paused;
				ses.async_add_torrent(params);
			}
		},

		[](lt::session& ses) {
			// make sure only 3 got started
			std::vector<lt::alert*> alerts;
			ses.pop_alerts(&alerts);

			lt::time_point start_time = alerts[0]->timestamp();

			int num_started = 0;
			int num_checking = 0;
			int num_seeding = 0;
			for (alert* a : alerts)
			{
				std::printf("%-3d %s\n", int(duration_cast<lt::seconds>(a->timestamp()
						- start_time).count()), a->message().c_str());
				if (alert_cast<torrent_resumed_alert>(a))
				{
					++num_started;

					std::printf("started: %d checking: %d seeding: %d\n"
						, num_started, num_checking, num_seeding);
				}
				else if (alert_cast<torrent_paused_alert>(a))
				{
					TEST_CHECK(num_started > 0);
					--num_started;

					std::printf("started: %d checking: %d seeding: %d\n"
						, num_started, num_checking, num_seeding);
				}
				else if (state_changed_alert* sc = alert_cast<state_changed_alert>(a))
				{
					if (sc->prev_state == torrent_status::checking_files)
						--num_checking;
					else if (sc->prev_state == torrent_status::seeding)
						--num_seeding;

					if (sc->state == torrent_status::checking_files)
						++num_checking;
					else if (sc->state == torrent_status::seeding)
						++num_seeding;

					std::printf("started: %d checking: %d seeding: %d\n"
						, num_started, num_checking, num_seeding);

					// while at least one torrent is checking, there may be another
					// started torrent (the checking one), other than that, only 3
					// torrents are allowed to be started and seeding
					TEST_CHECK(num_started <= 3 + 1);
					TEST_CHECK(num_started <= 1 || num_seeding > 0);
				}
			}

			TEST_EQUAL(num_started, 3);

			num_started = 0;
			for (torrent_handle const& h : ses.get_torrents())
			{
				TEST_CHECK(h.status().flags & torrent_flags::auto_managed);
				TEST_CHECK(h.status().is_seeding);
				num_started += !(h.status().flags & torrent_flags::paused);
			}
			TEST_EQUAL(num_started, 3);
		});
}

TORRENT_TEST(download_limit)
{
	run_test(
		[](settings_pack& sett) {
			// session settings
			// set the seed limit to 3
			sett.set_bool(settings_pack::dont_count_slow_torrents, false);
			sett.set_int(settings_pack::active_checking, 1);
			sett.set_int(settings_pack::active_downloads, 3);
		},

		[](lt::session& ses) {
			// add torrents
			// add 5 seeds
			for (int i = 0; i < num_torrents; ++i)
			{
				lt::add_torrent_params params = ::create_torrent(i, false);
				// torrents are paused and auto-managed
				params.flags |= torrent_flags::auto_managed;
				params.flags |= torrent_flags::paused;
				ses.async_add_torrent(params);
			}
		},

		[](lt::session& ses) {
			// make sure only 3 got started
			std::vector<lt::alert*> alerts;
			ses.pop_alerts(&alerts);

			lt::time_point start_time = alerts[0]->timestamp();

			int num_started = 0;
			int num_checking = 0;
			int num_downloading = 0;
			for (alert* a : alerts)
			{
				std::printf("%-3d %s\n", int(duration_cast<lt::seconds>(a->timestamp()
						- start_time).count()), a->message().c_str());
				if (alert_cast<torrent_resumed_alert>(a))
				{
					++num_started;

					std::printf("started: %d checking: %d downloading: %d\n"
						, num_started, num_checking, num_downloading);
				}
				else if (alert_cast<torrent_paused_alert>(a))
				{
					TEST_CHECK(num_started > 0);
					--num_started;

					std::printf("started: %d checking: %d downloading: %d\n"
						, num_started, num_checking, num_downloading);
				}
				else if (state_changed_alert* sc = alert_cast<state_changed_alert>(a))
				{
					if (sc->prev_state == torrent_status::checking_files)
						--num_checking;
					else if (sc->prev_state == torrent_status::downloading)
						--num_downloading;

					if (sc->state == torrent_status::checking_files)
						++num_checking;
					else if (sc->state == torrent_status::downloading)
						++num_downloading;

					std::printf("started: %d checking: %d downloading: %d\n"
						, num_started, num_checking, num_downloading);

					// while at least one torrent is checking, there may be another
					// started torrent (the checking one), other than that, only 3
					// torrents are allowed to be started and seeding
					TEST_CHECK(num_started <= 3 + 1);
					TEST_CHECK(num_started <= 1 || num_downloading > 0);
				}
			}

			TEST_EQUAL(num_started, 3);

			num_started = 0;
			for (torrent_handle const& h : ses.get_torrents())
			{
				TEST_CHECK(h.status().flags & torrent_flags::auto_managed);
				TEST_CHECK(!h.status().is_finished);
				num_started += !(h.status().flags & torrent_flags::paused);
			}
			TEST_EQUAL(num_started, 3);
		});
}
// make sure torrents don't announce to the tracker when transitioning from
// checking to paused downloading
TORRENT_TEST(checking_announce)
{
	run_test(
		[](settings_pack& sett) {
			// session settings
			// set the seed limit to 3
			sett.set_bool(settings_pack::dont_count_slow_torrents, false);
			sett.set_int(settings_pack::active_checking, 1);

			// just set the tracker retry intervals really long, to make sure we
			// don't keep retrying the tracker (since there's nothing running
			// there, it will fail)
			sett.set_int(settings_pack::tracker_backoff, 100000);
			// only the first torrent added should ever announce
			sett.set_int(settings_pack::active_seeds, 1);
		},

		[](lt::session& ses) {
			// add torrents
			// add 5 seeds
			for (int i = 0; i < num_torrents; ++i)
			{
				lt::add_torrent_params params = ::create_torrent(i, true);
				// torrents are paused and auto-managed
				params.flags |= torrent_flags::auto_managed;
				params.flags |= torrent_flags::paused;
				// we need this to get the tracker_announce_alert
				params.trackers.push_back("http://10.10.0.2/announce");
				ses.async_add_torrent(params);
			}
		},

		[](lt::session& ses) {
			// make sure only 3 got started
			std::vector<lt::alert*> alerts;
			ses.pop_alerts(&alerts);

			lt::time_point start_time = alerts[0]->timestamp();

			int num_announce = 0;
			for (alert* a : alerts)
			{
				std::printf("%-3d %s\n", int(duration_cast<lt::seconds>(a->timestamp()
						- start_time).count()), a->message().c_str());
				if (alert_cast<tracker_announce_alert>(a))
					++num_announce;
			}

			TEST_EQUAL(num_announce, 2);

			int num_started = 0;
			for (torrent_handle const& h : ses.get_torrents())
			{
				TEST_CHECK(h.status().flags & torrent_flags::auto_managed);
				num_started += !(h.status().flags & torrent_flags::paused);
			}
			TEST_EQUAL(num_started, 1);
		});
}

TORRENT_TEST(paused_checking)
{
	run_test(
		[](settings_pack& sett) {
			// session settings
			// set the seed limit to 3
			sett.set_bool(settings_pack::dont_count_slow_torrents, true);
			sett.set_int(settings_pack::active_checking, 1);
		},

		[](lt::session& ses) {
			// add torrents
			// add 5 seeds
			for (int i = 0; i < num_torrents; ++i)
			{
				lt::add_torrent_params params = ::create_torrent(i, true);
				// torrents are paused and auto-managed
				params.flags &= ~torrent_flags::auto_managed;
				params.flags |= torrent_flags::paused;
				ses.async_add_torrent(params);
			}
		},

		[](lt::session& ses) {
			// make sure only 3 got started
			std::vector<lt::alert*> alerts;
			ses.pop_alerts(&alerts);

			lt::time_point start_time = alerts[0]->timestamp();

			for (alert* a : alerts)
			{
				std::printf("%-3d %s\n", int(duration_cast<lt::seconds>(a->timestamp()
						- start_time).count()), a->message().c_str());
				if (state_changed_alert* sc = alert_cast<state_changed_alert>(a))
				{
					TEST_CHECK(sc->state == torrent_status::checking_files
						|| sc->state == torrent_status::checking_resume_data);
				}
			}

			for (torrent_handle const& h : ses.get_torrents())
			{
				// even though all torrents are seeding, libtorrent shouldn't know
				// that, because they should never have been checked (because they
				// were force stopped)
				TEST_CHECK(!h.status().is_seeding);
				TEST_CHECK(!(h.status().flags & torrent_flags::auto_managed));
				TEST_CHECK(h.status().flags & torrent_flags::paused);
			}
		});
}

// set the stop_when_ready flag and make sure we receive a paused alert *before*
// a state_changed_alert
TORRENT_TEST(stop_when_ready)
{
	run_test(
		[](settings_pack&) {},

		[](lt::session& ses) {
			// add torrents
			lt::add_torrent_params params = ::create_torrent(0, true);
			// torrents are started and auto-managed
			params.flags |= torrent_flags::auto_managed;
			params.flags |= torrent_flags::stop_when_ready;
			// we need this to get the tracker_announce_alert
			params.trackers.push_back("http://10.10.0.2/announce");
			ses.async_add_torrent(params);
		},

		[](lt::session& ses) {
			// verify result (none should have been started)
			std::vector<lt::alert*> alerts;
			ses.pop_alerts(&alerts);

			lt::time_point start_time = alerts[0]->timestamp();

			int num_paused = 0;
			int num_seeding = 0;
			time_point seeding_time;
			for (alert* a : alerts)
			{
				std::printf("%-3d %s\n", int(duration_cast<lt::seconds>(a->timestamp()
						- start_time).count()), a->message().c_str());

				if (alert_cast<torrent_paused_alert>(a))
				{
					++num_paused;
					TEST_EQUAL(num_seeding, 1);
					TEST_CHECK(a->timestamp() == seeding_time);
				}

				if (state_changed_alert* sc = alert_cast<state_changed_alert>(a))
				{
					if (sc->state == torrent_status::seeding)
					{
						++num_seeding;
						seeding_time = a->timestamp();
					}
				}
				// there should not have been any announces. the torrent should have
				// been stopped *before* announcing.
				TEST_CHECK(alert_cast<tracker_announce_alert>(a) == nullptr);
			}

			TEST_EQUAL(num_paused, 1);
			TEST_EQUAL(num_seeding, 1);

			for (torrent_handle const& h : ses.get_torrents())
			{
				// the torrent should have been force-stopped (after checking was
				// donw, because we set the stop_when_ready flag). Force stopped
				// means not auto-managed and paused.
				torrent_status st = h.status();
				TEST_CHECK(!(st.flags & torrent_flags::auto_managed));
				TEST_CHECK(st.flags & torrent_flags::paused);
				// it should be seeding. If it's not seeding it may not have had its
				// files checked.
				TEST_EQUAL(st.state, torrent_status::seeding);
			}
		});
}

// This test makes sure that the fastresume check will still run for stopped
// torrents. The actual checking of files won't start until the torrent is
// un-paused/resumed though
TORRENT_TEST(resume_reject_when_paused)
{
	run_test(
		[](settings_pack& sett) {
			sett.set_int(settings_pack::alert_mask, alert_category::all);
		},

		[](lt::session& ses) {
			// add torrents
			lt::add_torrent_params params = ::create_torrent(0, true);

			// the torrent is not auto managed and paused. Once the resume data
			// check completes, it will stay paused but the state_changed_alert
			// will be posted, when it goes to check the files
			params.flags &= ~torrent_flags::auto_managed;
			params.flags |= torrent_flags::paused;

			ses.async_add_torrent(params);
		},

		[](lt::session& ses) {
			std::vector<lt::alert*> alerts;
			ses.pop_alerts(&alerts);

			lt::time_point start_time = alerts[0]->timestamp();

			int num_piece_finished = 0;
			int checking_files = 0;
			int state_changed = 0;

			for (alert* a : alerts)
			{
				std::printf("%-3d %-25s %s\n", int(duration_cast<lt::seconds>(a->timestamp()
						- start_time).count())
					, a->what()
					, a->message().c_str());

				if (alert_cast<piece_finished_alert>(a))
					++num_piece_finished;

				if (auto sc = alert_cast<state_changed_alert>(a))
				{
					if (sc->state == torrent_status::checking_files)
						++checking_files;
					++state_changed;
				}
			}

			for (torrent_handle const& h : ses.get_torrents())
			{
				// the torrent should have been force-stopped. Force stopped means
				// not auto-managed and paused.
				torrent_status st = h.status();
				TEST_CHECK(!(st.flags & torrent_flags::auto_managed));
				TEST_CHECK(st.flags & torrent_flags::paused);
				// it should be checking files, because the resume data should have
				// failed validation.
				TEST_EQUAL(st.state, torrent_status::checking_files);
			}

			TEST_EQUAL(num_piece_finished, 0);
			// it should not actually check the files (since it's paused)
			// if the files were checked, the state would change to downloading
			// immediately, and state_changed would be 2. This asserts that's not
			// the case.
			TEST_EQUAL(state_changed, 1);
			TEST_EQUAL(checking_files, 1);
		});
}

// this test adds the torrent in paused state and no resume data. Expecting the
// resume check to complete and just transition into checking state, but without
// actually checking anything
TORRENT_TEST(no_resume_when_paused)
{
	run_test(
		[](settings_pack& sett) {
			sett.set_int(settings_pack::alert_mask, alert_category::all);
		},

		[](lt::session& ses) {
			// add torrents
			lt::add_torrent_params params = ::create_torrent(0, true);

			// the torrent is not auto managed and paused.
			params.flags &= ~torrent_flags::auto_managed;
			params.flags |= torrent_flags::paused;

			ses.async_add_torrent(params);
		},

		[](lt::session& ses) {
			std::vector<lt::alert*> alerts;
			ses.pop_alerts(&alerts);

			lt::time_point start_time = alerts[0]->timestamp();

			int num_piece_finished = 0;
			int resume_rejected = 0;
			int state_changed = 0;

			for (alert* a : alerts)
			{
				std::printf("%-3d %-25s %s\n", int(duration_cast<lt::seconds>(a->timestamp()
						- start_time).count())
					, a->what()
					, a->message().c_str());

				if (alert_cast<piece_finished_alert>(a))
					++num_piece_finished;

				if (alert_cast<fastresume_rejected_alert>(a))
					++resume_rejected;

				if (auto sc = alert_cast<state_changed_alert>(a))
				{
					if (sc->state == torrent_status::checking_files)
						++state_changed;
				}
			}

			for (torrent_handle const& h : ses.get_torrents())
			{
				// the torrent should have been force-stopped. Force stopped means
				// not auto-managed and paused.
				torrent_status st = h.status();
				TEST_CHECK(!(st.flags & torrent_flags::auto_managed));
				TEST_CHECK(st.flags & torrent_flags::paused);
				// it should be checking files, because the resume data should have
				// failed validation.
				TEST_EQUAL(st.state, torrent_status::checking_files);
			}

			TEST_EQUAL(num_piece_finished, 0);
			TEST_EQUAL(resume_rejected, 0);
			TEST_EQUAL(state_changed, 1);
		});
}

// this is just asserting that when the files are checked we do in fact get
// piece_finished_alerts. The other tests rely on this assumption
TORRENT_TEST(no_resume_when_started)
{
	run_test(
		[](settings_pack& sett) {
			sett.set_int(settings_pack::alert_mask, alert_category::all);
		},

		[](lt::session& ses) {
			// add torrents
			lt::add_torrent_params params = ::create_torrent(0, true);
			ses.async_add_torrent(params);
		},

		[](lt::session& ses) {
			std::vector<lt::alert*> alerts;
			ses.pop_alerts(&alerts);

			lt::time_point start_time = alerts[0]->timestamp();

			int num_piece_finished = 0;
			int state_changed = 0;

			for (alert* a : alerts)
			{
				std::printf("%-3d %-25s %s\n", int(duration_cast<lt::seconds>(a->timestamp()
						- start_time).count())
					, a->what()
					, a->message().c_str());

				if (alert_cast<piece_finished_alert>(a))
					++num_piece_finished;

				if (auto sc = alert_cast<state_changed_alert>(a))
				{
					if (sc->state == torrent_status::checking_files)
						++state_changed;
				}
			}

			TEST_EQUAL(num_piece_finished, 9);
			TEST_EQUAL(state_changed, 1);
		});
}

// when setting active_seeds to 0, any completed torrent should be paused
TORRENT_TEST(pause_completed_torrents)
{
	run_test(
		[](settings_pack& sett) {
			// session settings
			sett.set_bool(settings_pack::dont_count_slow_torrents, true);
			sett.set_int(settings_pack::active_downloads, 1);
			sett.set_int(settings_pack::active_seeds, 0);
		},

		[](lt::session& ses) {
			// add torrent
			lt::add_torrent_params params = ::create_torrent(0, true);
			params.flags |= torrent_flags::auto_managed;
			params.flags |= torrent_flags::paused;
			ses.async_add_torrent(params);
		},

		[](lt::session& ses) {
			// verify result
			// the torrent should have been paused immediately as it completed,
			// since we don't allow any seeding torrents

			std::vector<lt::alert*> alerts;
			ses.pop_alerts(&alerts);

			lt::time_point start_time = alerts[0]->timestamp();

			int num_started = 0;
			int num_finished = 0;
			int num_paused = 0;
			lt::time_point finished;
			lt::time_point paused;
			for (alert* a : alerts)
			{
				std::printf("%-3d %s\n", int(duration_cast<lt::seconds>(a->timestamp()
						- start_time).count()), a->message().c_str());
				if (alert_cast<torrent_resumed_alert>(a))
					++num_started;
				if (alert_cast<torrent_finished_alert>(a))
				{
					++num_finished;
					finished = a->timestamp();
				}
				if (alert_cast<torrent_paused_alert>(a))
				{
					++num_paused;
					paused = a->timestamp();
				}
			}

			TEST_EQUAL(num_started, 1);
			TEST_EQUAL(num_finished, 1);
			TEST_EQUAL(num_paused, 1);

			if (num_finished > 0 && num_paused > 0)
			{
				TEST_CHECK(paused >= finished);
				TEST_CHECK(paused - finished < chrono::milliseconds(1));
			}

			num_paused = 0;
			for (torrent_handle const& h : ses.get_torrents())
			{
				TEST_CHECK(h.status().flags & torrent_flags::auto_managed);
				num_paused += bool(h.status().flags & torrent_flags::paused);
			}
			TEST_EQUAL(num_paused, 1);
		});
}


// once a torrent has been marked inactive (because its transfer rate dropped
// below the inactive_down_rate threshold) it must stop being exempt from the
// active_downloads limit as soon as it's no longer slow, rather than remaining
// exempt for another auto_manage_startup seconds. This test doesn't simulate a
// rate recovery, it lowers inactive_down_rate below zero instead, which flips
// is_inactive_internal() from true to false the way a real rate recovery
// would.
TORRENT_TEST(inactive_recovery_is_immediate)
{
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	std::unique_ptr<sim::asio::io_context> ios = make_io_context(sim, 0);
	lt::session_proxy zombie;

	int const auto_manage_startup = 20;

	lt::settings_pack pack = settings();
	pack.set_bool(lt::settings_pack::dont_count_slow_torrents, true);
	pack.set_int(lt::settings_pack::active_downloads, 1);
	pack.set_int(lt::settings_pack::active_seeds, 1);
	pack.set_int(lt::settings_pack::auto_manage_startup, auto_manage_startup);
	pack.set_int(lt::settings_pack::inactive_down_rate, 1);
	// the fake peer never completes the BT handshake (it doesn't send one
	// back), so avoid the connection being torn down as a handshake timeout
	// during the test
	pack.set_int(lt::settings_pack::handshake_timeout, 3600);

	std::shared_ptr<lt::session> ses = std::make_shared<lt::session>(pack, *ios);

	// keep torrent A connected to a (silent) peer, without any real transfer,
	// so it keeps being ticked even after it's marked inactive
	fake_peer peer_a(sim, "60.0.0.0");

	lt::torrent_handle h_a;
	lt::torrent_handle h_b;
	int num_added = 0;

	lt::time_point start_time;
	bool have_start_time = false;

	struct pause_event
	{
		lt::time_point ts;
		lt::torrent_handle h;
	};
	std::vector<pause_event> pause_events;

	print_alerts(*ses, [&](lt::session&, lt::alert const* a) {
		if (!have_start_time)
		{
			start_time = a->timestamp();
			have_start_time = true;
		}

		if (auto* at = lt::alert_cast<lt::add_torrent_alert>(a))
		{
			if (num_added == 0)
			{
				h_a = at->handle;
				add_fake_peer(h_a, 0);
			}
			else
			{
				h_b = at->handle;
			}
			++num_added;
		}

		if (auto* pa = lt::alert_cast<lt::torrent_paused_alert>(a))
			pause_events.push_back({a->timestamp(), pa->handle});
	});

	lt::add_torrent_params params_a = ::create_torrent(0, false);
	params_a.flags |= lt::torrent_flags::auto_managed;
	params_a.flags |= lt::torrent_flags::paused;
	ses->async_add_torrent(params_a);

	lt::add_torrent_params params_b = ::create_torrent(1, false);
	params_b.flags |= lt::torrent_flags::auto_managed;
	params_b.flags |= lt::torrent_flags::paused;
	ses->async_add_torrent(params_b);

	// by now torrent A should have been marked inactive (at
	// auto_manage_startup) and torrent B should have started to fill the
	// freed slot. Simulate torrent A's rate recovering by lowering the
	// inactive threshold below its (always zero) rate. This is comfortably
	// before torrent B would independently be marked inactive on its own
	// (which would happen at roughly 2 * auto_manage_startup).
	int const recover_time = auto_manage_startup * 3 / 2;
	sim::timer t2(sim, lt::seconds(recover_time), [&](boost::system::error_code const&) {
		lt::settings_pack p;
		p.set_int(lt::settings_pack::inactive_down_rate, -1);
		ses->apply_settings(p);
	});

	sim::timer end_timer(
		sim, lt::seconds(auto_manage_startup * 9 / 4), [&](boost::system::error_code const&) {
			zombie = ses->abort();
			ses.reset();
		});

	sim.run();

	// torrent B should have been paused again shortly after torrent A
	// recovers (recovering immediately), not another auto_manage_startup
	// seconds later
	bool paused_promptly = false;
	for (auto const& e : pause_events)
	{
		int const t = int(lt::duration_cast<lt::seconds>(e.ts - start_time).count());
		std::printf("pause-event t=%d\n", t);
		if (e.h == h_b && t >= recover_time && t < recover_time + auto_manage_startup / 2)
			paused_promptly = true;
	}
	TEST_CHECK(paused_promptly);
}

// TODO: assert that the torrent_paused_alert is posted when pausing
//       downloading, seeding, checking torrents as well as the graceful pause
// TODO: test limits of tracker, DHT and LSD announces


