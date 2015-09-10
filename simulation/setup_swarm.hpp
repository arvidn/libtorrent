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

#include "libtorrent/io_service.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/torrent_handle.hpp"

#ifndef TORRENT_SWARM_SETUP_PROVIDER_HPP_INCLUDED
#define TORRENT_SWARM_SETUP_PROVIDER_HPP_INCLUDED

namespace libtorrent
{
	class alert;
	class session;
}

struct swarm_setup_provider
{
	// can be used to check expected end conditions
	virtual void on_exit(std::vector<libtorrent::torrent_handle> const& torrents) {}

	// called for every alert. if the simulation is done, return true
	virtual bool on_alert(libtorrent::alert const* alert
		, int session_idx
		, std::vector<libtorrent::torrent_handle> const& handles
		, libtorrent::session& ses) { return false; }

	// called for every torrent that's added (and every session that's started).
	// this is useful to give every session a unique save path and to make some
	// sessions seeds and others downloaders
	virtual libtorrent::add_torrent_params add_torrent(int idx) = 0;

	// called for every torrent that's added once the torrent_handle comes back.
	// can be used to set options on the torrent
	virtual void on_torrent_added(int idx, libtorrent::torrent_handle h) {}

	// called for every session that's created. Leaves an opportunity for the
	// configuration object to add extensions etc.
	virtual void on_session_added(int idx, libtorrent::session& ses) {}

	// called for every session that's added
	virtual libtorrent::settings_pack add_session(int idx) = 0;

	// called once a second. if it returns true, the simulation is terminated
	// by default, simulations end after 200 seconds
	virtual bool tick(int t) { return t > 200; }
};

void setup_swarm(int num_nodes, swarm_setup_provider& config);
void setup_swarm(int num_nodes, sim::simulation& sim, swarm_setup_provider& config);

#endif

