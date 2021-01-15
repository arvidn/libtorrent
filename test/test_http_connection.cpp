/*

Copyright (c) 2016, Steven Siloti
Copyright (c) 2007-2010, 2013-2020, Arvid Norberg
Copyright (c) 2016, Andrei Kurushin
Copyright (c) 2017-2018, 2020, Alden Torres
Copyright (c) 2020, Paul-Louis Ageneau
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"
#include "setup_transfer.hpp"
#include "test_utils.hpp"

#include "libtorrent/socket.hpp"
#include "libtorrent/aux_/socket_io.hpp" // print_endpoint
#include "libtorrent/aux_/http_connection.hpp"
#include "libtorrent/aux_/resolver.hpp"
#include "libtorrent/aux_/storage_utils.hpp"
#include "libtorrent/aux_/random.hpp"

#include <iostream>
#include <optional>

using namespace lt;

namespace {

io_context ios;
aux::resolver res(ios);

int connect_handler_called = 0;
int handler_called = 0;
int data_size = 0;
int http_status = 0;
error_code g_error_code;
char data_buffer[4000];

void print_http_header(aux::http_parser const& p)
{
	std::cout << time_now_string() << " < " << p.status_code() << " " << p.message() << std::endl;

	for (auto const& i : p.headers())
	{
		std::cout << time_now_string() << " < " << i.first << ": " << i.second << std::endl;
	}
}

void http_connect_handler_test(aux::http_connection& c)
{
	++connect_handler_called;
	TEST_CHECK(c.socket().is_open());
	error_code ec;
	std::cout << time_now_string() << " connected to: "
		<< aux::print_endpoint(c.socket().remote_endpoint(ec)) << std::endl;
// this is not necessarily true when using a proxy and proxying hostnames
//	TEST_CHECK(c.socket().remote_endpoint(ec).address() == make_address("127.0.0.1", ec));
}

void http_handler_test(error_code const& ec, aux::http_parser const& parser
	, span<char const> data, aux::http_connection&)
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
	, std::optional<error_code> ec, aux::proxy_settings const& ps
	, std::string const& auth = std::string())
{
	reset_globals();

	std::cout << " ===== TESTING: " << url << " =====" << std::endl;

	std::cout << time_now_string()
		<< " expecting: size: " << size
		<< " status: " << status
		<< " connected: " << connected
		<< " error: " << (ec?ec->message():"no error") << std::endl;

#if TORRENT_USE_SSL
	aux::ssl::context ssl_ctx(aux::ssl::context::sslv23_client);
	ssl_ctx.set_verify_mode(aux::ssl::context::verify_none);
#endif

	std::shared_ptr<aux::http_connection> h = std::make_shared<aux::http_connection>(ios
		, res, &::http_handler_test, true, 1024*1024, &::http_connect_handler_test
		, aux::http_filter_handler()
		, aux::hostname_filter_handler()
#if TORRENT_USE_SSL
		, &ssl_ctx
#endif
		);
	h->get(url, seconds(5), 0, &ps, 5, "test/user-agent", std::nullopt, aux::resolver_flags{}, auth);
	ios.restart();
	ios.run();

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

enum suite_flags_t
{
	flag_chunked_encoding = 1,
	flag_keepalive = 2
};

void run_suite(std::string const& protocol
	, settings_pack::proxy_type_t proxy_type
	, int flags = flag_keepalive)
{
	aux::random_bytes(data_buffer);
	ofstream("test_file").write(data_buffer, 3216);

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

	using err = std::optional<error_code>;

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

#if TORRENT_USE_SSL
TORRENT_TEST(no_proxy_ssl) { run_suite("https", settings_pack::none); }
TORRENT_TEST(http_ssl) { run_suite("https", settings_pack::http); }
TORRENT_TEST(http_pw_ssl) { run_suite("https", settings_pack::http_pw); }
TORRENT_TEST(socks5_proxy_ssl) { run_suite("https", settings_pack::socks5); }
TORRENT_TEST(socks5_pw_proxy_ssl) { run_suite("https", settings_pack::socks5_pw); }
#endif // USE_SSL

TORRENT_TEST(http_proxy) { run_suite("http", settings_pack::http); }
TORRENT_TEST(http__pwproxy) { run_suite("http", settings_pack::http_pw); }
TORRENT_TEST(socks5_proxy) { run_suite("http", settings_pack::socks5); }
TORRENT_TEST(socks5_pw_proxy) { run_suite("http", settings_pack::socks5_pw); }

TORRENT_TEST(no_keepalive)
{
	run_suite("http", settings_pack::none, 0);
}
