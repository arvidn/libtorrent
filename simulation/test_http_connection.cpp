/*

Copyright (c) 2010, Arvid Norberg
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

#include "test.hpp"
#include "settings.hpp"
#include "setup_swarm.hpp"
#include "simulator/simulator.hpp"
#include "simulator/http_server.hpp"
#include "simulator/http_proxy.hpp"
#include "simulator/socks_server.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/aux_/proxy_settings.hpp"
#include "libtorrent/http_connection.hpp"
#include "libtorrent/resolver.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/random.hpp"

#include "make_proxy_settings.hpp"

#include <iostream>
#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/crc.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

using namespace lt;
using namespace sim;
namespace io = lt::detail;

using chrono::duration_cast;

struct sim_config : sim::default_config
{
	chrono::high_resolution_clock::duration hostname_lookup(
		asio::ip::address const& requestor
		, std::string hostname
		, std::vector<asio::ip::address>& result
		, boost::system::error_code& ec) override
	{
		if (hostname == "try-next.com")
		{
			result.push_back(address_v4::from_string("10.0.0.10"));
			result.push_back(address_v4::from_string("10.0.0.9"));
			result.push_back(address_v4::from_string("10.0.0.8"));
			result.push_back(address_v4::from_string("10.0.0.7"));
			result.push_back(address_v4::from_string("10.0.0.6"));
			result.push_back(address_v4::from_string("10.0.0.5"));
			result.push_back(address_v4::from_string("10.0.0.4"));
			result.push_back(address_v4::from_string("10.0.0.3"));

			// this is the IP that works, all other should fail
			result.push_back(address_v4::from_string("10.0.0.2"));
			return duration_cast<chrono::high_resolution_clock::duration>(chrono::milliseconds(100));
		}

		if (hostname == "test-hostname.com")
		{
			result.push_back(address_v4::from_string("10.0.0.2"));
			return duration_cast<chrono::high_resolution_clock::duration>(chrono::milliseconds(100));
		}

		if (hostname == "dual-stack.test-hostname.com")
		{
			result.push_back(address_v4::from_string("10.0.0.2"));
			result.push_back(address_v6::from_string("ff::dead:beef"));
			return duration_cast<chrono::high_resolution_clock::duration>(chrono::milliseconds(100));
		}

		return default_config::hostname_lookup(requestor, hostname, result, ec);
	}
};

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

std::shared_ptr<http_connection> test_request(io_service& ios
	, resolver& res
	, std::string const& url
	, char const* expected_data
	, int const expected_size
	, int const expected_status
	, error_condition expected_error
	, lt::aux::proxy_settings const& ps
	, int* connect_handler_called
	, int* handler_called
	, std::string const& auth = std::string())
{
	std::printf(" ===== TESTING: %s =====\n", url.c_str());

#ifdef TORRENT_USE_OPENSSL
	ssl::context ssl_ctx(ssl::context::sslv23_client);
	ssl_ctx.set_verify_mode(ssl::context::verify_none);
#endif

	auto h = std::make_shared<http_connection>(ios
		, res
		, [=](error_code const& ec, http_parser const& parser
			, span<char const> data, http_connection&)
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
		, true, 1024*1024
		, [=](http_connection& c)
		{
			++*connect_handler_called;
			TEST_CHECK(c.socket().is_open());
			std::printf("CONNECTED: %s\n", url.c_str());
		}
		, lt::http_filter_handler()
#ifdef TORRENT_USE_OPENSSL
		, &ssl_ctx
#endif
		);

	h->get(url, seconds(1), 0, &ps, 5, "test/user-agent", boost::none
		, resolver_flags{}, auth);
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
	if (ps.proxy_hostnames == false)
	{
		// this hostname will resolve to multiple IPs, all but one that we cannot
		// connect to and the second one where we'll get the test file response. Make
		// sure the http_connection correcly tries the second IP if the first one
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

	sim::asio::io_service web_server(sim, address_v4::from_string("10.0.0.2"));
	sim::asio::io_service ios(sim, address_v4::from_string("10.0.0.1"));
	sim::asio::io_service proxy_ios(sim, address_v4::from_string("50.50.50.50"));
	lt::resolver res(ios);

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

	error_code e;
	sim.run(e);

	if (e) std::cerr << " run failed: " << e.message() << std::endl;
	TEST_EQUAL(e, error_code());

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
// both accept the incoming connection but never send anything back. we test that
// both ip addresses get tried in turn and that the connection attempts time out as expected.
TORRENT_TEST(http_connection_timeout_server_stalls)
{
	sim_config network_cfg;
	sim::simulation sim{network_cfg};
	// server has two ip addresses (ipv4/ipv6)
	sim::asio::io_service server_ios(sim, address_v4::from_string("10.0.0.2"));
	sim::asio::io_service server_ios_ipv6(sim, address_v6::from_string("ff::dead:beef"));
	// same for client
	sim::asio::io_service client_ios(sim, {
		address_v4::from_string("10.0.0.1"),
		address_v6::from_string("ff::abad:cafe")
	});
	lt::resolver resolver(client_ios);

	const unsigned short http_port = 8080;
	sim::http_server http(server_ios, http_port);
	sim::http_server http_ipv6(server_ios_ipv6, http_port);

	http.register_stall_handler("/timeout");
	http_ipv6.register_stall_handler("/timeout");

	char data_buffer[4000];
	lt::aux::random_bytes(data_buffer);

	int connect_counter = 0;
	int handler_counter = 0;

	error_condition timed_out(boost::system::errc::timed_out, boost::system::generic_category());

	auto c = test_request(client_ios, resolver
		, "http://dual-stack.test-hostname.com:8080/timeout", data_buffer, -1, -1
		, timed_out, lt::aux::proxy_settings()
		, &connect_counter, &handler_counter);

	error_code e;
	sim.run(e);
	TEST_CHECK(!e);
	TEST_EQUAL(connect_counter, 2); // both endpoints are connected to
	TEST_EQUAL(handler_counter, 1); // the handler only gets called once with error_code == timed_out
}

// tests the error scenario of a http server listening on two sockets (ipv4/ipv6) neither of which
// accept incoming connections. we test that both ip addresses get tried in turn and that the
// connection attempts time out as expected.
TORRENT_TEST(http_connection_timeout_server_does_not_accept)
{
	sim_config network_cfg;
	sim::simulation sim{network_cfg};
	// server has two ip addresses (ipv4/ipv6)
	sim::asio::io_service server_ios(sim, {
		address_v4::from_string("10.0.0.2"),
		address_v6::from_string("ff::dead:beef")
	});
	// same for client
	sim::asio::io_service client_ios(sim, {
		address_v4::from_string("10.0.0.1"),
		address_v6::from_string("ff::abad:cafe")
	});
	lt::resolver resolver(client_ios);

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

	error_condition timed_out(boost::system::errc::timed_out, boost::system::generic_category());

	char data_buffer[4000];
	lt::aux::random_bytes(data_buffer);

	auto c = test_request(client_ios, resolver
		, "http://dual-stack.test-hostname.com:8080/timeout_server_does_not_accept", data_buffer, -1, -1
		, timed_out, lt::aux::proxy_settings()
		, &connect_counter, &handler_counter);

	error_code e;
	sim.run(e);
	TEST_CHECK(!e);
	TEST_EQUAL(connect_counter, 0); // no connection takes place
	TEST_EQUAL(handler_counter, 1); // the handler only gets called once with error_code == timed_out
}

void test_proxy_failure(lt::settings_pack::proxy_type_t proxy_type)
{
	using sim::asio::ip::address_v4;
	sim_config network_cfg;
	sim::simulation sim{network_cfg};

	sim::asio::io_service web_server(sim, address_v4::from_string("10.0.0.2"));
	sim::asio::io_service ios(sim, address_v4::from_string("10.0.0.1"));
	lt::resolver res(ios);

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

	error_code e;
	sim.run(e);

	if (e) std::cerr << " run failed: " << e.message() << std::endl;
	TEST_EQUAL(e, error_code());
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

// Requests a proxied SSL connection. This test just ensures that the correct CONNECT request
// is sent to the proxy server.
TORRENT_TEST(http_connection_ssl_proxy)
{
	using sim::asio::ip::address_v4;
	sim_config network_cfg;
	sim::simulation sim{network_cfg};

	sim::asio::io_service client_ios(sim, address_v4::from_string("10.0.0.1"));
	sim::asio::io_service proxy_ios(sim, address_v4::from_string("50.50.50.50"));
	lt::resolver res(client_ios);

	sim::http_server http_proxy(proxy_ios, 4445);

	lt::aux::proxy_settings ps = make_proxy_settings(settings_pack::http);

	int client_counter = 0;
	int proxy_counter = 0;

	http_proxy.register_handler("10.0.0.2:8080"
		, [&proxy_counter](std::string method, std::string req, std::map<std::string, std::string>&)
		{
			proxy_counter++;
			TEST_EQUAL(method, "CONNECT");
			return sim::send_response(403, "Not supported", 1337);
		});

#ifdef TORRENT_USE_OPENSSL
	lt::ssl::context ssl_ctx(ssl::context::sslv23_client);
	ssl_ctx.set_verify_mode(ssl::context::verify_none);
#endif

	auto h = std::make_shared<http_connection>(client_ios
		, res
		, [&client_counter](error_code const& ec, http_parser const&
			, span<char const>, http_connection&)
		{
			client_counter++;
			TEST_EQUAL(ec, boost::asio::error::operation_not_supported);
		}
		, true, 1024*1024, lt::http_connect_handler()
		, http_filter_handler()
#ifdef TORRENT_USE_OPENSSL
		, &ssl_ctx
#endif
		);

	h->start("10.0.0.2", 8080, seconds(1), 0, &ps, true /*ssl*/);

	error_code e;
	sim.run(e);

	TEST_EQUAL(client_counter, 1);
	TEST_EQUAL(proxy_counter, 1);
	if (e) std::cerr << " run failed: " << e.message() << std::endl;
	TEST_EQUAL(e, error_code());
}

// TODO: test http proxy with password
// TODO: test socks5 with password
// TODO: test SSL
// TODO: test keepalive

