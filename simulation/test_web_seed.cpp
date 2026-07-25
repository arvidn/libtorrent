/*

Copyright (c) 2016, 2021, Alden Torres
Copyright (c) 2016-2021, Arvid Norberg
Copyright (c) 2016, tnextday
Copyright (c) 2017, Andrei Kurushin
Copyright (c) 2017, 2019, Steven Siloti
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/session.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/aux_/deadline_timer.hpp"
#include "libtorrent/aux_/escape_string.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/aux_/path.hpp"
#include "http_server.hpp"
#include "simulator/http_proxy.hpp"
#include "settings.hpp"
#include "libtorrent/create_torrent.hpp"
#include "simulator/simulator.hpp"
#include "setup_swarm.hpp"
#include "utils.hpp"
#include "setup_transfer.hpp"
#include "simulator/utils.hpp"
#include <iostream>
#include <memory>
#include <numeric>

using namespace sim;
using namespace lt;


int const piece_size = 0x4000;

add_torrent_params create_torrent(
	std::vector<lt::create_file_entry> files, create_flags_t const version = create_flags_t{})
{
	add_torrent_params ret = ::make_torrent(std::move(files), 0, version);
	ret.flags &= ~lt::torrent_flags::auto_managed;
	ret.flags &= ~lt::torrent_flags::paused;
	ret.save_path = ".";
	return ret;
}

namespace {
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
} // anonymous namespace

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

	std::vector<lt::create_file_entry> files;
	files.emplace_back("abc'abc", 0x8000); // this filename will have to be escaped
	lt::add_torrent_params params = ::create_torrent(std::move(files));
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
	std::vector<lt::create_file_entry> files;
	files.emplace_back(combine_path("foo", "abc'abc"), 0x8000); // this filename will have to be escaped
	files.emplace_back(combine_path("foo", "bar"), 0x3000);
	lt::add_torrent_params params = ::create_torrent(std::move(files));
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

std::string generate_content(std::int64_t const file_offset, int const piece_size
	, std::int64_t const offset, std::int64_t len)
{
	std::string ret;
	ret.reserve(lt::aux::numeric_cast<std::size_t>(len));
	piece_index_t piece(int((file_offset + offset) / piece_size));
	int piece_offset = (file_offset + offset) % piece_size;

	while (len > 0)
	{
		auto piece_buf = generate_piece(piece, piece_size);
		int const step = int(std::min(len, std::int64_t(piece_size - piece_offset)));
		ret.append(piece_buf.data() + piece_offset, step);
		len -= step;
		piece_offset = 0;
		++piece;
	}
	return ret;
}

void serve_content_for(sim::http_server& http, std::string const& path
	, lt::file_storage const& fs, file_index_t const file)
{
	TORRENT_ASSERT(!fs.pad_file_at(file));
	std::int64_t const file_offset = fs.file_offset(file);
	int const piece_size = fs.piece_length();
	http.register_content(path, fs.file_size(file)
		, [file_offset, piece_size](std::int64_t const offset, std::int64_t const len)
		{ return generate_content(file_offset, piece_size, offset, len); });
}

// returns the file_index_t of every non-pad file, in order. Pad files shift
// indices around depending on alignment, so tests that need to address "the
// second real file" look it up here rather than hard-coding an index.
std::vector<file_index_t> non_pad_files(file_storage const& fs)
{
	std::vector<file_index_t> ret;
	for (file_index_t const i : fs.file_range())
		if (!fs.pad_file_at(i))
			ret.push_back(i);
	return ret;
}

using transfer_test_flags_t =
	lt::flags::bitfield_flag<std::uint8_t, struct transfer_test_flags_tag>;

// route the session through an HTTP proxy
//
// TODO: this only exercises a plain, unauthenticated HTTP forward proxy,
// sim::http_proxy, which has no auth support. We should also cover an
// authenticated HTTP proxy and SOCKS4/5, with and without auth.
// simulation/libsimulator's sim::socks_server already supports SOCKS4/5 and
// could extend this dimension; authenticated HTTP proxy coverage would
// first need auth support added to sim::http_proxy.
//
// TODO: settings_pack::proxy_peer_connections (web_seed::no_proxy_peers in
// test/test_web_seed_socks5_no_peers.cpp) isn't swept here, so nothing
// checks that peer connections can opt out of the proxy while the web seed
// connection still uses it.
constexpr transfer_test_flags_t use_proxy = 0_bit;

// serve everything over HTTPS (self-signed test cert). validate_https_trackers
// is disabled to accept it, since it governs the same ssl_ctx used for both
// HTTPS trackers and HTTPS web seeds, despite the name. Only meaningful when
// built with TORRENT_USE_SSL; the sweep never sets this bit otherwise.
constexpr transfer_test_flags_t use_https = 1_bit;

// TODO: HTTP/1.0-style (no keep-alive) web seed connections aren't swept.
// sim::http_server::http_1_0 already exists for this; test/test_web_seed_
// redirect.cpp and test_web_seed_socks5_no_peers.cpp both exercise
// web_seed::no_keepalive.

// how the client discovers the file content: whether it hits the content
// server directly, or is redirected there (from the same or a different
// server than the other file). "different server, no redirect" isn't a
// meaningful case: without a redirect there's a single url_seed, so both
// files necessarily come from the same origin.
enum class redirect_mode_t
{
	// the url_seed points directly at the content server; no HTTP redirect
	// is involved
	no_redirect,
	// the url_seed's root server redirects both files to the same
	// content server
	same_server,
	// the url_seed's root server redirects each file to a different
	// content server
	different_server,
};

// exercises downloading (optionally redirected) single- or multi-file web
// seed content, across various file layouts (aligned, unaligned, pad files)
// and redirect modes. Whether there's one file or two is inferred from
// `files` itself. Whether the download is expected to succeed is derived
// from the resulting file layout: with a single file there's no file
// boundary to straddle, so it always succeeds; with two files it can succeed
// if the second file starts piece-aligned (no piece straddles the file
// boundary) or if both files are served from the same server (a straddling
// piece can still be requested over a single connection).
void test_web_seed_transfer(std::vector<lt::create_file_entry> files,
	create_flags_t const version,
	transfer_test_flags_t const flags,
	redirect_mode_t const mode)
{
	using namespace lt;
	bool const https = bool(flags & use_https);
	std::string const scheme = https ? "https" : "http";

	// v1-only torrents carry no v2 metadata, so they're never padded. A
	// hybrid or v2-only torrent gets a pad file after each real file whose
	// size isn't itself a multiple of the piece length (each file starts on
	// a piece boundary, since any preceding file was already padded to one),
	// except a single-file hybrid torrent, which is never padded either
	// (padding it would make it impossible to also represent as a valid
	// single-file v1 torrent). A single-file v2-only torrent has no such
	// v1 constraint, so it's still padded like any other v2 file: the v2
	// "file tree" is parsed by unconditionally appending a piece-aligning
	// pad after every file, whereas a hybrid torrent's file list comes from
	// its v1 "files" entry, which was never padded to begin with. Capture
	// each file's own alignment here, before create_torrent() consumes
	// "files", to check the resulting layout against this rule below.
	bool const single = files.size() < 2;
	bool const file1_size_aligned = (files[0].size % piece_size) == 0;
	bool const file2_size_aligned = single || (files[1].size % piece_size) == 0;

	lt::add_torrent_params params = ::create_torrent(std::move(files), version);
	// clang-format off
	params.url_seeds.push_back(mode == redirect_mode_t::no_redirect
		? scheme + "://3.3.3.3:4444/"
		: scheme + "://2.2.2.2:8080/");
	// clang-format on

	// this function may be called several times from a single TORRENT_TEST
	// (to sweep flag combinations), all sharing one save-path directory, so
	// give each invocation its own subdirectory. Otherwise leftover files
	// from a previous combination (generated for a different file layout)
	// would be mistaken for this combination's data.
	static int instance = 0;
	params.save_path = save_path(0, instance++);

	// the final torrent may differ from what we built above (because of pad
	// files), so look up the real file indices from the resulting layout.
	file_storage const& fs = params.ti->layout();
	std::vector<file_index_t> const real_files = non_pad_files(fs);
	TORRENT_ASSERT(single == (real_files.size() == 1));

	// non_pad_files() reports the same real-file count whether padding is
	// absent because none is needed or missing because it's broken, so
	// verify the pad-file count directly against the alignment rule above.
	bool const v1_only = bool(version & create_torrent::v1_only);
	bool const v2_only = bool(version & create_torrent::v2_only);
	int const expected_pad_files = v1_only ? 0
		: (single && !v2_only)			   ? 0
										   : int(!file1_size_aligned) + int(!file2_size_aligned);
	TEST_EQUAL(fs.num_files() - int(real_files.size()), expected_pad_files);

	// the request paths the client will actually make, derived from the
	// torrent's own layout, so this works whether "files" described a single
	// top-level file or a "foo/"-wrapped pair. file_path() joins components
	// with the platform separator, but the client always requests posix-style
	// paths (see web_peer_connection::escape_file_path()), so convert here too.
	auto const request_path = [](file_storage const& f, file_index_t const idx) {
		std::string path = "/" + f.file_path(idx);
		convert_path_to_posix(path);
		return path;
	};
	std::string const path1 = request_path(fs, real_files[0]);
	std::string const path2 = single ? std::string() : request_path(fs, real_files[1]);

	bool const same_server = mode != redirect_mode_t::different_server;
	bool const file2_aligned = single || (fs.file_offset(real_files[1]) % fs.piece_length()) == 0;
	bool const expect_seeding = single || file2_aligned || same_server;

	// clang-format off
	char const* const mode_name
		= mode == redirect_mode_t::no_redirect ? "no_redirect"
		: mode == redirect_mode_t::same_server ? "with_redirect_same_server"
		: "different_server";
	char const* const version_name
		= v1_only ? "v1_only"
		: v2_only ? "v2_only"
		: "hybrid";
	// clang-format on

	std::printf("file1: %" PRId64 " bytes, single: %d, version: %s, mode: %s, use_proxy: %d, "
				"https: %d -> file2 offset: %s (%s), expect seeding: %d\n",
		fs.file_size(real_files[0]),
		single,
		version_name,
		mode_name,
		bool(flags & use_proxy),
		https,
		single ? "n/a" : std::to_string(fs.file_offset(real_files[1])).c_str(),
		file2_aligned ? "aligned" : "unaligned",
		expect_seeding);

	bool seeding = false;

	run_test(
		[&](lt::session& ses) {
			if (https)
			{
				settings_pack pack;
				// accept the self-signed test certificate. This governs the
				// same ssl_ctx used for both HTTPS trackers and HTTPS web
				// seeds, despite the setting's name.
				pack.set_bool(settings_pack::validate_https_trackers, false);
				ses.apply_settings(pack);
			}
			if (flags & use_proxy)
				set_proxy(ses, settings_pack::http);

			ses.async_add_torrent(params);
		},
		[&](lt::session&, lt::alert const* alert) {
			if (lt::alert_cast<lt::torrent_finished_alert>(alert))
				seeding = true;
		},
		[&](sim::simulation& sim, lt::session&) {
			std::unique_ptr<sim::asio::io_context> proxy_ios;
			std::unique_ptr<sim::http_proxy> http_p;
			if (flags & use_proxy)
			{
				proxy_ios =
					std::make_unique<sim::asio::io_context>(sim, make_address_v4("50.50.50.50"));
				http_p = std::make_unique<sim::http_proxy>(*proxy_ios, 4445);
			}

			sim::http_server_flags_t const server_flags = https
				? sim::http_server::https | sim::http_server::keep_alive
				: sim::http_server::keep_alive;

			// http1 is the root web server that will just redirect requests to
			// other servers, unless we're testing a direct (non-redirected)
			// transfer
			std::unique_ptr<sim::asio::io_context> web_server1;
			std::unique_ptr<sim::http_server> http1;
			if (mode != redirect_mode_t::no_redirect)
			{
				web_server1 =
					std::make_unique<sim::asio::io_context>(sim, make_address_v4("2.2.2.2"));
				http1 = std::make_unique<sim::http_server>(*web_server1, 8080, server_flags);
				http1->register_redirect(path1, scheme + "://3.3.3.3:4444/bla/file1");
				if (!single)
					http1->register_redirect(path2,
						same_server ? scheme + "://3.3.3.3:4444/bar/file2"
									: scheme + "://4.4.4.4:9999/bar/file2");
			}

			// server for file 1 (and file 2, if they share a server, or if
			// there's no redirect at all)
			sim::asio::io_context web_server2(sim, make_address_v4("3.3.3.3"));
			sim::http_server http2(web_server2, 4444, server_flags);
			if (mode == redirect_mode_t::no_redirect)
			{
				serve_content_for(http2, path1, fs, real_files[0]);
				if (!single)
					serve_content_for(http2, path2, fs, real_files[1]);
			}
			else
			{
				serve_content_for(http2, "/bla/file1", fs, real_files[0]);
				if (!single && same_server)
					serve_content_for(http2, "/bar/file2", fs, real_files[1]);
			}

			// server for file 2, if it's redirected to a separate server
			std::unique_ptr<sim::asio::io_context> web_server3;
			std::unique_ptr<sim::http_server> http3;
			if (!single && mode == redirect_mode_t::different_server)
			{
				web_server3 =
					std::make_unique<sim::asio::io_context>(sim, make_address_v4("4.4.4.4"));
				http3 = std::make_unique<sim::http_server>(*web_server3, 9999, server_flags);
				serve_content_for(*http3, "/bar/file2", fs, real_files[1]);
			}

			sim.run();
		});

	TEST_EQUAL(seeding, expect_seeding);
}

// TODO: web seed banning isn't covered anywhere in this file.
// test/test_web_seed_ban.cpp points a torrent at a web seed serving
// content that doesn't match the torrent's hashes and checks that
// libtorrent bans it. Reproducing this here just needs a
// serve_content_for() variant that deliberately returns the wrong bytes for
// one or more pieces, plus asserting on url_seeds() being emptied instead of
// TEST_EQUAL(seeding, ...).
//
// TODO: chunked transfer-encoding isn't covered anywhere in this file.
// test/test_web_seed_chunked.cpp (web_seed::chunked_encoding) downloads over
// a server that sends "Transfer-Encoding: chunked" responses. sim::http_server
// has no chunked-encoding support.
//
// TODO: the BEP3 "single file represented as a multi-file torrent" layout
// isn't covered. run_http_suite()'s test case 7 (web_seed_suite.cpp) builds a
// single-file torrent nested under the torrent's own name directory (rather
// than the name field being the filename itself) with a trailing-slash
// url_seed, so the client has to append the relative file path itself. Our
// `single_file` dimension only produces a plain single-file torrent, not
// this nested representation.

// exhaustively covers every combination of
// * a single- or multi-file torrent
// * file-1 alignment
// * the torrent version: v1-only, hybrid (create_flags_t{}), or v2-only
//   (i.e. whether, and via which metadata, pad files fill unaligned gaps)
// * direct, HTTP redirected to the same server, or redirected to separate servers
// * an HTTP proxy or not
// * it's plain HTTP or HTTPS.
TORRENT_TEST(test_web_seed_transfer)
{
#if TORRENT_USE_SSL
	auto const https_values = {false, true};
#else
	auto const https_values = {false};
#endif

	for (bool const single_file : {false, true})
		for (bool const file1_aligned : {false, true})
			for (create_flags_t const version :
				{create_torrent::v1_only, create_flags_t{}, create_torrent::v2_only})
				for (bool const via_proxy : {false, true})
					for (bool const via_https : https_values)
						for (redirect_mode_t const mode : {redirect_mode_t::no_redirect,
								 redirect_mode_t::same_server,
								 redirect_mode_t::different_server})
						{
							if (single_file && mode == redirect_mode_t::different_server)
								continue;

							// sim::http_proxy doesn't support CONNECT
							if (via_https && via_proxy)
								continue;

							std::vector<lt::create_file_entry> files;
							if (single_file)
							{
								files.emplace_back("1", file1_aligned ? 0xc000 : 0xc030);
							}
							else
							{
								files.emplace_back(
									combine_path("foo", "1"), file1_aligned ? 0xc000 : 0xc030);
								files.emplace_back(combine_path("foo", "2"), 0xc030);
							}

							transfer_test_flags_t flags{};
							if (via_proxy)
								flags |= use_proxy;
							if (via_https)
								flags |= use_https;

							test_web_seed_transfer(std::move(files), version, flags, mode);
						}
}

// A web seed's credentials must keep being forwarded when redirected to the
// *same* origin, but must NOT be forwarded when redirected to a *different*
// origin. This is checked for both ways of supplying credentials:
//  - embedded in the URL ("user:pass@host"), handled via m_basic_auth, and
//  - the explicit web_seed_entry::auth, handled via m_external_auth, set
//    through torrent_info::add_url_seed()'s ext_auth argument.
void test_redirect_auth(
	char const* redirect_target, bool const same_origin, bool const external_auth)
{
	using namespace lt;
	std::vector<lt::create_file_entry> files;
	files.emplace_back("file1", 0xc000);
	lt::add_torrent_params params = ::create_torrent(std::move(files));
	if (external_auth)
	{
#if TORRENT_ABI_VERSION < 4
		// the credentials are supplied out-of-band, the URL has none. This
		// exercises the m_external_auth path (web_seed_entry::auth). The ext_auth
		// feature (and add_url_seed()) only exists before ABI version 4.
		params.ti->add_url_seed("http://2.2.2.2:8080/", "Bearer secret-token");
#endif
	}
	else
	{
		// the credentials are embedded in the URL. This exercises the
		// m_basic_auth path.
		params.url_seeds.push_back("http://testuser:testpass@2.2.2.2:8080/");
	}

	// the credentials must be present on the initial request (sanity check
	// that the test actually sends any), and on the redirect target only when
	// it's the same origin.
	bool auth_at_origin = false;
	bool target_was_hit = false;
	bool auth_at_target = false;

	auto record_auth = [](std::map<std::string, std::string>& headers, bool& flag) {
		if (headers.find("authorization") != headers.end()) flag = true;
	};

	run_test([&params](lt::session& ses) { ses.async_add_torrent(params); },
		[](lt::session&, lt::alert const*) {},
		[&](sim::simulation& sim, lt::session&) {
			// the origin web server, which redirects the file request
			sim::asio::io_context web_server1(sim, make_address_v4("2.2.2.2"));
			sim::http_server http1(web_server1, 8080);
			http1.register_handler("/file1",
				[&](std::string, std::string, std::map<std::string, std::string>& headers) {
					record_auth(headers, auth_at_origin);
					std::string const header = "Location: " + std::string(redirect_target) + "\r\n";
					char const* extra_headers[4] = {header.c_str(), "", "", ""};
					return sim::send_response(301, "Moved Permanently", 0, extra_headers);
				});
			// same-origin redirect lands here
			http1.register_handler("/file1_redirected",
				[&](std::string, std::string, std::map<std::string, std::string>& headers) {
					target_was_hit = true;
					record_auth(headers, auth_at_target);
					return sim::send_response(404, "not found", 0);
				});

			// a different origin, the cross-origin redirect lands here
			sim::asio::io_context web_server2(sim, make_address_v4("3.3.3.3"));
			sim::http_server http2(web_server2, 4444);
			http2.register_handler("/file1_redirected",
				[&](std::string, std::string, std::map<std::string, std::string>& headers) {
					target_was_hit = true;
					record_auth(headers, auth_at_target);
					return sim::send_response(404, "not found", 0);
				});

			sim.run();
		});

	TEST_CHECK(auth_at_origin);
	TEST_CHECK(target_was_hit);
	TEST_EQUAL(auth_at_target, same_origin);
}

// a path-only redirect resolves to the same origin, credentials kept
TORRENT_TEST(web_seed_redirect_same_origin_url_auth)
{
	test_redirect_auth("/file1_redirected", true, false);
}

#if TORRENT_ABI_VERSION < 4
// the ext_auth (web_seed_entry::auth) feature is deprecated and removed at ABI
// version 4, so these cases only apply to earlier ABI versions
TORRENT_TEST(web_seed_redirect_same_origin_external_auth)
{
	test_redirect_auth("/file1_redirected", true, true);
}
#endif

// a redirect to a different host must not leak the credentials
TORRENT_TEST(web_seed_redirect_cross_origin_url_auth)
{
	test_redirect_auth("http://3.3.3.3:4444/file1_redirected", false, false);
}

#if TORRENT_ABI_VERSION < 4
TORRENT_TEST(web_seed_redirect_cross_origin_external_auth)
{
	test_redirect_auth("http://3.3.3.3:4444/file1_redirected", false, true);
}
#endif

TORRENT_TEST(urlseed_timeout)
{
	bool timeout = false;
	run_test(
		[](lt::session& ses)
		{
			std::vector<lt::create_file_entry> files;
			files.emplace_back("timeout_test", 0x8000);
			lt::add_torrent_params params = ::create_torrent(std::move(files));
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
TORRENT_TEST(no_close_redundant_webseed)
{
	using namespace lt;

	std::vector<lt::create_file_entry> files;
	files.emplace_back("file1", 1);
	lt::add_torrent_params params = ::create_torrent(std::move(files));
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

	std::vector<lt::create_file_entry> files;
	files.emplace_back("file1", 1);
	lt::add_torrent_params params = ::create_torrent(std::move(files));
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
	std::vector<lt::create_file_entry> files;
	files.emplace_back("1", 0xc030);
	lt::add_torrent_params params = ::create_torrent(std::move(files));
	params.url_seeds.emplace_back(url);

	file_storage const& fs = params.ti->layout();

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
	std::vector<lt::create_file_entry> files;
	files.emplace_back("1", 0xc030);
	lt::add_torrent_params params = ::create_torrent(std::move(files));
	params.url_seeds.emplace_back(url);

	file_storage const& fs = params.ti->layout();

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
