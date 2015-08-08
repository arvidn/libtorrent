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
#include <boost/bind.hpp>

#include "test.hpp"
#include "setup_swarm.hpp"
#include "setup_transfer.hpp" // for create_torrent (factor this out!)
#include "settings.hpp"
#include <fstream>
#include <iostream>

using namespace libtorrent;
namespace lt = libtorrent;

// TODO: a lot of this class is boiler plate. Factor out into a reusable unit
struct swarm_config : swarm_setup_provider
{
	swarm_config(int num_peers)
		: m_swarm_id(std::rand())
		, m_start_time(lt::clock_type::now())
	{
		// in case the previous run did not exit gracefully
		clear_download_directories(num_peers);

		error_code ec;
		char save_path[200];
		snprintf(save_path, sizeof(save_path), "swarm-%04d-peer-%02d"
			, m_swarm_id, 0);

		create_directory(save_path, ec);
		std::ofstream file(combine_path(save_path, "temporary").c_str());
		m_ti = ::create_torrent(&file, 0x4000, 90, false);
		file.close();
	}

	virtual void on_exit(std::vector<torrent_handle> const& torrents)
	{
		TEST_CHECK(torrents.size() > 0);
		for (int i = 0; i < int(torrents.size()); ++i)
		{
			torrent_status st = torrents[i].status();
			TEST_CHECK(st.is_seeding);
			TEST_CHECK(st.total_upload > 0 || st.total_download > 0);
		}

		// if this check fails, there is a performance regression in the protocol,
		// either uTP or bittorrent itself. Be careful with the download request
		// queue size (and make sure it can grow fast enough, to keep up with
		// slow-start) and the send buffer watermark
		TEST_CHECK(lt::clock_type::now() < m_start_time + lt::milliseconds(8500));

		// clean up the download directories to make the next run start from
		// scratch.
		clear_download_directories(int(torrents.size()));
	}

	void clear_download_directories(int num_peers)
	{
		for (int i = 0; i < num_peers; ++i)
		{
			error_code ec;
			char save_path[200];
			snprintf(save_path, sizeof(save_path), "swarm-%04d-peer-%02d"
				, m_swarm_id, i);

			// in case the previous run did not exit gracefully
			remove_all(save_path, ec);
		}
	}

	// called for every alert. if the simulation is done, return true
	virtual bool on_alert(libtorrent::alert const* alert
		, int session_idx
		, std::vector<libtorrent::torrent_handle> const& torrents)
	{
		if (torrents.empty()) return false;

		bool all_are_seeding = true;
		for (int i = 0; i < int(torrents.size()); ++i)
		{
			if (torrents[i].status().is_seeding)
				continue;

			all_are_seeding = false;
			break;
		}

		// if all torrents are seeds, terminate the simulation, we're done
		return all_are_seeding;
	}

	// called for every torrent that's added (and every session that's started).
	// this is useful to give every session a unique save path and to make some
	// sessions seeds and others downloaders
	virtual libtorrent::add_torrent_params add_torrent(int idx)
	{
		add_torrent_params p;
		p.flags &= ~add_torrent_params::flag_paused;
		p.flags &= ~add_torrent_params::flag_auto_managed;

		if (idx == 0)
		{
			// skip checking up front, and get started with the transfer sooner
			p.flags |= add_torrent_params::flag_seed_mode;
		}

		p.ti = m_ti;

		char save_path[200];
		snprintf(save_path, sizeof(save_path), "swarm-%04d-peer-%02d"
			, m_swarm_id, idx);
		p.save_path = save_path;
		return p;
	}

	// called for every session that's added
	virtual libtorrent::settings_pack add_session(int idx)
	{
		settings_pack pack = settings();

		// force uTP connection
		pack.set_bool(settings_pack::enable_incoming_utp, true);
		pack.set_bool(settings_pack::enable_outgoing_utp, true);
		pack.set_bool(settings_pack::enable_incoming_tcp, false);
		pack.set_bool(settings_pack::enable_outgoing_tcp, false);

		// the encryption handshake adds several round-trips to the bittorrent
		// handshake, and slows it down significantly
		pack.set_int(settings_pack::out_enc_policy, settings_pack::pe_disabled);
		pack.set_int(settings_pack::in_enc_policy, settings_pack::pe_disabled);

		// make sure the sessions have different peer ids
		lt::peer_id pid;
		std::generate(&pid[0], &pid[0] + 20, &random_byte);
		pack.set_str(lt::settings_pack::peer_fingerprint, pid.to_string());

		return pack;
	}

private:
	int m_swarm_id;
	lt::time_point m_start_time;
	boost::shared_ptr<libtorrent::torrent_info> m_ti;
};

TORRENT_TEST(utp)
{
	// TODO: 3 simulate packet loss
	// TODO: 3 simulate unpredictible latencies
	// TODO: 3 simulate proper (taildrop) queues (perhaps even RED and BLUE)
	swarm_config cfg(2);
	setup_swarm(2, cfg);
}

