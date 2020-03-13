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
#include "setup_transfer.hpp"
#include "test_utils.hpp"

#include "libtorrent/socket.hpp"
#include "libtorrent/socket_io.hpp" // print_endpoint
#include "libtorrent/http_connection.hpp"
#include "libtorrent/resolver.hpp"
#include "libtorrent/random.hpp"

#include <fstream>
#include <iostream>
#include <boost/optional.hpp>

using namespace lt;

namespace {

io_service ios;
resolver res(ios);

int connect_handler_called = 0;
int handler_called = 0;
int data_size = 0;
int http_status = 0;
error_code g_error_code;
char data_buffer[4000];

void print_http_header(http_parser const& p)
{
	std::cout << time_now_string() << " < " << p.status_code() << " " << p.message() << std::endl;

	for (auto const& i : p.headers())
	{
		std::cout << time_now_string() << " < " << i.first << ": " << i.second << std::endl;
	}
}

void http_connect_handler_test(http_connection& c)
{
	++connect_handler_called;
	TEST_CHECK(c.socket().is_open());
	error_code ec;
	std::cout << time_now_string() << " connected to: "
		<< print_endpoint(c.socket().remote_endpoint(ec)) << std::endl;
// this is not necessarily true when using a proxy and proxying hostnames
//	TEST_CHECK(c.socket().remote_endpoint(ec).address() == address::from_string("127.0.0.1", ec));
}

void http_handler_test(error_code const& ec, http_parser const& parser
	, span<char const> data, http_connection&)
{
	++handler_called;
	data_size = int(data.size());
	g_error_code = ec;
	TORRENT_ASSERT(data.empty() || parser.finished());

	if (parser.header_finished())
	{
		http_status = parser.status_code();
		if (http_status == 200)
		{
			TEST_CHECK(span<char>(data_buffer, data.size()) == data);
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
	, boost::optional<error_code> ec, aux::proxy_settings const& ps
	, std::string const& auth = std::string())
{
	reset_globals();

	std::cout << " ===== TESTING: " << url << " =====" << std::endl;

	std::cout << time_now_string()
		<< " expecting: size: " << size
		<< " status: " << status
		<< " connected: " << connected
		<< " error: " << (ec?ec->message():"no error") << std::endl;

#ifdef TORRENT_USE_OPENSSL
	ssl::context ssl_ctx(ssl::context::sslv23_client);
	ssl_ctx.set_verify_mode(ssl::context::verify_none);
#endif

	std::shared_ptr<http_connection> h = std::make_shared<http_connection>(ios
		, res, &::http_handler_test, true, 1024*1024, &::http_connect_handler_test
		, http_filter_handler()
#ifdef TORRENT_USE_OPENSSL
		, &ssl_ctx
#endif
		);
	h->get(url, seconds(5), 0, &ps, 5, "test/user-agent", boost::none, resolver_flags{}, auth);
	ios.reset();
	error_code e;
	ios.run(e);
	if (e) std::cout << time_now_string() << " run failed: " << e.message() << std::endl;

	std::string const n = time_now_string();
	std::cout << n << " connect_handler_called: " << connect_handler_called << std::endl;
	std::cout << n << " handler_called: " << handler_called << std::endl;
	std::cout << n << " status: " << http_status << std::endl;
	std::cout << n << " size: " << data_size << std::endl;
	std::cout << n << " expected-size: " << size << std::endl;
	std::cout << n << " error_code: " << g_error_code.message() << std::endl;
	TEST_CHECK(connect_handler_called == connected);
	TEST_CHECK(handler_called == 1);
	TEST_CHECK(data_size == size || size == -1);
	TEST_CHECK(!ec || g_error_code == *ec);
	TEST_CHECK(http_status == status || status == -1);
}

void write_test_file()
{
	aux::random_bytes(data_buffer);
	error_code ec;
	file test_file("test_file", open_mode::write_only, ec);
	TEST_CHECK(!ec);
	if (ec) std::printf("file error: %s\n", ec.message().c_str());
	iovec_t b = { data_buffer, 3216};
	test_file.writev(0, b, ec);
	TEST_CHECK(!ec);
	if (ec) std::printf("file error: %s\n", ec.message().c_str());
	test_file.close();
}

enum suite_flags_t
{
	flag_chunked_encoding = 1,
	flag_keepalive = 2
};

void run_suite(std::string const& protocol
	, settings_pack::proxy_type_t proxy_type
	, int flags = flag_keepalive)
{
	write_test_file();

	// starting the web server will also generate test_file.gz (from test_file)
	// so it has to happen after we write test_file
	int port = start_web_server(protocol == "https"
		, (flags & flag_chunked_encoding) != 0
		, (flags & flag_keepalive) != 0);

	aux::proxy_settings ps;
	ps.hostname = "127.0.0.1";
	ps.username = "testuser";
	ps.password = "testpass";
	ps.type = proxy_type;

	if (ps.type != settings_pack::none)
		ps.port = aux::numeric_cast<std::uint16_t>(start_proxy(ps.type));

	using err = boost::optional<error_code>;

	char url[256];
	std::snprintf(url, sizeof(url), "%s://127.0.0.1:%d/", protocol.c_str(), port);
	std::string url_base(url);

	run_test(url_base + "relative/redirect", 3216, 200, 2, error_code(), ps);
	run_test(url_base + "redirect", 3216, 200, 2, error_code(), ps);
	// the actual error code for an abort caused by too many
	// redirects is a bit unpredictable. under SSL for instance,
	// it will be an ungraceful shutdown
	run_test(url_base + "infinite_redirect", 0, 301, 6, err(), ps);
	run_test(url_base + "test_file", 3216, 200, 1, error_code(), ps);
	run_test(url_base + "test_file.gz", 3216, 200, 1, error_code(), ps);
	run_test(url_base + "non-existing-file", -1, 404, 1, err(), ps);
	run_test(url_base + "password_protected", 3216, 200, 1, error_code(), ps
		, "testuser:testpass");

	// try a very long path
	std::string path;
	for (int i = 0; i < 6000; ++i)
	{
		path += static_cast<char>(i % 26) + 'a';
	}
	run_test(url_base + path, 0, 404, 1, err(), ps);

	// only run the tests to handle NX_DOMAIN if we have a proper internet
	// connection that doesn't inject false DNS responses (like Comcast does)
	hostent* h = gethostbyname("non-existent-domain.se");
	std::printf("gethostbyname(\"non-existent-domain.se\") = %p. h_errno = %d\n"
		, static_cast<void*>(h), h_errno);
	if (h == nullptr && h_errno == HOST_NOT_FOUND)
	{
		// if we have a proxy, we'll be able to connect to it, we will just get an
		// error from the proxy saying it failed to connect to the final target
		if (protocol == "http" && (ps.type == settings_pack::http || ps.type == settings_pack::http_pw))
			run_test(protocol + "://non-existent-domain.se/non-existing-file", -1, -1, 1, err(), ps);
		else
			run_test(protocol + "://non-existent-domain.se/non-existing-file", -1, -1, 0, err(), ps);
	}
	if (ps.type != settings_pack::none)
		stop_proxy(ps.port);
	stop_web_server();
}

} // anonymous namespace

#ifdef TORRENT_USE_OPENSSL
TORRENT_TEST(no_proxy_ssl) { run_suite("https", settings_pack::none); }
TORRENT_TEST(http_ssl) { run_suite("https", settings_pack::http); }
TORRENT_TEST(http_pw_ssl) { run_suite("https", settings_pack::http_pw); }
TORRENT_TEST(socks5_proxy_ssl) { run_suite("https", settings_pack::socks5); }
TORRENT_TEST(socks5_pw_proxy_ssl) { run_suite("https", settings_pack::socks5_pw); }
#endif // USE_OPENSSL

TORRENT_TEST(http_proxy) { run_suite("http", settings_pack::http); }
TORRENT_TEST(http__pwproxy) { run_suite("http", settings_pack::http_pw); }
TORRENT_TEST(socks5_proxy) { run_suite("http", settings_pack::socks5); }
TORRENT_TEST(socks5_pw_proxy) { run_suite("http", settings_pack::socks5_pw); }

TORRENT_TEST(no_keepalive)
{
	run_suite("http", settings_pack::none, 0);
}
