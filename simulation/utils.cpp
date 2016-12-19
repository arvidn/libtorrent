/*

Copyright (c) 2016, Arvid Norberg
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

#include "utils.hpp"
#include "test.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/session_stats.hpp"
#include "libtorrent/alert.hpp"
#include "setup_swarm.hpp"

using namespace libtorrent;

void utp_only(lt::session& ses)
{
	settings_pack p;
	utp_only(p);
	ses.apply_settings(p);
}

void enable_enc(lt::session& ses)
{
	settings_pack p;
	enable_enc(p);
	ses.apply_settings(p);
}

void filter_ips(lt::session& ses)
{
	ip_filter filter;
	filter.add_rule(address_v4::from_string("50.0.0.1")
		, address_v4::from_string("50.0.0.2"), ip_filter::blocked);
	ses.set_ip_filter(filter);
}

void set_cache_size(lt::session& ses, int val)
{
	settings_pack pack;
	pack.set_int(settings_pack::cache_size, val);
	ses.apply_settings(pack);
}

int get_cache_size(lt::session& ses)
{
	std::vector<stats_metric> stats = session_stats_metrics();
	int const read_cache_idx = find_metric_idx("disk.read_cache_blocks");
	int const write_cache_idx = find_metric_idx("disk.write_cache_blocks");
	TEST_CHECK(read_cache_idx >= 0);
	TEST_CHECK(write_cache_idx >= 0);
	ses.set_alert_notify([](){});
	ses.post_session_stats();
	std::vector<alert*> alerts;
	ses.pop_alerts(&alerts);
	int cache_size = -1;
	for (auto const a : alerts)
	{
		if (auto const* st = alert_cast<session_stats_alert>(a))
		{
			cache_size = st->values[read_cache_idx];
			cache_size += st->values[write_cache_idx];
			break;
		}
	}
	return cache_size;
}

void set_proxy(lt::session& ses, int proxy_type, int flags, bool proxy_peer_connections)
{
	// apply the proxy settings to session 0
	settings_pack p;
	p.set_int(settings_pack::proxy_type, proxy_type);
	if (proxy_type == settings_pack::socks4)
		p.set_int(settings_pack::proxy_port, 4444);
	else
		p.set_int(settings_pack::proxy_port, 5555);
	if (flags & ipv6)
		p.set_str(settings_pack::proxy_hostname, "2001::2");
	else
		p.set_str(settings_pack::proxy_hostname, "50.50.50.50");
	p.set_bool(settings_pack::proxy_hostnames, true);
	p.set_bool(settings_pack::proxy_peer_connections, proxy_peer_connections);
	p.set_bool(settings_pack::proxy_tracker_connections, true);
	p.set_bool(settings_pack::force_proxy, true);

	ses.apply_settings(p);
}

lt::address addr(char const* str)
{
	return lt::address::from_string(str);
}

void print_alerts(lt::session& ses
	, std::function<void(lt::session&, lt::alert const*)> on_alert)
{
	lt::time_point start_time = lt::clock_type::now();

	ses.set_alert_notify([&ses,start_time,on_alert] {
		ses.get_io_service().post([&ses,start_time,on_alert] {

		std::vector<lt::alert*> alerts;
		ses.pop_alerts(&alerts);

		for (lt::alert const* a : alerts)
		{
			printf("%-3d [0] %s\n", int(lt::duration_cast<lt::seconds>(a->timestamp()
				- start_time).count()), a->message().c_str());
			// call the user handler
			on_alert(ses, a);
		}
	} ); } );
}

