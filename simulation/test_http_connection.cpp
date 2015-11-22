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
#include "swarm_config.hpp"
#include "simulator/simulator.hpp"
#include "simulator/http_server.hpp"
#include "simulator/socks_server.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/aux_/proxy_settings.hpp"
#include "libtorrent/http_connection.hpp"
#include "libtorrent/resolver.hpp"
#include "libtorrent/io.hpp"

#include <boost/crc.hpp>

using namespace libtorrent;
using namespace sim;
namespace lt = libtorrent;
namespace io = lt::detail;

using chrono::duration_cast;

struct sim_config : sim::default_config
{
	chrono::high_resolution_clock::duration hostname_lookup(
		asio::ip::address const& requestor
		, std::string hostname
		, std::vector<asio::ip::address>& result
		, boost::system::error_code& ec)
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
		snprintf(header, sizeof(header), "%zx\r\n", i);
		ret += header;
		ret += s.substr(0, i);
		s.erase(s.begin(), s.begin() + i);
		i *= 2;
	}
	ret += "0\r\n\r\n";
	return ret;
}

boost::shared_ptr<http_connection> test_request(io_service& ios
	, resolver& res
	, std::string const& url
	, char const* expected_data
	, int expected_size
	, int expected_status
	, error_code expected_ec
	, lt::aux::proxy_settings const& ps
	, int* connect_handler_called
	, int* handler_called
	, std::string const& auth = std::string())
{
	fprintf(stderr, " ===== TESTING: %s =====\n", url.c_str());

	auto h = boost::make_shared<http_connection>(ios
		, res
		, [=](error_code const& ec, http_parser const& parser
			, char const* data, const int size, http_connection& c)
		{
			printf("RESPONSE: %s\n", url.c_str());
			++*handler_called;

			if (ec != expected_ec)
			{
				printf("ERROR: %s (expected: %s)\n"
					, ec.message().c_str()
					, expected_ec.message().c_str());
			}

			const int http_status = parser.status_code();
			if (expected_size != -1)
			{
				TEST_EQUAL(size, expected_size);
			}
			TEST_EQUAL(ec, expected_ec);
			if (expected_status != -1)
			{
				TEST_EQUAL(http_status, expected_status);
			}
			if (http_status == 200)
			{
				TEST_CHECK(expected_data
					&& size == expected_size
					&& memcmp(expected_data, data, size) == 0);
			}
		}
		, true, 1024*1024
		, [=](http_connection& c)
		{
			++*connect_handler_called;
			TEST_CHECK(c.socket().is_open());
			printf("CONNECTED: %s\n", url.c_str());
		});

	h->get(url, seconds(1), 0, &ps, 5, "test/user-agent", address_v4::any()
		, 0, auth);
	return h;
}

void print_http_header(std::map<std::string, std::string> const& headers)
{
	for (std::map<std::string, std::string>::const_iterator i
		= headers.begin(), end(headers.end()); i != end; ++i)
	{
		printf("%s: %s\n", i->first.c_str(), i->second.c_str());
	}
}

void run_test(settings_pack::proxy_type_t proxy_type, std::string url, int expect_size, int expect_status
	, boost::system::error_code expect_error, std::vector<int> expect_counters);

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

void run_suite(settings_pack::proxy_type_t proxy_type)
{
	std::string url_base = "http://10.0.0.2:8080";

	run_test(proxy_type, url_base + "/test_file", 1337, 200, error_code(), { 1, 1, 1});

	run_test(proxy_type, url_base + "/non-existent", 0, 404, error_code(), { 1, 1 });
	run_test(proxy_type, url_base + "/redirect", 1337, 200, error_code(), { 2, 1, 1, 1 });
	run_test(proxy_type, url_base + "/relative/redirect", 1337, 200, error_code(), {2, 1, 1, 0, 1});
	run_test(proxy_type, url_base + "/infinite/redirect", 0, 301, error_code(asio::error::eof), {6, 1, 0, 0, 0, 6});
	run_test(proxy_type, url_base + "/chunked_encoding", 1337, 200, error_code(), { 1, 1, 0, 0, 0, 0, 1});

	// we are on an IPv4 host, we can't connect to IPv6 addresses, make sure that
	// error is correctly propagated
	// with socks5 we would be able to do this, assuming the socks server
	// supported it, but the current socks implementation in libsimulator does
	// not support IPv6
	if (proxy_type != settings_pack::socks5)
	{
		run_test(proxy_type, "http://[ff::dead:beef]:8080/test_file", 0, -1, error_code(asio::error::address_family_not_supported)
			, {0,1});
	}

	// there is no node at 10.0.0.10, this should fail with connection refused
	run_test(proxy_type, "http://10.0.0.10:8080/test_file", 0, -1,
		error_code(boost::system::errc::connection_refused, boost::system::system_category())
		, {0,1});

	// this hostname will resolve to multiple IPs, all but one that we cannot
	// connect to and the second one where we'll get the test file response. Make
	// sure the http_connection correcly tries the second IP if the first one
	// fails.
	run_test(proxy_type, "http://try-next.com:8080/test_file", 1337, 200, error_code(), { 1, 1, 1});

	// make sure hostname lookup failures are passed through correctly
	run_test(proxy_type, "http://non-existent.com/test_file", 0, -1, asio::error::host_not_found, { 0, 1});

	// make sure we handle gzipped content correctly
	run_test(proxy_type, url_base + "/test_file.gz", 1337, 200, error_code(), { 1, 1, 0, 0, 0, 0, 0, 1});

// TODO: 2 test basic-auth
// TODO: 2 test https

}

void run_test(settings_pack::proxy_type_t proxy_type, std::string url, int expect_size, int expect_status
	, boost::system::error_code expect_error, std::vector<int> expect_counters)
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
	sim::socks_server socks(proxy_ios, 4444, proxy_type == settings_pack::socks4 ? 4 : 5);

	lt::aux::proxy_settings ps;
	if (proxy_type != settings_pack::none)
	{
		ps.hostname = "50.50.50.50";
		ps.port = 4444;
		ps.username = "testuser";
		ps.password = "testpass";
		ps.type = proxy_type;
		ps.proxy_hostnames = false;
		// TODO: 2 also test proxying host names, and verify they are in fact proxied
	}

	char data_buffer[4000];
	std::generate(data_buffer, data_buffer + sizeof(data_buffer), &std::rand);

	std::vector<int> counters(num_counters, 0);

	http.register_handler("/test_file"
		, [data_buffer,&counters](std::string method, std::string req
		, std::map<std::string, std::string>& headers)
	{
		++counters[test_file_req];
		print_http_header(headers);
		TEST_EQUAL(method, "GET");
		return sim::send_response(200, "OK", 1337).append(data_buffer, 1337);
	});

	http.register_handler("/chunked_encoding"
		, [data_buffer,&counters](std::string method, std::string req
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
	, [data_buffer,&counters](std::string method, std::string req
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
		boost::uint32_t checksum = crc.checksum();
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
		, [data_buffer,&counters](std::string method, std::string req
		, std::map<std::string, std::string>& headers)
	{
		++counters[redirect_req];
		TEST_EQUAL(method, "GET");
		return "HTTP/1.1 301 Moved Temporarily\r\n"
			"Location: /test_file\r\n"
			"\r\n";
	});

	http.register_handler("/relative/redirect"
		, [data_buffer,&counters](std::string method, std::string req
		, std::map<std::string, std::string>& headers)
	{
		++counters[rel_redirect_req];
		TEST_EQUAL(method, "GET");
		return "HTTP/1.1 301 Moved Temporarily\r\n"
			"Location: ../test_file\r\n"
			"\r\n";
	});

	http.register_handler("/infinite/redirect"
		, [data_buffer,&counters](std::string method, std::string req
		, std::map<std::string, std::string>& headers)
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
	for (int i = 0; i < counters.size(); ++i)
	{
		if (counters[i] != expect_counters[i]) fprintf(stderr, "i=%d\n", i);
		TEST_EQUAL(counters[i], expect_counters[i]);
	}
}

TORRENT_TEST(http_connection)
{
	run_suite(settings_pack::none);
}

TORRENT_TEST(http_connection_socks4)
{
	run_suite(settings_pack::socks4);
}

TORRENT_TEST(http_connection_socks5)
{
	run_suite(settings_pack::socks5);
}

TORRENT_TEST(http_connection_socks_error)
{
	// if we set up to user a proxy that does not exist, expect failure!
	using sim::asio::ip::address_v4;
	sim_config network_cfg;
	sim::simulation sim{network_cfg};

	sim::asio::io_service web_server(sim, address_v4::from_string("10.0.0.2"));
	sim::asio::io_service ios(sim, address_v4::from_string("10.0.0.1"));
	lt::resolver res(ios);

	sim::http_server http(web_server, 8080);

	lt::aux::proxy_settings ps;
	ps.hostname = "50.50.50.50";
	ps.port = 4444;
	ps.username = "testuser";
	ps.password = "testpass";
	ps.type = settings_pack::socks5;

	char data_buffer[4000];
	std::generate(data_buffer, data_buffer + sizeof(data_buffer), &std::rand);

	http.register_handler("/test_file"
		, [data_buffer](std::string method, std::string req
		, std::map<std::string, std::string>& headers)
	{
		print_http_header(headers);
		TEST_CHECK(false && "we're not supposed to get here!");
		return sim::send_response(200, "OK", 1337).append(data_buffer, 1337);
	});

	int connect_counter = 0;
	int handler_counter = 0;
	auto c = test_request(ios, res, "http://10.0.0.2:8080/test_file"
		, data_buffer, -1, -1
		, error_code(boost::system::errc::connection_refused, boost::system::system_category())
		, ps, &connect_counter, &handler_counter);

	error_code e;
	sim.run(e);

	if (e) std::cerr << " run failed: " << e.message() << std::endl;
	TEST_EQUAL(e, error_code());
}

// TODO: test http proxy
// TODO: test socks5 with password

