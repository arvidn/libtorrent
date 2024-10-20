/*

Copyright (c) 2024, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/session.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/session_params.hpp"
#include "libtorrent/write_resume_data.hpp"
#include "libtorrent/bencode.hpp"

#include <fstream>
#include <chrono>
#include <atomic>
#include <csignal>
#include <iostream>
#include <iterator>
#include <vector>
#include <filesystem>

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

	if (f.bad() || state.empty())
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

std::set<lt::sha1_hash> info_hashes;

void new_torrent(lt::session& ses, lt::sha1_hash ih)
{
	if (!info_hashes.insert(ih).second) return;

	lt::add_torrent_params adp;
	adp.info_hashes.v1 = ih;
	adp.save_path = "./non-existant-path";
	adp.file_priorities.resize(1000, lt::dont_download);
	adp.flags = torrent_flags::upload_mode;
	ses.async_add_torrent(adp);
}
} // anonymous namespace

int main(int argc, char*[])
{
	if (argc != 1) usage();

	namespace fs = std::filesystem;

	// list the directory of existing torrents, to populate our list.
	for(auto const& p : fs::directory_iterator("torrents"))
	{
		std::string file = p.path().stem().string();
		if (file.size() == 40)
		{
			// v1 torrent
			std::stringstream str(file);
			lt::sha1_hash ih;
			str >> ih;
			info_hashes.insert(ih);
		}
		else if (file.size() == 64)
		{
			// v2 torrent (recorded as truncated hashes)
			std::stringstream str(file);
			lt::sha256_hash ih;
			str >> ih;
			info_hashes.insert(sha1_hash(span<char const>(ih.data(), 20)));
		}
		else if (file.size() == 40 + 1 + 64)
		{
			// hybrid torrent
			std::stringstream str(file);
			lt::sha1_hash ih;
			str >> ih;
			info_hashes.insert(ih);
		}
	}
	std::cout << "know about " << info_hashes.size() << " torrents\n";

	signal(SIGINT, &stop);
	signal(SIGTERM, &stop);

	session_params sp = load_dht_state();
	sp.settings.set_bool(settings_pack::enable_lsd, false);
	sp.settings.set_bool(settings_pack::enable_dht, true);
	sp.settings.set_int(settings_pack::alert_mask
		, lt::alert_category::error
		| lt::alert_category::storage
		| lt::alert_category::status
		| lt::alert_category::dht_log
		| lt::alert_category::dht_operation
		| lt::alert_category::dht);
	sp.settings.set_int(settings_pack::active_limit, 10000);
	sp.settings.set_int(settings_pack::active_dht_limit, 10000);
	sp.settings.set_int(settings_pack::active_downloads, 10000);
	sp.settings.set_int(settings_pack::dht_announce_interval, 120);
	sp.settings.set_int(settings_pack::alert_queue_size, 10000);
	lt::session s(sp);

	lt::time_point next_send = lt::clock_type::now() + 5s;
	lt::time_point next_node_prune = lt::clock_type::now() + 30min;
	lt::time_point next_torrent_prune = lt::clock_type::now() + 6h;
	std::map<udp::endpoint, node_entry> nodes;

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
				std::cout << "DHT sample response: " << sa->samples().size() << '\n';
				for (auto const& ih : sa->samples())
					new_torrent(s, ih);

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
				// it's too verbose to print these
				continue;
			}
			else if (auto* aa = alert_cast<dht_announce_alert>(a))
			{
				new_torrent(s, aa->info_hash);
			}
			else if (auto* p = alert_cast<metadata_received_alert>(a))
			{
				torrent_handle const& h = p->handle;
				h.save_resume_data(torrent_handle::save_info_dict);
			}
			else if (auto* rd = alert_cast<save_resume_data_alert>(a))
			{
				auto const& atp = rd->params;
				std::vector<char> buf = write_resume_data_buf(atp);

				std::stringstream filename;
				filename << "torrents/";
				if (atp.info_hashes.has_v1())
					filename << atp.info_hashes.v1;
				if (atp.info_hashes.has_v2())
				{
					if (atp.info_hashes.has_v1())
						filename << "-";
					filename << atp.info_hashes.v2;
				}
				filename << ".torrent";
				std::fstream f(filename.str(), std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);
				f.write(buf.data(), static_cast<std::streamsize>(buf.size()));
				s.remove_torrent(rd->handle);
				std::cout << "saved torrent: " << filename.str() << '\n';
				// don't log this
				continue;
			}
			else if (alert_cast<dht_log_alert>(a)
				|| alert_cast<dht_get_peers_reply_alert>(a)
				|| alert_cast<dht_get_peers_alert>(a)
				|| alert_cast<dht_outgoing_get_peers_alert>(a)
				|| alert_cast<dht_live_nodes_alert>(a)
				|| alert_cast<dht_immutable_item_alert>(a)
				|| alert_cast<dht_mutable_item_alert>(a)
				|| alert_cast<dht_put_alert>(a)
				|| alert_cast<dht_reply_alert>(a)
				|| alert_cast<dht_direct_response_alert>(a)
				|| alert_cast<add_torrent_alert>(a)
				|| alert_cast<torrent_finished_alert>(a)
				|| alert_cast<torrent_checked_alert>(a)
				|| alert_cast<state_changed_alert>(a))
			{
				// it's too verbose to print these
				continue;
			}
			std::cout << a->message() << '\n';
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

		if (now > next_node_prune)
		{
			next_node_prune = now + 30min;

			// remove any node that we haven't seen in 6 hours
			for (auto it = nodes.begin(); it != nodes.end();)
			{
				if (it->second.last_seen + 6h < now)
					it = nodes.erase(it);
				else
					++it;
			}
		}

		// regularly, remove torrents that are too old, and probably won't
		// receive metadata
		if (now > next_torrent_prune)
		{
			next_torrent_prune = now + 6h;
			std::vector<torrent_status> const all_torrents = s.get_torrent_status([] (lt::torrent_status const&) -> bool { return true; });

			std::time_t const ptime_now = ::time(nullptr);
			for (auto st : all_torrents)
			{
				if (ptime_now - st.added_time > 12 * 3600)
				{
					s.remove_torrent(st.handle);
					std::cout << "failed to receive metadata: " << st.info_hashes << '\n';
				}
			}
		}
	}

	std::vector<char> const state = write_session_params_buf(s.session_state(session::save_dht_state));
	std::fstream f(".dht", std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);
	f.write(state.data(), static_cast<std::streamsize>(state.size()));

	return 0;
}

#endif
