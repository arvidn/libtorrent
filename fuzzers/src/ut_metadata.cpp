/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

// Fuzzer for the ut_metadata peer plugin (BEP 9), targeting both on_extended()
// and received_metadata().
//
// The fuzzer input is the raw body of a single ut_metadata extended message
// (ext_id 2). Corpus seeds should cover all three msg_type values:
//
//   request   (0): d8:msg_typei0e5:piecei0ee
//   piece     (1): d8:msg_typei1e5:piecei0e10:total_sizei16384ee<payload>
//   dont_have (2): d8:msg_typei2e5:piecei0ee
//
// Reaching received_metadata() requires msg_type=1 and piece=0 to be in
// m_sent_requests. The extended handshake we send announces metadata_size=16384
// (one 16 KiB piece). Because the torrent has no valid metadata, the plugin
// calls maybe_send_request() during on_extension_handshake(), which adds piece 0
// to m_sent_requests before the fuzz message is processed from the same TCP
// stream.

#include <array>
#include <memory>
#include <string_view>

#include "libtorrent/session.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/io_context.hpp"

#include "peer_session.hpp"

using namespace lt;

static peer_fuzz_session g_fz;

// Extended handshake announcing ut_metadata=2 (the ext_id we will use) and
// metadata_size=16384 (one piece). This causes the plugin to allocate one
// piece slot and immediately call maybe_send_request(), adding piece 0 to
// m_sent_requests before our fuzz message is processed.
static constexpr std::string_view k_extended_handshake =
	"d1:md11:ut_metadatai2ee13:metadata_sizei16384ee";

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
	// Add a torrent by info_hash only (no metadata). The ut_metadata plugin
	// will be in "downloading metadata" state, so maybe_send_request() fires
	// when we announce metadata_size in our extended handshake.
	if (g_fz.init([](settings_pack&) {
			add_torrent_params atp;
			atp.info_hashes.v1 = sha1_hash("abababababababababab");
			atp.save_path = ".";
			return atp;
		})
		< 0)
		fuzz_init_failed("session/torrent did not become ready");

	std::atexit([] { g_fz.ses.reset(); });
	return 0;
}

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
	if (size == 0) return 0;

	error_code ec;
	tcp::socket s = connect_to_session(g_fz.ios, g_fz.listen_port, ec);
	if (ec) return 0;

	// BT handshake with BEP 10 (extended protocol) flag set.
	send_bt_handshake(s, g_fz.info_hash.get_best());

	// Extended handshake: announce ut_metadata=2 and metadata_size=16384.
	// Processing this causes on_extension_handshake() to add piece 0 to
	// m_sent_requests before the next message in the stream is read.
	send_extended_message(s,
		0,
		{reinterpret_cast<std::uint8_t const*>(k_extended_handshake.data()),
			static_cast<std::ptrdiff_t>(k_extended_handshake.size())});

	// Send the fuzz input as a ut_metadata message (ext_id 2).
	send_extended_message(s, 2, {data, static_cast<std::ptrdiff_t>(size)});

	s.close(ec);

	return wait_for_disconnect(*g_fz.ses);
}
