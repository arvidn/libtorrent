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

#ifndef TORRENT_SWARM_CONFIG_HPP_INCLUDED
#define TORRENT_SWARM_CONFIG_HPP_INCLUDED

#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/time.hpp"
#include "setup_swarm.hpp"
#include "settings.hpp"
#include <string>
#include <fstream>

using namespace libtorrent;
namespace lt = libtorrent;

struct swarm_config : swarm_setup_provider
{
	swarm_config()
		: m_start_time(lt::clock_type::now())
	{
		m_swarm_id = test_counter();

		error_code ec;
		std::string path = save_path(0);
		create_directory(path, ec);
		if (ec) fprintf(stderr, "failed to create directory: \"%s\": %s\n"
			, path.c_str(), ec.message().c_str());
		std::ofstream file(combine_path(path, "temporary").c_str());
		m_ti = ::create_torrent(&file, 0x4000, 9, false);
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

		p.ti = m_ti;

		p.save_path = save_path(idx);
		return p;
	}

	std::string save_path(int idx) const
	{
		char path[200];
		snprintf(path, sizeof(path), "swarm-%04d-peer-%02d"
			, m_swarm_id, idx);
		return path;
	}

	// called for every session that's added
	virtual libtorrent::settings_pack add_session(int idx)
	{
		settings_pack pack = settings();

		// make sure the sessions have different peer ids
		lt::peer_id pid;
		std::generate(&pid[0], &pid[0] + 20, &random_byte);
		pack.set_str(lt::settings_pack::peer_fingerprint, pid.to_string());

		return pack;
	}

protected:
	int m_swarm_id;
	lt::time_point m_start_time;
	boost::shared_ptr<libtorrent::torrent_info> m_ti;
};

#endif // TORRENT_SWARM_CONFIG_HPP_INCLUDED

