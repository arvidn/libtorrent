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

		return default_config::hostname_lookup(requestor, hostname, result, ec);
	}
};

void on_alert_notify(lt::session* ses)
{
	std::vector<lt::alert*> alerts;
	ses->pop_alerts(&alerts);

	for (lt::alert* a : alerts)
	{
		lt::time_duration d = a->timestamp().time_since_epoch();
		boost::uint32_t millis = lt::duration_cast<lt::milliseconds>(d).count();
		printf("%4d.%03d: %s\n", millis / 1000, millis % 1000,
			a->message().c_str());
	}
}

boost::shared_ptr<http_connection> run_test(io_service& ios
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
			fprintf(stderr, "RESPONSE: %s\n", url.c_str());
			++*handler_called;
			const int http_status = parser.status_code();
			if (expected_size != -1)
			{
				TEST_EQUAL(size, expected_size);
			}
			TEST_EQUAL(ec, expected_ec);
			if (ec != expected_ec)
			{
				fprintf(stderr, "ERROR: %s (expected: %s)\n"
					, ec.message().c_str()
					, expected_ec.message().c_str());
			}
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
			fprintf(stderr, "CONNECTED: %s\n", url.c_str());
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
		std::cerr << i->first << ": " << i->second << std::endl;
	}
}
TORRENT_TEST(http_connection)
{
	using sim::asio::ip::address_v4;
	sim_config network_cfg;
	sim::simulation sim{network_cfg};

	sim::asio::io_service web_server(sim, address_v4::from_string("10.0.0.2"));
	sim::asio::io_service ios(sim, address_v4::from_string("10.0.0.1"));
	lt::resolver res(ios);

	sim::http_server http(web_server, 8080);

	char data_buffer[4000];
	std::generate(data_buffer, data_buffer + sizeof(data_buffer), &std::rand);

	http.register_handler("/test_file"
	, [data_buffer](std::string method, std::string req
		, std::map<std::string, std::string>& headers)
	{
		print_http_header(headers);
		TEST_EQUAL(headers["user-agent"], "test/user-agent");
		TEST_EQUAL(method, "GET");
		return sim::send_response(200, "OK", 1337).append(data_buffer, 1337);
	});

	int connect_handler_called = 0;
	int handler_called = 0;

	int expect_connect_handler_called = 0;
	int expect_handler_called = 0;

	lt::aux::proxy_settings ps;
	ps.hostname = "127.0.0.1";
	ps.username = "testuser";
	ps.password = "testpass";
	ps.type = settings_pack::none;

	auto c1 = run_test(ios, res, "http://10.0.0.2:8080/non-existent", NULL, 0, 404, error_code()
		, ps, &connect_handler_called, &handler_called);
	++expect_connect_handler_called;
	++expect_handler_called;

	auto c2 = run_test(ios, res, "http://10.0.0.2:8080/test_file", data_buffer, 1337, 200, error_code()
		, ps, &connect_handler_called, &handler_called);
	++expect_connect_handler_called;
	++expect_handler_called;

	error_code e;
	sim.run(e);

	if (e) std::cerr << " run failed: " << e.message() << std::endl;

	TEST_EQUAL(connect_handler_called, expect_connect_handler_called);
	TEST_EQUAL(handler_called, expect_handler_called);
}

