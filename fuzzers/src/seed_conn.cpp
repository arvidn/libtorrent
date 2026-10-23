/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

// Fuzzer for the seeding side of peer_connection: counterpart to
// peer_conn.cpp, whose local session only ever downloads, so it never
// reaches incoming_request() (its "we don't have this piece" branch always
// short-circuits first). This session always already has the full torrent,
// either via seed_mode (trusted up front, hashed lazily per request) or via
// fast-resume (have_pieces resume data). See seed_session.hpp for both.
//
// Four torrents are kept ready, two layouts (hybrid multi-block with a
// short last piece; v1-only two-file so a piece straddles a file boundary)
// each in both modes.

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string_view>

#include "libtorrent/session.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/aux_/io_bytes.hpp"
#include "libtorrent/io_context.hpp"

#include "seed_session.hpp"

using namespace lt;

namespace {

peer_fuzz_session g_fz;

constexpr int num_torrents = 4;

// index: 0 = hybrid/seed_mode, 1 = hybrid/real-seed,
//        2 = v1-only/seed_mode, 3 = v1-only/real-seed
std::array<info_hash_t, num_torrents> g_hash;

// must be non-zero: see send_bt_handshake()'s peer_id parameter.
peer_id const g_peer_id("-SC0001-FUZZFUZZFUZZ");

} // anonymous namespace

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
	settings_pack pack;
	configure_fuzz_session(pack);
	pack.set_str(settings_pack::listen_interfaces, "127.0.0.1:0");

	g_fz.ses = std::make_unique<session>(pack);

	if (wait_for_listen(*g_fz.ses, g_fz.listen_port) < 0)
		fuzz_init_failed("session did not start listening");

	// one full piece (32768) is two 16 KiB blocks; the file size leaves a
	// short last piece (20000 bytes, i.e. one full block plus a partial one)
	// so both multi-block pieces and a non-power-of-2 final piece are covered.
	seed_torrent_spec const hybrid_spec{
		"hybrid_multiblock", 32768, {3 * 32768 + 20000}, create_flags_t{}};

	// single-block pieces over two files whose combined size isn't a
	// multiple of the piece size, so a piece can straddle the file boundary
	// and the last piece is short; v1-only exercises the hash_failed[V2]
	// branch of on_seed_mode_hashed() staying indeterminate.
	seed_torrent_spec const v1_only_spec{
		"v1only_multifile", 16384, {10000, 30000}, create_torrent::v1_only};

	std::array<seed_torrent_spec const*, num_torrents> const specs = {
		&hybrid_spec, &hybrid_spec, &v1_only_spec, &v1_only_spec};
	std::array<bool, num_torrents> const seed_modes = {true, false, true, false};

	for (int i = 0; i < num_torrents; ++i)
	{
		add_torrent_params atp = build_seed_torrent(*specs[i], 'A', seed_modes[i]);
		g_hash[i] = atp.ti->info_hashes();
		g_fz.ses->add_torrent(std::move(atp));
	}

	if (wait_for_torrents_checked(*g_fz.ses, num_torrents) < 0)
		fuzz_init_failed("torrents did not finish checking");

	std::atexit([] { g_fz.ses.reset(); });

	return 0;
}

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
	// 8 bytes for handshake flags, at least 3 more for one message header
	if (size < 11)
		return 0;

	error_code ec;
	tcp::socket s = connect_to_session(g_fz.ios, g_fz.listen_port, ec);

	// BitTorrent handshake.
	// data[0..7]: extension flags (reserved bytes), same layout as peer_conn.
	// Byte 0 bit 0x01: protocol version selector for the hybrid torrents
	//   (selector 0 and 1 below): 0 = v2 peer (advertise the best hash),
	//   1 = v1 peer (advertise the SHA-1 hash). Doesn't apply to the v1-only
	//   torrents (2 and 3), which only have one hash.
	// Byte 0 bits 0x02 and 0x04: 2-bit torrent selector
	//   0 = hybrid, seed_mode
	//   1 = hybrid, fast-resumed
	//   2 = v1-only, seed_mode
	//   3 = v1-only, fast-resumed
	int const torrent_selector = (data[0] >> 1) & 0x03;
	bool const v1_peer = (data[0] & 0x01) != 0;
	sha1_hash const ih =
		v1_peer ? g_hash[torrent_selector].v1 : g_hash[torrent_selector].get_best();
	send_bt_handshake(s, ih, {reinterpret_cast<char const*>(data), 8}, g_peer_id);

	data += 8;
	size -= 8;

	// Declare interest deterministically, before any fuzz-controlled
	// message: loopback connections ignore the session's unchoke-slot
	// accounting and are unchoked the instant they say they're interested
	// (see session_impl's "local" peer class), so this reliably reaches
	// incoming_request() without the fuzz data having to fight the choker.
	send_bt_message(s, 2 /* interested */, {});

	// Same per-message framing as peer_conn. Unlike peer_conn, no extended
	// handshake is sent first: this fuzzer targets the core
	// request/piece/choke machinery, not BEP10 extensions, so every fuzz
	// byte goes toward that instead.
	while (size >= 3)
	{
		std::uint8_t const msg_type = data[0];
		std::size_t payload_len =
			(static_cast<std::size_t>(data[1]) << 8) | static_cast<std::size_t>(data[2]);
		data += 3;
		size -= 3;

		payload_len = std::min(payload_len, size);

		if (msg_type == 20)
		{
			if (payload_len >= 1)
				send_extended_message(
					s, data[0], {data + 1, static_cast<std::ptrdiff_t>(payload_len - 1)});
			else
				send_extended_message(s, 0, {});
		}
		else
		{
			send_bt_message(s, msg_type, {data, static_cast<std::ptrdiff_t>(payload_len)});
		}

		data += payload_len;
		size -= payload_len;
	}

	s.close(ec);

	return wait_for_disconnect(*g_fz.ses);
}
