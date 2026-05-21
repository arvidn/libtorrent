/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

// Fuzzer for the protocol encryption (MSE/PE) handshake in bt_peer_connection.
//
// The session is configured to accept both plaintext and encrypted incoming
// connections (in_enc_policy = pe_enabled, allowed_enc_level = pe_both).
//
// Each input drives a single incoming connection that performs a real DH key
// exchange (so the server can locate the torrent) but then uses fuzz-controlled
// values for the encrypted handshake fields: crypto_provide, padding, IA length,
// and IA content. Corruption flags can trigger every server-side error path:
//   sync_hash_not_found, invalid_info_hash, invalid_encryption_constant,
//   unsupported_encryption_mode, invalid_pad_size, invalid_encrypt_handshake.
//
// Corpus wire format (see .claude/rules/protocol-encryption.md for the protocol):
//
//   Byte 0 : pad_a_size   -- bytes of garbage between our DH key and sync hash
//                            (0-255); exercises the sync-hash scanning loop
//   Byte 1 : flags
//               bit 0 : corrupt_sync_hash  (send junk -> sync_hash_not_found)
//               bit 1 : corrupt_skey       (send junk -> invalid_info_hash)
//               bit 2 : corrupt_vc         (flip VC[0] -> invalid_encryption_constant)
//               bits 3-4 : crypto_provide selector
//                          00 -> pe_both (0x03)
//                          01 -> pe_plaintext (0x01)
//                          10 -> pe_rc4 (0x02)
//                          11 -> 0x00 (invalid -> unsupported_encryption_mode)
//   Byte 2 : len_pad_c    -- encrypted padding size * 2 (0-510 bytes)
//   Byte 3 : len_ia       -- IA size 0-255; server rejects values > 68
//   Bytes 4..(4+pad_a_size-1) : PadA content (from fuzz data)
//   Bytes onward : IA content (BT handshake bytes if a valid path is desired)

#include <array>
#include <vector>
#include <memory>
#include <cstring>
#include <algorithm>
#include <iostream>

#include "libtorrent/session.hpp"
#include "libtorrent/session_params.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/load_torrent.hpp"
#include "libtorrent/aux_/io_bytes.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/aux_/pe_crypto.hpp"
#include "libtorrent/hasher.hpp"

#include "peer_session.hpp"

using namespace lt;
using namespace lt::aux;

peer_fuzz_session g_fz;

#if !defined TORRENT_DISABLE_ENCRYPTION

// Build RC4 keys for an outgoing (client-initiating) PE connection.
// The client's send key is hash("keyA", S, SKEY); receive key is hash("keyB", S, SKEY).
static std::shared_ptr<rc4_handler> make_rc4(lt::aux::key_t const& secret, sha1_hash const& skey)
{
	std::array<char, 96> const secret_buf = export_key(secret);
	static const char keyA[4] = {'k', 'e', 'y', 'A'};
	static const char keyB[4] = {'k', 'e', 'y', 'B'};

	sha1_hash const out_key = hasher(keyA, 4)
								  .update(secret_buf.data(), static_cast<int>(secret_buf.size()))
								  .update(skey.data(), static_cast<int>(skey.size()))
								  .final();
	sha1_hash const in_key = hasher(keyB, 4)
								 .update(secret_buf.data(), static_cast<int>(secret_buf.size()))
								 .update(skey.data(), static_cast<int>(skey.size()))
								 .final();

	auto ret = std::make_shared<rc4_handler>();
	ret->set_outgoing_key(out_key);
	ret->set_incoming_key(in_key);
	return ret;
}

#endif // TORRENT_DISABLE_ENCRYPTION

extern "C" int LLVMFuzzerInitialize(int* /*argc*/, char*** /*argv*/)
{
	if (g_fz.init([](settings_pack& pack) {
			// Accept both plaintext and encrypted incoming connections.
			pack.set_int(settings_pack::in_enc_policy, settings_pack::pe_enabled);
			pack.set_int(settings_pack::allowed_enc_level, settings_pack::pe_both);
			// Same hybrid v1+v2 torrent as peer_conn.cpp so seed corpus entries are reusable.
			return make_fuzz_torrent_params();
		})
		< 0)
		fuzz_init_failed("session/torrent did not become ready");

	std::atexit([] { g_fz.ses.reset(); });
	return 0;
}

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, std::size_t size)
{
#if defined TORRENT_DISABLE_ENCRYPTION
	return 0;
#else
	if (size < 4) return 0;

	int const pad_a_size = data[0];
	std::uint8_t const flags = data[1];
	int const len_pad_c = int(data[2]) * 2; // 0-510 bytes of encrypted padding
	int const len_ia = data[3]; // 0-255; server rejects > 68
	data += 4;
	size -= 4;

	// crypto_provide from bits 3-4 of flags
	static constexpr std::uint8_t crypto_table[4] = {0x03, 0x01, 0x02, 0x00};
	std::uint8_t const crypto_provide = crypto_table[(flags >> 3) & 0x03];

	// Connect TCP socket.
	error_code ec;
	tcp::socket s = connect_to_session(g_fz.ios, g_fz.listen_port, ec);
	if (ec) return 0;

	// Generate a fresh DH key pair for this connection.
	dh_key_exchange dh;
	std::array<char, 96> const local_dh_key = export_key(dh.get_local_key());

	// Step 1: send our DH public key + PadA.
	// PadA bytes come from fuzz input so libFuzzer can explore the sync-hash
	// scanning loop (the server scans at most 512 bytes for the sync hash).
	int const actual_pad_a = std::min(pad_a_size, static_cast<int>(size));
	{
		std::array<boost::asio::const_buffer, 2> const bufs{{boost::asio::buffer(local_dh_key),
			boost::asio::buffer(data, static_cast<std::size_t>(actual_pad_a))}};
		error_code wec;
		boost::asio::write(s, bufs, wec);
	}
	data += static_cast<std::size_t>(actual_pad_a);
	size -= static_cast<std::size_t>(actual_pad_a);

	// Step 2: read the server's DH public key (first 96 bytes).
	// The server may send more bytes (PadB) but we only need the key.
	std::array<char, 96> srv_key{};
	{
		lt::error_code const rec = read_with_timeout(g_fz.ios, s, boost::asio::buffer(srv_key));
		if (rec)
		{
			s.close(ec);
			return wait_for_disconnect(*g_fz.ses);
		}
	}

	// Compute DH shared secret and derive RC4 keys.
	dh.compute_secret(reinterpret_cast<std::uint8_t const*>(srv_key.data()));
	lt::aux::key_t const secret = dh.get_secret();
	std::array<char, 96> const secret_buf = export_key(secret);

	// Use the v1 info hash as SKEY. The server's for_each loop tries both the
	// v1 SHA-1 hash and the first 20 bytes of the v2 SHA-256 hash.
	sha1_hash const& skey = g_fz.info_hash.v1;
	auto const rc4 = make_rc4(secret, skey);

	// Step 3: compute sync hash and obfuscated SKEY hash.

	// sync hash = hash('req1', S)
	static const char req1[4] = {'r', 'e', 'q', '1'};
	sha1_hash const sync_hash =
		hasher(req1, 4).update(secret_buf.data(), static_cast<int>(secret_buf.size())).final();

	// Obfuscated SKEY hash sent to server = hash('req2', SKEY) XOR hash('req3', S).
	// dh.get_hash_xor_mask() returns hash('req3', S) as computed in compute_secret(),
	// which is exactly what the server uses to de-obfuscate and find the torrent.
	static const char req2[4] = {'r', 'e', 'q', '2'};
	sha1_hash const skey_field =
		hasher(req2, 4).update(skey.data(), static_cast<int>(skey.size())).final()
		^ dh.get_hash_xor_mask();

	// Build the RC4-encrypted block:
	// VC(8) + crypto_provide(4) + len_pad_c(2) + PadC(len_pad_c) + len_ia(2) + IA(len_ia)
	std::vector<char> enc;

	// VC: 8 zero bytes; optionally corrupt the first byte to trigger
	// invalid_encryption_constant on the server side.
	for (int i = 0; i < 8; ++i)
		enc.push_back((i == 0 && (flags & 0x04)) ? char(0x01) : char(0));

	// crypto_provide (big-endian 32-bit)
	enc.push_back(char(0));
	enc.push_back(char(0));
	enc.push_back(char(0));
	enc.push_back(char(crypto_provide));

	// len_pad_c (big-endian 16-bit)
	enc.push_back(char(len_pad_c >> 8));
	enc.push_back(char(len_pad_c & 0xff));

	// PadC: len_pad_c zero bytes
	enc.insert(enc.end(), static_cast<std::size_t>(len_pad_c), char(0));

	// len_ia (big-endian 16-bit)
	enc.push_back(char(len_ia >> 8));
	enc.push_back(char(len_ia & 0xff));

	// IA content: use remaining fuzz bytes, zero-pad if short
	int const ia_from_data = std::min(len_ia, static_cast<int>(size));
	enc.insert(enc.end(), data, data + ia_from_data);
	enc.insert(enc.end(), static_cast<std::size_t>(len_ia - ia_from_data), char(0));

	// Encrypt the block in-place.
	span<char> enc_span(enc.data(), static_cast<std::ptrdiff_t>(enc.size()));
	rc4->encrypt(enc_span);

	// Assemble the final PE message.
	std::vector<char> pe_msg;

	// sync hash: send garbage to trigger sync_hash_not_found, else the real hash.
	if (flags & 0x01)
		pe_msg.insert(pe_msg.end(), 20, char(0xA5));
	else
		pe_msg.insert(pe_msg.end(), sync_hash.begin(), sync_hash.end());

	// Obfuscated SKEY hash: send garbage to trigger invalid_info_hash.
	if (flags & 0x02)
		pe_msg.insert(pe_msg.end(), 20, char(0x5A));
	else
		pe_msg.insert(pe_msg.end(), skey_field.begin(), skey_field.end());

	// Encrypted block (VC + crypto_provide + padding + IA).
	pe_msg.insert(pe_msg.end(), enc.begin(), enc.end());

	error_code wec;
	boost::asio::write(s, boost::asio::buffer(pe_msg), wec);
	s.close(wec);

	return wait_for_disconnect(*g_fz.ses);
#endif // TORRENT_DISABLE_ENCRYPTION
}
