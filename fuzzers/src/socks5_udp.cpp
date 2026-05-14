/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

// Fuzzer for the SOCKS5 UDP path (UDP ASSOCIATE). It exercises:
//
//   - The control plane: the socks5 state machine that lives inside
//     udp_socket.cpp, driving every supported tunnel configuration
//     (no-auth vs. user/password, send_local_ep on/off, IPv4 vs.
//     DOMAINNAME bind address in the UDP ASSOCIATE response, and the
//     proxy_peer/proxy_tracker_connections flags).
//
//   - The data plane: udp_socket::wrap() and udp_socket::unwrap()
//     receive arbitrary fuzzer bytes once the tunnel is active. Wrap
//     is exercised both by endpoint and by hostname (and across both
//     IPv4 and IPv6 destinations); unwrap is exercised by injecting a
//     UDP packet from a fake SOCKS5 relay socket bound to the address
//     advertised in the tunnel handshake.
//
// Per-input wire format:
//
//   Byte 0 : configuration flags (see flag_* constants below)
//   Bytes 1.. : raw bytes used as the body of:
//                 - the UDP packet injected from the fake relay
//                   (drives unwrap() through udp_socket::read())
//                 - the optional send()/send_hostname() calls
//                   (drives wrap() in both endpoint and hostname forms)
//
// The TCP transcript that the fake SOCKS5 proxy server returns is canned
// (built deterministically from the flags) so that the tunnel reaches the
// active state and both wrap and unwrap actually run.

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <thread>
#include <vector>
#include <string>

#include "libtorrent/io_context.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/address.hpp"

#include "libtorrent/aux_/udp_socket.hpp"
#include "libtorrent/aux_/listen_socket_handle.hpp"
#include "libtorrent/aux_/alert_manager.hpp"
#include "libtorrent/aux_/resolver_interface.hpp"
#include "libtorrent/aux_/proxy_settings.hpp"
#include "libtorrent/aux_/session_impl.hpp" // for aux::listen_socket_t

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/asio/write.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace {

	// Configuration flag bits in the first input byte.
	constexpr std::uint8_t flag_socks5_pw = 0x01; // auth = socks5_pw (else socks5)
	constexpr std::uint8_t flag_with_credentials = 0x02; // populate username/password
	constexpr std::uint8_t flag_send_local_ep = 0x04; // udp_socket send_local_ep argument
	constexpr std::uint8_t flag_proxy_peer = 0x08; // proxy_peer_connections
	constexpr std::uint8_t flag_proxy_tracker = 0x10; // proxy_tracker_connections
	constexpr std::uint8_t flag_atyp_domain =
		0x20; // UDP ASSOCIATE response uses DOMAINNAME atyp (else IPv4)
	constexpr std::uint8_t flag_server_offers_pw =
		0x40; // server's method-selection METHOD = 0x02 (vs 0x00)
	constexpr std::uint8_t flag_server_auth_fail =
		0x80; // sub-negotiation STATUS = 0x01 fail (vs 0x00 success)

	// A minimal synchronous resolver that returns an inline result. asio's real
	// resolver dispatches lookups through an internal thread, which adds latency
	// and shutdown coordination that the fuzzer does not need; "127.0.0.1" is the
	// only hostname we ever ask about, and make_address() handles that without
	// touching the network.
	struct fuzz_resolver final : lt::aux::resolver_interface
	{
		void async_resolve(std::string const& host, lt::aux::resolver_flags, callback_t h) override
		{
			lt::error_code ec;
			std::vector<lt::address> ips;
			lt::address const a = lt::make_address(host, ec);
			if (!ec) ips.push_back(a);
			h(ec, ips);
		}
		void abort() override {}
		void set_cache_timeout(lt::seconds) override {}
	};

	// Pump the io_context until idle. Bounded so a misbehaving handler chain
	// can never wedge the fuzzer. Returns true if the iteration budget was
	// exhausted (i.e. the queue was still non-empty), false if the queue
	// drained naturally. For the workloads this fuzzer drives, the bounded
	// exit should never trigger -- so the caller treats it as a hard error.
	bool drain(lt::io_context& ios, int const max_iters = 200)
	{
		for (int i = 0; i < max_iters; ++i)
		{
			ios.restart();
			if (ios.poll() == 0) return false;
		}
		return true;
	}

	void drain_or_die(lt::io_context& ios, int const max_iters = 200)
	{
		if (drain(ios, max_iters))
		{
			std::fprintf(stderr,
				"socks5_udp fuzzer: drain() exhausted %d iterations without"
				" emptying the io_context -- handler chain bug?\n",
				max_iters);
			assert(false && "drain() exhausted iteration budget");
		}
	}

	// Build the canned TCP transcript that the fake SOCKS5 proxy will write back
	// in response to the udp_socket's outgoing handshake. The server's choices
	// (which auth method to offer, whether sub-negotiation succeeds) are
	// independent of the client's configuration so that the fuzzer can explore
	// the disagreement matrix -- e.g. client expects pw auth but server picks
	// no auth, or sub-negotiation returns failure. When server's choices line
	// up with the client's, the tunnel reaches the active state with
	// m_udp_proxy_addr = (127.0.0.1, relay_port) and subsequent relay packets
	// get past the source-IP match in udp_socket::read().
	std::vector<std::uint8_t> build_proxy_transcript(bool const server_offers_pw,
		bool const server_auth_fail,
		bool const atyp_domain,
		std::uint16_t const relay_port)
	{
		std::vector<std::uint8_t> t;

		// Method-selection response.
		t.push_back(0x05); // VER = SOCKS5
		t.push_back(server_offers_pw ? 0x02 : 0x00); // METHOD = user/pw or no auth

		// Username/password sub-negotiation response (only emitted if the
		// server told the client to authenticate). The client only reads
		// these bytes when it actually entered the sub-negotiation phase;
		// otherwise the bytes pile up in the connection and are discarded
		// when the client closes the socket.
		if (server_offers_pw)
		{
			t.push_back(0x01); // VER = 1
			t.push_back(server_auth_fail ? 0x01 : 0x00); // STATUS
		}

		// UDP ASSOCIATE response. atyp=1 (IPv4) or atyp=3 (DOMAINNAME).
		t.push_back(0x05); // VER
		t.push_back(0x00); // REP = succeeded
		t.push_back(0x00); // RSV
		if (atyp_domain)
		{
			t.push_back(0x03); // ATYP = DOMAINNAME
			std::string const name = "127.0.0.1";
			t.push_back(static_cast<std::uint8_t>(name.size()));
			t.insert(t.end(), name.begin(), name.end());
		}
		else
		{
			t.push_back(0x01); // ATYP = IPv4
			t.push_back(0x7f);
			t.push_back(0x00);
			t.push_back(0x00);
			t.push_back(0x01); // 127.0.0.1
		}
		t.push_back(static_cast<std::uint8_t>(relay_port >> 8));
		t.push_back(static_cast<std::uint8_t>(relay_port & 0xff));

		return t;
	}

} // anonymous namespace

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, std::size_t size)
{
	if (size < 1) return 0;

	std::uint8_t const flags = data[0];
	std::uint8_t const* body = data + 1;
	std::size_t const body_len = size - 1;

	bool const socks5_pw = (flags & flag_socks5_pw) != 0;
	bool const with_credentials = (flags & flag_with_credentials) != 0;
	bool const send_local_ep = (flags & flag_send_local_ep) != 0;
	bool const proxy_peer = (flags & flag_proxy_peer) != 0;
	bool const proxy_tracker = (flags & flag_proxy_tracker) != 0;
	bool const atyp_domain = (flags & flag_atyp_domain) != 0;
	bool const server_offers_pw = (flags & flag_server_offers_pw) != 0;
	bool const server_auth_fail = (flags & flag_server_auth_fail) != 0;

	lt::io_context ios;
	lt::aux::alert_manager alerts(100);
	fuzz_resolver resolver;

	// listen_socket_t supplies the local endpoint and can_route() that the
	// socks5 state machine queries. We only need a v4 loopback address; the
	// netmask is wide enough that can_route() accepts the proxy's address.
	auto listen_sock = std::make_shared<lt::aux::listen_socket_t>();
	listen_sock->local_endpoint = lt::tcp::endpoint(lt::make_address_v4("127.0.0.1"), 0);
	listen_sock->netmask = lt::make_address_v4("255.0.0.0");
	lt::aux::listen_socket_handle ls(listen_sock);

	lt::error_code ec;

	// Fake SOCKS5 proxy server (TCP).
	lt::tcp::acceptor proxy_tcp(ios);
	proxy_tcp.open(lt::tcp::v4(), ec);
	if (ec) return 0;
	proxy_tcp.bind(lt::tcp::endpoint(lt::make_address_v4("127.0.0.1"), 0), ec);
	if (ec) return 0;
	proxy_tcp.listen(boost::asio::socket_base::max_listen_connections, ec);
	if (ec) return 0;
	std::uint16_t const proxy_port = proxy_tcp.local_endpoint().port();

	// Fake SOCKS5 UDP relay -- "the proxy's UDP-side socket". UDP packets
	// from this endpoint look authentic to the udp_socket because the socks5
	// handshake response (built below) advertises this exact port.
	lt::udp::socket relay(ios);
	relay.open(lt::udp::v4(), ec);
	if (ec) return 0;
	relay.bind(lt::udp::endpoint(lt::make_address_v4("127.0.0.1"), 0), ec);
	if (ec) return 0;
	relay.non_blocking(true, ec);
	if (ec) return 0;
	// Sanity check: the source-IP comparison in udp_socket::read()
	// depends on relay's outgoing packets carrying 127.0.0.1 as the
	// source. That is only guaranteed when relay is actually bound to
	// 127.0.0.1; assert it so a future regression here fails loud.
	assert(relay.local_endpoint().address() == lt::make_address_v4("127.0.0.1"));
	std::uint16_t const relay_port = relay.local_endpoint().port();

	// The udp_socket under test.
	lt::aux::udp_socket us(ios, ls);
	us.open(lt::udp::v4(), ec);
	if (ec) return 0;
	us.bind(lt::udp::endpoint(lt::make_address_v4("127.0.0.1"), 0), ec);
	if (ec) return 0;

	// Set up the proxy-side acceptor: when the udp_socket's socks5 state
	// machine connects, write our canned transcript.
	//
	// transcript and proxy_side are owned by shared_ptrs that the handler
	// chain captures by value, so their storage outlives any pending async
	// operation regardless of how cleanup is reordered. async_write keeps
	// a (ptr, size) buffer descriptor into transcript's storage until the
	// op completes or is cancelled, so transcript must remain valid that
	// long -- the shared_ptr in the completion handler guarantees it.
	auto transcript = std::make_shared<std::vector<std::uint8_t> const>(
		build_proxy_transcript(server_offers_pw, server_auth_fail, atyp_domain, relay_port));
	auto proxy_side = std::make_shared<std::optional<lt::tcp::socket>>();
	proxy_tcp.async_accept([transcript, proxy_side](lt::error_code const& aec, lt::tcp::socket s) {
		if (aec) return;
		proxy_side->emplace(std::move(s));
		boost::asio::async_write(**proxy_side,
			boost::asio::buffer(*transcript),
			[transcript](lt::error_code const&, std::size_t) {});
	});

	// Configure the proxy and kick off the handshake.
	lt::aux::proxy_settings ps;
	ps.hostname = "127.0.0.1";
	ps.port = proxy_port;
	ps.type = socks5_pw ? lt::settings_pack::socks5_pw : lt::settings_pack::socks5;
	if (with_credentials)
	{
		ps.username = "fuzz-user";
		ps.password = "fuzz-pass";
	}
	ps.proxy_peer_connections = proxy_peer;
	ps.proxy_tracker_connections = proxy_tracker;

	us.set_proxy_settings(ps, alerts, resolver, send_local_ep);

	// Drive the handshake to completion (or to an error). The fuzz_resolver
	// returns inline so the only async work is the loopback TCP traffic.
	drain_or_die(ios);

	// Data plane: drive unwrap() with arbitrary bytes from the relay.
	bool packet_in_flight = false;
	if (us.active_socks5() && body_len > 0)
	{
		relay.send_to(boost::asio::buffer(body, body_len), us.local_endpoint(), 0, ec);
		packet_in_flight = !ec;
	}

	// Pull the packet through udp_socket::read(). This is the production
	// code path that calls unwrap() on every packet whose source matches
	// the proxy's advertised relay endpoint. read() does a synchronous
	// receive_from() on a non-blocking socket, so it may return
	// would_block if the kernel hasn't placed the packet on the receive
	// queue yet. Retry briefly to absorb that delivery jitter, but only
	// when we know a packet is actually in flight -- otherwise the vast
	// majority of inputs (those that never reach active_socks5()) would
	// pay the full sleep budget for nothing.
	// Note: udp_socket::read() returning 0 here is the common case, not
	// an error. socks5_unwrap() rejects any packet that isn't shaped
	// like a valid SOCKS5 UDP header, and most random fuzzer payloads
	// aren't -- that rejection is exactly the unwrap() coverage we want.
	// We can't distinguish "kernel drop" / "source IP mismatch" /
	// "unwrap rejected" from outside read(), so we don't try.
	if (packet_in_flight)
	{
		std::array<lt::aux::udp_socket::packet, 4> pkts;
		for (int i = 0; i < 16; ++i)
		{
			lt::error_code rec;
			int const n = us.read(pkts, rec);
			if (n > 0) break;
			if (rec != boost::asio::error::would_block && rec != boost::asio::error::try_again)
				break;
			using namespace std::chrono_literals;
			std::this_thread::sleep_for(100us);
		}
	}

	// Data plane: drive wrap() through send() and send_hostname() with the
	// fuzz buffer as the payload. All three forms run on every input so
	// every wrap variant gets coverage from every byte mutation. When
	// active_socks5() is true these end up at our relay; the kernel queues
	// the writes so the buffers stay alive past the call site.
	{
		lt::span<char const> const payload(
			reinterpret_cast<char const*>(body), static_cast<std::ptrdiff_t>(body_len));

		// IPv4 destination through wrap-by-endpoint.
		{
			lt::udp::endpoint dest(lt::make_address_v4("8.8.8.8"), 1234);
			us.send(dest, payload, ec);
		}
		// IPv6 destination through wrap-by-endpoint: exercises the v6
		// branch of write_endpoint() and the dont_fragment v4-only check.
		{
			lt::udp::endpoint dest6(lt::make_address_v6("::1"), 1234);
			us.send(dest6, payload, ec);
		}
		// Hostname destination through wrap-by-hostname.
		us.send_hostname("example.com", 1234, payload, ec);
	}

	// Cleanup. close() on the udp_socket also closes its inner socks5
	// connection; any read/write handlers still pending get cancelled and
	// drain() runs them so they don't fire across invocations.
	us.close();
	if (proxy_side->has_value()) (**proxy_side).close(ec);
	proxy_tcp.close(ec);
	relay.close(ec);
	drain_or_die(ios, 500);

	return 0;
}
