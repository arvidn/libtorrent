/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

// Session-level fuzzer for libtorrent's web seed (BEP 19) HTTP client.
//
// The fuzzer acts as an HTTP server. LLVMFuzzerInitialize creates a libtorrent
// session with a hybrid (v1 + v2) torrent (see make_fuzz_torrent_params) and
// an HTTP URL seed pointing at the fuzzer's listening socket.
// LLVMFuzzerTestOneInput accepts the outgoing connection libtorrent makes,
// writes the fuzz input as the raw HTTP response body, closes the socket, and
// waits for libtorrent to disconnect.
//
// Wire format: raw bytes sent as the HTTP response (no wrapper). The corpus
// should contain valid and malformed HTTP responses to exercise the parser in
// web_peer_connection.cpp.

#include <iostream>
#include <memory>
#include <string>
#include <cstdint>
#include <cstddef>

#include "libtorrent/session.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/torrent_handle.hpp"

#include "peer_session.hpp"
#include "server_session.hpp"

using namespace lt;

std::unique_ptr<session> g_ses;
torrent_handle g_handle;
io_context g_ios;
tcp::acceptor g_acc(g_ios);
int g_port = 0;
std::string g_url;

extern "C" int LLVMFuzzerInitialize(int* /*argc*/, char*** /*argv*/)
{
	settings_pack pack;
	configure_fuzz_session(pack);
	// no incoming BitTorrent listen port; libtorrent connects outward to us
	pack.set_str(settings_pack::listen_interfaces, "");
	// ensure libtorrent reconnects quickly after each disconnect
	pack.set_int(settings_pack::urlseed_wait_retry, 0);
	// negative tick_interval bypasses the once-per-second second_tick gate so
	// maybe_connect_web_seeds() fires every fast tick (every ~1 ms)
	pack.set_int(settings_pack::tick_interval, -1);

	g_ses = std::unique_ptr<session>(new lt::session(pack));

	std::tie(g_acc, g_port) = make_tcp_server(g_ios);
	if (g_port == 0) fuzz_init_failed("could not bind TCP server socket");

	// URL ends with '/' so libtorrent appends the per-file path within the
	// torrent (e.g. "fuzzer_torrent/large.dat") to form the request URL.
	g_url = "http://127.0.0.1:" + std::to_string(g_port) + "/";

	add_torrent_params atp = make_fuzz_torrent_params();
	atp.url_seeds.push_back(g_url);

	g_handle = g_ses->add_torrent(std::move(atp));
	if (wait_for_torrent_resume(*g_ses) < 0) fuzz_init_failed("torrent did not resume");

	std::atexit([] { g_ses.reset(); });

	return 0;
}

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, std::size_t size)
{
	// accept the outgoing connection libtorrent makes to our fake HTTP server
	auto conn = accept_one(g_acc, g_ios);
	if (!conn)
	{
#if DEBUG_LOGGING
		std::cout << "web_seed: accept timed out, re-arming URL seed\n";
#endif
		g_handle.remove_url_seed(g_url);
		g_handle.add_url_seed(g_url);
		return -1;
	}

	// write fuzz bytes as the HTTP response, then close
	error_code ec;
	boost::asio::write(*conn, boost::asio::buffer(data, size), ec);
	conn->close(ec);

	int const ret = wait_for_disconnect(*g_ses);

	// re-arm the URL seed so libtorrent reconnects in the next iteration;
	// remove + add creates a fresh entry without any retry delay or disabled flag
	g_handle.remove_url_seed(g_url);
	g_handle.add_url_seed(g_url);

	return ret;
}
