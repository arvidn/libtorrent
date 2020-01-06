/*

Copyright (c) 2015, Steven Siloti
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

#include "simulator/simulator.hpp"

#include "libtorrent/aux_/listen_socket_handle.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/udp_socket.hpp"
#include "libtorrent/kademlia/dht_tracker.hpp"
#include "libtorrent/kademlia/dht_state.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/kademlia/dht_settings.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/kademlia/dht_observer.hpp"

#include <functional>
#include <cstdarg>
#include <cmath>

using namespace lt;
using namespace sim;
using namespace std::placeholders;

#if !defined TORRENT_DISABLE_DHT

struct obs : dht::dht_observer
{
	void set_external_address(lt::aux::listen_socket_handle const&, address const& /* addr */
		, address const& /* source */) override
	{}
	int get_listen_port(lt::aux::transport, lt::aux::listen_socket_handle const& s) override
	{ return s.get()->udp_external_port(); }
	void get_peers(sha1_hash const&) override {}
	void outgoing_get_peers(sha1_hash const& /* target */
		, sha1_hash const& /* sent_target */, udp::endpoint const& /* ep */) override {}
	void announce(sha1_hash const& /* ih */
		, address const& /* addr */, int /* port */) override {}
	bool on_dht_request(string_view /* query */
		, dht::msg const& /* request */, entry& /* response */) override
	{ return false; }

#ifndef TORRENT_DISABLE_LOGGING
	bool should_log(module_t) const override { return true; }
	void log(dht_logger::module_t, char const* fmt, ...) override
	{
		va_list v;
		va_start(v, fmt);
		vprintf(fmt, v);
		va_end(v);
		puts("\n");
	}
	void log_packet(message_direction_t /* dir */
		, span<char const> /* pkt */
		, udp::endpoint const& /* node */) override {}
#endif
};

void send_packet(lt::udp_socket& sock, lt::aux::listen_socket_handle const&, udp::endpoint const& ep
	, span<char const> p, error_code& ec, udp_send_flags_t const flags)
{
	sock.send(ep, p, ec, flags);
}

#endif // #if !defined TORRENT_DISABLE_DHT

TORRENT_TEST(dht_rate_limit)
{
#if !defined TORRENT_DISABLE_DHT

	default_config cfg;
	simulation sim(cfg);
	asio::io_service dht_ios(sim, address_v4::from_string("40.30.20.10"));

	// receiver (the DHT under test)
	lt::udp_socket sock(dht_ios, lt::aux::listen_socket_handle{});
	obs o;
	auto ls = std::make_shared<lt::aux::listen_socket_t>();
	ls->external_address.cast_vote(address_v4::from_string("40.30.20.10")
		, lt::aux::session_interface::source_dht, lt::address());
	ls->local_endpoint = tcp::endpoint(address_v4::from_string("40.30.20.10"), 8888);
	error_code ec;
	sock.bind(udp::endpoint(address_v4::from_string("40.30.20.10"), 8888), ec);
	dht::settings dhtsett;
	dhtsett.block_ratelimit = 100000; // disable the DOS blocker
	dhtsett.ignore_dark_internet = false;
	dhtsett.upload_rate_limit = 400;
	float const target_upload_rate = 400;
	int const num_packets = 2000;

	counters cnt;
	dht::dht_state state;
	std::unique_ptr<lt::dht::dht_storage_interface> dht_storage(dht::dht_default_storage_constructor(dhtsett));
	auto dht = std::make_shared<lt::dht::dht_tracker>(
		&o, dht_ios, std::bind(&send_packet, std::ref(sock), _1, _2, _3, _4, _5)
		, dhtsett, cnt, *dht_storage, std::move(state));
	dht->new_socket(ls);

	bool stop = false;
	std::function<void(error_code const&, size_t)> on_read
		= [&](error_code const& ec, size_t const /* bytes */)
	{
		if (ec) return;
		udp_socket::packet p;
		error_code err;
		int const num = int(sock.read(lt::span<udp_socket::packet>(&p, 1), err));
		if (num) dht->incoming_packet(ls, p.from, p.data);
		if (stop || err) return;
		sock.async_read(on_read);
	};
	sock.async_read(on_read);

	// sender
	int num_packets_sent = 0;
	asio::io_service sender_ios(sim, address_v4::from_string("10.20.30.40"));
	udp::socket sender_sock(sender_ios);
	sender_sock.open(udp::v4());
	sender_sock.bind(udp::endpoint(address_v4(), 4444));
	sender_sock.non_blocking(true);
	asio::high_resolution_timer timer(sender_ios);
	std::function<void(error_code const&)> sender_tick = [&](error_code const&)
	{
		if (num_packets_sent == num_packets)
		{
			// we're done. shut down (a second from now, to let the dust settle)
			timer.expires_from_now(chrono::seconds(1));
			timer.async_wait([&](error_code const&)
			{
				dht->stop();
				stop = true;
				sender_sock.close();
				sock.close();
			});
			return;
		}

		char const packet[] = "d1:ad2:id20:ababababababababababe1:y1:q1:q4:pinge";
		sender_sock.send_to(asio::const_buffers_1(packet, sizeof(packet)-1)
			, udp::endpoint(address_v4::from_string("40.30.20.10"), 8888));
		++num_packets_sent;

		timer.expires_from_now(chrono::milliseconds(10));
		timer.async_wait(sender_tick);
	};
	timer.expires_from_now(chrono::milliseconds(10));
	timer.async_wait(sender_tick);

	udp::endpoint from;
	int num_bytes_received = 0;
	int num_packets_received = 0;
	char buffer[1500];
	std::function<void(error_code const&, std::size_t)> on_receive
		= [&](error_code const& ec, std::size_t const bytes)
	{
		if (ec) return;

		num_bytes_received += int(bytes);
		++num_packets_received;

		sender_sock.async_receive_from(asio::mutable_buffers_1(buffer, sizeof(buffer))
			, from, on_receive);
	};
	sender_sock.async_receive_from(asio::mutable_buffers_1(buffer, sizeof(buffer))
		, from, on_receive);

	// run simulation
	lt::clock_type::time_point start = lt::clock_type::now();
	sim.run();
	lt::clock_type::time_point end = lt::clock_type::now();

	// subtract one target_upload_rate here, since we initialize the quota to one
	// full second worth of bandwidth
	float const average_upload_rate = (num_bytes_received - target_upload_rate)
		/ (duration_cast<chrono::milliseconds>(end - start).count() * 0.001f);

	std::printf("send %d packets. received %d packets (%d bytes). average rate: %f (target: %f)\n"
		, num_packets_sent, num_packets_received, num_bytes_received
		, average_upload_rate, target_upload_rate);

	// the actual upload rate should be within 5% of the target
	TEST_CHECK(std::abs(average_upload_rate - target_upload_rate) < target_upload_rate * 0.05);

	TEST_EQUAL(cnt[counters::dht_messages_in], num_packets);

	// the number of dropped packets + the number of received pings, should equal
	// exactly the number of packets we sent
	TEST_EQUAL(cnt[counters::dht_messages_in_dropped]
		+ cnt[counters::dht_ping_in], num_packets);

#endif // #if !defined TORRENT_DISABLE_DHT
}

// TODO: put test here to take advantage of existing code, refactor
TORRENT_TEST(dht_delete_socket)
{
#ifndef TORRENT_DISABLE_DHT

	sim::default_config cfg;
	sim::simulation sim(cfg);
	sim::asio::io_service dht_ios(sim, lt::address_v4::from_string("40.30.20.10"));

	lt::udp_socket sock(dht_ios, lt::aux::listen_socket_handle{});
	error_code ec;
	sock.bind(udp::endpoint(address_v4::from_string("40.30.20.10"), 8888), ec);

	obs o;
	auto ls = std::make_shared<lt::aux::listen_socket_t>();
	ls->external_address.cast_vote(address_v4::from_string("40.30.20.10")
		, lt::aux::session_interface::source_dht, lt::address());
	ls->local_endpoint = tcp::endpoint(address_v4::from_string("40.30.20.10"), 8888);
	dht::settings dhtsett;
	counters cnt;
	dht::dht_state state;
	std::unique_ptr<lt::dht::dht_storage_interface> dht_storage(dht::dht_default_storage_constructor(dhtsett));
	auto dht = std::make_shared<lt::dht::dht_tracker>(
		&o, dht_ios, std::bind(&send_packet, std::ref(sock), _1, _2, _3, _4, _5)
		, dhtsett, cnt, *dht_storage, std::move(state));

	dht->start([](std::vector<std::pair<dht::node_entry, std::string>> const&){});
	dht->new_socket(ls);

	// schedule the removal of the socket at exactly 2 second,
	// this simulates the fact that the internal scheduled call
	// to connection_timeout will be executed right after leaving
	// the state of cancellable
	asio::high_resolution_timer t1(dht_ios);
	t1.expires_from_now(chrono::seconds(2));
	t1.async_wait([&](error_code const&)
		{
			dht->delete_socket(ls);
		});

	// stop the DHT
	asio::high_resolution_timer t2(dht_ios);
	t2.expires_from_now(chrono::seconds(3));
	t2.async_wait([&](error_code const&) { dht->stop(); });

	sim.run();

#endif // TORRENT_DISABLE_DHT
}
