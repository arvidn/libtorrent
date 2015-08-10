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

#include "libtorrent/session.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/time.hpp" // for clock_type

#include "test.hpp"
#include "setup_transfer.hpp" // for create_torrent (factor this out!)
#include "setup_swarm.hpp"
#include "swarm_suite.hpp"
#include "swarm_config.hpp"

using namespace libtorrent;
namespace lt = libtorrent;

struct test_swarm_config : swarm_config
{
	test_swarm_config(int flags)
		: swarm_config()
		, m_flags(flags)
	{}

	virtual void on_exit(std::vector<torrent_handle> const& torrents) override
	{
		swarm_config::on_exit(torrents);

		TEST_CHECK(lt::clock_type::now() < m_start_time + lt::milliseconds(2100));
	}

	// called for every torrent that's added (and every session that's started).
	// this is useful to give every session a unique save path and to make some
	// sessions seeds and others downloaders
	virtual libtorrent::add_torrent_params add_torrent(int idx) override
	{
		add_torrent_params p = swarm_config::add_torrent(idx);

		// only the first session is set to seed mode
		if (idx == 0)
		{
			if (m_flags & seed_mode) p.flags |= add_torrent_params::flag_seed_mode;
		}

		return p;
	}

	// called for every session that's added
	virtual libtorrent::settings_pack add_session(int idx) override
	{
		settings_pack pack = swarm_config::add_session(idx);

		pack.set_bool(settings_pack::strict_super_seeding, m_flags & strict_super_seeding);

		if (m_flags & suggest_read_cache)
			pack.set_int(settings_pack::suggest_mode, settings_pack::suggest_read_cache);
		else
			pack.set_int(settings_pack::suggest_mode, 0);

		if (m_flags & explicit_cache)
		{
			pack.set_bool(settings_pack::explicit_read_cache, true);
			pack.set_int(settings_pack::explicit_cache_interval, 5);
		}
		else
		{
			pack.set_bool(settings_pack::explicit_read_cache, false);
		}

		if (m_flags & utp_only)
		{
			pack.set_bool(settings_pack::enable_incoming_utp, true);
			pack.set_bool(settings_pack::enable_outgoing_utp, true);
			pack.set_bool(settings_pack::enable_incoming_tcp, false);
			pack.set_bool(settings_pack::enable_outgoing_tcp, false);
		}
		else
		{
			pack.set_bool(settings_pack::enable_incoming_utp, false);
			pack.set_bool(settings_pack::enable_outgoing_utp, false);
			pack.set_bool(settings_pack::enable_incoming_tcp, true);
			pack.set_bool(settings_pack::enable_outgoing_tcp, true);
		}

		return pack;
	}

private:
	int m_flags;
};

void simulate_swarm(int flags)
{
	fprintf(stderr, "\n\n ==== TEST SWARM === %s%s%s%s%s%s%s ===\n\n\n"
		, (flags & super_seeding) ? "super-seeding ": ""
		, (flags & strict_super_seeding) ? "strict-super-seeding ": ""
		, (flags & seed_mode) ? "seed-mode ": ""
		, (flags & time_critical) ? "time-critical ": ""
		, (flags & suggest_read_cache) ? "suggest-read-cache ": ""
		, (flags & explicit_cache) ? "explicit-cache ": ""
		, (flags & utp_only) ? "utp-only": ""
		);

	test_swarm_config cfg(flags);
	setup_swarm(2, cfg);
}

