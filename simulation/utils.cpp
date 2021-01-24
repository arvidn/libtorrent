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
#include "libtorrent/io_context.hpp"
#include "setup_swarm.hpp"
#include "setup_transfer.hpp" // for addr()

using namespace lt;

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
	filter.add_rule(make_address_v4("50.0.0.1")
		, make_address_v4("50.0.0.2"), ip_filter::blocked);
	ses.set_ip_filter(filter);
}

void utp_only(lt::settings_pack& p)
{
	using namespace lt;
	p.set_bool(settings_pack::enable_outgoing_tcp, false);
	p.set_bool(settings_pack::enable_incoming_tcp, false);
	p.set_bool(settings_pack::enable_outgoing_utp, true);
	p.set_bool(settings_pack::enable_incoming_utp, true);
}

void enable_enc(lt::settings_pack& p)
{
	using namespace lt;
	p.set_bool(settings_pack::prefer_rc4, true);
	p.set_int(settings_pack::in_enc_policy, settings_pack::pe_forced);
	p.set_int(settings_pack::out_enc_policy, settings_pack::pe_forced);
	p.set_int(settings_pack::allowed_enc_level, settings_pack::pe_both);
}

std::string save_path(int swarm_id, int idx)
{
	char path[200];
	std::snprintf(path, sizeof(path), "swarm-%04d-peer-%02d"
		, swarm_id, idx);
	return path;
}

void add_extra_peers(lt::session& ses)
{
	auto handles = ses.get_torrents();
	TEST_EQUAL(handles.size(), 1);
	auto h = handles[0];

	for (int i = 0; i < 30; ++i)
	{
		char ep[30];
		std::snprintf(ep, sizeof(ep), "60.0.0.%d", i + 1);
		h.connect_peer(lt::tcp::endpoint(addr(ep), 6881));
	}
}

lt::torrent_status get_status(lt::session& ses)
{
	auto handles = ses.get_torrents();
	TEST_EQUAL(handles.size(), 1);
	if (handles.empty()) return lt::torrent_status();
	auto h = handles[0];
	return h.status();
}

bool has_metadata(lt::session& ses)
{
	auto handles = ses.get_torrents();
	TEST_EQUAL(handles.size(), 1);
	if (handles.empty()) return false;
	auto h = handles[0];
	return h.status().has_metadata;
}

bool is_seed(lt::session& ses)
{
	auto handles = ses.get_torrents();
	TEST_EQUAL(handles.size(), 1);
	if (handles.empty()) return false;
	auto h = handles[0];
	return h.status().is_seeding;
}

bool is_finished(lt::session& ses)
{
	auto handles = ses.get_torrents();
	TEST_EQUAL(handles.size(), 1);
	if (handles.empty()) return false;
	auto h = handles[0];
	return h.status().is_finished;
}

int completed_pieces(lt::session& ses)
{
	auto handles = ses.get_torrents();
	TEST_EQUAL(handles.size(), 1);
	if (handles.empty()) return 0;
	auto h = handles[0];
	return h.status().num_pieces;
}


void set_proxy(lt::session& ses, int proxy_type, test_transfer_flags_t const flags)
{
	// apply the proxy settings to session 0
	settings_pack p;
	p.set_int(settings_pack::proxy_type, proxy_type);
	if (proxy_type == settings_pack::socks4)
		p.set_int(settings_pack::proxy_port, 4444);
	else
		p.set_int(settings_pack::proxy_port, 5555);
	if (flags & tx::ipv6)
		p.set_str(settings_pack::proxy_hostname, "2001::2");
	else
		p.set_str(settings_pack::proxy_hostname, "50.50.50.50");
	p.set_bool(settings_pack::proxy_hostnames, true);
	p.set_bool(settings_pack::proxy_peer_connections, bool(flags & tx::proxy_peers));
	p.set_bool(settings_pack::proxy_tracker_connections, true);

	ses.apply_settings(p);
}

void print_alerts(lt::session& ses
	, std::function<void(lt::session&, lt::alert const*)> on_alert
	, int const idx)
{
	lt::time_point start_time = lt::clock_type::now();

	static std::vector<lt::alert*> alerts;

	ses.set_alert_notify([&ses,start_time,on_alert,idx] {
		post(ses.get_context(), [&ses,start_time,on_alert,idx] {

		try {
			alerts.clear();
			ses.pop_alerts(&alerts);

			for (lt::alert const* a : alerts)
			{
				std::printf("%-3d [%d] %s\n"
					, int(lt::duration_cast<lt::seconds>(a->timestamp() - start_time).count())
					, idx
					, a->message().c_str());
				// call the user handler
				on_alert(ses, a);
			}
		} catch (std::exception const& e) {
			std::printf("print alerts: ERROR failed with exception: %s"
				, e.what());
		} catch (...) {
			std::printf("print alerts: ERROR failed with (unknown) exception");
		}
	} ); } );
}

std::unique_ptr<sim::asio::io_context> make_io_context(sim::simulation& sim, int i)
{
	char ep[30];
	std::snprintf(ep, sizeof(ep), "50.0.%d.%d", (i + 1) >> 8, (i + 1) & 0xff);
	return std::make_unique<sim::asio::io_context>(sim, lt::make_address_v4(ep));
}
