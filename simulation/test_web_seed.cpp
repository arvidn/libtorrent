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
#include "libtorrent/aux_/path.hpp"
#include "simulator/http_server.hpp"
#include "simulator/http_proxy.hpp"
#include "settings.hpp"
#include "libtorrent/create_torrent.hpp"
#include "simulator/simulator.hpp"
#include "setup_swarm.hpp"
#include "utils.hpp"
#include "make_proxy_settings.hpp"
#include "simulator/utils.hpp"
#include <iostream>
#include <numeric>

using namespace sim;
using namespace lt;


int const piece_size = 0x4000;

add_torrent_params create_torrent(file_storage& fs, bool const v1_only = false)
{
	lt::create_torrent t(fs, piece_size
		, v1_only ? create_torrent::v1_only : create_flags_t{});

	std::vector<char> piece;
	piece.reserve(fs.piece_length());
	piece_index_t const num = fs.end_piece();
	for (piece_index_t i(0); i < num; ++i)
	{
		int k = 0;
		std::vector<file_slice> files = fs.map_block(i, 0, fs.piece_size(i));
		for (auto& f : files)
		{
			if (fs.pad_file_at(f.file_index))
			{
				for (int j = 0; j < f.size; ++j, ++k)
					piece.push_back('\0');
			}
			else
			{
				for (int j = 0; j < f.size; ++j, ++k)
					piece.push_back((k % 26) + 'A');
			}
		}

		t.set_hash(i, hasher(piece).final());
		if (!v1_only)
		{
			piece_index_t const file_first_piece(int(fs.file_offset(files[0].file_index) / fs.piece_length()));
			t.set_hash2(files[0].file_index, i - file_first_piece
				, hasher256(span<char>(piece).first(files[0].size)).final());
		}
		piece.clear();
	}

	std::vector<char> tmp;
	std::back_insert_iterator<std::vector<char>> out(tmp);

	entry tor = t.generate();

	bencode(out, tor);
	add_torrent_params ret;
	ret.ti = std::make_shared<torrent_info>(tmp, from_span);
	ret.flags &= ~lt::torrent_flags::auto_managed;
	ret.flags &= ~lt::torrent_flags::paused;
	ret.save_path = ".";
	return ret;
}

struct sim_config : sim::default_config
{
	explicit sim_config() {}

	chrono::high_resolution_clock::duration hostname_lookup(
		asio::ip::address const& requestor
		, std::string hostname
		, std::vector<asio::ip::address>& result
		, boost::system::error_code& ec) override
	{
		auto const ret = duration_cast<chrono::high_resolution_clock::duration>(chrono::milliseconds(100));
		if (hostname == "2.server.com")
		{
			result.push_back(make_address_v4("2.2.2.2"));
			return ret;
		}
		if (hostname == "2.xn--server-.com")
		{
			result.push_back(make_address_v4("2.2.2.2"));
			return ret;
		}
		if (hostname == "3.server.com")
		{
			result.push_back(make_address_v4("3.3.3.3"));
			return ret;
		}
		if (hostname == "3.xn--server-.com")
		{
			result.push_back(make_address_v4("3.3.3.3"));
			return ret;
		}
		if (hostname == "local-network.com")
		{
			result.push_back(make_address_v4("192.168.1.13"));
			return ret;
		}

		return default_config::hostname_lookup(requestor, hostname, result, ec);
	}
};

// this is the general template for these tests. create the session with custom
// settings (Settings), set up the test, by adding torrents with certain
// arguments (Setup), run the test and verify the end state (Test)
template <typename Setup, typename HandleAlerts, typename Test>
void run_test(Setup const& setup
	, HandleAlerts const& on_alert
	, Test const& test
	, lt::seconds const timeout = lt::seconds{100})
{
	// setup the simulation
	sim_config network_cfg;
	sim::simulation sim{network_cfg};
	std::unique_ptr<sim::asio::io_context> ios = make_io_context(sim, 0);
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
	sim::timer t(sim, timeout, [&](boost::system::error_code const&)
	{
		std::printf("shutting down\n");
		// shut down
		zombie = ses->abort();
		ses.reset();
	});

	test(sim, *ses);
}

TORRENT_TEST(single_file)
{
	using namespace lt;

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
		[](lt::session&, lt::alert const*) {},
		[&expected](sim::simulation& sim, lt::session&)
		{
			sim::asio::io_context web_server(sim, make_address_v4("2.2.2.2"));
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
	using namespace lt;
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
		[](lt::session&, lt::alert const*) {},
		[&expected](sim::simulation& sim, lt::session&)
		{
			sim::asio::io_context web_server(sim, make_address_v4("2.2.2.2"));
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

std::string generate_content(lt::file_storage const& fs, file_index_t file
	, std::int64_t offset, std::int64_t len)
{
	std::string ret;
	ret.reserve(lt::aux::numeric_cast<std::size_t>(len));
	std::int64_t const file_offset = fs.file_offset(file);
	int const piece_sz = fs.piece_length();
	for (std::int64_t i = offset + file_offset; i < offset + file_offset + len; ++i)
		ret.push_back(((i % piece_sz) % 26) + 'A');
	return ret;
}

void serve_content_for(sim::http_server& http, std::string const& path
	, lt::file_storage const& fs, file_index_t const file)
{
	http.register_content(path, fs.file_size(file_index_t(file))
		, [&fs,file](std::int64_t offset, std::int64_t len)
		{ return generate_content(fs, file, offset, len); });
}

// test redirecting *unaligned* files to the same server still working. i.e. the
// second redirect is added to the same web-seed entry as the first one
TORRENT_TEST(unaligned_file_redirect)
{
	using namespace lt;
	file_storage fs;
	fs.add_file(combine_path("foo", "1"), 0xc030);
	fs.add_file(combine_path("foo", "2"), 0xc030);
	lt::add_torrent_params params = ::create_torrent(fs, true);
	params.url_seeds.push_back("http://2.2.2.2:8080/");

	bool seeding = false;

	run_test(
		[&params](lt::session& ses)
		{
			ses.async_add_torrent(params);
		},
		[&](lt::session&, lt::alert const* alert) {
			if (lt::alert_cast<lt::torrent_finished_alert>(alert))
				seeding = true;
		},
		[&fs](sim::simulation& sim, lt::session&)
		{
			// http1 is the root web server that will just redirect requests to
			// other servers
			sim::asio::io_context web_server1(sim, make_address_v4("2.2.2.2"));
			sim::http_server http1(web_server1, 8080);
			// redirect file 1 and file 2 to the same servers
			http1.register_redirect("/foo/1", "http://3.3.3.3:4444/bla/file1");
			http1.register_redirect("/foo/2", "http://3.3.3.3:4444/bar/file2");

			// server for serving the content
			sim::asio::io_context web_server2(sim, make_address_v4("3.3.3.3"));
			sim::http_server http2(web_server2, 4444);
			serve_content_for(http2, "/bla/file1", fs, file_index_t(0));
			serve_content_for(http2, "/bar/file2", fs, file_index_t(1));

			sim.run();
		}
	);

	TEST_EQUAL(seeding, true);
}

// test redirecting *unaligned* but padded files to separate servers
TORRENT_TEST(multi_file_redirect_pad_files)
{
	using namespace lt;
	file_storage fs_;
	fs_.add_file(combine_path("foo", "1"), 0xc030);
	fs_.add_file(combine_path("foo", "2"), 0xc030);
	// true means use padfiles
	lt::add_torrent_params params = ::create_torrent(fs_);
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
		[&](lt::session&, lt::alert const* alert) {
			if (lt::alert_cast<lt::torrent_finished_alert>(alert))
				seeding = true;
		},
		[&fs](sim::simulation& sim, lt::session&)
		{
			// http1 is the root web server that will just redirect requests to
			// other servers
			sim::asio::io_context web_server1(sim, make_address_v4("2.2.2.2"));
			sim::http_server http1(web_server1, 8080);
			// redirect file 1 and file 2 to different servers
			http1.register_redirect("/foo/1", "http://3.3.3.3:4444/bla/file1");
			http1.register_redirect("/foo/2", "http://4.4.4.4:9999/bar/file2");

			// server for file 1
			sim::asio::io_context web_server2(sim, make_address_v4("3.3.3.3"));
			sim::http_server http2(web_server2, 4444);
			serve_content_for(http2, "/bla/file1", fs, file_index_t(0));

			// server for file 2
			sim::asio::io_context web_server3(sim, make_address_v4("4.4.4.4"));
			sim::http_server http3(web_server3, 9999);
			serve_content_for(http3, "/bar/file2", fs, file_index_t(2));

			sim.run();
		}
	);

	TEST_EQUAL(seeding, true);
}
// test that a web seed can redirect files to separate web servers (as long as
// they are piece aligned)
TORRENT_TEST(multi_file_redirect)
{
	using namespace lt;
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
		[&](lt::session&, lt::alert const* alert) {
			if (lt::alert_cast<lt::torrent_finished_alert>(alert))
				seeding = true;
		},
		[&fs](sim::simulation& sim, lt::session&)
		{
			// http1 is the root web server that will just redirect requests to
			// other servers
			sim::asio::io_context web_server1(sim, make_address_v4("2.2.2.2"));
			sim::http_server http1(web_server1, 8080);
			// redirect file 1 and file 2 to different servers
			http1.register_redirect("/foo/1", "http://3.3.3.3:4444/bla/file1");
			http1.register_redirect("/foo/2", "http://4.4.4.4:9999/bar/file2");

			// server for file 1
			sim::asio::io_context web_server2(sim, make_address_v4("3.3.3.3"));
			sim::http_server http2(web_server2, 4444);
			serve_content_for(http2, "/bla/file1", fs, file_index_t(0));

			// server for file 2
			sim::asio::io_context web_server3(sim, make_address_v4("4.4.4.4"));
			sim::http_server http3(web_server3, 9999);
			serve_content_for(http3, "/bar/file2", fs, file_index_t(1));

			sim.run();
		}
	);

	TEST_EQUAL(seeding, true);
}

// test web_seed redirect through proxy
TORRENT_TEST(multi_file_redirect_through_proxy)
{
	using namespace lt;
	file_storage fs;
	fs.add_file(combine_path("foo", "1"), 0xc000);
	fs.add_file(combine_path("foo", "2"), 0xc030);
	lt::add_torrent_params params = ::create_torrent(fs);
	params.url_seeds.push_back("http://2.2.2.2:8080/");

	bool seeding = false;

	run_test(
		[&params](lt::session& ses)
		{
			settings_pack pack;

			pack.set_int(settings_pack::proxy_type, settings_pack::http);
			pack.set_str(settings_pack::proxy_hostname, "50.50.50.50");
			pack.set_str(settings_pack::proxy_username, "testuser");
			pack.set_str(settings_pack::proxy_password, "testpass");
			pack.set_int(settings_pack::proxy_port, 4445);
			pack.set_bool(settings_pack::proxy_hostnames, true);
			ses.apply_settings(pack);

			ses.async_add_torrent(params);
		},
		[&](lt::session&, lt::alert const* alert) {
			if (lt::alert_cast<lt::torrent_finished_alert>(alert)) {
				seeding = true;
			}
		},
		[&fs](sim::simulation& sim, lt::session&)
		{
			sim::asio::io_context proxy_ios(sim, make_address_v4("50.50.50.50"));
			sim::http_proxy http_p(proxy_ios, 4445);

			// http1 is the root web server that will just redirect requests to
			// other servers
			sim::asio::io_context web_server1(sim, make_address_v4("2.2.2.2"));
			sim::http_server http1(web_server1, 8080);
			// redirect file 1 and file 2 to different servers
			http1.register_redirect("/foo/1", "http://3.3.3.3:4444/bla/file1");
			http1.register_redirect("/foo/2", "http://4.4.4.4:9999/bar/file2");

			// server for file 1
			sim::asio::io_context web_server2(sim, make_address_v4("3.3.3.3"));
			sim::http_server http2(web_server2, 4444);
			serve_content_for(http2, "/bla/file1", fs, file_index_t(0));

			// server for file 2
			sim::asio::io_context web_server3(sim, make_address_v4("4.4.4.4"));
			sim::http_server http3(web_server3, 9999);
			serve_content_for(http3, "/bar/file2", fs, file_index_t(1));

			sim.run();
		}
	);

	TEST_EQUAL(seeding, true);
}

// this is expected to fail, since the files are not aligned and redirected to
// separate servers, without pad files
TORRENT_TEST(multi_file_unaligned_redirect)
{
	using namespace lt;
	file_storage fs;
	fs.add_file(combine_path("foo", "1"), 0xc030);
	fs.add_file(combine_path("foo", "2"), 0xc030);
	lt::add_torrent_params params = ::create_torrent(fs, true);
	params.url_seeds.push_back("http://2.2.2.2:8080/");

	run_test(
		[&params](lt::session& ses)
		{
			ses.async_add_torrent(params);
		},
		[&](lt::session&, lt::alert const* alert) {
			// We don't expect to get this aslert
			TEST_CHECK(lt::alert_cast<lt::torrent_finished_alert>(alert) == nullptr);
		},
		[&fs](sim::simulation& sim, lt::session&)
		{
			// http1 is the root web server that will just redirect requests to
			// other servers
			sim::asio::io_context web_server1(sim, make_address_v4("2.2.2.2"));
			sim::http_server http1(web_server1, 8080);
			// redirect file 1 and file 2 to different servers
			http1.register_redirect("/foo/1", "http://3.3.3.3:4444/bla/file1");
			http1.register_redirect("/foo/2", "http://4.4.4.4:9999/bar/file2");

			// server for file 1
			sim::asio::io_context web_server2(sim, make_address_v4("3.3.3.3"));
			sim::http_server http2(web_server2, 4444);
			serve_content_for(http2, "/bla/file1", fs, file_index_t(0));

			// server for file 2
			sim::asio::io_context web_server3(sim, make_address_v4("4.4.4.4"));
			sim::http_server http3(web_server3, 9999);
			serve_content_for(http3, "/bar/file2", fs, file_index_t(1));

			sim.run();
		}
	);
}
TORRENT_TEST(urlseed_timeout)
{
	bool timeout = false;
	run_test(
		[](lt::session& ses)
		{
			file_storage fs;
			fs.add_file("timeout_test", 0x8000);
			lt::add_torrent_params params = ::create_torrent(fs);
			params.url_seeds.push_back("http://2.2.2.2:8080/");
			params.flags &= ~lt::torrent_flags::auto_managed;
			params.flags &= ~lt::torrent_flags::paused;
			params.save_path = ".";
			ses.async_add_torrent(params);
		},
		[&timeout](lt::session&, lt::alert const* alert) {
			const lt::peer_disconnected_alert *pda = lt::alert_cast<lt::peer_disconnected_alert>(alert);
			if (pda && pda->error == errors::timed_out_inactivity){
				timeout = true;
			}
		},
		[](sim::simulation& sim, lt::session&)
		{
			sim::asio::io_context web_server(sim, make_address_v4("2.2.2.2"));

			// listen on port 8080
			sim::http_server http(web_server, 8080);
			http.register_stall_handler("/timeout_test");
			sim.run();
		}
	);
	TEST_EQUAL(timeout, true);
}

// check for correct handle of unexpected http status response.
// with disabled "close_redundant_connections" alive web server connection
// may be closed in such manner.
TORRENT_TEST(no_close_redudant_webseed)
{
	using namespace lt;

	file_storage fs;
	fs.add_file("file1", 1);
	lt::add_torrent_params params = ::create_torrent(fs);
	params.url_seeds.push_back("http://2.2.2.2:8080/");

	bool expected = false;
	run_test(
		[&params](lt::session& ses)
		{
			lt::settings_pack pack;
			pack.set_bool(settings_pack::close_redundant_connections, false);
			ses.apply_settings(pack);
			ses.async_add_torrent(params);
		},
		[](lt::session&, lt::alert const*) {},
		[&expected](sim::simulation& sim, lt::session&)
		{
			sim::asio::io_context web_server(sim, make_address_v4("2.2.2.2"));
			// listen on port 8080
			sim::http_server http(web_server, 8080);

			http.register_handler("/file1"
				, [&expected](std::string method, std::string req
				, std::map<std::string, std::string>&)
			{
				expected = true;
				char const* extra_headers[4] = { "Content-Range: bytes 0-0/1\r\n", "", "", ""};

				return sim::send_response(206, "Partial Content", 1, extra_headers).
					append("A").
					append(sim::send_response(408, "REQUEST TIMEOUT", 0));
			});

			sim.run();
		}
	);

	TEST_CHECK(expected);
}

// make sure the max_web_seed_connections limit is honored
TORRENT_TEST(web_seed_connection_limit)
{
	using namespace lt;

	file_storage fs;
	fs.add_file("file1", 1);
	lt::add_torrent_params params = ::create_torrent(fs);
	params.url_seeds.push_back("http://2.2.2.1:8080/");
	params.url_seeds.push_back("http://2.2.2.2:8080/");
	params.url_seeds.push_back("http://2.2.2.3:8080/");
	params.url_seeds.push_back("http://2.2.2.4:8080/");

	std::array<int, 4> expected = {};
	run_test(
		[&params](lt::session& ses)
		{
			lt::settings_pack pack;
			pack.set_int(settings_pack::max_web_seed_connections, 2);
			ses.apply_settings(pack);
			ses.async_add_torrent(params);
		},
		[](lt::session&, lt::alert const*) {},
		[&expected](sim::simulation& sim, lt::session&)
		{
			using ios = sim::asio::io_context;
			ios web_server1{sim, make_address_v4("2.2.2.1")};
			ios web_server2{sim, make_address_v4("2.2.2.2")};
			ios web_server3{sim, make_address_v4("2.2.2.3")};
			ios web_server4{sim, make_address_v4("2.2.2.4")};

			// listen on port 8080
			using ws = sim::http_server;
			ws http1{web_server1, 8080};
			ws http2{web_server2, 8080};
			ws http3{web_server3, 8080};
			ws http4{web_server4, 8080};

			auto const handler = [&expected](std::string method, std::string req
				, std::map<std::string, std::string>&, int idx)
			{
				++expected[idx];
				// deliberately avoid sending the content, to cause a hang
				return sim::send_response(206, "Partial Content", 1);
			};

			using namespace std::placeholders;
			http1.register_handler("/file1", std::bind(handler, _1, _2, _3, 0));
			http2.register_handler("/file1", std::bind(handler, _1, _2, _3, 1));
			http3.register_handler("/file1", std::bind(handler, _1, _2, _3, 2));
			http4.register_handler("/file1", std::bind(handler, _1, _2, _3, 3));

			sim.run();
		},
		lt::seconds(15)
	);

	// make sure we only connected to 2 of the web seeds, since that's the limit
	TEST_CHECK(std::accumulate(expected.begin(), expected.end(), 0) == 2);
}

bool test_idna(char const* url, char const* redirect, bool allow_idna)
{
	using namespace lt;
	file_storage fs;
	fs.add_file("1", 0xc030);
	lt::add_torrent_params params = ::create_torrent(fs);
	params.url_seeds.emplace_back(url);

	bool seeding = false;

	error_code ignore;
	remove("1", ignore);

	run_test(
		[&](lt::session& ses)
		{
			settings_pack pack;
			pack.set_bool(settings_pack::allow_idna, allow_idna);
			ses.apply_settings(pack);
			ses.async_add_torrent(params);
		},
		[&](lt::session&, lt::alert const* alert) {
			if (lt::alert_cast<lt::torrent_finished_alert>(alert))
				seeding = true;
		},
		[&](sim::simulation& sim, lt::session&)
		{
			// http1 is the root web server that will just redirect requests to
			// other servers
			sim::asio::io_context web_server1(sim, make_address_v4("2.2.2.2"));
			sim::http_server http1(web_server1, 8080);
			// redirect file 1 and file 2 to the same servers
			if (redirect)
				http1.register_redirect("/1", redirect);

			// server for serving the content
			sim::asio::io_context web_server2(sim, make_address_v4("3.3.3.3"));
			sim::http_server http2(web_server2, 8080);
			serve_content_for(http2, "/1", fs, file_index_t(0));

			sim.run();
		}
	);

	return seeding;
}

TORRENT_TEST(idna)
{
	// disallow IDNA hostnames
	TEST_EQUAL(test_idna("http://3.server.com:8080", nullptr, false), true);
	TEST_EQUAL(test_idna("http://3.xn--server-.com:8080", nullptr, false), false);

	// allow IDNA hostnames
	TEST_EQUAL(test_idna("http://3.server.com:8080", nullptr, true), true);
	TEST_EQUAL(test_idna("http://3.xn--server-.com:8080", nullptr, true), true);
}

TORRENT_TEST(idna_redirect)
{
	// disallow IDNA hostnames
	TEST_EQUAL(test_idna("http://2.server.com:8080", "http://3.server.com:8080/1", false), true);
	TEST_EQUAL(test_idna("http://2.server.com:8080", "http://3.xn--server-.com:8080/1", false), false);

	TEST_EQUAL(test_idna("http://2.xn--server-.com:8080", "http://3.server.com:8080/1", false), false);
	TEST_EQUAL(test_idna("http://2.xn--server-.com:8080", "http://3.xn--server-.com:8080/1", false), false);

	// allow IDNA hostnames
	TEST_EQUAL(test_idna("http://2.server.com:8080", "http://3.server.com:8080/1", true), true);
	TEST_EQUAL(test_idna("http://2.server.com:8080", "http://3.xn--server-.com:8080/1", true), true);

	TEST_EQUAL(test_idna("http://2.xn--server-.com:8080", "http://3.server.com:8080/1", true), true);
	TEST_EQUAL(test_idna("http://2.xn--server-.com:8080", "http://3.xn--server-.com:8080/1", true), true);
}

bool test_ssrf(char const* url, char const* redirect, bool enable_feature)
{
	using namespace lt;
	file_storage fs;
	fs.add_file("1", 0xc030);
	lt::add_torrent_params params = ::create_torrent(fs);
	params.url_seeds.emplace_back(url);

	bool seeding = false;

	error_code ignore;
	remove("1", ignore);

	run_test(
		[&](lt::session& ses)
		{
			settings_pack pack;
			pack.set_bool(settings_pack::ssrf_mitigation, enable_feature);
			ses.apply_settings(pack);
			ses.async_add_torrent(params);
		},
		[&](lt::session&, lt::alert const* alert) {
			if (lt::alert_cast<lt::torrent_finished_alert>(alert))
				seeding = true;
		},
		[&](sim::simulation& sim, lt::session&)
		{
			// http1 is the root web server that will just redirect requests to
			// other servers
			sim::asio::io_context web_server1(sim, make_address_v4("2.2.2.2"));
			sim::http_server http1(web_server1, 8080);
			// redirect file 1 and file 2 to the same servers
			if (redirect)
				http1.register_redirect("/1", redirect);

			// server for serving the content. This is on the local network
			sim::asio::io_context web_server2(sim, make_address_v4("192.168.1.13"));
			sim::http_server http2(web_server2, 8080);
			serve_content_for(http2, "/1", fs, file_index_t(0));
			serve_content_for(http2, "/1?query_string=1", fs, file_index_t(0));

			sim::asio::io_context web_server3(sim, make_address_v4("3.3.3.3"));
			sim::http_server http3(web_server3, 8080);
			serve_content_for(http3, "/1", fs, file_index_t(0));
			serve_content_for(http3, "/1?query_string=1", fs, file_index_t(0));

			// a local network server that redurects
			sim::asio::io_context web_server4(sim, make_address_v4("192.168.1.14"));
			sim::http_server http4(web_server4, 8080);
			if (redirect)
				http4.register_redirect("/1", redirect);

			sim.run();
		}
	);

	return seeding;
}

TORRENT_TEST(ssrf_mitigation)
{
	TEST_CHECK(test_ssrf("http://192.168.1.13:8080/1", nullptr, true));
	TEST_CHECK(test_ssrf("http://192.168.1.13:8080/1", nullptr, false));
	TEST_CHECK(test_ssrf("http://local-network.com:8080/1", nullptr, true));
	TEST_CHECK(test_ssrf("http://local-network.com:8080/1", nullptr, false));

	TEST_CHECK(!test_ssrf("http://192.168.1.13:8080/1?query_string=1", nullptr, true));
	TEST_CHECK(test_ssrf("http://192.168.1.13:8080/1?query_string=1", nullptr, false));
	TEST_CHECK(!test_ssrf("http://local-network.com:8080/1?query_string=1", nullptr, true));
	TEST_CHECK(test_ssrf("http://local-network.com:8080/1?query_string=1", nullptr, false));
}

TORRENT_TEST(ssrf_mitigation_redirect)
{
	// All Global-IP -> Local-IP redirects are prevented by SSRF mitigation
	TEST_CHECK(!test_ssrf("http://2.2.2.2:8080/1", "http://192.168.1.13:8080/1", true));
	TEST_CHECK(test_ssrf("http://2.2.2.2:8080/1", "http://192.168.1.13:8080/1", false));
	TEST_CHECK(!test_ssrf("http://2.2.2.2:8080/1", "http://local-network.com:8080/1", true));
	TEST_CHECK(test_ssrf("http://2.2.2.2:8080/1", "http://local-network.com:8080/1", false));
	TEST_CHECK(!test_ssrf("http://2.2.2.2:8080/1", "http://192.168.1.13:8080/1?query_string=1", true));
	TEST_CHECK(test_ssrf("http://2.2.2.2:8080/1", "http://192.168.1.13:8080/1?query_string=1", false));
	TEST_CHECK(!test_ssrf("http://2.2.2.2:8080/1", "http://local-network.com:8080/1?query_string=1", true));


	// Global-IP -> Global-IP is OK
	TEST_CHECK(test_ssrf("http://2.2.2.2:8080/1", "http://3.server.com:8080/1", true));
	TEST_CHECK(test_ssrf("http://2.2.2.2:8080/1", "http://3.server.com:8080/1", false));
	TEST_CHECK(test_ssrf("http://2.2.2.2:8080/1", "http://3.server.com:8080/1", true));
	TEST_CHECK(test_ssrf("http://2.2.2.2:8080/1", "http://3.server.com:8080/1", false));
	TEST_CHECK(test_ssrf("http://2.2.2.2:8080/1", "http://3.server.com:8080/1?query_string=1", true));
	TEST_CHECK(test_ssrf("http://2.2.2.2:8080/1", "http://3.server.com:8080/1?query_string=1", false));
	TEST_CHECK(test_ssrf("http://2.2.2.2:8080/1", "http://3.server.com:8080/1?query_string=1", true));
	TEST_CHECK(test_ssrf("http://2.2.2.2:8080/1", "http://3.server.com:8080/1?query_string=1", false));
	TEST_CHECK(test_ssrf("http://2.2.2.2:8080/1", "http://3.3.3.3:8080/1", true));
	TEST_CHECK(test_ssrf("http://2.2.2.2:8080/1", "http://3.3.3.3:8080/1", false));
	TEST_CHECK(test_ssrf("http://2.2.2.2:8080/1", "http://3.3.3.3:8080/1", true));
	TEST_CHECK(test_ssrf("http://2.2.2.2:8080/1", "http://3.3.3.3:8080/1", false));
	TEST_CHECK(test_ssrf("http://2.2.2.2:8080/1", "http://3.3.3.3:8080/1?query_string=1", true));
	TEST_CHECK(test_ssrf("http://2.2.2.2:8080/1", "http://3.3.3.3:8080/1?query_string=1", false));
	TEST_CHECK(test_ssrf("http://2.2.2.2:8080/1", "http://3.3.3.3:8080/1?query_string=1", true));
	TEST_CHECK(test_ssrf("http://2.2.2.2:8080/1", "http://3.3.3.3:8080/1?query_string=1", false));

	// Local-IP -> Local-IP are OK, with the normal query string restrictions
	TEST_CHECK(test_ssrf("http://192.168.1.14:8080/1", "http://192.168.1.13:8080/1", true));
	TEST_CHECK(test_ssrf("http://192.168.1.14:8080/1", "http://192.168.1.13:8080/1", false));
	TEST_CHECK(test_ssrf("http://192.168.1.14:8080/1", "http://local-network.com:8080/1", true));
	TEST_CHECK(test_ssrf("http://192.168.1.14:8080/1", "http://local-network.com:8080/1", false));
	TEST_CHECK(!test_ssrf("http://192.168.1.14:8080/1", "http://192.168.1.13:8080/1?query_string=1", true));
	TEST_CHECK(test_ssrf("http://192.168.1.14:8080/1", "http://192.168.1.13:8080/1?query_string=1", false));
	TEST_CHECK(!test_ssrf("http://192.168.1.14:8080/1", "http://local-network.com:8080/1?query_string=1", true));
}
