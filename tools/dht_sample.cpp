/*

Copyright (c) 2019-2020, Arvid Norberg
Copyright (c) 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/


#include "libtorrent/session.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/session_params.hpp"
#include "libtorrent/aux_/random.hpp"

#include <fstream>
#include <chrono>
#include <atomic>
#include <csignal>
#include <iostream>
#include <iterator>
#include <vector>

using namespace lt;
using namespace lt::dht;
using namespace std::placeholders;

using namespace std::literals::chrono_literals;

lt::clock_type::duration const min_request_interval = 5min;

#ifdef TORRENT_DISABLE_DHT

int main(int, char*[])
{
	std::cerr << "not built with DHT support\n";
	return 1;
}

#else

namespace {

std::atomic_bool quit(false);

void stop(int) { quit = true; }

[[noreturn]] void usage()
{
	std::cerr << "USAGE: dht-sample\n";
	exit(1);
}

lt::session_params load_dht_state()
{
	std::fstream f(".dht", std::ios_base::in | std::ios_base::binary);
	f.unsetf(std::ios_base::skipws);
	std::cout << "load dht state from .dht\n";
	std::vector<char> const state(std::istream_iterator<char>{f}
		, std::istream_iterator<char>{});

	if (f.bad())
	{
		std::cerr << "failed to read .dht\n";
		return {};
	}
	return read_session_params(state);
}

struct node_entry
{
	lt::time_point next_request = lt::min_time();
	lt::time_point last_seen = lt::clock_type::now();
};

} // anonymous namespace

int main(int argc, char*[])
{
	if (argc != 1) usage();

	signal(SIGINT, &stop);
	signal(SIGTERM, &stop);

	session_params sp = load_dht_state();
	sp.settings.set_bool(settings_pack::enable_dht, true);
	sp.settings.set_int(settings_pack::alert_mask, 0x7fffffff);
	lt::session s(sp);

	lt::time_point next_send = lt::clock_type::now() + 5s;
	lt::time_point next_prune = lt::clock_type::now() + 30min;
	std::map<udp::endpoint, node_entry> nodes;
	std::set<lt::sha1_hash> info_hashes;

	while (!quit)
	{
		s.wait_for_alert(5s);

		std::vector<alert*> alerts;
		s.pop_alerts(&alerts);
		auto const now = lt::clock_type::now();
		for (alert* a : alerts)
		{
			if (auto* sa = lt::alert_cast<dht_sample_infohashes_alert>(a))
			{

				for (auto const& ih : sa->samples())
				{
					if (info_hashes.insert(ih).second)
						std::cout << ih << '\n';
				}
				for (auto const& n : sa->nodes())
				{
					auto it = nodes.find(n.second);
					if (it == nodes.end())
						it = nodes.insert({n.second, {}}).first;
					else
						it->second.last_seen = now;
					it->second.next_request = now + std::max(sa->interval
						, min_request_interval);
				}
				std::cout.flush();
			}
			else if (auto* dp = alert_cast<dht_pkt_alert>(a))
			{
				auto it = nodes.find(dp->node);
				if (it == nodes.end())
					nodes.insert({dp->node, {}});
				else
					it->second.last_seen = now;
			}
			else if (auto* aa = alert_cast<dht_announce_alert>(a))
			{
				if (info_hashes.insert(aa->info_hash).second)
					std::cout << aa->info_hash << std::endl;
			}
		}

		if (now > next_send)
		{
			next_send = now + 1s;
			auto const it = std::find_if(nodes.begin(), nodes.end()
				, [now](std::pair<udp::endpoint const, node_entry> const& n)
				{ return n.second.next_request < now; });
			if (it != nodes.end())
			{
				// just push this forward. If we get a response, this will be
				// updated with the interval announced by the node
				it->second.next_request = now + 1h;
				sha1_hash target;
				for (auto& b : target) b = std::uint8_t(std::rand());
				s.dht_sample_infohashes(it->first, target);
			}
		}

		if (now > next_prune)
		{
			next_prune = now + 30min;

			// remove any node that we haven't seen in 6 hours
			for (auto it = nodes.begin(); it != nodes.end();)
			{
				if (it->second.last_seen + 6h < now)
					it = nodes.erase(it);
				else
					++it;
			}
		}
	}

	std::vector<char> const state = write_session_params_buf(s.session_state(session::save_dht_state));
	std::fstream f(".dht", std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);
	f.write(state.data(), static_cast<std::streamsize>(state.size()));

	return 0;
}

#endif
