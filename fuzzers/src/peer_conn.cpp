/*

Copyright (c) 2019-2021, Arvid Norberg
Copyright (c) 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <memory>
#include <iostream>
#include "libtorrent/session.hpp"
#include "libtorrent/session_params.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/aux_/random.hpp"

#include "libtorrent/io_context.hpp"

using namespace lt;

std::unique_ptr<session> g_ses;
info_hash_t g_info_hash;
int g_listen_port = 0;
io_context g_ios;

//#define DEBUG_LOGGING 1

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	// set up a session
	settings_pack pack;
	pack.set_int(settings_pack::piece_timeout, 1);
	pack.set_int(settings_pack::request_timeout, 1);
	pack.set_int(settings_pack::peer_timeout, 1);
	pack.set_int(settings_pack::peer_connect_timeout, 1);
	pack.set_int(settings_pack::inactivity_timeout, 1);
	pack.set_int(settings_pack::handshake_timeout, 1);

#ifdef DEBUG_LOGGING
	pack.set_int(settings_pack::alert_mask, 0xffffff);
#else
	pack.set_int(settings_pack::alert_mask, alert_category::connect
		| alert_category::error
		| alert_category::status
		| alert_category::peer);
#endif

	pack.set_int(settings_pack::out_enc_policy, settings_pack::pe_disabled);
	pack.set_int(settings_pack::in_enc_policy, settings_pack::pe_disabled);

	// don't waste time making outbound connections
	pack.set_bool(settings_pack::enable_outgoing_tcp, false);
	pack.set_bool(settings_pack::enable_outgoing_utp, false);
	pack.set_bool(settings_pack::enable_upnp, false);
	pack.set_bool(settings_pack::enable_natpmp, false);
	pack.set_bool(settings_pack::enable_dht, false);
	pack.set_bool(settings_pack::enable_lsd, false);
	pack.set_bool(settings_pack::enable_ip_notifier, false);

	// pick an available listen port and only listen on loopback
	pack.set_str(settings_pack::listen_interfaces, "127.0.0.1:0");

	g_ses = std::unique_ptr<session>(new lt::session(pack));

	// create a torrent
	int const piece_size = 1024 * 1024;
	std::int64_t const total_size = std::int64_t(piece_size) * 100;
	std::vector<lt::create_file_entry> fs;
	fs.emplace_back("test_file", total_size);
	create_torrent t(std::move(fs), piece_size);

	for (piece_index_t i : t.piece_range())
		t.set_hash(i, sha1_hash("abababababababababab"));

	for (file_index_t const f : t.file_range())
		for (piece_index_t::diff_type i : t.file_piece_range(f))
			t.set_hash2(f, i, sha256_hash("abababababababababababababababababababababababababababababababab"));

	std::vector<char> buf;
	bencode(std::back_inserter(buf), t.generate());
	auto ti = std::make_shared<torrent_info>(buf, from_span);

	// remember the info-hash to give the fuzzer a chance to connect to it
	g_info_hash = ti->info_hashes();

	// add the torrent to the session
	add_torrent_params atp;
	atp.ti = std::move(ti);
	atp.save_path = ".";

	g_ses->add_torrent(std::move(atp));

	// pull the alerts for the listen socket we ended up using
	time_point const end_time = clock_type::now() + seconds(5);
	bool started = false;
	while (g_listen_port == 0 || !started)
	{
		std::vector<alert*> alerts;
		auto const now = clock_type::now();
		if (now > end_time) return -1;

		g_ses->wait_for_alert(end_time - now);
		g_ses->pop_alerts(&alerts);

		for (auto const a : alerts)
		{
			std::cout << a->message() << '\n';
			if (auto la = alert_cast<listen_succeeded_alert>(a))
			{
				if (la->socket_type == socket_type_t::tcp)
				{
					g_listen_port = la->port;
					std::cout << "listening on " << g_listen_port << '\n';
				}
			}
			if (alert_cast<torrent_resumed_alert>(a))
			{
				started = true;
			}
		}
	}

	// we have to destruct the session before global destructors, such as the
	// system error code category. The session objects rely on error_code during
	// its destruction
	std::atexit([]{ g_ses.reset(); });

	return 0;
}

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
	if (size < 8) return 0;

#ifdef DEBUG_LOGGING
	time_point const start_time = clock_type::now();
#endif
	// connect
	tcp::socket s(g_ios);
	error_code ec;
	do {
		ec.clear();
		error_code ignore;
		s.connect(tcp::endpoint(make_address("127.0.0.1", ignore), g_listen_port), ec);
	} while (ec == boost::system::errc::interrupted);

	// bittorrent handshake

	std::vector<char> handshake(1 + 19 + 8 + 20 + 20 + size - 8);
	std::memcpy(handshake.data(), "\x13" "BitTorrent protocol", 20);
	std::memcpy(handshake.data() + 20, data, 8);
	std::memcpy(handshake.data() + 28, g_info_hash.get_best().data(), 20);
	lt::aux::random_bytes({handshake.data() + 48, 20});
	data += 8;
	size -= 8;
	std::memcpy(handshake.data() + 68, data, size);

	// we're likely to fail to write entire (garbage) messages, as libtorrent may
	// disconnect us half-way through. This may fail with broken_pipe for
	// instance
	error_code ignore;
	boost::asio::write(s, boost::asio::buffer(handshake), ignore);

	s.close();

	// wait for the alert saying the connection was closed

	time_point const end_time = clock_type::now() + seconds(3);
	for (;;)
	{
		std::vector<alert*> alerts;
		auto const now = clock_type::now();
		if (now > end_time) return -1;

		g_ses->wait_for_alert(end_time - now);
		g_ses->pop_alerts(&alerts);

		for (auto const a : alerts)
		{
#ifdef DEBUG_LOGGING
			std::cout << duration_cast<milliseconds>(a->timestamp() - start_time).count()
				<< ": " << a->message() << '\n';
#endif
			if (alert_cast<peer_error_alert>(a)
				|| alert_cast<peer_disconnected_alert>(a))
			{
				goto done;
			}
		}
	}
done:

	return 0;
}

