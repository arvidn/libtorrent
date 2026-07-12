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
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>

#include "libtorrent/hasher.hpp"
#include "libtorrent/aux_/pe_crypto.hpp"
#include "libtorrent/aux_/random.hpp"
#include "libtorrent/aux_/receive_buffer.hpp"
#include "libtorrent/settings_pack.hpp"
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

// Transfer test through encryption_handler.
// Simulates data flowing through encryption_handler on the send side
// and matching encryption_handler on the recv side — the full pipeline
// bt_peer_connection uses for encrypting and decrypting streams.
template <typename Handler>
void test_enc_handler_transfer(
	lt::span<char const> send_outgoing_key, lt::span<char const> recv_incoming_key)
{
	using namespace lt::aux;

	int const repcount = 64;
	for (int rep = 0; rep < repcount; ++rep)
	{
		std::ptrdiff_t const buf_len = lt::aux::random(256 * 1024);
		std::vector<char> buf(static_cast<std::size_t>(buf_len));
		lt::aux::random_bytes(buf);
		std::vector<char> const original(buf);

		auto send_plugin = std::make_shared<Handler>();
		send_plugin->set_outgoing_key(send_outgoing_key);
		auto recv_plugin = std::make_shared<Handler>();
		recv_plugin->set_incoming_key(recv_incoming_key);

		// --- send side: encryption_handler ---
		encryption_handler send_h;
		send_h.switch_send_crypto(send_plugin, INT_MAX);

		lt::span<char> iov = buf;
		lt::span<lt::span<char>> iovec(&iov, 1);
		auto const [next_barrier, out_iovec] = send_h.encrypt(iovec);
		TEST_CHECK(buf != original);
		TEST_EQUAL(out_iovec.size(), 0);
		TEST_EQUAL(next_barrier, int(buf_len));

		// --- recv side: encryption_handler ---
		encryption_handler recv_h;
		receive_buffer recv_buf;
		crypto_receive_buffer crypto_buf(recv_buf);

		// Receive data first, then switch crypto — matches the
		// bt_peer_connection flow where data arrives during handshake
		// before encryption is activated
		recv_buf.reset(int(buf_len));
		{
			auto space = recv_buf.reserve(int(buf_len));
			std::memcpy(space.data(), buf.data(), std::size_t(buf_len));
			recv_buf.received(int(buf_len));
			recv_buf.advance_pos(int(buf_len));
		}
		recv_h.switch_recv_crypto(recv_plugin, crypto_buf);

		// The handshake leftover bytes need to be decrypted.
		// Pass the number of already-received bytes to decrypt.
		{
			std::size_t bt = std::size_t(buf_len);
			int const consumed = recv_h.decrypt(crypto_buf, bt);
			TORRENT_UNUSED(consumed);
		}

		// Verify roundtrip — decrypted data is in receive_buffer
		auto const decrypted = recv_buf.get();
		TEST_EQUAL(int(decrypted.size()), int(buf_len));
		TEST_CHECK(std::memcmp(decrypted.data(), original.data(), std::size_t(buf_len)) == 0);
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

TORRENT_TEST(rc4_transfer)
{
	using namespace lt;

	sha1_hash const test1_key = hasher("test1_key", 8).final();
	sha1_hash const test2_key = hasher("test2_key", 8).final();

	std::printf("testing RC4 handler transfer\n");
	// send side encrypts with test1_key, recv side decrypts with test1_key
	lt::span<char const> test1(test1_key.data(), 20);
	test_enc_handler_transfer<aux::rc4_handler>(test1, test1);
}

#if TORRENT_HAS_MSE_AES_CTR
TORRENT_TEST(aes_ctr)
{
	using namespace lt;

	sha1_hash test1_key = hasher("test1_key", 8).final();
	sha1_hash test2_key = hasher("test2_key", 8).final();

	std::printf("testing AES-CTR handler\n");
	aux::aes_ctr_handler h1;
	h1.set_incoming_key(test2_key);
	h1.set_outgoing_key(test1_key);
	aux::aes_ctr_handler h2;
	h2.set_incoming_key(test1_key);
	h2.set_outgoing_key(test2_key);
	test_enc_handler(h1, h2);
}

TORRENT_TEST(aes_ctr_transfer)
{
	using namespace lt;

	sha1_hash const test1_key = hasher("test1_key", 8).final();
	sha1_hash const test2_key = hasher("test2_key", 8).final();

	std::printf("testing AES-CTR handler transfer\n");
	// send side encrypts with test1_key, recv side decrypts with test1_key
	lt::span<char const> test1(test1_key.data(), 20);
	test_enc_handler_transfer<aux::aes_ctr_handler>(test1, test1);
}
#endif

// --- crypto negotiation: verify that a peer supporting both RC4 and
// AES-CTR negotiates correctly with peers that support fewer options ---

namespace {

	// Replicates the crypto-select logic from bt_peer_connection,
	// with the fix: when prefer_aes_ctr is set but AES-CTR is not available,
	// prefer RC4 over plaintext (rather than falling to plaintext).
	// crypto_provide = what the remote peer offers
	// allowed = local allowed_enc_level setting
	// prefer_aes_ctr / prefer_rc4 = local preference settings
	std::uint32_t negotiate_crypto(
		std::uint32_t crypto_provide, int allowed, bool prefer_aes_ctr, bool prefer_rc4)
	{
		std::uint32_t crypto_select = crypto_provide & std::uint32_t(allowed);

#if TORRENT_HAS_MSE_AES_CTR
		if (prefer_aes_ctr && (crypto_select & lt::settings_pack::pe_aes_ctr))
			crypto_select = lt::settings_pack::pe_aes_ctr;
		else
#endif
			if ((prefer_aes_ctr || prefer_rc4) && (crypto_select & lt::settings_pack::pe_rc4))
			crypto_select = lt::settings_pack::pe_rc4;
		else
		{
			// default: keep the least significant set bit
			std::uint32_t mask = std::numeric_limits<std::uint32_t>::max();
			while (crypto_select & (mask >> 1))
			{
				mask >>= 1;
				crypto_select = crypto_select & mask;
			}
		}
		return crypto_select;
	}

} // anonymous namespace

TORRENT_TEST(crypto_negotiation)
{
	using namespace lt;

	std::uint32_t const plain = settings_pack::pe_plaintext;
	std::uint32_t const rc4 = settings_pack::pe_rc4;
#if TORRENT_HAS_MSE_AES_CTR
	std::uint32_t const aes = settings_pack::pe_aes_ctr;
	int const all_three = int(plain | rc4 | aes);
#else
	int const all_three = int(plain | rc4);
#endif

	// Both support all three, AES-CTR preferred
	{
		std::uint32_t const sel = negotiate_crypto(all_three, all_three, true, false);
#if TORRENT_HAS_MSE_AES_CTR
		TEST_EQUAL(sel, aes);
#else
		TEST_EQUAL(sel, rc4);
#endif
	}

	// Both support all three, RC4 explicitly preferred → RC4
	{
		std::uint32_t const sel = negotiate_crypto(all_three, all_three, false, true);
		TEST_EQUAL(sel, rc4);
	}

#if TORRENT_HAS_MSE_AES_CTR
	// RC4+AES peer receives offer from RC4-only peer → negotiate RC4
	// (AES-CTR bit (4) not in intersection of 3 & 7 = 3; prefer_aes_ctr
	//  ensures we still pick RC4 over plaintext)
	{
		std::uint32_t const sel =
			negotiate_crypto(plain | rc4, int(plain | rc4 | aes), true, false);
		TEST_EQUAL(sel, rc4);
	}
#endif

	// RC4+AES peer offers, RC4-only peer selects with prefer_rc4 → negotiate RC4
	{
		std::uint32_t const sel = negotiate_crypto(all_three, int(plain | rc4), false, true);
		TEST_EQUAL(sel, rc4);
	}

	// Both support all three, no preference → least significant bit = plaintext
	{
		std::uint32_t const sel = negotiate_crypto(all_three, all_three, false, false);
		TEST_EQUAL(sel, plain);
	}

	// AES-CTR+R4 peer receives offer from plaintext-only peer → plaintext
	{
		std::uint32_t const sel = negotiate_crypto(plain, all_three, true, false);
		TEST_EQUAL(sel, plain);
	}
}

#else
TORRENT_TEST(disabled)
{
	std::printf("PE test not run because it's disabled\n");
}
#endif
