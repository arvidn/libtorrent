/*

Copyright (c) 2014-2017, 2019-2021, Arvid Norberg
Copyright (c) 2017, Steven Siloti
Copyright (c) 2018, 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "disk_io_test.hpp"
#include "test_utils.hpp"
#include "setup_transfer.hpp"
#include "web_seed_suite.hpp"
#include "settings.hpp"
#include "libtorrent/aux_/random.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/load_torrent.hpp"
#include "libtorrent/session_params.hpp"
#include "libtorrent/aux_/open_mode.hpp"

using namespace lt;

TORRENT_TEST_DISK_IO(web_seed_redirect)
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
		stop_web_server();
		return;
	}

	std::vector<char> const buf = bencode(t.generate());
	lt::add_torrent_params atp = load_torrent_buffer(buf);

	{
		settings_pack p = settings();
		p.set_int(settings_pack::max_queued_disk_bytes, 256 * 1024);
		session_params sp(p);
		sp.disk_io_constructor = disk_io;
		lt::session ses(std::move(sp));

		// disable keep-alive because otherwise the test will choke on seeing
		// the disconnect (from the redirect)
		test_transfer(ses, atp, 0, "http", web_seed::no_keepalive);
	}

	stop_web_server();
}

namespace {

// creates a single-file torrent with one web seed, at the specified path of
// the test web server
lt::add_torrent_params redirect_torrent(int const port, char const* path, char const* save_path)
{
	std::vector<lt::create_file_entry> fs;
	std::array<char, 16000> random_data;
	aux::random_bytes(random_data);
	ofstream("test_file").write(random_data.data(), random_data.size());
	fs.emplace_back("test_file", 16000);

	lt::create_torrent t(std::move(fs), 0x4000);
	char tmp[512];
	std::snprintf(tmp, sizeof(tmp), "http://127.0.0.1:%d%s", port, path);
	t.add_url_seed(tmp);
	error_code ec;
	set_piece_hashes(t, ".", ec);
	if (ec)
		TEST_ERROR("failed to create hashes");

	std::vector<char> const buf = bencode(t.generate());
	lt::add_torrent_params atp = load_torrent_buffer(buf);
	atp.save_path = save_path;
	atp.flags &= ~torrent_flags::paused;
	atp.flags &= ~torrent_flags::auto_managed;
	return atp;
}

} // anonymous namespace

// the web seed redirects 4 times before arriving at the file. The web seeds
// that have redirected used to keep counting against
// max_web_seed_connections (3 by default), so the last one was never
// connected.
TORRENT_TEST(web_seed_redirect_hops)
{
	int const port = start_web_server();

	{
		lt::session ses(settings());
		torrent_handle th =
			ses.add_torrent(redirect_torrent(port, "/redirect_hops/3", "tmp_redirect_hops"));

		bool is_seeding = false;
		for (int i = 0; i < 100 && !is_seeding; ++i)
		{
			std::this_thread::sleep_for(lt::milliseconds(100));
			is_seeding = th.status().is_seeding;
		}
		TEST_CHECK(is_seeding);
	}

	stop_web_server();
}

// a web seed that keeps redirecting to new URLs is not supposed to make us
// follow it forever, adding a new web seed every time
TORRENT_TEST(web_seed_redirect_chain)
{
	int const port = start_web_server();

	{
		lt::session ses(settings());
		torrent_handle th =
			ses.add_torrent(redirect_torrent(port, "/redirect_chain/0", "tmp_redirect_chain"));

		// the original web seed and the 5 redirects we follow. The last one
		// is removed again when it turns out to redirect too
		std::size_t num_web_seeds = 0;
		for (int i = 0; i < 100 && num_web_seeds < 5; ++i)
		{
			std::this_thread::sleep_for(lt::milliseconds(100));
			num_web_seeds = std::max(num_web_seeds, th.url_seeds().size());
		}
		TEST_CHECK(num_web_seeds >= 5);

		// give it some time to follow more redirects
		for (int i = 0; i < 20; ++i)
		{
			std::this_thread::sleep_for(lt::milliseconds(100));
			num_web_seeds = std::max(num_web_seeds, th.url_seeds().size());
		}
		TEST_CHECK(num_web_seeds <= 6);
	}

	stop_web_server();
}
