/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "libtorrent/socks5_stream.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/error_code.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/asio/write.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace {

	lt::io_context g_ios;

} // anonymous namespace

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, std::size_t size)
{
	if (size < 5) return 0;

	g_ios.restart();

	lt::tcp::acceptor acceptor(g_ios, lt::tcp::endpoint(lt::make_address_v4("127.0.0.1"), 0));
	std::optional<lt::tcp::socket> proxy_side;

	acceptor.async_accept([&](lt::error_code const& ec, lt::tcp::socket s) {
		if (ec) return;
		proxy_side.emplace(std::move(s));

		// Send the fuzzer payload as a mock SOCKS server transcript.
		// The client-side parser consumes this in multiple read steps.
		boost::asio::async_write(*proxy_side,
			boost::asio::buffer(reinterpret_cast<char const*>(data + 5), size - 5),
			[](lt::error_code const&, std::size_t) {});
	});

	lt::socks5_stream s(g_ios);
	s.set_proxy("127.0.0.1", acceptor.local_endpoint().port());

	// steer a few key state-machine branches
	if (data[0] & 1)
		s.set_version(4);
	else if (data[0] & 2)
		s.set_version(6);
	else
		s.set_version(5);

	if (data[0] & 4) s.set_command(lt::socks5_stream::socks5_udp_associate);

	if (data[0] & 8)
	{
		std::size_t const user_len = std::min<std::size_t>(data[1] % 8, size - 1);
		std::size_t const pass_len = std::min<std::size_t>(data[2] % 8, size - 2);
		std::string user(reinterpret_cast<char const*>(data + 1), user_len);
		std::string pass(reinterpret_cast<char const*>(data + 2), pass_len);
		s.set_username(user, pass);
	}

	lt::error_code ignore;
	lt::tcp::endpoint dst(lt::make_address_v4("1.2.3.4", ignore),
		static_cast<std::uint16_t>((std::uint16_t(data[3]) << 8) | std::uint16_t(data[4])));
	if (data[0] & 16) s.set_dst_name("example.org");

	bool done = false;
	s.async_connect(dst, [&](lt::error_code const&) { done = true; });

	for (int i = 0; i < 200 && !done; ++i)
		g_ios.poll();

	lt::error_code ec;
	s.close(ec);
	if (proxy_side) proxy_side->close(ec);
	acceptor.close(ec);

	// Drain any handlers queued by the close() calls above (e.g. cancellations
	// of pending async_read/async_write/async_resolve). These handlers capture
	// `this` of `s` and `proxy_side`. If left in the queue across a return,
	// the next LLVMFuzzerTestOneInput call will execute them against destroyed
	// stack objects, which ASAN reports as stack-use-after-return.
	g_ios.run();
	return 0;
}
