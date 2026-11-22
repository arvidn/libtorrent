/*

Copyright (c) 2026, Arthur SC Chan
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
//   bytes 1..32:   seed
//   bytes 33..64:  pk
//   bytes 65..128: signature
//   bytes 129..160: scalar
//   remaining bytes: message (may be empty)
// Inputs shorter than 161 bytes are rejected.

#include <array>
#include <cstdint>

#include "libtorrent/aux_/ed25519.hpp"
#include "libtorrent/span.hpp"
#include "dht_reply_session.hpp"

using namespace lt;

namespace {

constexpr std::size_t key_len = 32; // public key / seed / scalar
constexpr std::size_t sig_len = 64; // signature
constexpr std::size_t priv_len = 64; // private key
constexpr std::size_t header_len = 1 + key_len + key_len + sig_len + key_len;

} // anonymous namespace

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, std::size_t size)
{
	// ed25519 always reads full fixed-width fields
	if (size < header_len)
		return -1;
	span<std::uint8_t const> in(data, static_cast<std::ptrdiff_t>(size));
	int const mode = in[0] % 8;
	in = in.subspan(1);

	auto const seed = dht_fuzz::take(in, std::ptrdiff_t(key_len));
	auto const pk_fuzz = dht_fuzz::take(in, std::ptrdiff_t(key_len));
	auto const sig_fuzz = dht_fuzz::take(in, std::ptrdiff_t(sig_len));
	auto const scalar = dht_fuzz::take(in, std::ptrdiff_t(key_len));

	// remaining bytes are the message; hasher512::update() requires non-empty
	std::uint8_t empty_byte = 0;
	span<std::uint8_t const> message = in;
	if (message.empty())
		message = {&empty_byte, 1};

	std::array<std::uint8_t, key_len> pk{};
	std::array<std::uint8_t, priv_len> sk{};
	std::array<std::uint8_t, sig_len> sig{};
	std::array<std::uint8_t, key_len> shared{};
	std::array<std::uint8_t, key_len> pk_out{};
	std::array<std::uint8_t, priv_len> sk_out{};

	aux::ed25519_create_keypair(pk.data(), sk.data(), seed.data());

	int verify_result = -1;

	switch (mode)
	{
		case 0:
			aux::ed25519_sign(sig.data(), message.data(), message.size(), pk.data(), sk.data());
			verify_result =
				aux::ed25519_verify(sig.data(), message.data(), message.size(), pk.data());
			break;
		case 1:
			verify_result =
				aux::ed25519_verify(sig_fuzz.data(), message.data(), message.size(), pk.data());
			break;
		case 2:
			verify_result = aux::ed25519_verify(
				sig_fuzz.data(), message.data(), message.size(), pk_fuzz.data());
			break;
		case 3:
			aux::ed25519_add_scalar(pk_out.data(), nullptr, scalar.data());
			break;
		case 4:
			aux::ed25519_add_scalar(nullptr, sk_out.data(), scalar.data());
			break;
		case 5:
			aux::ed25519_key_exchange(shared.data(), pk_fuzz.data(), sk.data());
			break;
		case 6:
			aux::ed25519_sign(
				sig.data(), message.data(), message.size(), pk_fuzz.data(), sk.data());
			break;
		default:
			aux::ed25519_sign(sig.data(), message.data(), message.size(), pk.data(), sk.data());
			verify_result =
				aux::ed25519_verify(sig.data(), message.data(), message.size(), pk.data());
			aux::ed25519_add_scalar(pk_out.data(), nullptr, scalar.data());
			aux::ed25519_add_scalar(nullptr, sk_out.data(), scalar.data());
			aux::ed25519_key_exchange(shared.data(), pk_out.data(), sk_out.data());
			break;
	}

	// keep the results observable so the operations cannot be optimized away
	volatile int const sink =
		verify_result + int(sig[0]) + int(shared[0]) + int(pk_out[0]) + int(sk_out[0]);
	(void)sink;
	return 0;
}
