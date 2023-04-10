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

#include "libtorrent/load_torrent.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/aux_/merkle.hpp"

namespace libtorrent {

namespace {
	void update_atp(add_torrent_params& atp)
	{
		auto const& ti = atp.ti;
		auto trackers = ti->_drain_trackers();
		for (auto& ae : trackers)
		{
			atp.trackers.push_back(std::move(ae.url));
			atp.tracker_tiers.push_back(ae.tier);
		}

		auto web_seeds = ti->_drain_web_seeds();
		for (auto& ws : web_seeds)
		{
			if (ws.type == web_seed_entry::url_seed)
				atp.url_seeds.push_back(std::move(ws.url));
			else if (ws.type == web_seed_entry::http_seed)
				atp.http_seeds.push_back(std::move(ws.url));
		}

		atp.dht_nodes = ti->_drain_nodes();
		atp.info_hash = atp.ti->info_hash();
	}
}

	add_torrent_params load_torrent_file(std::string const& filename)
	{ return load_torrent_file(filename, load_torrent_limits{}); }
	add_torrent_params load_torrent_buffer(span<char const> buffer)
	{ return load_torrent_buffer(buffer, load_torrent_limits{}); }
	add_torrent_params load_torrent_parsed(bdecode_node const& torrent_file)
	{ return load_torrent_parsed(torrent_file, load_torrent_limits{}); }

	// TODO: move the loading logic from torrent_info constructor into here
	add_torrent_params load_torrent_file(std::string const& filename, load_torrent_limits const& cfg)
	{
		add_torrent_params ret;
		ret.ti = std::make_shared<torrent_info>(filename, cfg);
		update_atp(ret);
		return ret;
	}

	add_torrent_params load_torrent_buffer(span<char const> buffer, load_torrent_limits const& cfg)
	{
		add_torrent_params ret;
		ret.ti = std::make_shared<torrent_info>(buffer, cfg, from_span);
		update_atp(ret);
		return ret;
	}

	add_torrent_params load_torrent_parsed(bdecode_node const& torrent_file, load_torrent_limits const& cfg)
	{
		add_torrent_params ret;
		ret.ti = std::make_shared<torrent_info>(torrent_file, cfg);
		update_atp(ret);
		return ret;
	}

}


