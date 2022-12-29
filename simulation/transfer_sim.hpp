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

#ifndef TORRENT_TRANSFER_SIM_HPP
#define TORRENT_TRANSFER_SIM_HPP

#include <string>
#include <array>
#include <iostream>

#include "simulator/simulator.hpp"
#include "simulator/socks_server.hpp"
#include "simulator/utils.hpp"
#include "simulator/http_server.hpp"

#include "libtorrent/session.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/session_stats.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/aux_/proxy_settings.hpp"
#include "libtorrent/aux_/escape_string.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/random.hpp"

#include "test.hpp"
#include "create_torrent.hpp"
#include "settings.hpp"
#include "setup_swarm.hpp"
#include "utils.hpp"
#include "test_utils.hpp"
#include "setup_transfer.hpp" // for addr()
#include "disk_io.hpp"

template <typename Setup, typename HandleAlerts, typename Test>
void run_test(
	Setup setup
	, HandleAlerts on_alert
	, Test test
	, test_transfer_flags_t flags = {}
	, test_disk const downloader_disk_constructor = test_disk()
	, test_disk const seed_disk_constructor = test_disk()
	, lt::seconds const timeout = lt::seconds(60)
	)
{
	using lt::settings_pack;
	using lt::address;
	using lt::alert_cast;

	const bool use_ipv6 = bool(flags & tx::ipv6);

	char const* peer0_ip[2] = { "50.0.0.1", "feed:face:baad:f00d::1" };
	char const* peer1_ip[2] = { "50.0.0.2", "feed:face:baad:f00d::2" };

	address peer0 = addr(peer0_ip[use_ipv6]);
	address peer1 = addr(peer1_ip[use_ipv6]);
	address proxy = (flags & tx::ipv6) ? addr("2001::2") : addr("50.50.50.50");

	// setup the simulation
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	sim::asio::io_context ios0 { sim, peer0 };
	sim::asio::io_context ios1 { sim, peer1 };

	lt::session_proxy zombie[2];

	sim::asio::io_context proxy_ios{sim, proxy };
	sim::socks_server socks4(proxy_ios, 4444, 4);
	sim::socks_server socks5(proxy_ios, 5555, 5);
	socks5.bind_start_port(3000);

	lt::session_params params;
	// setup settings pack to use for the session (customization point)
	lt::settings_pack& pack = params.settings;
	pack = settings();
	pack.set_bool(settings_pack::disable_hash_checks, false);

	// disable utp by default
	pack.set_bool(settings_pack::enable_outgoing_utp, false);
	pack.set_bool(settings_pack::enable_incoming_utp, false);

	// disable encryption by default
	pack.set_bool(settings_pack::prefer_rc4, false);
	pack.set_int(settings_pack::in_enc_policy, settings_pack::pe_disabled);
	pack.set_int(settings_pack::out_enc_policy, settings_pack::pe_disabled);
	pack.set_int(settings_pack::allowed_enc_level, settings_pack::pe_plaintext);

	pack.set_str(settings_pack::listen_interfaces, make_ep_string(peer0_ip[use_ipv6], use_ipv6, "6881"));
	// create session
	std::shared_ptr<lt::session> ses[2];

	// session 0 is a downloader, session 1 is a seed

	params.disk_io_constructor = downloader_disk_constructor;
	ses[0] = std::make_shared<lt::session>(params, ios0);

	pack.set_str(settings_pack::listen_interfaces, make_ep_string(peer1_ip[use_ipv6], use_ipv6, "6881"));
	if (flags & tx::resume_restart)
	{
		// if we don't enable this, the second connection attempt will be
		// rejected
		pack.set_bool(settings_pack::allow_multiple_connections_per_ip, true);
	}

	params.disk_io_constructor = seed_disk_constructor.set_files(existing_files_mode::full_valid);
	ses[1] = std::make_shared<lt::session>(params, ios1);

	setup(*ses[0], *ses[1]);

	// only monitor alerts for session 0 (the downloader)
	print_alerts(*ses[0], [&](lt::session& ses, lt::alert const* a) {
		if (auto ta = lt::alert_cast<lt::add_torrent_alert>(a))
		{
			if (!(flags & tx::web_seed))
			{
				if (flags & tx::connect_proxy)
					ta->handle.connect_peer(lt::tcp::endpoint(proxy, 3000));
				else
					ta->handle.connect_peer(lt::tcp::endpoint(peer1, 6881));
			}
		}
		on_alert(ses, a);
	}, 0);

	print_alerts(*ses[1], [](lt::session&, lt::alert const*){}, 1);

	int const piece_size = (flags & tx::small_pieces) ? lt::default_block_size
		: (flags & tx::large_pieces) ? (4 * lt::default_block_size)
		: (flags & tx::odd_pieces) ? (2 * lt::default_block_size + 123)
		: (2 * lt::default_block_size);

	int const num_pieces = 10;

	lt::create_flags_t const cflags
		= ((flags & tx::v2_only) ? lt::create_torrent::v2_only
		: (flags & tx::v1_only) ? lt::create_torrent::v1_only
		: lt::create_flags_t{})
		| lt::create_torrent::allow_odd_piece_size;

	int const num_files = (flags & tx::multiple_files) ? 3 : 1;

	lt::add_torrent_params atp;

	atp.ti = ::create_test_torrent(piece_size, num_pieces, cflags, num_files);
	// this is unused by the test disk I/O
	atp.save_path = ".";
	atp.flags &= ~lt::torrent_flags::auto_managed;
	atp.flags &= ~lt::torrent_flags::paused;

	sim::asio::io_context web_server(sim, lt::make_address_v4("2.2.2.2"));
	sim::http_server http(web_server, 8080);

	int corrupt_counter = INT_MAX;
	if (flags & tx::corruption)
		corrupt_counter = lt::default_block_size * 2;

	if (flags & tx::web_seed)
	{
		auto const& fs = atp.ti->files();
		for (lt::file_index_t f : fs.file_range())
		{
			std::string file_path = fs.file_path(f, "/");
			lt::convert_path_to_posix(file_path);
			http.register_content(file_path, fs.file_size(f)
				, [&fs,f,&corrupt_counter](std::int64_t offset, std::int64_t len)
				{
					TORRENT_ASSERT(offset + len <= fs.file_size(f));
					auto const req = fs.map_file(f, offset, len);
					std::string ret;
					ret.resize(req.length);
					generate_block(&ret[0], req, 0, fs.piece_length());
					if (corrupt_counter < 0)
						lt::aux::random_bytes(ret);
					else if (corrupt_counter - len < 0)
						lt::aux::random_bytes(lt::span<char>(ret).subspan(corrupt_counter));
					corrupt_counter -= len;
					return ret;
				});
		}
	}

	// if we're seeding with a web server, no need to start the second session
	if (!(flags & tx::web_seed))
		ses[1]->async_add_torrent(atp);

	auto torrent = atp.ti;

	atp.save_path = save_path(0);
	if (flags & tx::magnet_download)
	{
		atp.info_hashes = atp.ti->info_hashes();
		atp.ti.reset();
	}
	if (flags & tx::web_seed)
		atp.url_seeds.emplace_back("http://2.2.2.2:8080/");

	ses[0]->async_add_torrent(atp);

	sim::timer t(sim, timeout, [&](boost::system::error_code const&)
	{
		auto h = ses[0]->get_torrents();
		auto ti = h[0].torrent_file_with_hashes();

		// if we're a seed, we should definitely have the torrent info. If we're
		// note a seed, we may still have the torrent_info in case it's a v1
		// torrent
		if (is_seed(*ses[0])) TEST_CHECK(ti);

		if (ti)
		{
			if (ti->v2())
				TEST_EQUAL(ti->v2_piece_hashes_verified(), true);

			auto downloaded = serialize(*ti);
			auto added = serialize(*torrent);
			TEST_CHECK(downloaded == added);
		}

		test(ses);

		// shut down
		int idx = 0;
		for (auto& s : ses)
		{
			zombie[idx++] = s->abort();
			s.reset();
		}
	});

	sim.run();
}

bool run_matrix_test(test_transfer_flags_t const flags, existing_files_mode const files);

void no_init(lt::session& ses0, lt::session& ses1);


struct combine_t
{
	combine_t() {}

	void operator()(lt::session& s, lt::alert const* a)
	{
		for (auto& h : m_handlers)
			h(s, a);
	}

	void add(std::function<void(lt::session&, lt::alert const*)> h)
	{
		m_handlers.emplace_back(std::move(h));
	}

	std::vector<std::function<void(lt::session&, lt::alert const*)>> m_handlers;
};

// this alert handler records all pieces that complete and pass hash check into
// the set passed in to its constructor
struct record_finished_pieces
{
	record_finished_pieces(std::set<lt::piece_index_t>& p);
	void operator()(lt::session&, lt::alert const* a) const;
	std::set<lt::piece_index_t>* m_passed;
};

// this alert handler will save resume data, remove the torrent and add it back
// resuming from the saved state
struct restore_from_resume
{
	restore_from_resume();
	void operator()(lt::session&, lt::alert const* a);

	lt::time_point m_last_check;
	std::vector<char> m_resume_buffer;
	bool m_triggered = false;
	bool m_done = false;
};

struct expect_seed
{
	expect_seed(bool e);
	void operator()(std::shared_ptr<lt::session> ses[2]) const;
	bool m_expect;
};

int blocks_per_piece(test_transfer_flags_t const flags);
int num_pieces(test_transfer_flags_t const flags);

template <typename F>
void run_all_combinations(F fun)
{
	for (test_transfer_flags_t piece_size : {test_transfer_flags_t{}, tx::odd_pieces, tx::small_pieces, tx::large_pieces})
		for (test_transfer_flags_t web_seed : {tx::web_seed, test_transfer_flags_t{}})
			for (test_transfer_flags_t corruption : {test_transfer_flags_t{}, tx::corruption})
				for (test_transfer_flags_t bt_version : {test_transfer_flags_t{}, tx::v2_only, tx::v1_only})
					for (test_transfer_flags_t magnet : {test_transfer_flags_t{}, tx::magnet_download})
						for (test_transfer_flags_t multi_file : {test_transfer_flags_t{}, tx::multiple_files})
							for (test_transfer_flags_t resume : {tx::resume_restart, test_transfer_flags_t{}})
								if (fun(piece_size | bt_version | magnet
									| multi_file | web_seed | corruption | resume))
									return;
}

#endif
