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
#include "libtorrent/alert_types.hpp"
#include "libtorrent/aux_/proxy_settings.hpp"
#include "libtorrent/http_connection.hpp"
#include "libtorrent/resolver.hpp"

using namespace libtorrent;
using namespace sim;
namespace lt = libtorrent;

using chrono::duration_cast;

struct sim_config : sim::default_config
{
	chrono::high_resolution_clock::duration hostname_lookup(
		asio::ip::address const& requestor
		, std::string hostname
		, std::vector<asio::ip::address>& result
		, boost::system::error_code& ec)
	{
		if (hostname == "tracker.com")
		{
			result.push_back(address_v4::from_string("10.0.0.2"));
			result.push_back(address_v6::from_string("ff::dead:beef"));
			return duration_cast<chrono::high_resolution_clock::duration>(chrono::milliseconds(100));
		}

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
			result.push_back(address_v4::from_string("10.0.0.2"));
			return duration_cast<chrono::high_resolution_clock::duration>(chrono::milliseconds(100));
		}

		return default_config::hostname_lookup(requestor, hostname, result, ec);
	}
};

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
	std::cerr << " ===== TESTING: " << url << " =====" << std::endl;

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

void run_test(std::string url, int expect_size, int expect_status
	, boost::system::error_code expect_error, std::vector<int> expect_counters);

enum expect_counters
{
	connect_handler = 0,
	handler = 1,
	test_file_req = 2,
	redirect_req = 3,
	rel_redirect_req = 4,
	inf_redirect_req = 5,

	num_counters
};

TORRENT_TEST(http_connection)
{
	std::string url_base = "http://10.0.0.2:8080";

	run_test(url_base + "/non-existent", 0, 404, error_code(), { 1, 1 });
	run_test(url_base + "/test_file", 1337, 200, error_code(), { 1, 1, 1});
	run_test(url_base + "/redirect", 1337, 200, error_code(), { 2, 1, 1, 1 });
	run_test(url_base + "/relative/redirect", 1337, 200, error_code(), {2, 1, 1, 0, 1});
	run_test(url_base + "/infinite/redirect", 0, 301, error_code(asio::error::eof), {6, 1, 0, 0, 0, 6});

	// we are on an IPv4 host, we can't connect to IPv6 addresses, make sure that
	// error is correctly propagated
	run_test("http://[ff::dead:beef]:8080/test_file", 0, -1, error_code(asio::error::address_family_not_supported)
		, {0,1});

	// this hostname will resolve to multiple IPs, all but one that we cannot
	// connect to and the second one where we'll get the test file response. Make
	// sure the http_connection correcly tries the second IP if the first one
	// fails.
	run_test("http://try-next.com:8080/test_file", 1337, 200, error_code(), { 1, 1, 1});

	// make sure hostname lookup failures are passed through correctly
	run_test("http://non-existent.com/test_file", 0, -1, asio::error::host_not_found, { 0, 1});

//	run_test(url_base + "/password_protected", 1337, 200, error_code(), { 1, 1, 1});
//	run_test(url_base + "/test_file.gz", 1337, 200, error_code(), { 1, 1, 1});

//#error test all proxies
//#error test https
//#error test chunked encoding

}

void run_test(std::string url, int expect_size, int expect_status
	, boost::system::error_code expect_error, std::vector<int> expect_counters)
{
	using sim::asio::ip::address_v4;
	sim_config network_cfg;
	sim::simulation sim{network_cfg};

	// allow sparse expected counters
	expect_counters.resize(num_counters, 0);

	sim::asio::io_service web_server(sim, address_v4::from_string("10.0.0.2"));
	sim::asio::io_service ios(sim, address_v4::from_string("10.0.0.1"));
	sim::asio::io_service ipv6_host(sim, address_v6::from_string("ff::dead:beef"));
	lt::resolver res(ios);

	sim::http_server http(web_server, 8080);
	sim::http_server http_v6(ipv6_host, 8080);

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

	lt::aux::proxy_settings ps;
	ps.hostname = "127.0.0.1";
	ps.username = "testuser";
	ps.password = "testpass";
	ps.type = settings_pack::none;

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

