/*

Copyright (c) 2007, Un Shyam
Copyright (c) 2007, 2011, 2013, 2015-2021, Arvid Norberg
Copyright (c) 2016, 2018, 2020-2021, Alden Torres
Copyright (c) 2018, Steven Siloti
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "libtorrent/hasher.hpp"
#include "libtorrent/hex.hpp"
#include "libtorrent/aux_/pe_crypto.hpp"
#include "libtorrent/aux_/random.hpp"
#include "libtorrent/span.hpp"

#include "test.hpp"

#if !defined TORRENT_DISABLE_ENCRYPTION

namespace {

void test_enc_handler(lt::crypto_plugin& a, lt::crypto_plugin& b)
{
	int const repcount = 128;
	for (int rep = 0; rep < repcount; ++rep)
	{
		std::ptrdiff_t const buf_len = lt::aux::random(512 * 1024);
		std::vector<char> buf(static_cast<std::size_t>(buf_len));
		std::vector<char> cmp_buf(static_cast<std::size_t>(buf_len));

		lt::aux::random_bytes(buf);
		std::copy(buf.begin(), buf.end(), cmp_buf.begin());

		using namespace lt::aux;

		{
			lt::span<char> iovec(buf.data(), buf_len);
			auto const [next_barrier, iovec_out] = a.encrypt(iovec);
			TEST_CHECK(buf != cmp_buf);
			TEST_EQUAL(iovec_out.size(), 0);
			TEST_EQUAL(next_barrier, int(buf_len));
		}

		{
			lt::span<char> iovec(buf.data(), buf_len);
			auto const [consume, produce, packet_size] = b.decrypt(iovec);
			TEST_CHECK(buf == cmp_buf);
			TEST_EQUAL(consume, 0);
			TEST_EQUAL(produce, int(buf_len));
			TEST_EQUAL(packet_size, 0);
		}

		{
			lt::span<char> iovec(buf.data(), buf_len);
			auto const [next_barrier, iovec_out] = b.encrypt(iovec);
			TEST_EQUAL(iovec_out.size(), 0);
			TEST_CHECK(buf != cmp_buf);
			TEST_EQUAL(next_barrier, int(buf_len));

			lt::span<char> iovec2(buf.data(), buf_len);
			auto const [consume, produce, packet_size] = a.decrypt(iovec2);
			TEST_CHECK(buf == cmp_buf);
			TEST_EQUAL(consume, 0);
			TEST_EQUAL(produce, int(buf_len));
			TEST_EQUAL(packet_size, 0);
		}
	}
}

// splits buf into two encrypt() calls at "split" bytes instead of one, to
// exercise rc4_encrypt's scalar tail loop and the x/y state hand-off
// between separate encrypt() calls -- every RFC 6229 offset used below is a
// multiple of 8, so a single encrypt() call per checkpoint would only ever
// touch the batched 8-byte-at-a-time path
void encrypt_split(lt::aux::rc4_handler& rc4, lt::span<char> buf, int split)
{
	lt::span<char> iovec1 = buf.first(split);
	rc4.encrypt(iovec1);
	lt::span<char> iovec2 = buf.subspan(split);
	rc4.encrypt(iovec2);
}

} // anonymous namespace

TORRENT_TEST(diffie_hellman)
{
	using namespace lt;

	const int repcount = 128;

	for (int rep = 0; rep < repcount; ++rep)
	{
		aux::dh_key_exchange DH1, DH2;
		TEST_CHECK(DH1.good());
		TEST_CHECK(DH2.good());

		TEST_CHECK(
			DH1.compute_secret(reinterpret_cast<std::uint8_t const*>(DH2.get_local_key().data())));
		TEST_CHECK(
			DH2.compute_secret(reinterpret_cast<std::uint8_t const*>(DH1.get_local_key().data())));

		TEST_CHECK(DH1.get_secret() == DH2.get_secret());
		// guard against a backend that "succeeds" without writing anything:
		// two all-zero buffers would compare equal
		std::array<char, 96> const zero{};
		TEST_CHECK(DH1.get_secret() != zero);
		if (DH1.get_secret() != DH2.get_secret())
		{
			std::printf("DH1 local: %s\n", aux::to_hex(DH1.get_local_key()).c_str());
			std::printf("DH2 local: %s\n", aux::to_hex(DH2.get_local_key()).c_str());
			std::printf("DH1 shared_secret: %s\n", aux::to_hex(DH1.get_secret()).c_str());
			std::printf("DH2 shared_secret: %s\n", aux::to_hex(DH2.get_secret()).c_str());
		}
	}
}

TORRENT_TEST(diffie_hellman_degenerate_key)
{
	// MODP DH prime (BEP 8) used by dh_key_exchange, 96 bytes big-endian.
	// The generator is 2.
	char const dh_prime_hex[] =
		"FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD129024E088A67CC74020"
		"BBEA63B139B22514A08798E3404DDEF9519B3CD3A431B302B0A6DF25F14374FE1356D"
		"6D51C245E485B576625E7EC6F44C42E9A63A36210000000000090563";

	std::array<std::uint8_t, 96> prime{};
	TEST_CHECK(lt::aux::from_hex(
		{dh_prime_hex, int(sizeof(dh_prime_hex)) - 1}, reinterpret_cast<char*>(prime.data())));

	// the prime ends in 0x63, so nearby values can be formed by patching
	// the last byte
	auto const patched = [&prime](std::uint8_t const last) {
		std::array<std::uint8_t, 96> ret = prime;
		ret[95] = last;
		return ret;
	};

	std::array<std::uint8_t, 96> key{};

	// public keys outside [2, p-2] are degenerate and must be rejected.
	// Otherwise an attacker can fix the shared secret to a small set of
	// known values, defeating the encryption.
	{
		lt::aux::dh_key_exchange dh;
		TEST_CHECK(!dh.compute_secret(key.data())); // 0
		// a degenerate peer key is the peer's fault, not a local failure
		TEST_CHECK(dh.good());
	}
	{
		lt::aux::dh_key_exchange dh;
		key[95] = 1;
		TEST_CHECK(!dh.compute_secret(key.data()));
		TEST_CHECK(dh.good());
	}
	{
		lt::aux::dh_key_exchange dh;
		auto const p_minus_1 = patched(0x62);
		TEST_CHECK(!dh.compute_secret(p_minus_1.data()));
		TEST_CHECK(dh.good());
	}
	{
		lt::aux::dh_key_exchange dh;
		TEST_CHECK(!dh.compute_secret(prime.data()));
		TEST_CHECK(dh.good());
	}
	{
		// p+1 is congruent to 1, so accepting it would fix the shared
		// secret to 1 regardless of our secret exponent
		lt::aux::dh_key_exchange dh;
		auto const p_plus_1 = patched(0x64);
		TEST_CHECK(!dh.compute_secret(p_plus_1.data()));
		TEST_CHECK(dh.good());
	}

	// boundary values inside the valid range must be accepted.
	{
		lt::aux::dh_key_exchange dh;
		key[95] = 2;
		TEST_CHECK(dh.compute_secret(key.data()));
	}
	{
		lt::aux::dh_key_exchange dh;
		auto const p_minus_2 = patched(0x61);
		TEST_CHECK(dh.compute_secret(p_minus_2.data()));
	}
}

// a rejected peer key must not leave the previous exchange's shared secret
// readable through the accessors
TORRENT_TEST(diffie_hellman_failure_clears_secret)
{
	lt::aux::dh_key_exchange dh, peer;
	TEST_CHECK(dh.good());
	TEST_CHECK(peer.good());

	TEST_CHECK(
		dh.compute_secret(reinterpret_cast<std::uint8_t const*>(peer.get_local_key().data())));

	std::array<char, 96> const zero{};
	TEST_CHECK(dh.get_secret() != zero);
	TEST_CHECK(!dh.get_hash_xor_mask().is_all_zeros());

	// 1 is degenerate, so this exchange must fail -- and must not leave the
	// secret from the successful exchange above readable
	std::array<std::uint8_t, 96> degenerate{};
	degenerate[95] = 1;
	TEST_CHECK(!dh.compute_secret(degenerate.data()));
	// a degenerate peer key is the peer's fault, so the object is still good
	TEST_CHECK(dh.good());
	TEST_CHECK(dh.get_secret() == zero);
	TEST_CHECK(dh.get_hash_xor_mask().is_all_zeros());
}

// the xor mask must be hash('req3', S) over exactly what get_secret()
// returns. This pins the 'req3' label and the identity of the hashed
// buffer. It pins neither the width, the endianness, nor the padding of
// that buffer, since both sides of the comparison would change together
TORRENT_TEST(diffie_hellman_xor_mask)
{
	static char const req3[4] = {'r', 'e', 'q', '3'};

	lt::aux::dh_key_exchange dh, peer;
	TEST_CHECK(dh.good());
	TEST_CHECK(peer.good());
	if (!dh.good() || !peer.good())
		return;

	TEST_CHECK(
		dh.compute_secret(reinterpret_cast<std::uint8_t const*>(peer.get_local_key().data())));

	TEST_CHECK(dh.get_hash_xor_mask() == lt::hasher(req3).update(dh.get_secret()).final());
}

TORRENT_TEST(rc4)
{
	using namespace lt;

	sha1_hash test1_key = hasher("test1_key",8).final();
	sha1_hash test2_key = hasher("test2_key",8).final();

	std::printf("testing RC4 handler\n");
	aux::rc4_handler rc41;
	rc41.set_incoming_key(test2_key);
	rc41.set_outgoing_key(test1_key);
	aux::rc4_handler rc42;
	rc42.set_incoming_key(test1_key);
	rc42.set_outgoing_key(test2_key);
	test_enc_handler(rc41, rc42);
}

// RC4 keystream test vectors from RFC 6229, "Test Vectors for the Stream
// Cipher RC4". rc4_handler always discards the first 1024 bytes of keystream
// before producing any output, so the RFC's offset-1024 block is exactly what
// encrypt() returns right after set_outgoing_key().
// https://datatracker.ietf.org/doc/html/rfc6229#page-4
TORRENT_TEST(rc4_rfc6229_vectors)
{
	using namespace lt;

	struct rc4_vector
	{
		char const* key_hex;
		char const* at_1024_hex;
		char const* at_4096_hex;
	};

	rc4_vector const vectors[] = {
		// 40-bit key
		{"0102030405", "30abbcc7c20b01609f23ee2d5f6bb7df", "ff25b58995996707e51fbdf08b34d875"},
		// 128-bit key
		{"0102030405060708090a0b0c0d0e0f10",
			"bdf0324e6083dcc6d3cedd3ca8c53c16",
			"a36a4c301ae8ac13610ccbc12256cacc"},
		// 256-bit key
		{"0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20",
			"7fec5bfd9f9b89ce6548309092d7e958",
			"f3e4c0a2e02d1d01f7f0a74618af2b48"},
	};

	for (auto const& v : vectors)
	{
		int const keylen = int(std::strlen(v.key_hex)) / 2;
		std::printf("testing RC4 RFC 6229 vector, %d-bit key\n", keylen * 8);

		std::array<char, 32> key{};
		TEST_CHECK(aux::from_hex({v.key_hex, int(std::strlen(v.key_hex))}, key.data()));

		std::array<char, 16> expected_1024{};
		TEST_CHECK(
			aux::from_hex({v.at_1024_hex, int(std::strlen(v.at_1024_hex))}, expected_1024.data()));

		std::array<char, 16> expected_4096{};
		TEST_CHECK(
			aux::from_hex({v.at_4096_hex, int(std::strlen(v.at_4096_hex))}, expected_4096.data()));

		// exercise every possible way the 16 bytes at each checkpoint can
		// be split across two encrypt() calls (0 means "no split" and 16
		// means "everything in the first call"), so the scalar tail loop
		// and the batch/tail state hand-off get checked against the RFC's
		// known-correct bytes at every phase, not just against each other.
		// each split starts from a fresh key so the trials are independent
		for (int split = 0; split <= 16; ++split)
		{
			aux::rc4_handler rc4;
			rc4.set_outgoing_key({key.data(), keylen});

			std::array<char, 16> buf{};
			encrypt_split(rc4, {buf.data(), int(buf.size())}, split);
			TEST_CHECK(buf == expected_1024);
		}

		for (int split = 0; split <= 16; ++split)
		{
			aux::rc4_handler rc4;
			rc4.set_outgoing_key({key.data(), keylen});

			// skip ahead to offset 4096 (the constructor already discarded
			// the first 1024 bytes)
			std::vector<char> discard(4096 - 1024, 0);
			span<char> discard_iovec(discard.data(), int(discard.size()));
			rc4.encrypt(discard_iovec);

			std::array<char, 16> buf{};
			encrypt_split(rc4, {buf.data(), int(buf.size())}, split);
			TEST_CHECK(buf == expected_4096);
		}
	}
}

#else
TORRENT_TEST(disabled)
{
	std::printf("PE test not run because it's disabled\n");
}
#endif
