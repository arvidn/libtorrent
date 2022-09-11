/*

Copyright (c) 2014-2017, 2019-2021, Arvid Norberg
Copyright (c) 2017, Steven Siloti
Copyright (c) 2018, 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "test_utils.hpp"
#include "setup_transfer.hpp"
#include "web_seed_suite.hpp"
#include "settings.hpp"
#include "libtorrent/aux_/random.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/session_params.hpp"
#include "libtorrent/aux_/open_mode.hpp"

using namespace lt;

TORRENT_TEST(web_seed_redirect)
{
	using namespace lt;

	error_code ec;

	std::vector<lt::create_file_entry> fs;
	int piece_size = 0x4000;

	std::array<char, 16000> random_data;
	aux::random_bytes(random_data);

	ofstream("test_file").write(random_data.data(), random_data.size());
	fs.emplace_back("test_file", 16000);

	int port = start_web_server();

	// generate a torrent with pad files to make sure they
	// are not requested web seeds
	lt::create_torrent t(std::move(fs), piece_size);

	char tmp[512];
	std::snprintf(tmp, sizeof(tmp), "http://127.0.0.1:%d/redirect", port);
	t.add_url_seed(tmp);

	// calculate the hash for all pieces
	set_piece_hashes(t, ".", ec);

	if (ec)
	{
		std::printf("error creating hashes for test torrent: %s\n"
			, ec.message().c_str());
		TEST_ERROR("failed to create hashes");
		return;
	}

	std::vector<char> buf;
	bencode(std::back_inserter(buf), t.generate());
	auto torrent_file = std::make_shared<torrent_info>(buf, ec, from_span);

	{
		settings_pack p = settings();
		p.set_int(settings_pack::max_queued_disk_bytes, 256 * 1024);
		lt::session ses(p);

		// disable keep-alive because otherwise the test will choke on seeing
		// the disconnect (from the redirect)
		test_transfer(ses, torrent_file, 0, "http", true, false, false, false);
	}

	stop_web_server();
}
