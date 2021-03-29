/*

Copyright (c) 2020, Alden Torres
Copyright (c) 2020, Arvid Norberg
Copyright (c) 2020, Paul-Louis Ageneau
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/config.hpp"
#include <libtorrent/aux_/torrent.hpp>
#include <libtorrent/magnet_uri.hpp>

#include "test.hpp"
#include "test_utils.hpp"
#include "setup_transfer.hpp"
#include "settings.hpp"
#include "session_mock.hpp"

#if TORRENT_USE_RTC

#include <boost/asio.hpp>

#include <iostream>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <random>

#include <cstdio>
#include <cstdarg>

using namespace std::placeholders;
using namespace std::chrono_literals;
using namespace lt;

using aux::rtc_signaling;
using aux::rtc_stream;
using aux::rtc_stream_init;
using aux::rtc_offer;
using aux::rtc_answer;

namespace {

void test_parse_endpoint() {
	error_code ec;
	rtc_stream::endpoint_type endpoint;

	endpoint = aux::rtc_parse_endpoint("10.9.8.7:65432", ec);
	TEST_CHECK(!ec);
	TEST_EQUAL(endpoint.address().to_string(), "10.9.8.7");
	TEST_EQUAL(endpoint.port(), 65432);
	ec.clear();

	endpoint = aux::rtc_parse_endpoint("2001:0db8:85a3::8a2e:370:7334:1234", ec);
	TEST_CHECK(!ec);
	TEST_EQUAL(endpoint.address().to_string(), "2001:db8:85a3::8a2e:370:7334");
	TEST_EQUAL(endpoint.port(), 1234);
	ec.clear();

	endpoint = aux::rtc_parse_endpoint("10.9.8.7", ec);
	TEST_EQUAL(ec, errors::parse_failed);
	ec.clear();

	endpoint = aux::rtc_parse_endpoint("invalid:6666", ec);
	TEST_CHECK(ec);
}

boost::asio::io_context io_context;
bool success = false;

void run_test() {
	success = false;

	std::chrono::seconds const duration = 30s;
	auto const begin_time = clock_type::now();
	auto const end_time = begin_time + duration;
	do {
		io_context.restart();
		io_context.run_one_until(end_time);
	}
	while (!success && clock_type::now() < end_time);

	if(!success)
		std::cout << "Test timed out after " << duration.count() << " seconds" << std::endl;

	TEST_CHECK(success == true);
}

void test_offers()
{
	time_point const start_time = clock_type::now();

	session_mock ses(io_context);
	aux::torrent tor(ses, false, parse_magnet_uri("magnet:?xt=urn:btih:cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"));

	std::shared_ptr<rtc_signaling> sig;

	int const requested_offers_count = 10;

	auto offers_handler = [&](error_code const& ec, std::vector<rtc_offer> offers) {
		TEST_CHECK(!ec);

		std::cout << "Generated " << int(offers.size()) << " offers" << std::endl;
		TEST_EQUAL(int(offers.size()), requested_offers_count);

		std::cout << "Test succeeded" << std::endl;
		success = true;
	};

	auto handler = [&](rtc_stream_init) {};

	sig = std::make_shared<rtc_signaling>(io_context, &tor, handler);

	std::cout << "Generating " << requested_offers_count << " offers" << std::endl;
	sig->generate_offers(requested_offers_count, offers_handler);

	run_test();

	ses.print_alerts(start_time);

	sig->close();
}

void test_connectivity()
{
	time_point const start_time = clock_type::now();

	session_mock ses1(io_context);
	aux::torrent tor1(ses1, false, parse_magnet_uri("magnet:?xt=urn:btih:cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"));

	session_mock ses2(io_context);
	aux::torrent tor2(ses2, false, parse_magnet_uri("magnet:?xt=urn:btih:cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"));

	std::shared_ptr<rtc_signaling> sig1, sig2;
	rtc_stream_init init1, init2;
	bool endpoint1_connected = false;
	bool endpoint2_connected = false;

	auto answer_callback = [&](peer_id const&, rtc_answer const& answer) {
		std::cout << "Signaling 2: Generated an answer" << std::endl;

		std::cout << "Signaling 1: Processing the answer" << std::endl;
		sig1->process_answer(answer);
	};

	auto offers_handler = [&](error_code const& ec, std::vector<rtc_offer> offers) {
		TEST_CHECK(!ec);

		std::cout << "Signaling 1: Generated " << int(offers.size()) << " offer(s)" << std::endl;
		TEST_EQUAL(int(offers.size()), 1);

		rtc_offer offer = offers[0];
		offer.answer_callback = answer_callback;

		std::cout << "Signaling 2: Processing the offer" << std::endl;
		sig2->process_offer(offer);
	};

	auto handler1 = [&](rtc_stream_init init) {
		TEST_CHECK(init.peer_connection);
		TEST_CHECK(init.data_channel);
		init1 = std::move(init);

		std::cout << "Signaling 1: Endpoint is connected" << std::endl;
		endpoint1_connected = true;

		if(endpoint2_connected)
		{
			std::cout << "Test succeeded" << std::endl;
			success = true;
		}
	};

	auto handler2 = [&](rtc_stream_init init) {
		TEST_CHECK(init.peer_connection);
		TEST_CHECK(init.data_channel);
		init2 = std::move(init);

		std::cout << "Signaling 2: Endpoint is connected" << std::endl;
		endpoint2_connected = true;

		if(endpoint1_connected)
		{
			std::cout << "Test succeeded" << std::endl;
			success = true;
		}
	};

	sig1 = std::make_shared<rtc_signaling>(io_context, &tor1, handler1);
	sig2 = std::make_shared<rtc_signaling>(io_context, &tor2, handler2);

	std::cout << "Signaling 1: Generating 1 offer" << std::endl;
	sig1->generate_offers(1, offers_handler);

	run_test();

	ses1.print_alerts(start_time);
	ses2.print_alerts(start_time);

	sig1->close();
	sig2->close();
}

void test_stream()
{
	time_point const start_time = clock_type::now();

	session_mock ses1(io_context);
	aux::torrent tor1(ses1, false, parse_magnet_uri("magnet:?xt=urn:btih:cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"));

	session_mock ses2(io_context);
	aux::torrent tor2(ses2, false, parse_magnet_uri("magnet:?xt=urn:btih:cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"));

	std::shared_ptr<rtc_signaling> sig1, sig2;
	std::shared_ptr<rtc_stream> stream1, stream2;

	std::vector<char> message(16*1024);
	std::iota(message.begin(), message.end(), char(0));
	std::random_device rd;
	std::shuffle(message.begin(), message.end(), std::mt19937(rd()));

	std::vector<char> message_buffer(message.size());
	boost::asio::mutable_buffer read_buffer(message_buffer.data(), message_buffer.size());

	bool written = false;
	bool received = false;

	auto answer_callback = [&](peer_id const&, rtc_answer const& answer) {
		std::cout << "Signaling 2: Generated an answer" << std::endl;

		std::cout << "Signaling 1: Processing the answer" << std::endl;
		sig1->process_answer(answer);
	};

	auto offers_handler = [&](error_code const& ec, std::vector<rtc_offer> offers) {
		TEST_CHECK(!ec);

		std::cout << "Signaling 1: Generated " << int(offers.size()) << " offer(s)" << std::endl;
		TEST_EQUAL(int(offers.size()), 1);

		rtc_offer offer = offers[0];
		offer.answer_callback = answer_callback;

		std::cout << "Signaling 2: Processing the offer" << std::endl;
		sig2->process_offer(offer);
	};

	auto read_handler = [&](error_code const& ec, std::size_t size) {
		if (success) return;
		TEST_CHECK(!ec);

		std::cout << "Stream 1: Received a message, size=" << message.size() << std::endl;
		TEST_EQUAL(size, message.size());
		TEST_EQUAL(read_buffer.size(), message.size());

		auto begin = static_cast<char const*>(read_buffer.data());
		auto end = begin + read_buffer.size();
		TEST_CHECK(std::equal(begin, end, message.begin(), message.end()));

		std::cout << "Stream 1: Received message checks out" << std::endl;
		received = true;
		if(written)
		{
			std::cout << "Test succeeded" << std::endl;
			success = true;
		}
	};

	auto write_handler = [&](error_code const& ec, std::size_t size) {
		TEST_CHECK(!ec);

		std::cout << "Stream 2: Message has been written, size=" << size << std::endl;
		TEST_EQUAL(size, message.size());

		written = true;
		if(received)
		{
			std::cout << "Test succeeded" << std::endl;
			success = true;
		}
	};

	auto handler1 = [&](rtc_stream_init init) {
		TEST_CHECK(init.peer_connection);
		TEST_CHECK(init.data_channel);

		std::cout << "Signaling 1: Endpoint is connected, creating stream 1" << std::endl;
		stream1 = std::make_shared<rtc_stream>(io_context, init);

		std::cout << "Stream 1: Reading a message" << std::endl;
		stream1->async_read_some(read_buffer, read_handler);
	};

	auto handler2 = [&](rtc_stream_init init) {
		TEST_CHECK(init.peer_connection);
		TEST_CHECK(init.data_channel);

		std::cout << "Signaling 2: Endpoint is connected, creating stream 2" << std::endl;
		stream2 = std::make_shared<rtc_stream>(io_context, init);

		std::cout << "Stream 2: Writing a message, size=" << message.size() << std::endl;
		stream2->async_write_some(boost::asio::const_buffer(message.data(), message.size()), write_handler);
	};

	sig1 = std::make_shared<rtc_signaling>(io_context, &tor1, handler1);
	sig2 = std::make_shared<rtc_signaling>(io_context, &tor2, handler2);

	std::cout << "Signaling 1: Generating 1 offer" << std::endl;
	sig1->generate_offers(1, offers_handler);

	run_test();

	TEST_CHECK(written == true);

	TEST_CHECK(stream1 != nullptr);
	TEST_CHECK(stream2 != nullptr);

	ses1.print_alerts(start_time);
	ses2.print_alerts(start_time);

	if (stream1)
		stream1->close();
	if (stream2)
		stream2->close();

	sig1->close();
	sig2->close();
}


} // namespace

TORRENT_TEST(parse_endpoint) { test_parse_endpoint(); }
TORRENT_TEST(signaling_offers) { test_offers(); }
TORRENT_TEST(signaling_connectivity) { test_connectivity(); }
TORRENT_TEST(signaling_stream) { test_stream(); }
#else
TORRENT_TEST(disabled) {}
#endif // TORRENT_USE_RTC
