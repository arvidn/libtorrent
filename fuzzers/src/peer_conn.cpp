/*

Copyright (c) 2019-2021, Arvid Norberg
Copyright (c) 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <array>
#include <memory>
#include <iostream>
#include <string>
#include <string_view>
#include "libtorrent/session.hpp"
#include "libtorrent/session_params.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/aux_/io_bytes.hpp"
#include "libtorrent/io_context.hpp"

#include "peer_session.hpp"

using namespace lt;

peer_fuzz_session g_fz;

// A second torrent, added as a v1-only magnet alongside g_fz's pre-built
// hybrid one. Its metadata and info-hash are fixed at startup, not derived
// from fuzz input, so delivering it via ut_metadata always succeeds,
// unlike the fuzz-controlled ut_metadata.cpp corpus, which would have to
// find a SHA-1 preimage to ever reach torrent::set_metadata(). Connecting
// to this torrent instead of g_fz's exercises handshake claims (e.g. the
// v2-upgrade bit, reserved byte 7 bit 0x10, already part of the
// fuzz-controlled handshake bytes below) against metadata that resolves
// to v1-only.
info_hash_t g_v1_only_hash;
std::string g_v1_only_metadata_piece;
// second, minimal extended handshake (BEP 10 supports re-negotiation)
// announcing metadata_size, so the plugin queues a request for piece 0
// (see maybe_send_request() in src/ut_metadata.cpp) before the metadata
// piece below is sent. k_extended_handshake doesn't carry metadata_size
// itself since that value depends on the metadata size, computed at
// runtime, and this only matters for the v1-only magnet; for the
// already-has-metadata torrent maybe_send_request() is a no-op regardless.
std::string g_v1_only_announce_size;

// A third torrent, added as a hybrid (v1 + v2) magnet: same idea as the
// v1-only magnet above, but metadata resolves the other way. Exercises the
// case where a peer's speculative v2 claim turns out to be honest (the
// protocol_v2 flag must survive metadata resolution instead of being
// forced false), as a counterpart to the v1-only magnet forcing it false.
// Built with a distinct fill byte from both other torrents (see
// build_fuzz_torrent()'s comment) so it gets its own info-hash instead of
// colliding with g_fz's identically-laid-out hybrid torrent.
info_hash_t g_v2_hybrid_hash;
std::string g_v2_hybrid_metadata_piece;
std::string g_v2_hybrid_announce_size;

// Pre-encoded BEP 10 extended handshake announcing:
//   ut_pex (1), ut_metadata (2), upload_only (3)
// d1:m d 11:ut_metadata i2e 6:ut_pex i1e 11:upload_only i3e e 4:reqq i500e 1:v 6:fuzzer e
static std::string_view const k_extended_handshake =
	"d1:md11:ut_metadatai2e6:ut_pexi1e11:upload_onlyi3ee4:reqqi500e1:v6:fuzzere";

namespace {

struct fuzz_magnet
{
	info_hash_t hash;
	std::string metadata_piece;
	std::string announce_size;
};

// Builds a torrent (v1-only or hybrid, depending on include_v2), adds it to
// g_fz's session as a magnet (info-hash only, no metadata), and pre-encodes
// the ut_metadata announce/piece messages a per-input run sends to resolve
// that magnet's metadata deterministically. include_v2 controls both which
// hash(es) build_fuzz_torrent() fills in and which of them the magnet
// declares (a hybrid magnet declares both up front, like a real magnet URI
// with both xt=urn:btih and xt=urn:btmh, so a peer can attach using either).
fuzz_magnet add_fuzz_magnet(bool const include_v2, char const fill)
{
	add_torrent_params const ti = build_fuzz_torrent(include_v2, fill);
	fuzz_magnet result;
	result.hash = ti.ti->info_hashes();
	auto const info = ti.ti->info_section();
	std::vector<char> const metadata(info.begin(), info.end());

	std::string const header =
		"d8:msg_typei1e5:piecei0e10:total_sizei" + std::to_string(metadata.size()) + "ee";
	result.metadata_piece = header;
	result.metadata_piece.insert(result.metadata_piece.end(), metadata.begin(), metadata.end());

	result.announce_size =
		"d1:md11:ut_metadatai2ee13:metadata_sizei" + std::to_string(metadata.size()) + "ee";

	add_torrent_params magnet;
	magnet.info_hashes = include_v2 ? result.hash : info_hash_t(result.hash.v1);
	magnet.save_path = ".";
	g_fz.ses->async_add_torrent(magnet);
	return result;
}

} // anonymous namespace

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv)
{
	if (g_fz.init([](settings_pack&) { return make_fuzz_torrent_params(); }) < 0)
		fuzz_init_failed("session/torrent did not become ready");

	fuzz_magnet const v1_only = add_fuzz_magnet(false, 'a');
	g_v1_only_hash = v1_only.hash;
	g_v1_only_metadata_piece = v1_only.metadata_piece;
	g_v1_only_announce_size = v1_only.announce_size;
	if (wait_for_torrent_resume(*g_fz.ses) < 0)
		fuzz_init_failed("v1-only magnet did not become ready");

	fuzz_magnet const v2_hybrid = add_fuzz_magnet(true, 'c');
	g_v2_hybrid_hash = v2_hybrid.hash;
	g_v2_hybrid_metadata_piece = v2_hybrid.metadata_piece;
	g_v2_hybrid_announce_size = v2_hybrid.announce_size;
	if (wait_for_torrent_resume(*g_fz.ses) < 0)
		fuzz_init_failed("hybrid magnet did not become ready");

	std::atexit([] { g_fz.ses.reset(); });

	return 0;
}


extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
	// 8 bytes for extension flags, at least 3 more for one message header
	if (size < 11) return 0;

	// connect
	error_code ec;
	tcp::socket s = connect_to_session(g_fz.ios, g_fz.listen_port, ec);

	// BitTorrent handshake.
	// data[0..7]: extension flags (reserved bytes).
	// Byte 0 bit 0x01: protocol version selector, for torrents that have a
	// v1 and a v2 hash to choose between (torrent selector 0 and 2 below)
	//   0 = v2 peer: use first 20 bytes of SHA-256 info-hash -> protocol_v2 = true
	//   1 = v1 peer: use SHA-1 info-hash                     -> protocol_v2 = false
	// Byte 0 bits 0x02 and 0x04: 2-bit torrent selector
	//   0 = g_fz's pre-built hybrid torrent (metadata valid from the start)
	//   1 = the v1-only magnet: metadata delivered below always resolves to
	//       v1-only, so the torrent's info-hash never gains a v2 component
	//       regardless of what byte 7 bit 0x10 (the v2-upgrade bit) claims;
	//       bit 0x01 doesn't apply since there's no v2 hash to choose between
	//   2 = the hybrid magnet: metadata delivered below always resolves to a
	//       genuine hybrid (v1 + v2) torrent, so an honest v2 claim should
	//       survive metadata resolution instead of being corrected; bit
	//       0x01 applies exactly as it does for torrent selector 0
	//   3 = unused, falls back to torrent selector 0
	int const torrent_selector = (data[0] >> 1) & 0x03;
	bool const v1_peer = (data[0] & 0x01) != 0;
	sha1_hash ih;
	switch (torrent_selector)
	{
		case 1:
			ih = g_v1_only_hash.v1;
			break;
		case 2:
			ih = v1_peer ? g_v2_hybrid_hash.v1 : g_v2_hybrid_hash.get_best();
			break;
		default:
			ih = v1_peer ? g_fz.info_hash.v1 : g_fz.info_hash.get_best();
			break;
	}
	send_bt_handshake(s, ih, {reinterpret_cast<char const*>(data), 8});

	data += 8;
	size -= 8;

	// Send an extended handshake (ext_id 0) announcing the extensions we
	// support. This puts libtorrent's extension dispatch table in a known
	// state so that subsequent extension messages are routed to their handlers
	// rather than dropped as "unknown extension".
	send_extended_message(s,
		0,
		{reinterpret_cast<std::uint8_t const*>(k_extended_handshake.data()),
			static_cast<std::ptrdiff_t>(k_extended_handshake.size())});

	// announce metadata_size (BEP 10 re-negotiation), which queues piece 0
	// in the plugin's m_sent_requests, then deliver the matching metadata
	// piece so torrent::set_metadata() succeeds deterministically.
	std::string const* const announce_size = torrent_selector == 1 ? &g_v1_only_announce_size
		: torrent_selector == 2									   ? &g_v2_hybrid_announce_size
																   : nullptr;
	std::string const* const metadata_piece = torrent_selector == 1 ? &g_v1_only_metadata_piece
		: torrent_selector == 2										? &g_v2_hybrid_metadata_piece
																	: nullptr;
	if (announce_size != nullptr)
	{
		send_extended_message(s,
			0,
			{reinterpret_cast<std::uint8_t const*>(announce_size->data()),
				static_cast<std::ptrdiff_t>(announce_size->size())});
		send_extended_message(s,
			2,
			{reinterpret_cast<std::uint8_t const*>(metadata_piece->data()),
				static_cast<std::ptrdiff_t>(metadata_piece->size())});
	}

	// Parse the remaining fuzzer data as a sequence of BitTorrent messages and
	// send each with a correct 4-byte length prefix so the message dispatcher
	// is actually invoked. Without proper framing, libtorrent reads the first
	// 4 garbage bytes as a huge message length and immediately disconnects.
	//
	// Wire format per message:
	//   [1 byte : msg_type]
	//   [2 bytes big-endian : payload_len]  -- caps each payload at 65535 bytes
	//   [payload_len bytes : payload]
	//
	// msg_type 20 (extended protocol) is handled specially: the first payload
	// byte is the extended message ID, the rest is the extension payload.
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
			// extended message: first payload byte is the extended message ID
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
