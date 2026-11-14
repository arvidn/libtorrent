/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

// ed25519 primitive fuzzer.
//
// Drives the raw ed25519 primitives exported from src/kademlia/ed25519.cpp
// (keypair generation, signing, verification, scalar addition, key exchange)
// with fully attacker-controlled keys, signatures and messages.
//
// libtorrent consumes these primitives on the wire: DHT mutable-item puts
// and replies verify `sig`/`v` pairs against a 32-byte public key taken
// straight from the (untrusted) DHT message, so malformed points, non-canonical
// scalars and crafted signatures reach this arithmetic. The dht_put_item_request
// and dht_get_item_reply fuzzers exercise the item layer with one fixed
// keypair; this fuzzer complements them by pointing arbitrary public keys,
// signatures and scalars straight at the primitive layer, which is what
// reaches the point-decoding and scalar-reduction rejection paths
// (ge_frombytes / sc_reduce / sc_muladd edge cases).
//
// Corpus wire format:
//   byte 0: mode (mod 8)
//     0: keypair(seed); sign(pk, sk, msg); verify(sig, msg, pk)
//     1: keypair(seed); verify(fuzzed sig, msg, pk)     - arbitrary signature
//     2: verify(fuzzed sig, msg, fuzzed pk)             - arbitrary pk + sig
//     3: keypair(seed); add_scalar(pk out, nullptr, scalar)
//     4: keypair(seed); add_scalar(nullptr, sk out, scalar)
//     5: keypair(seed); key_exchange(shared, fuzzed pk, sk)
//     6: sign(fuzzed pk, seed-derived sk, msg)          - sign with foreign pk
//     7: chain: keypair; sign; verify; add_scalar(pk); add_scalar(sk);
//        key_exchange
//   bytes 1..32:   seed      (zero-padded when short)
//   bytes 33..64:  pk        (zero-padded)
//   bytes 65..128: signature (zero-padded)
//   bytes 129..160: scalar   (zero-padded)
//   remaining bytes: message (may be empty)

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "libtorrent/aux_/ed25519.hpp"

namespace {

constexpr std::size_t key_len = 32;   // public key / seed / scalar
constexpr std::size_t sig_len = 64;   // signature
constexpr std::size_t priv_len = 64;  // private key

// copies the next `len` bytes of the input into out (which is zero-filled
// first), advancing the cursor; zero-pads when the input is exhausted
template <std::size_t N>
void take(std::array<std::uint8_t, N>& out, std::uint8_t const*& cursor
	, std::uint8_t const* end)
{
	out.fill(0);
	std::size_t const n = std::min<std::size_t>(N, std::size_t(end - cursor));
	std::memcpy(out.data(), cursor, n);
	cursor += n;
}

} // anonymous namespace

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, std::size_t size)
{
	if (size == 0) return 0;
	int const mode = data[0] % 8;
	std::uint8_t const* cursor = data + 1;
	std::uint8_t const* const end = data + size;

	std::array<std::uint8_t, key_len> seed;
	take(seed, cursor, end);
	std::array<std::uint8_t, key_len> pk_fuzz;
	take(pk_fuzz, cursor, end);
	std::array<std::uint8_t, sig_len> sig_fuzz;
	take(sig_fuzz, cursor, end);
	std::array<std::uint8_t, key_len> scalar;
	take(scalar, cursor, end);
	// the primitives hash the message via hasher512::update(), which has a
	// documented precondition of non-empty input (TORRENT_ASSERT(data.size()
	// > 0)); libtorrent's own callers never sign or verify empty buffers
	std::vector<std::uint8_t> message(cursor, end);
	if (message.empty()) message.push_back(0);

	std::array<unsigned char, key_len> pk{};
	std::array<unsigned char, priv_len> sk{};
	std::array<unsigned char, sig_len> sig{};
	std::array<unsigned char, key_len> shared{};
	std::array<unsigned char, key_len> pk_out{};
	std::array<unsigned char, priv_len> sk_out{};

	libtorrent::aux::ed25519_create_keypair(pk.data(), sk.data(), seed.data());

	int verify_result = -1;

	switch (mode)
	{
		case 0:
			libtorrent::aux::ed25519_sign(sig.data(), message.data()
				, std::ptrdiff_t(message.size()), pk.data(), sk.data());
			verify_result = libtorrent::aux::ed25519_verify(sig.data(), message.data()
				, std::ptrdiff_t(message.size()), pk.data());
			break;
		case 1:
			verify_result = libtorrent::aux::ed25519_verify(sig_fuzz.data(), message.data()
				, std::ptrdiff_t(message.size()), pk.data());
			break;
		case 2:
			verify_result = libtorrent::aux::ed25519_verify(sig_fuzz.data(), message.data()
				, std::ptrdiff_t(message.size()), pk_fuzz.data());
			break;
		case 3:
			libtorrent::aux::ed25519_add_scalar(pk_out.data(), nullptr, scalar.data());
			break;
		case 4:
			libtorrent::aux::ed25519_add_scalar(nullptr, sk_out.data(), scalar.data());
			break;
		case 5:
			libtorrent::aux::ed25519_key_exchange(shared.data(), pk_fuzz.data(), sk.data());
			break;
		case 6:
			libtorrent::aux::ed25519_sign(sig.data(), message.data()
				, std::ptrdiff_t(message.size()), pk_fuzz.data(), sk.data());
			break;
		default:
			libtorrent::aux::ed25519_sign(sig.data(), message.data()
				, std::ptrdiff_t(message.size()), pk.data(), sk.data());
			verify_result = libtorrent::aux::ed25519_verify(sig.data(), message.data()
				, std::ptrdiff_t(message.size()), pk.data());
			libtorrent::aux::ed25519_add_scalar(pk_out.data(), nullptr, scalar.data());
			libtorrent::aux::ed25519_add_scalar(nullptr, sk_out.data(), scalar.data());
			libtorrent::aux::ed25519_key_exchange(shared.data(), pk_out.data(), sk_out.data());
			break;
	}

	// keep the results observable so the operations cannot be optimized away
	volatile int const sink = verify_result + int(sig[0]) + int(shared[0])
		+ int(pk_out[0]) + int(sk_out[0]);
	(void)sink;
	return 0;
}
