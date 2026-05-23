/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

// Shared helpers for fuzzers where the fuzzer acts as a server and libtorrent
// connects outward to it (web seed, SOCKS proxy, UPnP, etc.).

#pragma once

#include <optional>
#include <tuple>
#include <chrono>

#include "libtorrent/socket.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/deadline_timer.hpp"

// Creates a TCP server bound on 127.0.0.1:0 and returns {acceptor, port}.
// On any error, the returned acceptor is unopened and port is 0.
inline std::tuple<lt::tcp::acceptor, int> make_tcp_server(lt::io_context& ios)
{
	lt::error_code ec;
	lt::tcp::acceptor acc(ios);
	acc.open(lt::tcp::v4(), ec);
	if (ec) return {std::move(acc), 0};
	acc.set_option(lt::tcp::acceptor::reuse_address(true), ec);
	acc.bind(lt::tcp::endpoint(lt::make_address_v4("127.0.0.1"), 0), ec);
	if (ec) return {std::move(acc), 0};
	acc.listen(1, ec);
	if (ec) return {std::move(acc), 0};
	int const port = acc.local_endpoint(ec).port();
	return {std::move(acc), port};
}

// Waits up to timeout for a connection on acc, then accepts it.
// Returns the connected socket, or nullopt on timeout or error.
inline std::optional<lt::tcp::socket> accept_one(lt::tcp::acceptor& acc,
	lt::io_context& ios,
	std::chrono::milliseconds const timeout = std::chrono::seconds(2))
{
	lt::tcp::socket s(ios);
	lt::error_code accept_ec;

	lt::aux::deadline_timer timer(ios);

	acc.async_accept(s, [&](lt::error_code const& ec) {
		accept_ec = ec;
		// stop waiting for the timeout, the accept completed (or was
		// cancelled by the timer)
		timer.cancel();
	});

	timer.expires_after(timeout);
	timer.async_wait([&](lt::error_code const& ec) {
		// if the wait was cancelled the accept already completed
		if (!ec) acc.cancel();
	});

	ios.restart();
	ios.run();

	if (accept_ec) return std::nullopt;
	return std::optional<lt::tcp::socket>(std::move(s));
}
