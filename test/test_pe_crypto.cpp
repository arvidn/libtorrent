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
#include <iostream>

#include "libtorrent/hasher.hpp"
#include "libtorrent/aux_/pe_crypto.hpp"
#include "libtorrent/aux_/random.hpp"
#include "libtorrent/span.hpp"

#include "test.hpp"

#if !defined TORRENT_DISABLE_ENCRYPTION

namespace {

	// a from-scratch, textbook RC4 (KSA + PRGA) implementation, independent
	// of whichever backend aux::rc4_handler is built against. It exists
	// purely to catch a change to rc4_handler's keystream that would break
	// interop with peers running a differently-configured libtorrent build.
	struct reference_rc4_state
	{
		std::array<std::uint8_t, 256> s;
		std::uint8_t i = 0;
		std::uint8_t j = 0;
	};

	void reference_rc4_init(
		unsigned char const* key, std::size_t keylen, reference_rc4_state& state)
	{
		for (int n = 0; n < 256; ++n)
			state.s[std::size_t(n)] = std::uint8_t(n);

		std::uint8_t j = 0;
		for (int n = 0; n < 256; ++n)
		{
			j = std::uint8_t(j + state.s[std::size_t(n)] + key[std::size_t(n) % keylen]);
			std::swap(state.s[std::size_t(n)], state.s[j]);
		}
	}

	void reference_rc4_encrypt(unsigned char* out, std::size_t len, reference_rc4_state& state)
	{
		while (len--)
		{
			state.i = std::uint8_t(state.i + 1);
			state.j = std::uint8_t(state.j + state.s[state.i]);
			std::swap(state.s[state.i], state.s[state.j]);
			*out++ ^= state.s[std::uint8_t(state.s[state.i] + state.s[state.j])];
		}
	}

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

} // anonymous namespace

TORRENT_TEST(diffie_hellman)
{
	using namespace lt;

	const int repcount = 128;

	for (int rep = 0; rep < repcount; ++rep)
	{
		aux::dh_key_exchange DH1, DH2;

		TEST_CHECK(DH1.compute_secret(DH2.get_local_key()));
		TEST_CHECK(DH2.compute_secret(DH1.get_local_key()));

		TEST_EQUAL(DH1.get_secret(), DH2.get_secret());
		if (!DH1.get_secret() != DH2.get_secret())
		{
			std::printf("DH1 local: ");
			std::cout << DH1.get_local_key() << std::endl;

			std::printf("DH2 local: ");
			std::cout << DH2.get_local_key() << std::endl;

			std::printf("DH1 shared_secret: ");
			std::cout << DH1.get_secret() << std::endl;

			std::printf("DH2 shared_secret: ");
			std::cout << DH2.get_secret() << std::endl;
		}
	}
}

TORRENT_TEST(diffie_hellman_degenerate_key)
{
	// MODP DH prime (BEP 8) used by dh_key_exchange. The generator is 2.
	lt::aux::key_t const dh_prime(
		"0xFFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD129024E088A67CC74020"
		"BBEA63B139B22514A08798E3404DDEF9519B3CD3A431B302B0A6DF25F14374FE1356D"
		"6D51C245E485B576625E7EC6F44C42E9A63A36210000000000090563");

	// public keys outside [2, p-2] are degenerate and must be rejected.
	// Otherwise an attacker can fix the shared secret to a small set of
	// known values, defeating the encryption.
	{
		lt::aux::dh_key_exchange dh;
		TEST_CHECK(!dh.compute_secret(lt::aux::key_t(0)));
	}
	{
		lt::aux::dh_key_exchange dh;
		TEST_CHECK(!dh.compute_secret(lt::aux::key_t(1)));
	}
	{
		lt::aux::dh_key_exchange dh;
		TEST_CHECK(!dh.compute_secret(dh_prime - 1));
	}
	{
		lt::aux::dh_key_exchange dh;
		TEST_CHECK(!dh.compute_secret(dh_prime));
	}

	// boundary values inside the valid range must be accepted.
	{
		lt::aux::dh_key_exchange dh;
		TEST_CHECK(dh.compute_secret(lt::aux::key_t(2)));
	}
	{
		lt::aux::dh_key_exchange dh;
		TEST_CHECK(dh.compute_secret(dh_prime - 2));
	}
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

// make sure rc4_handler produces the exact keystream we expect, regardless
// of which RC4 backend it's built against. See reference_rc4_init() /
// reference_rc4_encrypt() above.
TORRENT_TEST(rc4_known_keystream)
{
	using namespace lt;

	sha1_hash const key = hasher("rc4 test key", 12).final();

	reference_rc4_state ref_state;
	reference_rc4_init(
		reinterpret_cast<unsigned char const*>(key.data()), std::size_t(key.size()), ref_state);

	// rc4_handler discards the first 1024 bytes of keystream when a key is
	// set, to satisfy the MSE spec
	std::array<unsigned char, 1024> discard{};
	reference_rc4_encrypt(discard.data(), discard.size(), ref_state);

	std::array<unsigned char, 64> expected{};
	reference_rc4_encrypt(expected.data(), expected.size(), ref_state);

	aux::rc4_handler rc4;
	rc4.set_outgoing_key(key);

	std::array<char, 64> buf{};
	lt::span<char> iovec(buf.data(), int(buf.size()));
	auto const [next_barrier, iovec_out] = rc4.encrypt(iovec);
	TEST_EQUAL(next_barrier, int(buf.size()));
	TEST_EQUAL(iovec_out.size(), 0);

	for (std::size_t i = 0; i < buf.size(); ++i)
		TEST_EQUAL(int(std::uint8_t(buf[i])), int(expected[i]));
}

#else
TORRENT_TEST(disabled)
{
	std::printf("PE test not run because it's disabled\n");
}
#endif
