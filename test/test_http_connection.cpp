/*

Copyright (c) 2008, Arvid Norberg
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
#include "libtorrent/socket.hpp"
#include "libtorrent/connection_queue.hpp"
#include "libtorrent/http_connection.hpp"
#include "setup_transfer.hpp"

#include <fstream>
#include <boost/optional.hpp>

using namespace libtorrent;

io_service ios;
connection_queue cq(ios);

int connect_handler_called = 0;
int handler_called = 0;
int data_size = 0;
int http_status = 0;
error_code g_error_code;
char data_buffer[4000];

void print_http_header(http_parser const& p)
{
	std::cerr << " < " << p.status_code() << " " << p.message() << std::endl;

	for (std::map<std::string, std::string>::const_iterator i
		= p.headers().begin(), end(p.headers().end()); i != end; ++i)
	{
		std::cerr << " < " << i->first << ": " << i->second << std::endl;
	}
}

void http_connect_handler(http_connection& c)
{
	++connect_handler_called;
	TEST_CHECK(c.socket().is_open());
	std::cerr << "connected to: " << c.socket().remote_endpoint() << std::endl;
	TEST_CHECK(c.socket().remote_endpoint().address() == address::from_string("127.0.0.1"));
}

void http_handler(error_code const& ec, http_parser const& parser
	, char const* data, int size, http_connection& c)
{
	++handler_called;
	data_size = size;
	g_error_code = ec;

	if (parser.header_finished())
	{
		http_status = parser.status_code();
		if (http_status == 200)
		{
			TEST_CHECK(memcmp(data, data_buffer, size) == 0);
		}
	}
	print_http_header(parser);
}

void reset_globals()
{
	connect_handler_called = 0;
	handler_called = 0;
	data_size = 0;
	http_status = 0;
	g_error_code = error_code();
}

void run_test(std::string const& url, int size, int status, int connected
	, boost::optional<error_code> ec, proxy_settings const& ps)
{
	reset_globals();

	std::cerr << " ===== TESTING: " << url << " =====" << std::endl;

	boost::shared_ptr<http_connection> h(new http_connection(ios, cq
		, &::http_handler, true, &::http_connect_handler));
	h->get(url, seconds(1), 0, &ps);
	ios.reset();
	ios.run();

	std::cerr << "connect_handler_called: " << connect_handler_called << std::endl;
	std::cerr << "handler_called: " << handler_called << std::endl;
	std::cerr << "status: " << http_status << std::endl;
	std::cerr << "size: " << data_size << std::endl;
	std::cerr << "error_code: " << g_error_code.message() << std::endl;
	TEST_CHECK(connect_handler_called == connected);
	TEST_CHECK(handler_called == 1);	
	TEST_CHECK(data_size == size || size == -1);
	TEST_CHECK(!ec || g_error_code == *ec);
	TEST_CHECK(http_status == status || status == -1);
}

void run_suite(std::string const& protocol, proxy_settings const& ps)
{
	if (ps.type != proxy_settings::none)
	{
		start_proxy(ps.port, ps.type);
	}
	char const* test_name[] = {"no", "SOCKS4", "SOCKS5"
		, "SOCKS5 password protected", "HTTP", "HTTP password protected"};
	std::cout << "\n\n********************** using " << test_name[ps.type]
		<< " proxy **********************\n" << std::endl;

	typedef boost::optional<error_code> err;
	// this requires the hosts file to be modified
//	run_test(protocol + "://test.dns.ts:8001/test_file", 3216, 200, 1, error_code(), ps);

	run_test(protocol + "://127.0.0.1:8001/relative/redirect", 3216, 200, 2, error_code(), ps);
	run_test(protocol + "://127.0.0.1:8001/redirect", 3216, 200, 2, error_code(), ps);
	run_test(protocol + "://127.0.0.1:8001/infinite_redirect", 0, 301, 6, error_code(), ps);
	run_test(protocol + "://127.0.0.1:8001/test_file", 3216, 200, 1, error_code(), ps);
	run_test(protocol + "://127.0.0.1:8001/test_file.gz", 3216, 200, 1, error_code(), ps);
	run_test(protocol + "://127.0.0.1:8001/non-existing-file", -1, 404, 1, err(), ps);
	// if we're going through an http proxy, we won't get the same error as if the hostname
	// resolution failed
	if ((ps.type == proxy_settings::http || ps.type == proxy_settings::http_pw) && protocol != "https")
		run_test(protocol + "://non-existent-domain.se/non-existing-file", -1, 502, 1, err(), ps);
	else
		run_test(protocol + "://non-existent-domain.se/non-existing-file", -1, -1, 0, err(), ps);

	if (ps.type != proxy_settings::none)
		stop_proxy(ps.port);
}

int test_main()
{
	std::srand(std::time(0));
	std::generate(data_buffer, data_buffer + sizeof(data_buffer), &std::rand);
	std::ofstream test_file("test_file", std::ios::trunc);
	test_file.write(data_buffer, 3216);
	TEST_CHECK(test_file.good());
	test_file.close();
	std::system("gzip -9 -c test_file > test_file.gz");
	
	proxy_settings ps;
	ps.hostname = "127.0.0.1";
	ps.port = 8034;
	ps.username = "testuser";
	ps.password = "testpass";
	
	start_web_server(8001);
	for (int i = 0; i < 5; ++i)
	{
		ps.type = (proxy_settings::proxy_type)i;
		run_suite("http", ps);
	}
	stop_web_server(8001);

#ifdef TORRENT_USE_OPENSSL
	start_web_server(8001, true);
	for (int i = 0; i < 5; ++i)
	{
		ps.type = (proxy_settings::proxy_type)i;
		run_suite("https", ps);
	}
	stop_web_server(8001);
#endif

	std::remove("test_file");
	return 0;
}

