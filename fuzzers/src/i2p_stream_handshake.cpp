/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

// I2P SAM bridge wire protocol fuzzer.
//
// The fuzzer plays the SAM bridge (the local i2p router proxy). libtorrent's
// i2p_stream / i2p_connection consume the bridge's responses, so a malicious
// or compromised router is the attacker: every byte of the transcript below is
// attacker-controlled input to libtorrent's response parsers. In production
// these paths are reached by sessions configured with an i2p proxy (tracker
// and peer connections through i2p, name lookups for .i2p hostnames).
//
// Corpus wire format:
//   byte 0: mode
//     0: i2p_connection::open() flow (HELLO -> SESSION CREATE -> NAMING "ME")
//     1: like 0, plus a queued async_name_lookup() for a second name, to
//        exercise the name-lookup queue and its draining
//     2: raw i2p_stream with cmd_create_session
//     3: raw i2p_stream with cmd_connect
//     4: raw i2p_stream with cmd_accept (after STREAM STATUS the parser reads
//        the cmd_incoming destination line)
//   remaining bytes: the mock SAM bridge's response transcript, consumed one
//   '\n'-terminated line at a time by i2p_stream's byte-wise line reader.
//
// The socket is closed with SO_LINGER 0 once the transcript has been flushed,
// so the connection is torn down with a reset: a parser waiting for more bytes
// terminates with an error instead of hanging, and no ephemeral port is left
// in TIME_WAIT state per invocation. An upper bound on poll iterations guards
// the remaining cases.

#include <cstdint>
#include <optional>
#include <string>

#include "libtorrent/i2p_stream.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/error_code.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/asio/write.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace {

	lt::io_context g_ios;

	enum mode_t : std::uint8_t
	{
		conn_open = 0,
		conn_open_lookup = 1,
		raw_create_session = 2,
		raw_connect = 3,
		raw_accept = 4
	};

} // anonymous namespace

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, std::size_t size)
{
	if (size < 2) return 0;
	std::uint8_t const mode = data[0];
	char const* const transcript = reinterpret_cast<char const*>(data + 1);
	std::size_t const transcript_size = size - 1;

	g_ios.restart();

	lt::tcp::acceptor acceptor(g_ios, lt::tcp::endpoint(lt::make_address_v4("127.0.0.1"), 0));
	std::optional<lt::tcp::socket> bridge_side;

	acceptor.async_accept([&](lt::error_code const& ec, lt::tcp::socket s) {
		if (ec) return;
		bridge_side.emplace(std::move(s));

		// send the fuzzer payload as the mock SAM bridge's response transcript
		boost::asio::async_write(*bridge_side, boost::asio::buffer(transcript, transcript_size),
			[&](lt::error_code const&, std::size_t) {
				lt::error_code ignore;
				// RST instead of FIN at teardown: this fuzzer creates a fresh
				// loopback connection per input, and i2p_stream reads its
				// input byte-wise until EOF, so a clean shutdown would put
				// one ephemeral port into TIME_WAIT *per invocation* and
				// exhaust the local port range within minutes. A lingering
				// close (SO_LINGER 0) resets the connection instead; the
				// client has long consumed the transcript by then, and a
				// truncated transcript simply surfaces as a reset error,
				// exercising the same error paths as an EOF would.
				boost::asio::socket_base::linger linger_opt(true, 0);
				bridge_side->set_option(linger_opt, ignore);
			});
	});

	bool done = false;

	// both objects live in the function scope so that the final drain
	// g_ios.run() below executes any still-pending completion handlers
	// (e.g. an async_resolve result posted back from asio's resolver
	// thread after the poll loop gave up) against *live* objects. Nested
	// scopes would destroy them before the drain, which ASAN reports as
	// stack-use-after-scope when such a late completion touches `this`.
	std::optional<lt::i2p_connection> conn;
	std::optional<lt::i2p_stream> stream;

	if (mode == conn_open || mode == conn_open_lookup)
	{
		conn.emplace(g_ios);
		lt::i2p_session_options opts;
		conn->open("127.0.0.1", acceptor.local_endpoint().port(), opts,
			[&](lt::error_code const&) { done = true; });

		// the second lookup is queued behind the "ME" lookup open() issues,
		// exercising the m_name_lookup queue and its draining
		if (mode == conn_open_lookup)
			conn->async_name_lookup("target", [](lt::error_code const&, char const*) {});

		for (int i = 0; i < 500 && !done; ++i) g_ios.poll();
	}
	else
	{
		stream.emplace(g_ios);
		stream->set_proxy("127.0.0.1", acceptor.local_endpoint().port());
		stream->set_session_id("fuzzid");

		switch (mode)
		{
			case raw_create_session:
				stream->set_command(lt::i2p_stream::cmd_create_session);
				break;
			case raw_connect:
				stream->set_command(lt::i2p_stream::cmd_connect);
				stream->set_destination("fuzzdest~abcdefgh");
				break;
			case raw_accept:
				stream->set_command(lt::i2p_stream::cmd_accept);
				break;
			default:
				stream->set_command(lt::i2p_stream::cmd_create_session);
				break;
		}

		lt::error_code ignore;
		lt::tcp::endpoint dst(lt::make_address_v4("1.2.3.4", ignore), 7654);
		stream->async_connect(dst, [&](lt::error_code const&) { done = true; });

		for (int i = 0; i < 500 && !done; ++i) g_ios.poll();
	}

	// closing cancels the pending socket operations; any handler still
	// queued (cancellations, resolver completions) runs in the drain below,
	// while conn/stream/bridge_side/acceptor are all still alive
	lt::error_code ec;
	if (conn) conn->close(ec);
	if (stream) stream->close(ec);
	if (bridge_side) bridge_side->close(ec);
	acceptor.close(ec);

	// Drain any handlers queued by the close() calls above (e.g. cancellations
	// of pending async_read/async_write). If left in the queue across a
	// return, the next LLVMFuzzerTestOneInput call would execute them against
	// destroyed stack objects, which ASAN reports as stack-use-after-return.
	g_ios.run();
	return 0;
}
