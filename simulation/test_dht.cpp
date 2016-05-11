/*

Copyright (c) 2015, Arvid Norberg
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

#include "test.hpp"
#include "settings.hpp"
#include "setup_dht.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/alert.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/session_settings.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/session_stats.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/deadline_timer.hpp"
#include "libtorrent/socket_io.hpp"
#include "setup_swarm.hpp"
#include "setup_dht.hpp"

namespace lt = libtorrent;

TORRENT_TEST(dht_bootstrap)
{
	sim::default_config cfg;
	sim::simulation sim{cfg};

	dht_network dht(sim, 1000);

	int routing_table_depth = 0;
	int num_nodes = 0;

	setup_swarm(1, swarm_test::download, sim
		// add session
		, [](lt::settings_pack& pack) {
		}
		// add torrent
		, [](lt::add_torrent_params& params) {}
		// on alert
		, [&](lt::alert const* a, lt::session& ses)
		{
			if (lt::dht_stats_alert const* p = lt::alert_cast<lt::dht_stats_alert>(a))
			{
				routing_table_depth = p->routing_table.size();
				int c = 0;
				for (auto const& b : p->routing_table)
				{
					c += b.num_nodes;
					c += b.num_replacements;
				}
				num_nodes = c;
				print_routing_table(p->routing_table);
			}
			else if (lt::session_stats_alert const* sa = lt::alert_cast<lt::session_stats_alert>(a))
			{
				int const dht_nodes = lt::find_metric_idx("dht.nodes");
				TEST_CHECK(sa->values[dht_nodes] > 2);
			}
		}
		// terminate?
		, [&](int ticks, lt::session& ses) -> bool
		{
			if (ticks == 0)
			{
				lt::dht_settings sett;
				sett.ignore_dark_internet = false;
				ses.set_dht_settings(sett);

				// bootstrap off of 8 of the nodes
				lt::entry state;
				lt::entry::list_type& nodes = state["dht state"]["nodes"].list();
				for (auto const& n : dht.router_nodes())
				{
					std::string node;
					std::back_insert_iterator<std::string> out(node);
					lt::detail::write_endpoint(n, out);
					nodes.push_back(lt::entry(node));
				}

				std::vector<char> buf;
				lt::bencode(std::back_inserter(buf), state);
				lt::bdecode_node e;
				lt::error_code ec;
				lt::bdecode(&buf[0], &buf[0] + buf.size(), e, ec);

				ses.load_state(e);
				lt::settings_pack pack;
				pack.set_bool(lt::settings_pack::enable_dht, true);
				ses.apply_settings(pack);
			}
			if (ticks > 2)
			{
				ses.post_session_stats();
				printf("depth: %d nodes: %d\n", routing_table_depth, num_nodes);
				TEST_CHECK(routing_table_depth >= 9);
				TEST_CHECK(num_nodes >= 115);
				dht.stop();
				return true;
			}
			ses.post_dht_stats();
			return false;
		});

	sim.run();

}

