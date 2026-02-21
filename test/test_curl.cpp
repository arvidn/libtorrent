/*

Copyright (c) 2026, Arvid Norberg
Copyright (c) 2026, The Baron Vladimir Harkonnen
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/config.hpp"
#include "test.hpp"

#if TORRENT_USE_CURL
#include "setup_transfer.hpp"
#include "test_utils.hpp"
#include "libtorrent/aux_/curl_pool.hpp"
#include "libtorrent/aux_/curl_request.hpp"
#include "libtorrent/aux_/curl_tracker_manager.hpp"

using namespace libtorrent;
using namespace libtorrent::aux;

namespace {
constexpr std::size_t megabyte_buffer = 1024*1024;

size_t ignore_data_cb(char* /*ptr*/,
		const size_t /*unused*/,
		const size_t nmemb,
		void* /*userdata*/)
{
	return nmemb;
}

std::unique_ptr<curl_request> create_request(const std::string& url)
{
	auto request = std::make_unique<curl_request>(megabyte_buffer);
	request->set_defaults();
	request->set_private_data(&request);
	request->set_timeout(seconds32(15));
	request->set_url(url);
	request->set_write_callback(ignore_data_cb);
	return request;
}

template<typename F>
void get_url(const std::string& url, F&& on_complete)
{
	curl_global_initializer raii;
	auto request = create_request(url);

	io_context ios;
	curl_pool pool(ios.get_executor());
	CURLcode resultcode = CURL_LAST;
	pool.set_completion_callback([&resultcode](CURL*, CURLcode code) {
		resultcode = code;
	});
	pool.add_request(request->handle());
	ios.run();

	TEST_NE(resultcode, CURL_LAST);
	on_complete(*request, resultcode);
}
} // anonymous namespace

TORRENT_TEST(curl_bad_urls)
{
	struct test_entry {
		std::string url;
		error_code result;
	};

	const std::vector<test_entry> test_errors = {
		{"maybe-a-hostname", boost::asio::error::host_not_found},
		{"", errors::url_parse_error},
		{"http://", errors::url_parse_error},
	};

	for (auto& entry : test_errors) {
		get_url(entry.url, [&entry](const curl_request&r, CURLcode code) {
			auto [ec, op, message] = r.get_error(code);
			TEST_EQUAL(entry.result, ec);
		});
	}
}

TORRENT_TEST(curl_connection_reuse)
{
	curl_global_initializer raii;

	int const http_port = start_web_server();
	const std::string url = std::string("http://127.0.0.1:") + std::to_string(http_port) + "/10MiB";

	std::array requests = {
		create_request(url),
		create_request(url)
	};

	io_context ios;
	curl_pool pool(ios.get_executor());

	for (auto& entry : requests)
	{
		pool.add_request(entry->handle());
	}

	ios.run();

	constexpr std::size_t cMiB10 = 1024 * 1024 * 10;
	long connection_count = 0;
	for (auto& entry : requests)
	{
		TEST_EQUAL(entry->get_compressed_body_size() , cMiB10);
		TEST_CHECK(entry->get_header_size() > 0);
		TEST_CHECK(entry->get_request_size() > 0);
		TEST_EQUAL(entry->http_status(), errors::http_errors::ok);
		connection_count += entry->get_num_connects();
	}
	TEST_EQUAL(connection_count, 1);

	stop_web_server();
}

TORRENT_TEST(curl_parallel)
{
	curl_global_initializer raii;

	int const http_port = start_web_server();
	const std::string url = std::string("http://127.0.0.1:") + std::to_string(http_port) + "/announce";

	io_context ios;
	curl_pool pool(ios.get_executor());
	pool.set_max_host_connections(0);

	std::vector<std::unique_ptr<curl_request>> requests = {};
	for (int i = 0; i < 30; ++i)
	{
		auto r = create_request(url);
		r->set_pipewait(false);
		pool.add_request(r->handle());
		requests.push_back(std::move(r));
	}

	ios.run();

	for (auto& entry : requests)
	{
		TEST_CHECK(entry->get_compressed_body_size() > 0);
		TEST_CHECK(entry->get_header_size() > 0);
		TEST_CHECK(entry->get_request_size() > 0);
		TEST_EQUAL(entry->http_status(), errors::http_errors::ok);
	}

	stop_web_server();
}

TORRENT_TEST(address_compatibility)
{
	// make sure scope_id is not printed in to_string()
	address addr = make_address_v6("::1");
	TEST_CHECK(addr.to_string().find('%') == std::string::npos);
	TEST_EQUAL(addr.to_string(), "::1");
}

#else

TORRENT_TEST(no_test)
{
}

#endif
