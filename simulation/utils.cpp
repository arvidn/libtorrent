/*

Copyright (c) 2016-2019, 2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
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
#include "libtorrent/aux_/random.hpp"
#include "setup_swarm.hpp"
#include "setup_transfer.hpp" // for addr()

using namespace lt;

#ifdef _MSC_VER
namespace libtorrent {
namespace aux {
	// see test_error_handling.cpp for a description of this variable
	// TODO: in C++17, make this inline
	thread_local int g_must_not_fail = 0;
}
}
#endif

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


void set_proxy(lt::session& ses, int proxy_type, test_transfer_flags_t const flags
	, bool const proxy_peers)
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
	p.set_bool(settings_pack::proxy_tracker_connections, proxy_peers);
	p.set_bool(settings_pack::socks5_udp_send_local_ep, true);

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
				auto const ts = a->timestamp() - start_time;
				std::printf("\x1b[%dm%3d.%03d %s\n"
					, idx == 0 ? 0 : 34
					, int(lt::duration_cast<lt::seconds>(ts).count())
					, int(lt::duration_cast<lt::milliseconds>(ts).count() % 1000)
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

sha256_hash rand_sha256()
{
	sha256_hash ret;
	aux::random_bytes(ret);
	return ret;
}

sha1_hash rand_sha1()
{
	sha1_hash ret;
	aux::random_bytes(ret);
	return ret;
}

