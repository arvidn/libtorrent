/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

// Fuzzer for the ut_pex peer plugin (BEP 11), targeting on_extended().
//
// The fuzzer input is the raw body of a single ut_pex extended message
// (ext_id 1). Corpus seeds should be bencoded dicts with the fields that
// on_extended() parses:
//
//   added/added.f   -- IPv4 peers to add (6 bytes each / 1 flag byte each)
//   dropped         -- IPv4 peers to remove (6 bytes each)
//   added6/added6.f -- IPv6 peers to add (18 bytes each / 1 flag byte each)
//   dropped6        -- IPv6 peers to remove (18 bytes each)

#include <array>
#include <memory>
#include <string_view>
#include <vector>

#include "libtorrent/session.hpp"
#include "libtorrent/session_params.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/load_torrent.hpp"
#include "libtorrent/io_context.hpp"

#include "peer_session.hpp"

using namespace lt;

static peer_fuzz_session g_fz;

// Extended handshake announcing ut_pex=1 (the ext_id we will use).
// This sets m_message_index=1 in ut_pex_peer_plugin so on_extended()
// does not return early.
static constexpr std::string_view k_extended_handshake = "d1:md6:ut_pexi1eee";

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
	// Create a minimal torrent so the session has a torrent to attach peer
	// connections to. The ut_pex plugin calls m_torrent.add_peer() with peer
	// addresses from the received PEX message, which requires a valid torrent.
	if (g_fz.init([](settings_pack&) {
			int const piece_size = 1024 * 1024;
			std::vector<lt::create_file_entry> fs;
			fs.emplace_back("test_file", piece_size);
			create_torrent t(std::move(fs), piece_size);
			t.set_hash(piece_index_t{0}, sha1_hash("abababababababababab"));

			std::vector<char> buf;
			bencode(std::back_inserter(buf), t.generate());
			add_torrent_params atp = load_torrent_buffer(buf);
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

	// Extended handshake: announce ut_pex=1. This sets m_message_index=1 in
	// the ut_pex plugin so that our subsequent ext_id=1 message is dispatched
	// to on_extended() rather than dropped.
	send_extended_message(s,
		0,
		{reinterpret_cast<std::uint8_t const*>(k_extended_handshake.data()),
			static_cast<std::ptrdiff_t>(k_extended_handshake.size())});

	// Send the fuzz input as a ut_pex message (ext_id 1).
	send_extended_message(s, 1, {data, static_cast<std::ptrdiff_t>(size)});

	s.close(ec);

	return wait_for_disconnect(*g_fz.ses);
}
