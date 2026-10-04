/*

Copyright (c) 2020, Alden Torres
Copyright (c) 2020-2021, Arvid Norberg
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

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <rtc/rtc.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include <boost/asio.hpp>

#include <iostream>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <random>

#include <cstdio>
#include <cstdarg>
#include <string>

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

// shared across all tests in this file (rather than one per test) because
// libdatachannel's background thread pool is process-global and doesn't
// synchronously stop when a test's connections are closed; a late completion
// posted from those threads to a per-test io_context that has since been
// destroyed would be a dangling reference. With a process-lifetime
// io_context that's always safe, at the cost of stale completions from one
// test occasionally being serviced while a later test is running (made
// harmless by the weak_ptr checks in rtc_signaling's callbacks).
boost::asio::io_context io_context;
bool success = false;

void run_test() {
	success = false;

	std::chrono::seconds const duration = 30s;
	auto const begin_time = clock_type::now();
	auto const end_time = begin_time + duration;
	do {
		// restart() requires stopped() == true; outstanding per-connection
		// timers mean that isn't always the case, so check rather than
		// calling it unconditionally
		if (io_context.stopped()) io_context.restart();
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
		if (offers.empty()) return;

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
		if (offers.empty()) return;

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

// regression test for a bug in rtc_stream_impl::write_data(): when a write's
// total size lands exactly on a max-message-size boundary (i.e. it's an
// exact multiple of the negotiated max message size), the chunk-splitting
// loop left the final chunk's buffer entry in m_write_buffer without
// shrinking or erasing it. Since issue_write()'s driving loop only stops
// once m_write_buffer is empty, this caused an infinite loop: each iteration
// re-sent a spurious 0-byte message and never made progress, spinning the
// network thread and leaking a small allocation every iteration (observed
// as an unbounded, fast climb in RSS -- tens of GB within a minute -- before
// being killed). A remote peer controls the negotiated max message size via
// SDP, so this was remotely triggerable by aligning a write size to it.
void test_write_exact_chunk_boundary()
{
	time_point const start_time = clock_type::now();

	session_mock ses1(io_context);
	aux::torrent tor1(ses1,
		false,
		parse_magnet_uri("magnet:?xt=urn:btih:cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"));

	session_mock ses2(io_context);
	aux::torrent tor2(ses2,
		false,
		parse_magnet_uri("magnet:?xt=urn:btih:cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"));

	std::shared_ptr<rtc_signaling> sig1, sig2;
	std::shared_ptr<rtc_stream> stream1, stream2;
	std::vector<char> message;

	auto answer_callback = [&](peer_id const&, rtc_answer const& answer) {
		sig1->process_answer(answer);
	};

	auto offers_handler = [&](error_code const& ec, std::vector<rtc_offer> offers) {
		TEST_CHECK(!ec);
		TEST_EQUAL(int(offers.size()), 1);
		if (offers.empty()) return;
		rtc_offer offer = offers[0];
		offer.answer_callback = answer_callback;
		sig2->process_offer(offer);
	};

	auto write_handler = [&](error_code const& ec, std::size_t size) {
		std::cout << "Stream 2: write completed, ec: " << ec.message() << " size: " << size
				  << std::endl;
		TEST_CHECK(!ec);
		TEST_EQUAL(size, message.size());
		success = true;
	};

	auto handler1 = [&](rtc_stream_init init) {
		TEST_CHECK(init.peer_connection);
		TEST_CHECK(init.data_channel);
		stream1 = std::make_shared<rtc_stream>(io_context, init);
	};

	auto handler2 = [&](rtc_stream_init init) {
		TEST_CHECK(init.peer_connection);
		TEST_CHECK(init.data_channel);

		// an exact multiple of the negotiated max message size is the
		// boundary condition that used to make write_data() spin forever
		std::size_t const max_message_size = init.data_channel->maxMessageSize();
		TEST_CHECK(max_message_size > 0);
		if (max_message_size == 0) return;
		message.assign(3 * max_message_size, char(0x42));

		std::cout << "Signaling 2: Endpoint is connected, writing " << message.size()
				  << " bytes (3x max_message_size=" << max_message_size << ")" << std::endl;
		stream2 = std::make_shared<rtc_stream>(io_context, init);
		stream2->async_write_some(
			boost::asio::const_buffer(message.data(), message.size()), write_handler);
	};

	sig1 = std::make_shared<rtc_signaling>(io_context, &tor1, handler1);
	sig2 = std::make_shared<rtc_signaling>(io_context, &tor2, handler2);

	std::cout << "Signaling 1: Generating 1 offer" << std::endl;
	sig1->generate_offers(1, offers_handler);

	run_test();

	ses1.print_alerts(start_time);
	ses2.print_alerts(start_time);

	if (stream1) stream1->close();
	if (stream2) stream2->close();

	sig1->close();
	sig2->close();
}

// a local UDP socket that never responds, used as a STUN server. It keeps
// ICE gathering pending (libjuice only gives up on an unresponsive STUN
// server after ~23 seconds), so an offer can be made to time out before it
// has been generated. A bound socket is used rather than an unused port so
// that no ICMP port unreachable can make libjuice fail the request early
struct unresponsive_stun_server
{
	unresponsive_stun_server()
		: sock(io_context,
			  boost::asio::ip::udp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0))
	{}
	std::string address() const
	{
		return "127.0.0.1:" + std::to_string(sock.local_endpoint().port());
	}
	boost::asio::ip::udp::socket sock;
};

// regression test for https://github.com/arvidn/libtorrent/issues/7281
// when an offer timed out before ICE gathering completed, its connection was
// removed without ever reporting the offer to its batch. The batch never
// completed, so the announce waiting on it was never sent and the tracker
// was stuck "updating" forever
void test_offer_timeout()
{
	time_point const start_time = clock_type::now();

	unresponsive_stun_server stun;
	session_mock ses(io_context);
	ses.mutable_settings().set_str(settings_pack::webtorrent_stun_server, stun.address());
	ses.mutable_settings().set_int(settings_pack::webtorrent_connection_timeout, 1);
	aux::torrent tor(ses,
		false,
		parse_magnet_uri("magnet:?xt=urn:btih:cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"));

	int calls = 0;
	auto offers_handler = [&](error_code const& ec, std::vector<rtc_offer> const& offers) {
		++calls;
		std::cout << "Batch completed with " << int(offers.size())
				  << " offers, ec: " << ec.message() << std::endl;
		TEST_EQUAL(ec, error_code(boost::asio::error::timed_out));
		TEST_CHECK(offers.empty());
		success = true;
	};

	auto sig = std::make_shared<rtc_signaling>(io_context, &tor, [](rtc_stream_init) {});
	sig->generate_offers(3, offers_handler);

	run_test();
	TEST_EQUAL(calls, 1);

	ses.print_alerts(start_time);
	sig->close();
}

// regression test for https://github.com/arvidn/libtorrent/issues/7281
// generated offers used to be added to whichever batch was at the front of
// the queue. A batch with an offer that never completed would then take
// offers from the next batch, which would in turn stall waiting for them,
// leaving a deficit that carried over to every subsequent batch
void test_offer_batches_are_independent()
{
	time_point const start_time = clock_type::now();

	unresponsive_stun_server stun;
	session_mock ses(io_context);
	ses.mutable_settings().set_int(settings_pack::webtorrent_connection_timeout, 3);
	aux::torrent tor(ses,
		false,
		parse_magnet_uri("magnet:?xt=urn:btih:cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"));

	bool slow_done = false;
	bool fast_done = false;

	auto slow_handler = [&](error_code const& ec, std::vector<rtc_offer> const& offers) {
		std::cout << "Slow batch completed with " << int(offers.size())
				  << " offers, ec: " << ec.message() << std::endl;
		TEST_CHECK(!slow_done);
		// these offers can't be generated before they time out, and must not
		// have been given offers belonging to the other batch
		TEST_EQUAL(ec, error_code(boost::asio::error::timed_out));
		TEST_CHECK(offers.empty());
		slow_done = true;
		if (fast_done)
			success = true;
	};

	auto fast_handler = [&](error_code const& ec, std::vector<rtc_offer> const& offers) {
		std::cout << "Fast batch completed with " << int(offers.size())
				  << " offers, ec: " << ec.message() << std::endl;
		TEST_CHECK(!fast_done);
		TEST_CHECK(!ec);
		TEST_EQUAL(int(offers.size()), 2);
		// must not have to wait for the slow batch
		TEST_CHECK(!slow_done);
		fast_done = true;
		if (slow_done)
			success = true;
	};

	auto sig = std::make_shared<rtc_signaling>(io_context, &tor, [](rtc_stream_init) {});

	// the STUN server is read when each connection is created, so this
	// makes only the first batch's offers stall
	ses.mutable_settings().set_str(settings_pack::webtorrent_stun_server, stun.address());
	sig->generate_offers(2, slow_handler);
	ses.mutable_settings().set_str(settings_pack::webtorrent_stun_server, "");
	sig->generate_offers(2, fast_handler);

	run_test();
	TEST_CHECK(slow_done);
	TEST_CHECK(fast_done);

	ses.print_alerts(start_time);
	sig->close();
}

// regression test for https://github.com/arvidn/libtorrent/issues/7281
// when libdatachannel can't create the ICE agent's UDP socket (e.g. the
// process ran out of file descriptors), createDataChannel() throws. That
// exception used to escape generate_offers(), leaving its batch waiting for
// offers that were never going to be created, stalling the announce
void test_offer_creation_failure()
{
	time_point const start_time = clock_type::now();

	session_mock ses(io_context);
	aux::torrent tor(ses,
		false,
		parse_magnet_uri("magnet:?xt=urn:btih:cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"));

	int calls = 0;
	error_code result;
	int num_offers = -1;

	auto offers_handler = [&](error_code const& ec, std::vector<rtc_offer> const& offers) {
		++calls;
		result = ec;
		num_offers = int(offers.size());
		success = true;
	};

	auto sig = std::make_shared<rtc_signaling>(
		io_context,
		&tor,
		[](rtc_stream_init) {},
		[]() { throw std::runtime_error("test offer creation failure"); });

	bool threw = false;
	try
	{
		sig->generate_offers(1, offers_handler);
	}
	catch (std::exception const& e)
	{
		threw = true;
		std::cout << "generate_offers() threw: " << e.what() << std::endl;
	}

	TEST_CHECK(!threw);

	run_test();

	TEST_EQUAL(calls, 1);
	TEST_EQUAL(result,
		boost::system::errc::make_error_code(boost::system::errc::resource_unavailable_try_again));
	TEST_EQUAL(num_offers, 0);

	ses.print_alerts(start_time);
	sig->close();
}

// when only some of a batch's offers fail to be created, the batch must still
// complete, delivering the offers that succeeded without an error. The
// failure happens after the connection was created, which is where
// libdatachannel throws when it can't open a UDP socket
void test_offer_creation_partial_failure()
{
	time_point const start_time = clock_type::now();

	session_mock ses(io_context);
	aux::torrent tor(ses,
		false,
		parse_magnet_uri("magnet:?xt=urn:btih:cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"));

	int calls = 0;
	error_code result;
	int num_offers = -1;

	auto offers_handler = [&](error_code const& ec, std::vector<rtc_offer> const& offers) {
		++calls;
		result = ec;
		num_offers = int(offers.size());
		success = true;
	};

	int hook_calls = 0;
	auto sig = std::make_shared<rtc_signaling>(
		io_context,
		&tor,
		[](rtc_stream_init) {},
		[&]() {
			if (++hook_calls == 2)
				throw std::runtime_error("test offer creation failure");
		});

	bool threw = false;
	try
	{
		sig->generate_offers(3, offers_handler);
	}
	catch (std::exception const& e)
	{
		threw = true;
		std::cout << "generate_offers() threw: " << e.what() << std::endl;
	}

	TEST_CHECK(!threw);
	TEST_EQUAL(hook_calls, 3);

	run_test();

	TEST_EQUAL(calls, 1);
	TEST_EQUAL(result, error_code{});
	TEST_EQUAL(num_offers, 2);

	ses.print_alerts(start_time);
	sig->close();
}

} // namespace

TORRENT_TEST(parse_endpoint) { test_parse_endpoint(); }
TORRENT_TEST(signaling_offers) { test_offers(); }
TORRENT_TEST(signaling_connectivity) { test_connectivity(); }
TORRENT_TEST(signaling_stream) { test_stream(); }
TORRENT_TEST(write_exact_chunk_boundary) { test_write_exact_chunk_boundary(); }
TORRENT_TEST(signaling_offer_timeout) { test_offer_timeout(); }
TORRENT_TEST(signaling_offer_batches_are_independent) { test_offer_batches_are_independent(); }
TORRENT_TEST(signaling_offer_creation_failure) { test_offer_creation_failure(); }
TORRENT_TEST(signaling_offer_creation_partial_failure) { test_offer_creation_partial_failure(); }
#else
TORRENT_TEST(disabled) {}
#endif // TORRENT_USE_RTC
