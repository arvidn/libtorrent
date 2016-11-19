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

#include "libtorrent/session.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/deadline_timer.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/alert_types.hpp"
#include "simulator/http_server.hpp"
#include "settings.hpp"
#include "libtorrent/create_torrent.hpp"
#include "simulator/simulator.hpp"
#include "setup_swarm.hpp"
#include "utils.hpp"
#include "simulator/utils.hpp"
#include <iostream>

using namespace sim;
using namespace libtorrent;

namespace lt = libtorrent;

std::unique_ptr<sim::asio::io_service> make_io_service(sim::simulation& sim, int i)
{
	char ep[30];
	snprintf(ep, sizeof(ep), "50.0.%d.%d", (i + 1) >> 8, (i + 1) & 0xff);
	return std::unique_ptr<sim::asio::io_service>(new sim::asio::io_service(
		sim, address_v4::from_string(ep)));
}

boost::shared_ptr<torrent_info> create_torrent(file_storage& fs)
{
	int const piece_size = 0x4000;
	libtorrent::create_torrent t(fs, piece_size);

	std::vector<char> piece(piece_size);
	for (int i = 0; i < int(piece.size()); ++i)
		piece[i] = (i % 26) + 'A';

	// calculate the hash for all pieces
	int const num = t.num_pieces();
	sha1_hash ph = hasher(&piece[0], piece.size()).final();
	for (int i = 0; i < num; ++i)
		t.set_hash(i, ph);

	std::vector<char> tmp;
	std::back_insert_iterator<std::vector<char> > out(tmp);

	entry tor = t.generate();

	bencode(out, tor);
	error_code ec;
	return boost::make_shared<torrent_info>(
		&tmp[0], tmp.size(), boost::ref(ec), 0);
}
// this is the general template for these tests. create the session with custom
// settings (Settings), set up the test, by adding torrents with certain
// arguments (Setup), run the test and verify the end state (Test)
template <typename Setup, typename HandleAlerts, typename Test>
void run_test(Setup const& setup
	, HandleAlerts const& on_alert
	, Test const& test)
{
	// setup the simulation
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	std::unique_ptr<sim::asio::io_service> ios = make_io_service(sim, 0);
	lt::session_proxy zombie;

	lt::settings_pack pack = settings();
	// create session
	std::shared_ptr<lt::session> ses = std::make_shared<lt::session>(pack, *ios);

	// set up test, like adding torrents (customization point)
	setup(*ses);

	// only monitor alerts for session 0 (the downloader)
	print_alerts(*ses, [=](lt::session& ses, lt::alert const* a) {
		on_alert(ses, a);
	});

	// set up a timer to fire later, to verify everything we expected to happen
	// happened
	sim::timer t(sim, lt::seconds(100), [&](boost::system::error_code const& ec)
	{
		fprintf(stderr, "shutting down\n");
		// shut down
		zombie = ses->abort();
		ses.reset();
	});

	test(sim, *ses);
}

TORRENT_TEST(single_file_torrent)
{
	using namespace libtorrent;
	bool expected = false;
	run_test(
		[](lt::session& ses)
		{
			file_storage fs;
			fs.add_file("abc'abc", 0x8000); // this filename will have to be escaped
			lt::add_torrent_params params;
			params.ti = ::create_torrent(fs);
			params.url_seeds.push_back("http://2.2.2.2:8080/");
			params.flags &= ~lt::add_torrent_params::flag_auto_managed;
			params.flags &= ~lt::add_torrent_params::flag_paused;
			params.save_path = ".";
			ses.async_add_torrent(params);
		},
		[](lt::session& ses, lt::alert const* alert) {
		},
		[&expected](sim::simulation& sim, lt::session& ses)
		{
			sim::asio::io_service web_server(sim, address_v4::from_string("2.2.2.2"));
			// listen on port 8080
			sim::http_server http(web_server, 8080);

			// make sure the requested file is correctly escaped
			http.register_handler("/abc%27abc"
				, [&expected](std::string method, std::string req
				, std::map<std::string, std::string>&)
			{
				expected = true;
				return sim::send_response(404, "Not Found", 0);
			});

			sim.run();
		}
	);

	TEST_CHECK(expected);
}

TORRENT_TEST(urlseed_timeout)
{
	bool timeout = false;
	run_test(
		[](lt::session& ses)
		{	
			file_storage fs;
			fs.add_file("timeout_test", 0x8000);
			lt::add_torrent_params params;
			params.ti = ::create_torrent(fs);
			params.url_seeds.push_back("http://2.2.2.2:8080/");
			params.flags &= ~lt::add_torrent_params::flag_auto_managed;
			params.flags &= ~lt::add_torrent_params::flag_paused;
			params.save_path = ".";
			ses.async_add_torrent(params);
		},
		[&timeout](lt::session& ses, lt::alert const* alert) {
			const lt::peer_disconnected_alert *pda = lt::alert_cast<lt::peer_disconnected_alert>(alert);
			if (pda && pda->error == errors::timed_out_inactivity){
				timeout = true;
			}
		},
		[](sim::simulation& sim, lt::session& ses)
		{
			sim::asio::io_service web_server(sim, address_v4::from_string("2.2.2.2"));
			
			// listen on port 8080
			sim::http_server http(web_server, 8080);
			http.register_stall_handler("/timeout_test");
			sim.run();
		}
	);
	TEST_EQUAL(timeout, true);
}