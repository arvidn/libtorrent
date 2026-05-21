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

// Pre-encoded BEP 10 extended handshake announcing:
//   ut_pex (1), ut_metadata (2), upload_only (3)
// d1:m d 11:ut_metadata i2e 6:ut_pex i1e 11:upload_only i3e e 4:reqq i500e 1:v 6:fuzzer e
static std::string_view const k_extended_handshake =
	"d1:md11:ut_metadatai2e6:ut_pexi1e11:upload_onlyi3ee4:reqqi500e1:v6:fuzzere";

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	if (g_fz.init([](settings_pack&) { return make_fuzz_torrent_params(); }) < 0)
		fuzz_init_failed("session/torrent did not become ready");

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
	// Byte 0 bit 0x01: protocol version selector
	//   0 = v2 peer: use first 20 bytes of SHA-256 info-hash -> protocol_v2 = true
	//   1 = v1 peer: use SHA-1 info-hash                     -> protocol_v2 = false
	bool const v1_peer = (data[0] & 0x01) != 0;
	sha1_hash const ih = v1_peer ? g_fz.info_hash.v1 : g_fz.info_hash.get_best();
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
