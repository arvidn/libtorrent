/*

Copyright (c) 2026, Arthur SC Chan
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

#include <array>
#include <cstdint>

#include "libtorrent/aux_/ed25519.hpp"
#include "libtorrent/span.hpp"
#include "dht_reply_session.hpp"

using namespace lt;

namespace {

constexpr std::size_t key_len = 32;   // public key / seed / scalar
constexpr std::size_t sig_len = 64;   // signature
constexpr std::size_t priv_len = 64;  // private key

// ed25519 always reads a full fixed-width field. Prefer a view into the
// fuzz input (no copy) when enough bytes remain; otherwise zero-pad via
// dht_fuzz::take_padded.
template <std::size_t N>
unsigned char const* take_field(span<std::uint8_t const>& in
	, std::array<char, N>& storage)
{
	if (in.size() >= static_cast<std::ptrdiff_t>(N))
	{
		span<std::uint8_t const> const chunk = dht_fuzz::take(in
			, static_cast<std::ptrdiff_t>(N));
		return reinterpret_cast<unsigned char const*>(chunk.data());
	}
	storage = dht_fuzz::take_padded<N>(in);
	return reinterpret_cast<unsigned char const*>(storage.data());
}

} // anonymous namespace

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, std::size_t size)
{
	span<std::uint8_t const> in(data, static_cast<std::ptrdiff_t>(size));
	if (in.empty()) return 0;
	int const mode = in[0] % 8;
	in = in.subspan(1);

	std::array<char, key_len> seed_storage;
	std::array<char, key_len> pk_fuzz_storage;
	std::array<char, sig_len> sig_fuzz_storage;
	std::array<char, key_len> scalar_storage;

	unsigned char const* const seed = take_field(in, seed_storage);
	unsigned char const* const pk_fuzz = take_field(in, pk_fuzz_storage);
	unsigned char const* const sig_fuzz = take_field(in, sig_fuzz_storage);
	unsigned char const* const scalar = take_field(in, scalar_storage);

	// remaining bytes are the message; hasher512::update() requires non-empty
	std::uint8_t empty_byte = 0;
	span<std::uint8_t const> message = in;
	if (message.empty()) message = {&empty_byte, 1};

	std::array<unsigned char, key_len> pk{};
	std::array<unsigned char, priv_len> sk{};
	std::array<unsigned char, sig_len> sig{};
	std::array<unsigned char, key_len> shared{};
	std::array<unsigned char, key_len> pk_out{};
	std::array<unsigned char, priv_len> sk_out{};

	aux::ed25519_create_keypair(pk.data(), sk.data(), seed);

	int verify_result = -1;

	switch (mode)
	{
		case 0:
			aux::ed25519_sign(sig.data(), message.data()
				, message.size(), pk.data(), sk.data());
			verify_result = aux::ed25519_verify(sig.data(), message.data()
				, message.size(), pk.data());
			break;
		case 1:
			verify_result = aux::ed25519_verify(sig_fuzz, message.data()
				, message.size(), pk.data());
			break;
		case 2:
			verify_result = aux::ed25519_verify(sig_fuzz, message.data()
				, message.size(), pk_fuzz);
			break;
		case 3:
			aux::ed25519_add_scalar(pk_out.data(), nullptr, scalar);
			break;
		case 4:
			aux::ed25519_add_scalar(nullptr, sk_out.data(), scalar);
			break;
		case 5:
			aux::ed25519_key_exchange(shared.data(), pk_fuzz, sk.data());
			break;
		case 6:
			aux::ed25519_sign(sig.data(), message.data()
				, message.size(), pk_fuzz, sk.data());
			break;
		default:
			aux::ed25519_sign(sig.data(), message.data()
				, message.size(), pk.data(), sk.data());
			verify_result = aux::ed25519_verify(sig.data(), message.data()
				, message.size(), pk.data());
			aux::ed25519_add_scalar(pk_out.data(), nullptr, scalar);
			aux::ed25519_add_scalar(nullptr, sk_out.data(), scalar);
			aux::ed25519_key_exchange(shared.data(), pk_out.data(), sk_out.data());
			break;
	}

	// keep the results observable so the operations cannot be optimized away
	volatile int const sink = verify_result + int(sig[0]) + int(shared[0])
		+ int(pk_out[0]) + int(sk_out[0]);
	(void)sink;
	return 0;
}
