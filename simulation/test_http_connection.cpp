/*

Copyright (c) 2015-2022, Arvid Norberg
Copyright (c) 2017, Jan Berkel
Copyright (c) 2020-2021, Alden Torres
Copyright (c) 2020, Paul-Louis Ageneau
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "settings.hpp"
#include "setup_swarm.hpp"
#include "simulator/simulator.hpp"
#include "http_server.hpp"
#include "simulator/http_proxy.hpp"
#include "simulator/socks_server.hpp"
#include "simulator/utils.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/aux_/proxy_settings.hpp"
#include "libtorrent/aux_/http_connection.hpp"
#include "libtorrent/aux_/resolver.hpp"
#include "libtorrent/aux_/random.hpp"

#include "make_proxy_settings.hpp"

#include <iostream>
#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/crc.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

using namespace lt;
using namespace sim;

using chrono::duration_cast;

namespace {

	using conn_test_flags_t = flags::bitfield_flag<std::uint8_t, struct conn_test_flags_tag>;
	constexpr conn_test_flags_t keep_alive = 0_bit;
	constexpr conn_test_flags_t through_redirect = 1_bit;

	struct sim_config : sim::default_config
	{
		chrono::high_resolution_clock::duration hostname_lookup(asio::ip::address const& requestor,
			std::string hostname,
			std::vector<asio::ip::address>& result,
			boost::system::error_code& ec) override
		{
			if (hostname == "try-next.com")
			{
				result.push_back(make_address_v4("10.0.0.10"));
				result.push_back(make_address_v4("10.0.0.9"));
				result.push_back(make_address_v4("10.0.0.8"));
				result.push_back(make_address_v4("10.0.0.7"));
				result.push_back(make_address_v4("10.0.0.6"));
				result.push_back(make_address_v4("10.0.0.5"));
				result.push_back(make_address_v4("10.0.0.4"));
				result.push_back(make_address_v4("10.0.0.3"));

				// this is the IP that works, all other should fail
				result.push_back(make_address_v4("10.0.0.2"));
				return duration_cast<chrono::high_resolution_clock::duration>(
					chrono::milliseconds(100));
			}

			if (hostname == "test-hostname.com")
			{
				result.push_back(make_address_v4("10.0.0.2"));
				return duration_cast<chrono::high_resolution_clock::duration>(
					chrono::milliseconds(100));
			}

			if (hostname == "dual-stack.test-hostname.com")
			{
				result.push_back(make_address_v4("10.0.0.2"));
				result.push_back(make_address_v6("ff::dead:beef"));
				return duration_cast<chrono::high_resolution_clock::duration>(
					chrono::milliseconds(100));
			}

			if (hostname == "two-endpoints.com")
			{
				result.push_back(make_address_v4("10.0.0.2"));
				result.push_back(make_address_v4("10.0.0.3"));
				return duration_cast<chrono::high_resolution_clock::duration>(
					chrono::milliseconds(100));
			}

			return default_config::hostname_lookup(requestor, hostname, result, ec);
		}
};
} // anonymous namespace

// takes a string of data and chunks it up using HTTP chunked encoding
std::string chunk_string(std::string s)
{
	size_t i = 10;
	std::string ret;
	while (!s.empty())
	{
		i = std::min(i, s.size());
		char header[50];
		std::snprintf(header, sizeof(header), "%x\r\n", int(i));
		ret += header;
		ret += s.substr(0, i);
		s.erase(s.begin(), s.begin() + i);
		i *= 2;
	}
	ret += "0\r\n\r\n";
	return ret;
}

std::shared_ptr<lt::aux::http_connection> test_request(io_context& ios,
	lt::aux::resolver& res,
	std::string const& url,
	char const* expected_data,
	int const expected_size,
	int const expected_status,
	error_condition expected_error,
	lt::aux::proxy_settings const& ps,
	int* connect_handler_called,
	int* handler_called,
	std::string const& auth = std::string(),
	// fire-and-forget mode: write the request but never read a response.
	// Completion is signaled through the write handler instead of the
	// response handler above, so *handler_called is incremented there
	// instead (see set_write_handler() below).
	bool const write_only = false)
{
	std::printf(" ===== TESTING: %s =====\n", url.c_str());

#if TORRENT_USE_SSL
	aux::ssl::context ssl_ctx(aux::ssl::context::sslv23_client);
	ssl_ctx.set_verify_mode(aux::ssl::context::verify_none);
#endif

	auto h = std::make_shared<lt::aux::http_connection>(ios
		, res
		, [=](error_code const& ec, lt::aux::http_parser const& parser
			, span<char const> data, lt::aux::http_connection&)
		{
			std::printf("RESPONSE: %s\n", url.c_str());
			++*handler_called;

			// this is pretty gross. Since boost.asio is a header-only library, when this test is
			// build against shared libraries of libtorrent and simulator, there will be multiple
			// (distinct) error categories in boost.asio. The traditional comparison of error_code
			// and error_condition may hence fail.
			const bool error_ok = ec == expected_error
				|| (strcmp(ec.category().name(), expected_error.category().name()) == 0
				&& ec.value() == expected_error.value());

			if (!error_ok)
			{
				std::printf("ERROR: %s (expected: %s)\n"
					, ec.message().c_str()
					, expected_error.message().c_str());
			}

			const int http_status = parser.status_code();
			if (expected_size != -1)
			{
				TEST_EQUAL(int(data.size()), expected_size);
			}
			TEST_CHECK(error_ok);
			if (expected_status != -1)
			{
				TEST_EQUAL(http_status, expected_status);
			}
			if (http_status == 200)
			{
				TEST_CHECK(expected_data
					&& int(data.size()) == expected_size
					&& memcmp(expected_data, data.data(), data.size()) == 0);
			}
		}
		, 1024 * 1024
		, [=](lt::aux::http_connection& c)
		{
			++*connect_handler_called;
			TEST_CHECK(c.socket().is_open());
			std::printf("CONNECTED: %s\n", url.c_str());
		}
		, lt::aux::http_filter_handler()
		, lt::aux::hostname_filter_handler()
#if TORRENT_USE_SSL
		, &ssl_ctx
#endif
		);

	if (write_only)
	{
		// mirrors the two-phase pattern a real write_only caller
		// (http_tracker_connection::next_request()) uses: the handler fires
		// once right after the write completes (nothing to do yet -- don't
		// close, so the connection stays open like a real drain-and-wait),
		// then fires a second time when the deadline timer expires again,
		// at which point it closes. Closing on the very first call would
		// never give on_timeout() a chance to fire at all, so it couldn't
		// exercise the write_only-vs-retry-other-endpoint ordering this
		// test is meant to check.
		auto write_calls = std::make_shared<int>(0);
		h->set_write_handler([=](lt::aux::http_connection& c) {
			++*write_calls;
			std::printf("WRITE COMPLETE (%d): %s\n", *write_calls, url.c_str());
			if (*write_calls < 2) return;
			++*handler_called;
			c.close();
		});
	}

	h->get(url,
		seconds(1),
		&ps,
		5,
		"test/user-agent",
		std::nullopt,
		lt::aux::resolver_flags{},
		auth,
#if TORRENT_USE_I2P
		nullptr,
#endif
		false, // keep_alive
		write_only);
	return h;
}

void print_http_header(std::map<std::string, std::string> const& headers)
{
	for (std::map<std::string, std::string>::const_iterator i
		= headers.begin(), end(headers.end()); i != end; ++i)
	{
		std::printf("%s: %s\n", i->first.c_str(), i->second.c_str());
	}
}

void run_test(lt::aux::proxy_settings ps, std::string url, int expect_size, int expect_status
	, boost::system::error_condition expect_error, std::vector<int> expect_counters);

enum expect_counters
{
	connect_handler = 0,
	handler = 1,
	test_file_req = 2,
	redirect_req = 3,
	rel_redirect_req = 4,
	inf_redirect_req = 5,
	chunked_req = 6,
	test_file_gz_req = 7,

	num_counters
};

void run_suite(lt::aux::proxy_settings ps)
{
	std::string url_base = "http://10.0.0.2:8080";

	run_test(ps, url_base + "/test_file", 1337, 200, error_condition(), { 1, 1, 1});

	// positive test with a successful hostname
	run_test(ps, "http://test-hostname.com:8080/test_file", 1337, 200, error_condition(), { 1, 1, 1});

	run_test(ps, url_base + "/non-existent", 0, 404, error_condition(), { 1, 1 });
	run_test(ps, url_base + "/redirect", 1337, 200, error_condition(), { 2, 1, 1, 1 });
	run_test(ps, url_base + "/relative/redirect", 1337, 200, error_condition(), {2, 1, 1, 0, 1});

	run_test(ps, url_base + "/infinite/redirect", 0, 301
		, error_condition(asio::error::eof, asio::error::get_misc_category()), {6, 1, 0, 0, 0, 6});

	run_test(ps, url_base + "/chunked_encoding", 1337, 200, error_condition(), { 1, 1, 0, 0, 0, 0, 1});

	// we are on an IPv4 host, we can't connect to IPv6 addresses, make sure that
	// error is correctly propagated
	// with socks5 we would be able to do this, assuming the socks server
	// supported it, but the current socks implementation in libsimulator does
	// not support IPv6
	if (ps.type != settings_pack::socks5
		&& ps.type != settings_pack::http)
	{
		const auto expected_code = ps.type == settings_pack::socks4 ?
			boost::system::errc::address_family_not_supported :
			boost::system::errc::address_not_available;

		run_test(ps, "http://[ff::dead:beef]:8080/test_file", 0, -1
			, error_condition(expected_code, generic_category())
			, {0,1});
	}

	// there is no node at 10.0.0.10, this should fail with connection refused
	if (ps.type != settings_pack::http)
	{
		run_test(ps, "http://10.0.0.10:8080/test_file", 0, -1,
			error_condition(boost::system::errc::connection_refused, generic_category())
			, {0,1});
	}
	else
	{
		run_test(ps, "http://10.0.0.10:8080/test_file", 0, 503,
			error_condition(), {1,1});
	}

	// the try-next test in his case would test the socks proxy itself, whether
	// it has robust retry behavior (which the simple test proxy that comes with
	// libsimulator doesn't).
	if (ps.type != settings_pack::socks5 && ps.proxy_hostnames == false)
	{
		// this hostname will resolve to multiple IPs, all but one that we cannot
		// connect to and the second one where we'll get the test file response. Make
		// sure the http_connection correctly tries the second IP if the first one
		// fails.
		run_test(ps, "http://try-next.com:8080/test_file", 1337, 200
			, error_condition(), { 1, 1, 1});
	}

	// the http proxy does not support hostname lookups yet
	if (ps.type != settings_pack::http)
	{
		const error_condition expected_error = ps.proxy_hostnames
			? error_condition(boost::system::errc::host_unreachable, generic_category())
			: error_condition(asio::error::host_not_found, boost::asio::error::get_netdb_category());

		// make sure hostname lookup failures are passed through correctly
		run_test(ps, "http://non-existent.com/test_file", 0, -1
			, expected_error, { 0, 1 });
	}

	// make sure we handle gzipped content correctly
	run_test(ps, url_base + "/test_file.gz", 1337, 200, error_condition(), { 1, 1, 0, 0, 0, 0, 0, 1});

// TODO: 2 test basic-auth
// TODO: 2 test https
}

void run_test(lt::aux::proxy_settings ps, std::string url, int expect_size, int expect_status
	, boost::system::error_condition expect_error, std::vector<int> expect_counters)
{
	using sim::asio::ip::address_v4;
	sim_config network_cfg;
	sim::simulation sim{network_cfg};

	// allow sparse expected counters
	expect_counters.resize(num_counters, 0);

	sim::asio::io_context web_server(sim, make_address_v4("10.0.0.2"));
	sim::asio::io_context ios(sim, make_address_v4("10.0.0.1"));
	sim::asio::io_context proxy_ios(sim, make_address_v4("50.50.50.50"));
	lt::aux::resolver res(ios);

	sim::http_server http(web_server, 8080);
	sim::socks_server socks(proxy_ios, 4444, ps.type == settings_pack::socks4 ? 4 : 5);
	sim::http_proxy http_p(proxy_ios, 4445);

	char data_buffer[4000];
	lt::aux::random_bytes(data_buffer);

	std::vector<int> counters(num_counters, 0);

	http.register_handler("/test_file"
		, [&data_buffer,&counters](std::string method, std::string req
		, std::map<std::string, std::string>& headers)
	{
		++counters[test_file_req];
		print_http_header(headers);
		TEST_EQUAL(method, "GET");
		return sim::send_response(200, "OK", 1337).append(data_buffer, 1337);
	});

	http.register_handler("/chunked_encoding"
		, [&data_buffer,&counters](std::string method, std::string req
		, std::map<std::string, std::string>& headers)
	{
		++counters[chunked_req];
		print_http_header(headers);
		TEST_EQUAL(method, "GET");

		// there's no content length with chunked encoding
		return "HTTP/1.1 200 OK\r\nTransfer-encoding: Chunked\r\n\r\n"
			+ chunk_string(std::string(data_buffer, 1337));
	});

	http.register_handler("/test_file.gz"
	, [&data_buffer,&counters](std::string method, std::string req
		, std::map<std::string, std::string>& headers)
	{
		++counters[test_file_gz_req];
		print_http_header(headers);
		TEST_EQUAL(method, "GET");

		char const* extra_headers[4] = {"Content-Encoding: gzip\r\n", "", "", ""};
		unsigned char const gzheader[] = {
			0x1f , 0x8b , 0x08 , 0x00 // ID, compression=deflate, flags=0
			, 0x00 , 0x00 , 0x00 , 0x00 // mtime=0
			, 0x00, 0x01 // extra headers, OS
			, 0x01 // last block, uncompressed
			, 0x39 , 0x05, 0xc6 , 0xfa // length = 1337 (little endian 16 bit and inverted)
		};
		unsigned char trailer[8] = { 0, 0, 0, 0, 0x39, 0x05, 0x00, 0x00 };
		boost::crc_32_type crc;
		crc.process_bytes(data_buffer, 1337);
		std::uint32_t checksum = crc.checksum();
		trailer[0] = checksum >> 24;
		trailer[1] = (checksum >> 16) & 0xff;
		trailer[2] = (checksum >> 8) & 0xff;
		trailer[3] = (checksum) & 0xff;

		std::string ret = sim::send_response(200, "OK", 1337 + sizeof(gzheader)
			+ sizeof(trailer), extra_headers);
		ret.append(std::string((char const*)gzheader, sizeof(gzheader)));
		ret.append(data_buffer, 1337);
		ret.append(std::string((char const*)trailer, sizeof(trailer)));
		return ret;
	});

	http.register_handler("/redirect"
		, [&counters](std::string method, std::string req
		, std::map<std::string, std::string>&)
	{
		++counters[redirect_req];
		TEST_EQUAL(method, "GET");
		return "HTTP/1.1 301 Moved Temporarily\r\n"
			"Location: /test_file\r\n"
			"\r\n";
	});

	http.register_handler("/relative/redirect"
		, [&counters](std::string method, std::string req
		, std::map<std::string, std::string>&)
	{
		++counters[rel_redirect_req];
		TEST_EQUAL(method, "GET");
		return "HTTP/1.1 301 Moved Temporarily\r\n"
			"Location: ../test_file\r\n"
			"\r\n";
	});

	http.register_handler("/infinite/redirect"
		, [&counters](std::string method, std::string req
		, std::map<std::string, std::string>&)
	{
		++counters[inf_redirect_req];
		TEST_EQUAL(method, "GET");
		return "HTTP/1.1 301 Moved Temporarily\r\n"
			"Location: /infinite/redirect\r\n"
			"\r\n";
	});

	auto c = test_request(ios, res, url, data_buffer, expect_size
		, expect_status, expect_error, ps, &counters[connect_handler]
		, &counters[handler]);

	sim.run();

	TEST_EQUAL(counters.size(), expect_counters.size());
	for (int i = 0; i < int(counters.size()); ++i)
	{
		if (counters[i] != expect_counters[i]) std::printf("i=%d\n", i);
		TEST_EQUAL(counters[i], expect_counters[i]);
	}
}

TORRENT_TEST(http_connection)
{
	lt::aux::proxy_settings ps = make_proxy_settings(settings_pack::none);
	run_suite(ps);
}

TORRENT_TEST(http_connection_http)
{
	lt::aux::proxy_settings ps = make_proxy_settings(settings_pack::http);
	ps.proxy_hostnames = true;
	run_suite(ps);
}

TORRENT_TEST(http_connection_socks4)
{
	lt::aux::proxy_settings ps = make_proxy_settings(settings_pack::socks4);
	run_suite(ps);
}

TORRENT_TEST(http_connection_socks5)
{
	lt::aux::proxy_settings ps = make_proxy_settings(settings_pack::socks5);
	run_suite(ps);
}

TORRENT_TEST(http_connection_socks5_proxy_names)
{
	lt::aux::proxy_settings ps = make_proxy_settings(settings_pack::socks5);
	ps.proxy_hostnames = true;
	run_suite(ps);
}

// tests the error scenario of a http server listening on two sockets (ipv4/ipv6) which
// both accept the incoming connection but never send anything back. In the normal
// (non-write_only) case, both ip addresses get tried in turn before the connection
// attempt times out. In write_only mode, retrying a different endpoint is skipped in
// favor of promptly and gracefully finishing up via the write handler once the first
// endpoint accepts -- retrying elsewhere would only stall the shutdown that mode
// exists to keep prompt.
void test_timeout_server_stalls(bool const write_only)
{
	sim_config network_cfg;
	sim::simulation sim{network_cfg};
	// server has two ip addresses (ipv4/ipv6)
	sim::asio::io_context server_ios(sim, make_address_v4("10.0.0.2"));
	sim::asio::io_context server_ios_ipv6(sim, make_address_v6("ff::dead:beef"));
	// same for client
	sim::asio::io_context client_ios(sim, {
		make_address_v4("10.0.0.1"),
		make_address_v6("ff::abad:cafe")
	});
	lt::aux::resolver resolver(client_ios);

	const unsigned short http_port = 8080;
	sim::http_server http(server_ios, http_port);
	sim::http_server http_ipv6(server_ios_ipv6, http_port);

	http.register_stall_handler("/timeout");
	http_ipv6.register_stall_handler("/timeout");

	char data_buffer[4000];
	lt::aux::random_bytes(data_buffer);

	int connect_counter = 0;
	int handler_counter = 0;

	error_condition timed_out(lt::errors::timed_out, lt::libtorrent_category());

	auto c = test_request(client_ios,
		resolver,
		"http://dual-stack.test-hostname.com:8080/timeout",
		data_buffer,
		-1,
		-1,
		timed_out,
		lt::aux::proxy_settings(),
		&connect_counter,
		&handler_counter,
		std::string(),
		write_only);

	sim.run();
	// normal mode retries the second endpoint after the first stalls; write_only
	// mode gives up gracefully after the first rather than retrying elsewhere.
	TEST_EQUAL(connect_counter, write_only ? 1 : 2);
	// the handler (response handler normally, write handler in write_only mode)
	// only gets called once.
	TEST_EQUAL(handler_counter, 1);
}

TORRENT_TEST(http_connection_timeout_server_stalls) { test_timeout_server_stalls(false); }
TORRENT_TEST(http_connection_timeout_server_stalls_write_only) { test_timeout_server_stalls(true); }

// a write-only connection must reconnect, not reuse a socket the peer already
// closed while draining (on_drain()'s error path must undo on_write()'s
// optimistic m_reusable).
TORRENT_TEST(http_connection_write_only_drain_eof_forces_reconnect)
{
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	sim::asio::io_context server_ios(sim, make_address_v4("10.0.0.2"));
	sim::asio::io_context client_ios(sim, make_address_v4("10.0.0.1"));
	lt::aux::resolver resolver(client_ios);

	const unsigned short http_port = 8080;
	// no keep_alive flag: the server closes right after responding, so the
	// drain loop observes EOF.
	sim::http_server http(server_ios, http_port, 0);
	http.register_handler(
		"/announce", [](std::string, std::string, std::map<std::string, std::string>&) {
			std::string const body = "d8:intervali1800e5:peers0:e";
			return sim::send_response(200, "OK", int(body.size())) + body;
		});

	lt::aux::proxy_settings const ps;
	int connect_count = 0;
	int write_count = 0;

	auto h = std::make_shared<lt::aux::http_connection>(
		client_ios,
		resolver,
		[](error_code const&,
			lt::aux::http_parser const&,
			span<char const>,
			lt::aux::http_connection&) {},
		1024 * 1024,
		[&](lt::aux::http_connection&) { ++connect_count; },
		lt::aux::http_filter_handler(),
		lt::aux::hostname_filter_handler()
#if TORRENT_USE_SSL
			,
		nullptr
#endif
	);

	auto const dispatch = [&] {
		h->get("http://10.0.0.2:8080/announce",
			seconds(1),
			&ps,
			5,
			"test/user-agent",
			std::nullopt,
			lt::aux::resolver_flags{},
			std::string(),
#if TORRENT_USE_I2P
			nullptr,
#endif
			true, // keep_alive
			true // write_only
		);
	};

	// the write handler fires three times: once when the first write
	// completes (nothing to do yet), once when the drain loop notices the
	// server's subsequent close (EOF) -- the cue that this connection is
	// confirmed dead, so it's safe to check whether a second dispatch
	// reconnects -- and once when that second write completes.
	h->set_write_handler([&](lt::aux::http_connection& c) {
		++write_count;
		if (write_count == 1) return;
		if (write_count == 2)
		{
			dispatch();
			return;
		}
		c.close();
	});

	dispatch();

	sim.run();

	// must reconnect, not reuse the closed socket.
	TEST_EQUAL(connect_count, 2);
	TEST_EQUAL(write_count, 3);
}

// close() must release both m_handler and m_write_handler, so whatever a
// caller's response/write handler captured doesn't needlessly outlive the
// http_connection object once it's closed.
TORRENT_TEST(http_connection_close_releases_handlers)
{
	sim::default_config network_cfg;
	sim::simulation sim{network_cfg};
	sim::asio::io_context client_ios(sim, make_address_v4("10.0.0.1"));
	lt::aux::resolver resolver(client_ios);

	auto tracked_handler = std::make_shared<int>(0);
	std::weak_ptr<int> const weak_handler = tracked_handler;

	auto h = std::make_shared<lt::aux::http_connection>(
		client_ios,
		resolver,
		[tracked_handler](error_code const&,
			lt::aux::http_parser const&,
			span<char const>,
			lt::aux::http_connection&) {},
		1024 * 1024,
		[](lt::aux::http_connection&) {},
		lt::aux::http_filter_handler(),
		lt::aux::hostname_filter_handler()
#if TORRENT_USE_SSL
			,
		nullptr
#endif
	);
	tracked_handler.reset();

	auto tracked_write_handler = std::make_shared<int>(0);
	std::weak_ptr<int> const weak_write_handler = tracked_write_handler;
	h->set_write_handler([tracked_write_handler](lt::aux::http_connection&) {});
	tracked_write_handler.reset();

	TEST_CHECK(!weak_handler.expired());
	TEST_CHECK(!weak_write_handler.expired());
	h->close();
	TEST_CHECK(weak_handler.expired());
	TEST_CHECK(weak_write_handler.expired());
}

// tests the error scenario of a http server listening on two sockets (ipv4/ipv6) neither of which
// accept incoming connections. we test that both ip addresses get tried in turn and that the
// connection attempts time out as expected.
TORRENT_TEST(http_connection_timeout_server_does_not_accept)
{
	sim_config network_cfg;
	sim::simulation sim{network_cfg};
	// server has two ip addresses (ipv4/ipv6)
	sim::asio::io_context server_ios(sim, {
		make_address_v4("10.0.0.2"),
		make_address_v6("ff::dead:beef")
	});
	// same for client
	sim::asio::io_context client_ios(sim, {
		make_address_v4("10.0.0.1"),
		make_address_v6("ff::abad:cafe")
	});
	lt::aux::resolver resolver(client_ios);

	const unsigned short http_port = 8080;

	// listen on two sockets, but don't accept connections
	asio::ip::tcp::acceptor server_socket_ipv4(server_ios);
	server_socket_ipv4.open(tcp::v4());
	server_socket_ipv4.bind(tcp::endpoint(address_v4::any(), http_port));
	server_socket_ipv4.listen();

	asio::ip::tcp::acceptor server_socket_ipv6(server_ios);
	server_socket_ipv6.open(tcp::v6());
	server_socket_ipv6.bind(tcp::endpoint(address_v6::any(), http_port));
	server_socket_ipv6.listen();

	int connect_counter = 0;
	int handler_counter = 0;

	error_condition timed_out(lt::errors::timed_out, lt::libtorrent_category());

	char data_buffer[4000];
	lt::aux::random_bytes(data_buffer);

	auto c = test_request(client_ios, resolver
		, "http://dual-stack.test-hostname.com:8080/timeout_server_does_not_accept", data_buffer, -1, -1
		, timed_out, lt::aux::proxy_settings()
		, &connect_counter, &handler_counter);

	sim.run();
	TEST_EQUAL(connect_counter, 0); // no connection takes place
	TEST_EQUAL(handler_counter, 1); // the handler only gets called once with error_code == timed_out
}

void test_proxy_failure(lt::settings_pack::proxy_type_t proxy_type)
{
	using sim::asio::ip::address_v4;
	sim_config network_cfg;
	sim::simulation sim{network_cfg};

	sim::asio::io_context web_server(sim, make_address_v4("10.0.0.2"));
	sim::asio::io_context ios(sim, make_address_v4("10.0.0.1"));
	lt::aux::resolver res(ios);

	sim::http_server http(web_server, 8080);

	lt::aux::proxy_settings ps = make_proxy_settings(proxy_type);

	char data_buffer[4000];
	lt::aux::random_bytes(data_buffer);

	http.register_handler("/test_file"
		, [&data_buffer](std::string method, std::string req
		, std::map<std::string, std::string>& headers)
	{
		print_http_header(headers);
		// we're not supposed to get here
		TEST_CHECK(false);
		return sim::send_response(200, "OK", 1337).append(data_buffer, 1337);
	});

	int connect_counter = 0;
	int handler_counter = 0;
	auto c = test_request(ios, res, "http://10.0.0.2:8080/test_file"
		, data_buffer, -1, -1
		, error_condition(boost::system::errc::connection_refused, boost::system::generic_category())
		, ps, &connect_counter, &handler_counter);

	sim.run();
}

// if we set up to user a proxy that does not exist, expect failure!
// if this doesn't fail, the other tests are invalid because the proxy may not
// be exercised!
TORRENT_TEST(http_connection_socks_error)
{
	test_proxy_failure(settings_pack::socks5);
}

TORRENT_TEST(http_connection_http_error)
{
	test_proxy_failure(settings_pack::http);
}

void test_connection_ssl_proxy(bool const with_hostname)
{
	using sim::asio::ip::address_v4;
	sim_config network_cfg;
	sim::simulation sim{network_cfg};

	sim::asio::io_context client_ios(sim, make_address_v4("10.0.0.1"));
	sim::asio::io_context proxy_ios(sim, make_address_v4("50.50.50.50"));
	lt::aux::resolver res(client_ios);

	sim::http_server http_proxy(proxy_ios, 4445);

	lt::aux::proxy_settings ps = make_proxy_settings(settings_pack::http);
	ps.send_host_in_connect = with_hostname;

	int client_counter = 0;
	int proxy_counter = 0;

	std::string expected_target = with_hostname ? "test-hostname.com:8080" : "10.0.0.2:8080";

	http_proxy.register_handler(expected_target
		, [&proxy_counter, with_hostname, expected_target](std::string method, std::string req, std::map<std::string, std::string>& headers)
		{
			proxy_counter++;
			TEST_EQUAL(method, "CONNECT");

			// Host header is always sent to comply with RFC 9110 and RFC 9112 requirements.
			// The send_host_in_connect setting controls the format:
			// - true: Host header contains domain:port format
			// - false: Host header contains ip:port format
			if (with_hostname)
			{
				// When send_host_in_connect is true, Host header should contain domain:port
				TEST_EQUAL(headers["host"], "test-hostname.com:8080");
			}
			else
			{
				// When send_host_in_connect is false, Host header should contain ip:port
				TEST_EQUAL(headers["host"], "10.0.0.2:8080");
			}
			return sim::send_response(403, "Not supported", 1337);
		});

#if TORRENT_USE_SSL
	aux::ssl::context ssl_ctx(aux::ssl::context::sslv23_client);
	ssl_ctx.set_verify_mode(aux::ssl::context::verify_none);
#endif

	auto h = std::make_shared<lt::aux::http_connection>(client_ios
		, res
		, [&client_counter](error_code const& ec, lt::aux::http_parser const&
			, span<char const>, lt::aux::http_connection&)
		{
			client_counter++;
			TEST_EQUAL(ec, boost::asio::error::operation_not_supported);
		}
		, 1024 * 1024, lt::aux::http_connect_handler()
		, lt::aux::http_filter_handler()
		, lt::aux::hostname_filter_handler()
#if TORRENT_USE_SSL
		, &ssl_ctx
#endif
		);

	// Use hostname when testing with_hostname=true, IP when with_hostname=false
	std::string target_host = with_hostname ? "test-hostname.com" : "10.0.0.2";
	h->start(target_host, 8080, seconds(1), &ps, true /*ssl*/);

	sim.run();

	TEST_EQUAL(client_counter, 1);
	TEST_EQUAL(proxy_counter, 1);
}

// Tests SSL proxy connection with send_host_in_connect=false.
// Uses IP address for connection and Host header should contain ip:port format.
// This verifies that the Host header is always present and contains the target IP:port.
TORRENT_TEST(http_connection_ssl_proxy_no_hostname)
{
	test_connection_ssl_proxy(false);
}

// Tests SSL proxy connection with send_host_in_connect=true.
// Uses hostname for connection and Host header should contain domain:port format.
// This ensures proper hostname handling when send_host_in_connect is enabled.
TORRENT_TEST(http_connection_ssl_proxy_hostname)
{
	test_connection_ssl_proxy(true);
}


// verify that http_connection emits "Connection: close" by default and
// "Connection: keep-alive" when the keep_alive flag is passed to get(). When
// through_redirect is set, an intervening 301 is followed first; the header
// check is applied to the redirected request to verify keep_alive is preserved.
void test_connection_header(conn_test_flags_t const flags)
{
	bool const use_keep_alive = bool(flags & keep_alive);
	bool const use_redirect = bool(flags & through_redirect);

	using sim::asio::ip::address_v4;
	sim_config network_cfg;
	sim::simulation sim{network_cfg};

	sim::asio::io_context client_ios(sim, make_address_v4("10.0.0.1"));
	sim::asio::io_context server_ios(sim, make_address_v4("10.0.0.2"));
	lt::aux::resolver res(client_ios);

	sim::http_server http(server_ios, 8080);

	if (use_redirect)
	{
		http.register_handler(
			"/redirect", [](std::string, std::string, std::map<std::string, std::string>&) {
				return "HTTP/1.1 301 Moved Permanently\r\n"
					   "Location: /test\r\n"
					   "\r\n";
			});
	}

	int server_counter = 0;
	http.register_handler("/test",
		[&server_counter, use_keep_alive](std::string method,
			std::string /* req */
			,
			std::map<std::string, std::string>& headers) {
			++server_counter;
			TEST_EQUAL(method, "GET");
			TEST_EQUAL(headers["connection"], use_keep_alive ? "keep-alive" : "close");
			return sim::send_response(200, "OK", 0);
		});

	int client_counter = 0;
	auto h = std::make_shared<lt::aux::http_connection>(
		client_ios,
		res,
		[&client_counter](error_code const&,
			lt::aux::http_parser const&,
			span<char const>,
			lt::aux::http_connection& c) {
			++client_counter;
			c.close();
		},
		1024 * 1024,
		lt::aux::http_connect_handler(),
		lt::aux::http_filter_handler(),
		lt::aux::hostname_filter_handler()
#if TORRENT_USE_SSL
			,
		nullptr
#endif
	);

	std::string const start_url =
		use_redirect ? "http://10.0.0.2:8080/redirect" : "http://10.0.0.2:8080/test";
	h->get(start_url,
		seconds(5),
		nullptr,
		5,
		std::string(),
		std::nullopt,
		lt::aux::resolver_flags{},
		std::string(),
#if TORRENT_USE_I2P
		nullptr,
#endif
		use_keep_alive);

	sim.run();

	TEST_EQUAL(server_counter, 1);
	TEST_EQUAL(client_counter, 1);
}

TORRENT_TEST(http_connection_connection_close_header) { test_connection_header({}); }

TORRENT_TEST(http_connection_connection_keep_alive_header) { test_connection_header(keep_alive); }

// issue two sequential requests on the same http_connection. When the server
// keeps the connection alive, the second request must reuse the socket (a single
// accepted connection, the connect handler fires once). When the server closes
// the connection, the second request must transparently open a fresh socket.
void test_http_connection_reuse(int const server_flags, int const expected_connections)
{
	using sim::asio::ip::address_v4;
	sim_config network_cfg;
	sim::simulation sim{network_cfg};

	sim::asio::io_context client_ios(sim, make_address_v4("10.0.0.1"));
	sim::asio::io_context server_ios(sim, make_address_v4("10.0.0.2"));
	lt::aux::resolver res(client_ios);

	sim::http_server http(server_ios, 8080, server_flags);

	int a_count = 0;
	int b_count = 0;
	http.register_handler(
		"/a", [&a_count](std::string, std::string, std::map<std::string, std::string>&) {
			++a_count;
			return sim::send_response(200, "OK", 1) + "A";
		});
	http.register_handler(
		"/b", [&b_count](std::string, std::string, std::map<std::string, std::string>&) {
			++b_count;
			return sim::send_response(200, "OK", 1) + "B";
		});

	int connect_count = 0;
	int response_count = 0;
	std::shared_ptr<lt::aux::http_connection> h;

	auto const issue = [&](char const* path) {
		h->get(std::string("http://10.0.0.2:8080") + path,
			seconds(5),
			nullptr,
			5,
			std::string(),
			std::nullopt,
			lt::aux::resolver_flags{},
			std::string(),
#if TORRENT_USE_I2P
			nullptr,
#endif
			true // keep_alive
		);
	};

	h = std::make_shared<lt::aux::http_connection>(
		client_ios,
		res,
		[&](error_code const&,
			lt::aux::http_parser const&,
			span<char const>,
			lt::aux::http_connection& c) {
			++response_count;
			if (response_count == 1)
				issue("/b");
			else
				c.close();
		},
		1024 * 1024,
		[&connect_count](lt::aux::http_connection&) { ++connect_count; },
		lt::aux::http_filter_handler(),
		lt::aux::hostname_filter_handler()
#if TORRENT_USE_SSL
			,
		nullptr
#endif
	);

	issue("/a");

	sim.run();

	TEST_EQUAL(a_count, 1);
	TEST_EQUAL(b_count, 1);
	TEST_EQUAL(response_count, 2);
	TEST_EQUAL(http.accepted_connections(), expected_connections);
	TEST_EQUAL(connect_count, expected_connections);
}

TORRENT_TEST(http_connection_keep_alive_reuse)
{
	// with a keep-alive server, the second request reuses the first socket
	test_http_connection_reuse(sim::http_server::keep_alive, 1);
}

TORRENT_TEST(http_connection_keep_alive_server_close)
{
	// a server that does not keep connections alive responds with
	// "Connection: close"; the second request opens a new socket
	test_http_connection_reuse(0, 2);
}

TORRENT_TEST(http_connection_http_1_0_no_reuse)
{
	// an HTTP/1.0 server closes after each response (no Connection header); the
	// client must detect this from the protocol version and open a new socket
	test_http_connection_reuse(sim::http_server::http_1_0, 2);
}

// when http_connection reuses a keep-alive socket and the second request times
// out, on_timeout must not close the live socket and reconnect to the next
// endpoint (which would send an empty HTTP request). It must call
// callback(timed_out) cleanly.
TORRENT_TEST(http_connection_keepalive_reuse_timeout_does_not_reconnect)
{
	// two-endpoints.com resolves to 10.0.0.2 and 10.0.0.3 (see sim_config)
	sim_config network_cfg;
	sim::simulation sim{network_cfg};

	sim::asio::io_context client_ios(sim, make_address_v4("10.0.0.1"));
	sim::asio::io_context server_ios(sim, make_address_v4("10.0.0.2"));
	sim::asio::io_context server2_ios(sim, make_address_v4("10.0.0.3"));
	lt::aux::resolver res(client_ios);

	// both servers handle /first normally and stall /second; either may receive
	// the initial connection depending on random_shuffle in on_resolve
	sim::http_server http(server_ios, 8080);
	sim::http_server http2(server2_ios, 8080);

	for (sim::http_server* s : {&http, &http2})
	{
		s->register_handler(
			"/first", [](std::string, std::string, std::map<std::string, std::string>&) {
				return sim::send_response(200, "OK", 2) + "ok";
			});
		s->register_stall_handler("/second");
	}

	int connect_count = 0;
	int response_count = 0;
	std::shared_ptr<lt::aux::http_connection> h;

	auto const issue = [&](char const* path) {
		h->get(std::string("http://two-endpoints.com:8080") + path,
			seconds(1),
			nullptr,
			5,
			std::string(),
			std::nullopt,
			lt::aux::resolver_flags{},
			std::string(),
#if TORRENT_USE_I2P
			nullptr,
#endif
			true // keep_alive
		);
	};

	h = std::make_shared<lt::aux::http_connection>(
		client_ios,
		res,
		[&](error_code const& ec,
			lt::aux::http_parser const&,
			span<char const>,
			lt::aux::http_connection& c) {
			++response_count;
			if (!ec)
			{
				issue("/second");
			}
			else
			{
				TEST_EQUAL(ec, make_error_code(lt::errors::timed_out));
				c.close();
			}
		},
		1024 * 1024,
		[&connect_count](lt::aux::http_connection&) { ++connect_count; },
		lt::aux::http_filter_handler(),
		lt::aux::hostname_filter_handler()
#if TORRENT_USE_SSL
			,
		nullptr
#endif
	);

	issue("/first");

	sim.run();

	// two callbacks: success for /first, timed_out for /second
	TEST_EQUAL(response_count, 2);
	// one TCP connection: the keep-alive socket must not be replaced by a
	// reconnect to the second endpoint
	TEST_EQUAL(connect_count, 1);
	TEST_EQUAL(http.accepted_connections() + http2.accepted_connections(), 1);
}

// a get() call that follows a redirect must preserve the keep_alive flag from
// the original request, so the redirected request also sends keep-alive and
// the connection remains eligible for reuse.
TORRENT_TEST(http_connection_redirect_preserves_keep_alive)
{
	test_connection_header(keep_alive | through_redirect);
}

// TODO: test http proxy with password
// TODO: test socks5 with password
// TODO: test SSL
