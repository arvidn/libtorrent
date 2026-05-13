/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

// Session-level fuzzer for libtorrent's Local Service Discovery (LSD)
// receive path (src/lsd.cpp).
//
// The fuzzer constructs an lsd instance without calling start(), so no
// UDP socket is ever bound. Fuzz input is fed directly into
// lsd::process_packet(), which is the helper extracted from on_announce()
// for exactly this purpose. This avoids the port 6771 collision that
// would otherwise prevent running multiple fuzzer workers in parallel,
// and removes a kernel round-trip per packet.
//
// The 127.0.0.1 / 255.0.0.0 pair passed to the constructor is the
// listen interface and netmask used by match_addr_mask. Pairing
// 127.0.0.1 with 255.0.0.0 ensures the loopback sender used below
// always passes the network check.
//
// Wire format: length-prefixed packets. Each fuzz input is parsed as a
// sequence of [2-byte big-endian length][payload] pairs and each payload
// is dispatched as one BT-SEARCH packet.
//
// Interesting code paths exercised:
//   process_packet         -- HTTP parse, method/header validation
//   match_addr_mask        -- always passes (synthetic sender is 127.0.0.1)
//   header lookup          -- "port", "cookie", "infohash" parsing
//   from_hex / sha1_hash   -- hex decode of infohash

#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <memory>

#include "libtorrent/aux_/lsd.hpp"
#include "libtorrent/aux_/io_bytes.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/sha1_hash.hpp"

namespace {

	struct fuzz_lsd_cb final : lt::aux::lsd_callback
	{
		void on_lsd_peer(lt::tcp::endpoint const&, lt::sha1_hash const&) override {}

#ifndef TORRENT_DISABLE_LOGGING
		bool should_log_lsd() const override { return false; }
		void log_lsd(char const*) const override {}
#endif
	};

} // anonymous namespace

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, std::size_t size)
{
	lt::io_context ios;
	fuzz_lsd_cb cb;

	auto l = std::make_shared<lt::aux::lsd>(
		ios, cb, lt::make_address_v4("127.0.0.1"), lt::make_address_v4("255.0.0.0"));

	// Synthetic sender endpoint. The port value is opaque to the receive
	// path; only the address is checked (against the listen netmask).
	lt::udp::endpoint const from(lt::make_address_v4("127.0.0.1"), 12345);

	std::uint8_t const* cursor = data;
	std::uint8_t const* const end = data + size;
	while (end - cursor >= 2)
	{
		std::size_t const len = lt::aux::read_uint16(cursor);
		std::size_t const n = std::min(len, std::size_t(end - cursor));
		// Cap individual packets at LSD's 1500-byte receive buffer; that
		// is the maximum the production receive path ever sees.
		std::size_t const packet_n = std::min<std::size_t>(n, 1500);
		l->process_packet(
			lt::span<char const>{reinterpret_cast<char const*>(cursor), std::ptrdiff_t(packet_n)},
			from);
		cursor += packet_n;
	}

	return 0;
}
