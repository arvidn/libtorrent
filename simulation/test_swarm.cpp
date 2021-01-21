/*

Copyright (c) 2008, Arvid Norberg
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

#include "setup_swarm.hpp"
#include "test.hpp"
#include "utils.hpp"
#include "libtorrent/alert.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/session_stats.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/time.hpp"
#include "settings.hpp"
#include "setup_transfer.hpp" // for ep()
#include "fake_peer.hpp"

#include "simulator/nat.hpp"
#include "simulator/queue.hpp"
#include "utils.hpp"

#include <fstream>

using namespace lt;

TORRENT_TEST(seed_mode)
{
	// with seed mode
	setup_swarm(3, swarm_test::upload
		// add session
		, [](lt::settings_pack&) {}
		// add torrent
		, [](lt::add_torrent_params& params) {
			params.flags |= torrent_flags::seed_mode;
		}
		// on alert
		, [](lt::alert const*, lt::session&) {}
		// terminate
		, [](int, lt::session&) -> bool
		{ return false; });
}

TORRENT_TEST(seed_mode_disable_hash_checks)
{
	// all nodes need to disable hash checking, otherwise the downloader would
	// just fail
	settings_pack swarm_settings = settings();
	swarm_settings.set_bool(settings_pack::disable_hash_checks, true);

	dsl_config network_cfg;
	sim::simulation sim{network_cfg};

	// with seed mode
	setup_swarm(2, swarm_test::upload, sim, swarm_settings, add_torrent_params()
		// add session
		, [](lt::settings_pack& pack) {
			pack.set_int(settings_pack::suggest_mode, settings_pack::suggest_read_cache);
		}
		// add torrent
		, [](lt::add_torrent_params& params) {
			params.flags |= torrent_flags::seed_mode;
			// just to make sure the disable_hash_checks really work, we
			// shouldn't be verifying anything from the storage
//			params.storage = disabled_storage_constructor;
		}
		// on alert
		, [](lt::alert const*, lt::session&) {}
		// terminate
		, [](int, lt::session&) -> bool
		{ return false; });
}

TORRENT_TEST(seed_mode_suggest)
{
	setup_swarm(2, swarm_test::upload
		// add session
		, [](lt::settings_pack& pack) {
			pack.set_int(settings_pack::suggest_mode, settings_pack::suggest_read_cache);
#if TORRENT_ABI_VERSION == 1
			pack.set_int(settings_pack::cache_size, 2);
#endif
		}
		// add torrent
		, [](lt::add_torrent_params& params) {
			params.flags |= torrent_flags::seed_mode;
		}
		// on alert
		, [](lt::alert const*, lt::session&) {}
		// terminate
		, [](int, lt::session&) -> bool
		{ return true; });
}

TORRENT_TEST(plain)
{
	setup_swarm(2, swarm_test::download
		// add session
		, [](lt::settings_pack&) {}
		// add torrent
		, [](lt::add_torrent_params&) {}
		// on alert
		, [](lt::alert const*, lt::session&) {}
		// terminate
		, [](int const ticks, lt::session& ses) -> bool
		{
			if (ticks > 80)
			{
				TEST_ERROR("timeout");
				return true;
			}
			if (!is_seed(ses)) return false;
			std::printf("completed in %d ticks\n", ticks);
			return true;
		});
}

TORRENT_TEST(session_stats)
{
	std::vector<stats_metric> stats = session_stats_metrics();
	int const downloading_idx = find_metric_idx("ses.num_downloading_torrents");
	TEST_CHECK(downloading_idx >= 0);
	int const incoming_extended_idx = find_metric_idx("ses.num_incoming_extended");
	TEST_CHECK(incoming_extended_idx >= 0);

	setup_swarm(2, swarm_test::download
		// add session
		, [](lt::settings_pack&) {}
		// add torrent
		, [](lt::add_torrent_params&) {}
		// on alert
		, [=](lt::alert const* a, lt::session&)
		{
			auto const* ss = lt::alert_cast<session_stats_alert>(a);
			if (!ss) return;

			// there's one downloading torrent
			TEST_EQUAL(ss->counters()[downloading_idx], 1);
			TEST_EQUAL(ss->counters()[incoming_extended_idx], 1);
		}
		// terminate
		, [](int const ticks, lt::session& ses) -> bool
		{
			ses.post_session_stats();
			if (ticks > 80)
			{
				TEST_ERROR("timeout");
				return true;
			}
			if (!is_seed(ses)) return false;
			std::printf("completed in %d ticks\n", ticks);
			return true;
		});
}

// this test relies on picking up log alerts
#ifndef TORRENT_DISABLE_LOGGING
TORRENT_TEST(suggest)
{
	int num_suggests = 0;
	setup_swarm(10, swarm_test::upload
		// add session
		, [](lt::settings_pack& pack) {
			pack.set_int(settings_pack::suggest_mode, settings_pack::suggest_read_cache);
			pack.set_int(settings_pack::max_suggest_pieces, 10);
#if TORRENT_ABI_VERSION == 1
			pack.set_int(settings_pack::cache_size, 2);
#endif
		}
		// add torrent
		, [](lt::add_torrent_params&) {}
		// on alert
		, [&num_suggests](lt::alert const* a, lt::session&) {
			if (auto pl = alert_cast<peer_log_alert>(a))
			{
				if (pl->direction == peer_log_alert::outgoing_message
					&& pl->event_type == std::string("SUGGEST"))
				{
					++num_suggests;
				}
			}
		}
		// terminate
		, [](int const ticks, lt::session&) -> bool
		{
			if (ticks > 500)
			{
				return true;
			}
			return false;
		});

	// for now, just make sure we send any suggests at all. This feature is
	// experimental and it's not entirely clear it's correct or how to verify
	// that it does what it's supposed to do.
	// perhaps a better way would be to look at piece upload distribution over
	// time
	TEST_CHECK(num_suggests > 0);
}
#endif

TORRENT_TEST(utp_only)
{
	setup_swarm(2, swarm_test::download
		// add session
		, [](lt::settings_pack& pack) {
			pack.set_bool(settings_pack::enable_incoming_utp, true);
			pack.set_bool(settings_pack::enable_outgoing_utp, true);
			pack.set_bool(settings_pack::enable_incoming_tcp, false);
			pack.set_bool(settings_pack::enable_outgoing_tcp, false);
		}
		// add torrent
		, [](lt::add_torrent_params&) {}
		// on alert
		, [](lt::alert const*, lt::session&) {}
		// terminate
		, [](int const ticks, lt::session& ses) -> bool
		{
			if (ticks > 80)
			{
				TEST_ERROR("timeout");
				return true;
			}
			if (!is_seed(ses)) return false;
			return true;
		});
}

void test_stop_start_download(swarm_test_t type, bool graceful)
{
	bool paused_once = false;
	bool resumed = false;

	setup_swarm(3, type
		// add session
		, [](lt::settings_pack& pack) {
			// this test will pause and resume the torrent immediately, we expect
			// to reconnect immediately too, so disable the min reconnect time
			// limit.
			pack.set_int(settings_pack::min_reconnect_time, 0);
		}
		// add torrent
		, [](lt::add_torrent_params&) {

		}
		// on alert
		, [&](lt::alert const* a, lt::session& ses) {

			if (lt::alert_cast<lt::add_torrent_alert>(a))
				add_extra_peers(ses);

			if (auto tp = lt::alert_cast<lt::torrent_paused_alert>(a))
			{
				TEST_EQUAL(resumed, false);
				std::printf("\nSTART\n\n");
				tp->handle.resume();
				resumed = true;
			}
		}
		// terminate
		, [&](int const ticks, lt::session& ses) -> bool
		{
			if (paused_once == false)
			{
				auto st = get_status(ses);
				const bool limit_reached = (type & swarm_test::download)
					? st.total_wanted_done > st.total_wanted / 2
					: st.total_payload_upload >= 3 * 16 * 1024;

				if (limit_reached)
				{
					std::printf("\nSTOP\n\n");
					auto h = ses.get_torrents()[0];
					h.pause(graceful ? torrent_handle::graceful_pause : pause_flags_t{});
					paused_once = true;
				}
			}

			std::printf("tick: %d\n", ticks);

			const int timeout = (type & swarm_test::download) ? 22 : 100;
			if (ticks > timeout)
			{
				TEST_ERROR("timeout");
				return true;
			}
			if (type & swarm_test::upload) return false;
			if (!is_seed(ses)) return false;
			std::printf("completed in %d ticks\n", ticks);
			return true;
		});

	TEST_EQUAL(paused_once, true);
	TEST_EQUAL(resumed, true);
}

TORRENT_TEST(stop_start_download)
{
	test_stop_start_download(swarm_test::download, false);
}

TORRENT_TEST(stop_start_download_graceful)
{
	test_stop_start_download(swarm_test::download, true);
}

TORRENT_TEST(stop_start_download_graceful_no_peers)
{
	bool paused_once = false;
	bool resumed = false;

	setup_swarm(1, swarm_test::download
		// add session
		, [](lt::settings_pack&) {}
		// add torrent
		, [](lt::add_torrent_params&) {}
		// on alert
		, [&](lt::alert const* a, lt::session&) {
			if (auto tp = lt::alert_cast<lt::torrent_paused_alert>(a))
			{
				TEST_EQUAL(resumed, false);
				std::printf("\nSTART\n\n");
				tp->handle.resume();
				resumed = true;
			}
		}
		// terminate
		, [&](int const ticks, lt::session& ses) -> bool
		{
			if (paused_once == false
				&& ticks == 6)
			{
				std::printf("\nSTOP\n\n");
				auto h = ses.get_torrents()[0];
				h.pause(torrent_handle::graceful_pause);
				paused_once = true;
			}

			std::printf("tick: %d\n", ticks);

			// when there's only one node (i.e. no peers) we won't ever download
			// the torrent. It's just a test to make sure we still get the
			// torrent_paused_alert
			return ticks > 60;
		});

	TEST_EQUAL(paused_once, true);
	TEST_EQUAL(resumed, true);
}


TORRENT_TEST(stop_start_seed)
{
	test_stop_start_download(swarm_test::upload, false);
}

TORRENT_TEST(stop_start_seed_graceful)
{
	test_stop_start_download(swarm_test::upload, true);
}

TORRENT_TEST(shutdown)
{
	setup_swarm(4, swarm_test::download
		// add session
		, [](lt::settings_pack&) {}
		// add torrent
		, [](lt::add_torrent_params&) {}
		// on alert
		, [](lt::alert const*, lt::session&) {}
		// terminate
		, [](int, lt::session& ses) -> bool
		{
			if (completed_pieces(ses) == 0) return false;
			TEST_EQUAL(is_seed(ses), false);
			return true;
		});
}

// make the delays on the connections unreasonable long, so libtorrent times-out
// the connection attempts
struct timeout_config : sim::default_config
{
	virtual sim::route incoming_route(lt::address ip) override
	{
		auto it = m_incoming.find(ip);
		if (it != m_incoming.end()) return sim::route().append(it->second);
		it = m_incoming.insert(it, std::make_pair(ip, std::make_shared<queue>(
			m_sim->get_io_context()
			, 1000
			, lt::duration_cast<lt::time_duration>(seconds(10))
			, 1000, "packet-loss modem in")));
		return sim::route().append(it->second);
	}

	virtual sim::route outgoing_route(lt::address ip) override
	{
		auto it = m_outgoing.find(ip);
		if (it != m_outgoing.end()) return sim::route().append(it->second);
		it = m_outgoing.insert(it, std::make_pair(ip, std::make_shared<queue>(
			m_sim->get_io_context(), 1000
			, lt::duration_cast<lt::time_duration>(seconds(5)), 200 * 1000, "packet-loss out")));
		return sim::route().append(it->second);
	}
};

// make sure peers that are no longer alive are handled correctly.
TORRENT_TEST(dead_peers)
{
	int num_connect_timeout = 0;

	timeout_config network_cfg;
	sim::simulation sim{network_cfg};
	setup_swarm(1, swarm_test::download, sim
		// add session
		, [](lt::settings_pack& p) {
			p.set_int(settings_pack::peer_connect_timeout, 1);
		}
		// add torrent
		, [](lt::add_torrent_params& params) {
			params.peers.assign({
				ep("66.66.66.60", 9999)
				, ep("66.66.66.61", 9999)
				, ep("66.66.66.62", 9999)
			});
		}
		// on alert
		, [&](lt::alert const* a, lt::session&) {
			auto* e = alert_cast<peer_disconnected_alert>(a);
			if (e
				&& e->op == operation_t::connect
				&& e->error == error_code(errors::timed_out))
			{
				++num_connect_timeout;
			}
		}
		// terminate
		, [](int t, lt::session&) -> bool
		{ return t > 100; });

	TEST_EQUAL(num_connect_timeout, 3);
}

// the address 50.0.0.1 sits behind a NAT. All of its outgoing connections have
// their source address rewritten to 51.51.51.51
struct nat_config : sim::default_config
{
	nat_config() : m_nat_hop(std::make_shared<nat>(addr("51.51.51.51"))) {}

	sim::route outgoing_route(lt::address ip) override
	{
		// This is extremely simplistic. It will simply alter the percieved source
		// IP of the connecting client.
		sim::route r;
		if (ip == addr("50.0.0.1")) r.append(m_nat_hop);
		return r;
	}
	std::shared_ptr<nat> m_nat_hop;
};

TORRENT_TEST(self_connect)
{
	int num_self_connection_disconnects = 0;

	nat_config network_cfg;
	sim::simulation sim{network_cfg};

	setup_swarm(1, swarm_test::download, sim
		// add session
		, [](lt::settings_pack& p) {
			p.set_bool(settings_pack::enable_incoming_utp, false);
			p.set_bool(settings_pack::enable_outgoing_utp, false);
		}
		// add torrent
		, [](lt::add_torrent_params& params) {
			// this is our own address and listen port, just to make sure we get
			// ourself as a peer (which normally happens one way or another in the
			// wild)
			params.peers.assign({ep("50.0.0.1", 6881)});
		}
		// on alert
		, [&](lt::alert const* a, lt::session&) {
			auto* e = alert_cast<peer_disconnected_alert>(a);
			if (e
				&& e->op == operation_t::bittorrent
				&& e->error == error_code(errors::self_connection))
			{
				++num_self_connection_disconnects;
			}
		}
		// terminate
		, [](int t, lt::session&) -> bool
		{ return t > 100; });

	TEST_EQUAL(num_self_connection_disconnects, 1);
}

TORRENT_TEST(delete_files)
{
	std::string save_path;

	setup_swarm(2, swarm_test::download
		// add session
		, [](lt::settings_pack&) {}
		// add torrent
		, [](lt::add_torrent_params&) {}
		// on alert
		, [](lt::alert const*, lt::session&) {}
		// terminate
		, [&save_path](int, lt::session& ses) -> bool
		{
			if (completed_pieces(ses) == 0) return false;

			auto h = ses.get_torrents()[0];
			save_path = h.status().save_path;
			ses.remove_torrent(h, session::delete_files);
			return true;
		});

	// assert the file is no longer there
	file_status st;
	error_code ec;
	stat_file(combine_path(save_path, "temporary"), &st, ec);
	std::printf("expecting \"%s/temporary\" to NOT exist [%s | %s]\n"
		, save_path.c_str()
		, ec.category().name()
		, ec.message().c_str());
	TEST_EQUAL(ec, error_code(boost::system::errc::no_such_file_or_directory, system_category()));
}

TORRENT_TEST(delete_partfile)
{
	std::string save_path;
	setup_swarm(2, swarm_test::download
		// add session
		, [](lt::settings_pack&) {}
		// add torrent
		, [](lt::add_torrent_params&) {}
		// on alert
		, [](lt::alert const*, lt::session&) {}
		// terminate
		, [&save_path](int, lt::session& ses) -> bool
		{
			if (completed_pieces(ses) == 0) return false;

			auto h = ses.get_torrents()[0];
			save_path = h.status().save_path;
			ses.remove_torrent(h, session::delete_partfile);
			return true;
		});
	// assert the file *is* still there
	file_status st;
	error_code ec;
	stat_file(combine_path(save_path, "temporary"), &st, ec);
	std::printf("expecting \"%s/temporary\" to exist [%s]\n", save_path.c_str()
		, ec.message().c_str());
	TEST_CHECK(!ec);
}

TORRENT_TEST(torrent_completed_alert)
{
	int num_file_completed = false;

	setup_swarm(2, swarm_test::download
		// add session
		, [](lt::settings_pack& pack)
		{
			pack.set_int(lt::settings_pack::alert_mask, alert_category::file_progress);
		}
		// add torrent
		, [](lt::add_torrent_params&) {}
		// on alert
		, [&](lt::alert const* a, lt::session&)
		{
			auto tc = alert_cast<lt::file_completed_alert>(a);
			if (tc == nullptr) return;
			++num_file_completed;
		}
		// terminate
		, [](int ticks, lt::session& ses) -> bool
		{
			if (ticks > 80)
			{
				TEST_ERROR("timeout");
				return true;
			}
			if (!is_seed(ses)) return false;
			printf("completed in %d ticks\n", ticks);
			return true;
		});

	TEST_EQUAL(num_file_completed, 1);
}

TORRENT_TEST(block_uploaded_alert)
{
	// blocks[piece count][number of blocks per piece] (each block's element will
	// be set to true when a block_uploaded_alert alert is received for that block)
	std::vector<std::vector<bool>> blocks;

	setup_swarm(2, swarm_test::upload
		// add session
		, [](lt::settings_pack& pack)
		{
			pack.set_int(lt::settings_pack::alert_mask,
				alert_category::upload | alert_category::status);
		}
		// add torrent
		, [](lt::add_torrent_params&) {}
		// on alert
		, [&](lt::alert const* a, lt::session&) {
			if (auto at = lt::alert_cast<lt::add_torrent_alert>(a))
			{
				// init blocks vector, MUST happen before any block_uploaded_alert alerts
				int blocks_per_piece = at->handle.torrent_file()->piece_length() / 0x4000;
				blocks.resize(at->handle.torrent_file()->num_pieces(), std::vector<bool>(blocks_per_piece, false));
			}
			else if (auto ua = lt::alert_cast<lt::block_uploaded_alert>(a))
			{
				TEST_EQUAL(blocks[static_cast<int>(ua->piece_index)][ua->block_index], false);
				blocks[static_cast<int>(ua->piece_index)][ua->block_index] = true;
			}
		}
		// terminate
		, [](int, lt::session&) -> bool
		{ return false; });

		// ensure a block_uploaded_alert was received for each block in the torrent
		TEST_CHECK(std::all_of(blocks.begin(), blocks.end(),
			[](std::vector<bool> const& piece_row) {
				return std::all_of(piece_row.begin(), piece_row.end(),
					[](bool upload_alert_received) {
						return upload_alert_received;
					}
				);
			}
		));
}

// template for testing running swarms with edge case settings
template <typename SettingsFun>
void test_settings(SettingsFun fun)
{
	setup_swarm(2, swarm_test::download
		// add session
		, fun
		// add torrent
		, [](lt::add_torrent_params&) {}
		// on alert
		, [](lt::alert const*, lt::session&) {}
		// terminate
		, [](int ticks, lt::session& ses) -> bool
		{
			if (ticks > 89)
			{
				TEST_ERROR("timeout");
				return true;
			}
			if (!is_seed(ses)) return false;
			return true;
		});
}

TORRENT_TEST(unlimited_connections)
{
	test_settings([](lt::settings_pack& pack) {
		pack.set_int(settings_pack::connections_limit, std::numeric_limits<int>::max()); }
	);
}

TORRENT_TEST(default_connections_limit)
{
	test_settings([](lt::settings_pack& pack) {
		pack.set_int(settings_pack::connections_limit, 0); }
	);
}

TORRENT_TEST(default_connections_limit_negative)
{
	test_settings([](lt::settings_pack& pack) {
		pack.set_int(settings_pack::connections_limit, -1); }
	);
}


TORRENT_TEST(redundant_have)
{
	test_settings([](lt::settings_pack& pack) {
		pack.set_bool(settings_pack::send_redundant_have, false); }
	);
}

#if TORRENT_ABI_VERSION == 1
TORRENT_TEST(lazy_bitfields)
{
	test_settings([](lt::settings_pack& pack) {
		pack.set_bool(settings_pack::lazy_bitfields, true); }
	);
}
#endif

TORRENT_TEST(prioritize_partial_pieces)
{
	test_settings([](lt::settings_pack& pack) {
		pack.set_bool(settings_pack::prioritize_partial_pieces, true); }
	);
}

TORRENT_TEST(active_downloads)
{
	test_settings([](lt::settings_pack& pack) {
		pack.set_int(settings_pack::active_downloads, std::numeric_limits<int>::max()); }
	);
}

TORRENT_TEST(active_seeds)
{
	test_settings([](lt::settings_pack& pack) {
		pack.set_int(settings_pack::active_seeds, std::numeric_limits<int>::max()); }
	);
}

TORRENT_TEST(active_seeds_negative)
{
	test_settings([](lt::settings_pack& pack) {
		pack.set_int(settings_pack::active_seeds, -1); }
	);
}

TORRENT_TEST(active_limit)
{
	test_settings([](lt::settings_pack& pack) {
		pack.set_int(settings_pack::active_limit, std::numeric_limits<int>::max()); }
	);
}

TORRENT_TEST(active_limit_negative)
{
	test_settings([](lt::settings_pack& pack) {
		pack.set_int(settings_pack::active_limit, -1); }
	);
}

TORRENT_TEST(upload_rate_limit)
{
	test_settings([](lt::settings_pack& pack) {
		pack.set_int(settings_pack::upload_rate_limit, std::numeric_limits<int>::max()); }
	);
}

TORRENT_TEST(upload_rate_limit_negative)
{
	test_settings([](lt::settings_pack& pack) {
		pack.set_int(settings_pack::upload_rate_limit, -1); }
	);
}

TORRENT_TEST(download_rate_limit)
{
	test_settings([](lt::settings_pack& pack) {
		pack.set_int(settings_pack::download_rate_limit, std::numeric_limits<int>::max()); }
	);
}

TORRENT_TEST(download_rate_limit_negative)
{
	test_settings([](lt::settings_pack& pack) {
		pack.set_int(settings_pack::download_rate_limit, -1); }
	);
}

TORRENT_TEST(unchoke_slots_limit)
{
	test_settings([](lt::settings_pack& pack) {
		pack.set_int(settings_pack::unchoke_slots_limit, std::numeric_limits<int>::max()); }
	);
}

TORRENT_TEST(unchoke_slots_limit_negative)
{
	test_settings([](lt::settings_pack& pack) {
		pack.set_int(settings_pack::unchoke_slots_limit, -1);
		pack.set_int(settings_pack::choking_algorithm, settings_pack::fixed_slots_choker);
	});
}

TORRENT_TEST(settings_stress_test)
{
	std::array<int, 11> const settings{{
		settings_pack::unchoke_slots_limit,
		settings_pack::connections_limit,
		settings_pack::predictive_piece_announce,
		settings_pack::allow_multiple_connections_per_ip,
		settings_pack::send_redundant_have,
		settings_pack::rate_limit_ip_overhead,
		settings_pack::rate_limit_ip_overhead,
		settings_pack::anonymous_mode,
//		settings_pack::enable_upnp,
//		settings_pack::enable_natpmp,
		settings_pack::enable_lsd,
		settings_pack::enable_ip_notifier,
		settings_pack::piece_extent_affinity,
	}};
	std::array<int, 4> const values{{-1, 0, 1, std::numeric_limits<int>::max()}};

	for (auto t : { swarm_test::download, swarm_test::upload})
	{
		for (auto s1 : settings)
		{
			for (auto s2 : settings)
			{
				if (s1 == s2) continue;

				setup_swarm(2, t
					// add session
					, [](lt::settings_pack& p) {
					p.set_int(settings_pack::choking_algorithm, settings_pack::fixed_slots_choker);
					}
					// add torrent
					, [](lt::add_torrent_params& params) {}
					// on alert
					, [](lt::alert const*, lt::session&) {}
					// terminate
					, [&](int tick, lt::session& session) -> bool
					{
						int const s = (tick & 1) ? s2 : s1;
						settings_pack p;
						if ((s & settings_pack::type_mask) == settings_pack::bool_type_base)
							p.set_bool(s, bool(tick & 2));
						else
							p.set_int(s, values[(tick >> 1) % values.size()]);
						session.apply_settings(std::move(p));
						return tick > int(settings.size() * values.size() * 2);
					});
			}
		}
	}
}

TORRENT_TEST(pex)
{
	// we create 3 nodes. Node 0 seeds and node 1 and 2 are downloaders.
	// node 0 is initially only connected to node 1. The test ensures that node
	// 0 eventually is connected to node 2, as it should have been introduced
	// via PEX

	dsl_config network_cfg;
	sim::simulation sim{network_cfg};

	asio::io_context ios(sim);
	lt::time_point start_time(lt::clock_type::now());

	std::vector<std::shared_ptr<lt::session>> nodes;
	std::vector<std::shared_ptr<sim::asio::io_context>> io_service;
	std::vector<lt::session_proxy> zombies;
	lt::deadline_timer timer(ios);

	lt::error_code ec;
	int const swarm_id = test_counter();
	std::string path = save_path(swarm_id, 0);

	lt::create_directory(path, ec);
	if (ec) std::printf("failed to create directory: \"%s\": %s\n"
		, path.c_str(), ec.message().c_str());
	std::ofstream file(lt::combine_path(path, "temporary").c_str());
	auto ti = ::create_torrent(&file, "temporary", 0x4000, 50, false);
	file.close();

	int const num_nodes = 3;

	bool done = false;

	// session 0 is the seeding one.
	// the IPs are 50.0.0.1, 50.0.0.2 and 50.0.0.3
	for (int i = 0; i < num_nodes; ++i)
	{
		// create a new io_service
		char ep[30];
		std::snprintf(ep, sizeof(ep), "50.0.%d.%d", (i + 1) >> 8, (i + 1) & 0xff);
		io_service.push_back(std::make_shared<sim::asio::io_context>(sim, addr(ep)));

		lt::settings_pack pack = settings();

		// make sure the sessions have different peer ids
		lt::peer_id pid;
		lt::aux::random_bytes(pid);
		pack.set_str(lt::settings_pack::peer_fingerprint, pid.to_string());
		std::shared_ptr<lt::session> ses =
			std::make_shared<lt::session>(pack, *io_service.back());
		nodes.push_back(ses);

		lt::add_torrent_params p;
		p.flags &= ~lt::torrent_flags::paused;
		p.flags &= ~lt::torrent_flags::auto_managed;

		// node 0 and 1 are downloaders and node 2 is a seed
		// save path 0 is where the files are, so that's for seeds
		// It's important that node 1 and 2 want to stay connected, otherwise
		// node 1 won't be able to gossip about 2 to 0.
		p.save_path = save_path(swarm_id, i > 1 ? 0 : 1);
		p.ti = ti;
		ses->async_add_torrent(p);

		ses->set_alert_notify([&, i]() {
			// this function is called inside libtorrent and we cannot perform work
			// immediately in it. We have to notify the outside to pull all the alerts
			post(*io_service[i], [&,i]()
			{
				lt::session* ses = nodes[i].get();

				// when shutting down, we may have destructed the session
				if (ses == nullptr) return;

				std::vector<lt::alert*> alerts;
				ses->pop_alerts(&alerts);

				for (lt::alert* a : alerts)
				{
					// only print alerts from the session under test
					lt::time_duration d = a->timestamp() - start_time;
					std::uint32_t const millis = std::uint32_t(
						lt::duration_cast<lt::milliseconds>(d).count());

					if (i == 0) {
					std::printf("%4d.%03d: %-25s %s\n"
						, millis / 1000, millis % 1000
						, a->what()
						, a->message().c_str());
					}

					// if a torrent was added save the torrent handle
					if (lt::add_torrent_alert* at = lt::alert_cast<lt::add_torrent_alert>(a))
					{
						lt::torrent_handle h = at->handle;

						if (i == 0)
						{
							// node only connects to node 1
							h.connect_peer(lt::tcp::endpoint(addr("50.0.0.2"), 6881));
						}
						else
						{
							// other nodes connect to each other
							for (int k = 1; k < num_nodes; ++k)
							{
								char ep[30];
								std::snprintf(ep, sizeof(ep), "50.0.%d.%d"
									, (k + 1) >> 8, (k + 1) & 0xff);
								h.connect_peer(lt::tcp::endpoint(addr(ep), 6881));
							}
						}
					}

					if (i == 0)
					{
						// if node 0 was connected to 50.0.0.3, we're done
						if (lt::peer_connect_alert* ca = lt::alert_cast<lt::peer_connect_alert>(a))
						{
							if (ca->endpoint.address() == addr("50.0.0.3"))
								done = true;
						}
						if (lt::incoming_connection_alert* ca = lt::alert_cast<lt::incoming_connection_alert>(a))
						{
							if (ca->endpoint.address() == addr("50.0.0.3"))
								done = true;
						}
					}
				}
			});
		});
	}

	std::function<void(lt::error_code const&)> on_done
		= [&](lt::error_code const& ec)
	{
		if (ec) return;

		std::printf("TERMINATING\n");

		// terminate simulation
		for (int i = 0; i < int(nodes.size()); ++i)
		{
			zombies.push_back(nodes[i]->abort());
			nodes[i].reset();
		}
	};

	timer.expires_after(lt::seconds(65));
	timer.async_wait(on_done);

	sim.run();

	TEST_EQUAL(done, true);
}

// TODO: add test that makes sure a torrent in graceful pause mode won't make
// outgoing connections
// TODO: add test that makes sure a torrent in graceful pause mode won't accept
// incoming connections
// TODO: test the different storage allocation modes
// TODO: test contiguous buffer


