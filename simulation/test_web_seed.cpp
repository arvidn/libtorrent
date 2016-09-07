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

add_torrent_params create_torrent(file_storage& fs, bool const pad_files = false)
{
	int const piece_size = 0x4000;
	libtorrent::create_torrent t(fs, piece_size
		, pad_files ? piece_size : -1
		, pad_files ? create_torrent::optimize_alignment : 0);

	std::vector<char> piece(piece_size);
	int const num = t.num_pieces();
	if (pad_files)
	{
		for (int i = 0; i < num; ++i)
		{
			std::vector<file_slice> files = fs.map_block(i, 0, fs.piece_size(i));
			int k = 0;
			for (auto& f : files)
			{
				if (fs.pad_file_at(f.file_index))
				{
					for (int j = 0; j < f.size; ++j)
						piece[k++] = 0;
				}
				else
				{
					for (int j = 0; j < f.size; ++j, ++k)
						piece[k] = (k % 26) + 'A';
				}
			}
			// the last piece may be shorter. pad with zeroes
			for (; k < int(piece.size()); ++k)
				piece[k++] = 0;

			sha1_hash ph = hasher(&piece[0], int(piece.size())).final();
			t.set_hash(i, ph);
		}
	}
	else
	{
		for (int i = 0; i < int(piece.size()); ++i)
			piece[i] = (i % 26) + 'A';

		// calculate the hash for all pieces
		sha1_hash ph = hasher(&piece[0], int(piece.size())).final();
		for (int i = 0; i < num; ++i)
			t.set_hash(i, ph);
	}

	// the last piece may have a different size
	if ((fs.total_size() % piece_size) > 0)
	{
		piece.resize(fs.total_size() % piece_size);
		t.set_hash(num-1, hasher(&piece[0], int(piece.size())).final());
	}

	std::vector<char> tmp;
	std::back_insert_iterator<std::vector<char> > out(tmp);

	entry tor = t.generate();

	bencode(out, tor);
	error_code ec;
	add_torrent_params ret;
	ret.ti = std::make_shared<torrent_info>(
		&tmp[0], int(tmp.size()), std::ref(ec), 0);
	ret.flags &= ~lt::add_torrent_params::flag_auto_managed;
	ret.flags &= ~lt::add_torrent_params::flag_paused;
	ret.save_path = ".";
	return ret;
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

TORRENT_TEST(single_file)
{
	using namespace libtorrent;

	file_storage fs;
	fs.add_file("abc'abc", 0x8000); // this filename will have to be escaped
	lt::add_torrent_params params = ::create_torrent(fs);
	params.url_seeds.push_back("http://2.2.2.2:8080/");

	bool expected = false;
	run_test(
		[&params](lt::session& ses)
		{
			ses.async_add_torrent(params);
		},
		[](lt::session& ses, lt::alert const* alert) {},
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

TORRENT_TEST(multi_file)
{
	using namespace libtorrent;
	file_storage fs;
	fs.add_file(combine_path("foo", "abc'abc"), 0x8000); // this filename will have to be escaped
	fs.add_file(combine_path("foo", "bar"), 0x3000);
	lt::add_torrent_params params = ::create_torrent(fs);
	params.url_seeds.push_back("http://2.2.2.2:8080/");

	std::array<bool, 2> expected{{ false, false }};
	run_test(
		[&params](lt::session& ses)
		{
			ses.async_add_torrent(params);
		},
		[](lt::session& ses, lt::alert const* alert) {},
		[&expected](sim::simulation& sim, lt::session& ses)
		{
			sim::asio::io_service web_server(sim, address_v4::from_string("2.2.2.2"));
			// listen on port 8080
			sim::http_server http(web_server, 8080);

			// make sure the requested file is correctly escaped
			http.register_handler("/foo/abc%27abc"
				, [&expected](std::string, std::string, std::map<std::string, std::string>&)
			{
				expected[0] = true;
				return sim::send_response(404, "not found", 0);
			});
			http.register_handler("/foo/bar"
				, [&expected](std::string, std::string, std::map<std::string, std::string>&)
			{
				expected[1] = true;
				return sim::send_response(404, "not found", 0);
			});

			sim.run();
		}
	);

	TEST_CHECK(expected[0]);
	TEST_CHECK(expected[1]);
}

std::string generate_content(lt::file_storage const& fs, int file
	, std::int64_t offset, std::int64_t len)
{
	std::string ret;
	ret.reserve(len);
	std::int64_t const file_offset = fs.file_offset(file);
	int const piece_size = fs.piece_length();
	for (std::int64_t i = offset; i < offset + len; ++i)
		ret.push_back((((i + file_offset) % piece_size) % 26) + 'A');
	return ret;
}

void serve_content_for(sim::http_server& http, std::string const& path
	, lt::file_storage const& fs, int const file)
{
	http.register_content(path, fs.file_size(file)
		, [&fs,file](std::int64_t offset, std::int64_t len)
		{ return generate_content(fs, file, offset, len); });
}

// test redirecting *unaligned* files to the same server still working. i.e. the
// second redirect is added to the same web-seed entry as the first one
TORRENT_TEST(unaligned_file_redirect)
{
	using namespace libtorrent;
	file_storage fs;
	fs.add_file(combine_path("foo", "1"), 0xc030);
	fs.add_file(combine_path("foo", "2"), 0xc030);
	lt::add_torrent_params params = ::create_torrent(fs);
	params.url_seeds.push_back("http://2.2.2.2:8080/");

	bool seeding = false;

	run_test(
		[&params](lt::session& ses)
		{
			ses.async_add_torrent(params);
		},
		[&](lt::session& ses, lt::alert const* alert) {
			if (lt::alert_cast<lt::torrent_finished_alert>(alert))
				seeding = true;
		},
		[&fs](sim::simulation& sim, lt::session& ses)
		{
			// http1 is the root web server that will just redirect requests to
			// other servers
			sim::asio::io_service web_server1(sim, address_v4::from_string("2.2.2.2"));
			sim::http_server http1(web_server1, 8080);
			// redirect file 1 and file 2 to the same servers
			http1.register_redirect("/foo/1", "http://3.3.3.3:4444/bla/file1");
			http1.register_redirect("/foo/2", "http://3.3.3.3:4444/bar/file2");

			// server for serving the content
			sim::asio::io_service web_server2(sim, address_v4::from_string("3.3.3.3"));
			sim::http_server http2(web_server2, 4444);
			serve_content_for(http2, "/bla/file1", fs, 0);
			serve_content_for(http2, "/bar/file2", fs, 1);

			sim.run();
		}
	);

	TEST_EQUAL(seeding, true);
}

// test redirecting *unaligned* but padded files to separate servers
TORRENT_TEST(multi_file_redirect_pad_files)
{
	using namespace libtorrent;
	file_storage fs_;
	fs_.add_file(combine_path("foo", "1"), 0xc030);
	fs_.add_file(combine_path("foo", "2"), 0xc030);
	// true means use padfiles
	lt::add_torrent_params params = ::create_torrent(fs_, true);
	params.url_seeds.push_back("http://2.2.2.2:8080/");

	// since the final torrent is different than what we built (because of pad
	// files), ask about it.
	file_storage const& fs = params.ti->files();

	bool seeding = false;

	run_test(
		[&params](lt::session& ses)
		{
			ses.async_add_torrent(params);
		},
		[&](lt::session& ses, lt::alert const* alert) {
			if (lt::alert_cast<lt::torrent_finished_alert>(alert))
				seeding = true;
		},
		[&fs](sim::simulation& sim, lt::session& ses)
		{
			// http1 is the root web server that will just redirect requests to
			// other servers
			sim::asio::io_service web_server1(sim, address_v4::from_string("2.2.2.2"));
			sim::http_server http1(web_server1, 8080);
			// redirect file 1 and file 2 to different servers
			http1.register_redirect("/foo/1", "http://3.3.3.3:4444/bla/file1");
			http1.register_redirect("/foo/2", "http://4.4.4.4:9999/bar/file2");

			// server for file 1
			sim::asio::io_service web_server2(sim, address_v4::from_string("3.3.3.3"));
			sim::http_server http2(web_server2, 4444);
			serve_content_for(http2, "/bla/file1", fs, 0);

			// server for file 2
			sim::asio::io_service web_server3(sim, address_v4::from_string("4.4.4.4"));
			sim::http_server http3(web_server3, 9999);
			serve_content_for(http3, "/bar/file2", fs, 2);

			sim.run();
		}
	);

	TEST_EQUAL(seeding, true);
}
// test that a web seed can redirect files to separate web servers (as long as
// they are piece aligned)
TORRENT_TEST(multi_file_redirect)
{
	using namespace libtorrent;
	file_storage fs;
	fs.add_file(combine_path("foo", "1"), 0xc000);
	fs.add_file(combine_path("foo", "2"), 0xc030);
	lt::add_torrent_params params = ::create_torrent(fs);
	params.url_seeds.push_back("http://2.2.2.2:8080/");

	bool seeding = false;

	run_test(
		[&params](lt::session& ses)
		{
			ses.async_add_torrent(params);
		},
		[&](lt::session& ses, lt::alert const* alert) {
			if (lt::alert_cast<lt::torrent_finished_alert>(alert))
				seeding = true;
		},
		[&fs](sim::simulation& sim, lt::session& ses)
		{
			// http1 is the root web server that will just redirect requests to
			// other servers
			sim::asio::io_service web_server1(sim, address_v4::from_string("2.2.2.2"));
			sim::http_server http1(web_server1, 8080);
			// redirect file 1 and file 2 to different servers
			http1.register_redirect("/foo/1", "http://3.3.3.3:4444/bla/file1");
			http1.register_redirect("/foo/2", "http://4.4.4.4:9999/bar/file2");

			// server for file 1
			sim::asio::io_service web_server2(sim, address_v4::from_string("3.3.3.3"));
			sim::http_server http2(web_server2, 4444);
			serve_content_for(http2, "/bla/file1", fs, 0);

			// server for file 2
			sim::asio::io_service web_server3(sim, address_v4::from_string("4.4.4.4"));
			sim::http_server http3(web_server3, 9999);
			serve_content_for(http3, "/bar/file2", fs, 1);

			sim.run();
		}
	);

	TEST_EQUAL(seeding, true);
}

// this is expected to fail, since the files are not aligned and redirected to
// separate servers, without pad files
TORRENT_TEST(multi_file_unaligned_redirect)
{
	using namespace libtorrent;
	file_storage fs;
	fs.add_file(combine_path("foo", "1"), 0xc030);
	fs.add_file(combine_path("foo", "2"), 0xc030);
	lt::add_torrent_params params = ::create_torrent(fs);
	params.url_seeds.push_back("http://2.2.2.2:8080/");

	run_test(
		[&params](lt::session& ses)
		{
			ses.async_add_torrent(params);
		},
		[&](lt::session& ses, lt::alert const* alert) {
			// We don't expect to get this aslert
			TEST_CHECK(lt::alert_cast<lt::torrent_finished_alert>(alert) == nullptr);
		},
		[&fs](sim::simulation& sim, lt::session& ses)
		{
			// http1 is the root web server that will just redirect requests to
			// other servers
			sim::asio::io_service web_server1(sim, address_v4::from_string("2.2.2.2"));
			sim::http_server http1(web_server1, 8080);
			// redirect file 1 and file 2 to different servers
			http1.register_redirect("/foo/1", "http://3.3.3.3:4444/bla/file1");
			http1.register_redirect("/foo/2", "http://4.4.4.4:9999/bar/file2");

			// server for file 1
			sim::asio::io_service web_server2(sim, address_v4::from_string("3.3.3.3"));
			sim::http_server http2(web_server2, 4444);
			serve_content_for(http2, "/bla/file1", fs, 0);

			// server for file 2
			sim::asio::io_service web_server3(sim, address_v4::from_string("4.4.4.4"));
			sim::http_server http3(web_server3, 9999);
			serve_content_for(http3, "/bar/file2", fs, 1);

			sim.run();
		}
	);
}
