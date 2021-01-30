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

#include "libtorrent/session.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/time.hpp" // for clock_type
#include "libtorrent/aux_/utp_stream.hpp"
#include "libtorrent/session_stats.hpp"

#include "test.hpp"
#include "utils.hpp"
#include "setup_swarm.hpp"
#include "settings.hpp"
#include <fstream>
#include <iostream>
#include <tuple>
#include <vector>

#include "simulator/packet.hpp"

using namespace lt;

namespace {

struct pppoe_config final : sim::default_config
{
	int path_mtu(address, address) override
	{
		// this is the size left after IP and UDP headers are deducted
		return 1464;
	}
};

std::int64_t metric(std::vector<std::int64_t> const& counters, char const* key)
{
	auto const idx = lt::find_metric_idx(key);
	return (idx < 0) ? -1 : counters[idx];
}

std::vector<std::int64_t> utp_test(sim::configuration& cfg)
{
	sim::simulation sim{cfg};

	std::vector<std::int64_t> cnt;

	setup_swarm(2, swarm_test::upload | swarm_test::large_torrent | swarm_test::no_auto_stop, sim
		// add session
		, [](lt::settings_pack& pack) {
		// force uTP connection
			utp_only(pack);
		}
		// add torrent
		, [](lt::add_torrent_params& params) {
			params.flags |= torrent_flags::seed_mode;
		}
		// on alert
		, [&](lt::alert const* a, lt::session& ses) {
			if (auto ss = alert_cast<session_stats_alert>(a))
				cnt.assign(ss->counters().begin(), ss->counters().end());
		}
		// terminate
		, [&](int const ticks, lt::session& s) -> bool
		{
			if (ticks == 100)
				s.post_session_stats();

			if (ticks > 100)
			{
				if (is_seed(s)) return true;

				TEST_ERROR("timeout");
				return true;
			}
			return false;
		});
	return cnt;
}
}

// TODO: 3 simulate non-congestive packet loss
// TODO: 3 simulate unpredictible latencies
// TODO: 3 simulate proper (taildrop) queues (perhaps even RED and BLUE)

// The counters checked by these tests are proxies for the expected behavior. If
// they change, ensure the utp log and graph plot by parse_utp_log.py look good
// still!

TORRENT_TEST(utp_pmtud)
{
#if TORRENT_UTP_LOG
	lt::aux::set_utp_stream_logging(true);
#endif

	pppoe_config cfg;

	std::vector<std::int64_t> cnt = utp_test(cfg);

	// This is the one MTU probe that's lost. Note that fast-retransmit packets
	// (nor MTU-probes) are treated as congestion. Only packets treated as
	// congestion count as utp_packet_loss.
	TEST_EQUAL(metric(cnt, "utp.utp_fast_retransmit"), 2);
	TEST_EQUAL(metric(cnt, "utp.utp_packet_resend"), 2);

	TEST_EQUAL(metric(cnt, "utp.utp_packet_loss"), 0);

	// TODO: 3 This timeout happens at shutdown. It's not very clean
	TEST_EQUAL(metric(cnt, "utp.utp_timeout"), 1);

	TEST_EQUAL(metric(cnt, "utp.utp_packets_in"), 610);
	TEST_EQUAL(metric(cnt, "utp.utp_payload_pkts_in"), 23);

	TEST_EQUAL(metric(cnt, "utp.utp_packets_out"), 611);

	// we don't expect any invalid packets, since we're talking to ourself
	TEST_EQUAL(metric(cnt, "utp.utp_invalid_pkts_in"), 0);
	TEST_EQUAL(metric(cnt, "utp.utp_redundant_pkts_in"), 0);
}

TORRENT_TEST(utp_plain)
{
#if TORRENT_UTP_LOG
	lt::aux::set_utp_stream_logging(true);
#endif

	// the available bandwidth is so high the test never bumps up against it
	sim::default_config cfg;

	std::vector<std::int64_t> cnt = utp_test(cfg);

	TEST_EQUAL(metric(cnt, "utp.utp_packet_loss"), 0);
	TEST_EQUAL(metric(cnt, "utp.utp_timeout"), 1);
	TEST_EQUAL(metric(cnt, "utp.utp_fast_retransmit"), 0);
	TEST_EQUAL(metric(cnt, "utp.utp_packet_resend"), 0);

	TEST_EQUAL(metric(cnt, "utp.utp_packets_in"), 608);
	TEST_EQUAL(metric(cnt, "utp.utp_payload_pkts_in"), 23);

	TEST_EQUAL(metric(cnt, "utp.utp_packets_out"), 607);

	// we don't expect any invalid packets, since we're talking to ourself
	TEST_EQUAL(metric(cnt, "utp.utp_invalid_pkts_in"), 0);
	TEST_EQUAL(metric(cnt, "utp.utp_redundant_pkts_in"), 0);
}

TORRENT_TEST(utp_buffer_bloat)
{
#if TORRENT_UTP_LOG
	lt::aux::set_utp_stream_logging(true);
#endif

	// 50 kB/s, 500 kB send buffer size. That's 10 seconds
	dsl_config cfg(50, 500000);

	std::vector<std::int64_t> cnt = utp_test(cfg);

	TEST_EQUAL(metric(cnt, "utp.utp_packet_loss"), 0);
	TEST_EQUAL(metric(cnt, "utp.utp_timeout"), 1);
	TEST_EQUAL(metric(cnt, "utp.utp_fast_retransmit"), 0);
	TEST_EQUAL(metric(cnt, "utp.utp_packet_resend"), 0);

	TEST_EQUAL(metric(cnt, "utp.utp_samples_above_target"), 425);
	TEST_EQUAL(metric(cnt, "utp.utp_samples_below_target"), 155);

	TEST_EQUAL(metric(cnt, "utp.utp_packets_in"), 643);
	TEST_EQUAL(metric(cnt, "utp.utp_payload_pkts_in"), 60);

	TEST_EQUAL(metric(cnt, "utp.utp_packets_out"), 642);

	// we don't expect any invalid packets, since we're talking to ourself
	TEST_EQUAL(metric(cnt, "utp.utp_invalid_pkts_in"), 0);
	TEST_EQUAL(metric(cnt, "utp.utp_redundant_pkts_in"), 0);
}

// low bandwidth limit, but virtually no buffer
TORRENT_TEST(utp_straw)
{
#if TORRENT_UTP_LOG
	lt::aux::set_utp_stream_logging(true);
#endif

	// 50 kB/s, 500 kB send buffer size. That's 10 seconds
	dsl_config cfg(50, 1500);

	std::vector<std::int64_t> cnt = utp_test(cfg);

	TEST_EQUAL(metric(cnt, "utp.utp_packet_loss"), 50);
	TEST_EQUAL(metric(cnt, "utp.utp_timeout"), 47);
	TEST_EQUAL(metric(cnt, "utp.utp_fast_retransmit"), 55);
	TEST_EQUAL(metric(cnt, "utp.utp_packet_resend"), 161);

	TEST_EQUAL(metric(cnt, "utp.utp_samples_above_target"), 0);
	TEST_EQUAL(metric(cnt, "utp.utp_samples_below_target"), 260);

	TEST_EQUAL(metric(cnt, "utp.utp_packets_in"), 400);
	TEST_EQUAL(metric(cnt, "utp.utp_payload_pkts_in"), 40);

	TEST_EQUAL(metric(cnt, "utp.utp_packets_out"), 564);

	// we don't expect any invalid packets, since we're talking to ourself
	TEST_EQUAL(metric(cnt, "utp.utp_invalid_pkts_in"), 0);
	TEST_EQUAL(metric(cnt, "utp.utp_redundant_pkts_in"), 0);
}
