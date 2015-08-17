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
#include "libtorrent/alert_types.hpp"

namespace lt = libtorrent;

struct network_config : network_setup_provider
{
	network_config()
		: m_start_time(lt::clock_type::now())
		, m_ticks(0)
	{}

	virtual void on_exit() override final {}

	// called for every alert. if the simulation is done, return true
	virtual bool on_alert(lt::alert const* alert
		, int session_idx) override final
	{
		if (lt::dht_stats_alert const* p = lt::alert_cast<lt::dht_stats_alert>(alert))
		{
			int bucket = 0;
			for (std::vector<lt::dht_routing_bucket>::const_iterator i = p->routing_table.begin()
				, end(p->routing_table.end()); i != end; ++i, ++bucket)
			{
				char const* progress_bar =
					"################################"
					"################################"
					"################################"
					"################################";
				char const* short_progress_bar = "--------";
				printf("%3d [%3d, %d] %s%s\n"
					, bucket, i->num_nodes, i->num_replacements
					, progress_bar + (128 - i->num_nodes)
					, short_progress_bar + (8 - (std::min)(8, i->num_replacements)));
			}
		}

		return false;
	}

	bool on_tick() override final
	{
		m_first_session->post_dht_stats();
		if (++m_ticks > 80) return true;
		return false;
	}

	// called for every session that's added
	virtual lt::settings_pack add_session(int idx) override final
	{
		lt::settings_pack pack = settings();

		pack.set_bool(lt::settings_pack::enable_dht, true);

		return pack;
	}

	virtual void setup_session(lt::session& ses, int idx) override final
	{
		if (idx == 0) m_first_session = &ses;

		// we have to do this since all of our simulated IP addresses are close to
		// each other
		lt::dht_settings sett;
		sett.restrict_routing_ips = false;
		sett.restrict_search_ips = false;
		sett.privacy_lookups = false;
		sett.extended_routing_table = false;
		ses.set_dht_settings(sett);
	}

private:
	lt::time_point m_start_time;
	boost::shared_ptr<lt::torrent_info> m_ti;
	lt::session* m_first_session;
	int m_ticks;
};

TORRENT_TEST(dht)
{
	network_config cfg;
	setup_dht(10, cfg);
}

