/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <cstdint>
#include <cstddef>
#include <array>
#include <memory>
#include <algorithm>

#include "libtorrent/io_context.hpp"
#include "libtorrent/natpmp.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/aux_/enum_net.hpp"
#include "libtorrent/aux_/io_bytes.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/aux_/session_impl.hpp"

#include "portmap_session.hpp"

namespace {

	lt::udp::endpoint discover_natpmp_client_endpoint(lt::udp::socket& router)
	{
		std::array<char, 1500> buf{};
		lt::udp::endpoint from;
		lt::error_code ec;
		for (int i = 0; i < 32; ++i)
		{
			auto const n = router.receive_from(boost::asio::buffer(buf), from, 0, ec);
			if (!ec && n > 0) return from;
			if (ec != boost::asio::error::would_block && ec != boost::asio::error::try_again) break;
		}
		return {};
	}

} // anonymous namespace

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, std::size_t size)
{
	lt::io_context ios;
	lt::aux::session_settings settings;
	fuzz_portmap_cb cb;
	auto const ls = lt::aux::listen_socket_handle(std::make_shared<lt::aux::listen_socket_t>());
	auto np = std::make_shared<lt::natpmp>(ios, settings, cb, ls);

	lt::error_code ec;
	lt::udp::socket router(ios);
	router.open(lt::udp::v4(), ec);
	if (ec) return 0;
	router.bind({lt::make_address_v4("127.0.0.1"), 5351}, ec);
	if (ec) return 0;
	router.non_blocking(true, ec);
	if (ec) return 0;

	lt::aux::ip_interface iface;
	iface.interface_address = lt::make_address_v4("127.0.0.1");
	iface.netmask = lt::make_address_v4("255.0.0.0");
	np->start(iface, lt::make_address_v4("127.0.0.1"));

	np->add_mapping(lt::portmap_protocol::tcp, 0, {lt::make_address_v4("127.0.0.1"), 6881}, "");
	np->add_mapping(lt::portmap_protocol::udp, 0, {lt::make_address_v4("127.0.0.1"), 6882}, "");

	ios.restart();
	ios.poll();

	lt::udp::endpoint const client = discover_natpmp_client_endpoint(router);
	if (client.address().is_unspecified())
	{
		np->close();
		return 0;
	}

	auto cur = reinterpret_cast<char const*>(data);
	auto const end = cur + size;
	while (end - cur >= 2)
	{
		std::size_t const len = lt::aux::read_uint16(cur);
		std::size_t const n = std::min(len, std::size_t(end - cur));
		router.send_to(boost::asio::buffer(cur, n), client, 0, ec);
		cur += n;

		ios.restart();
		ios.poll();
	}

	np->close();
	ios.restart();
	ios.poll();
	return 0;
}
