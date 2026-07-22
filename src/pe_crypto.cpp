/*

Copyright (c) 2007, Un Shyam
Copyright (c) 2011, 2014-2016, 2018-2022, Arvid Norberg
Copyright (c) 2016-2018, 2021, Alden Torres
Copyright (c) 2016, Andrei Kurushin
Copyright (c) 2016, 2018, Steven Siloti
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#if !defined TORRENT_DISABLE_ENCRYPTION

#include <cstdint>
#include <cstring>
#include <algorithm>
#include <iterator>
#include <optional>
#include <utility>

#if defined TORRENT_USE_LIBCRYPTO && !defined TORRENT_USE_WOLFSSL
#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <openssl/bn.h>
#include <openssl/err.h>
#include "libtorrent/aux_/disable_warnings_pop.hpp"
#else
#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/multiprecision/integer.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"
#endif

#include "libtorrent/aux_/random.hpp"
#include "libtorrent/aux_/alloca.hpp"
#include "libtorrent/aux_/pe_crypto.hpp"
#include "libtorrent/aux_/scope_end.hpp"
#include "libtorrent/hasher.hpp"

namespace libtorrent::aux {

	namespace {
		// the prime P from the MSE spec, 768 bits big-endian. The
		// generator is 2
		// clang-format off
		unsigned char const dh_prime_bytes[96] = {
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xc9, 0x0f, 0xda, 0xa2, 0x21, 0x68, 0xc2, 0x34,
			0xc4, 0xc6, 0x62, 0x8b, 0x80, 0xdc, 0x1c, 0xd1,
			0x29, 0x02, 0x4e, 0x08, 0x8a, 0x67, 0xcc, 0x74,
			0x02, 0x0b, 0xbe, 0xa6, 0x3b, 0x13, 0x9b, 0x22,
			0x51, 0x4a, 0x08, 0x79, 0x8e, 0x34, 0x04, 0xdd,
			0xef, 0x95, 0x19, 0xb3, 0xcd, 0x3a, 0x43, 0x1b,
			0x30, 0x2b, 0x0a, 0x6d, 0xf2, 0x5f, 0x14, 0x37,
			0x4f, 0xe1, 0x35, 0x6d, 0x6d, 0x51, 0xc2, 0x45,
			0xe4, 0x85, 0xb5, 0x76, 0x62, 0x5e, 0x7e, 0xc6,
			0xf4, 0x4c, 0x42, 0xe9, 0xa6, 0x3a, 0x36, 0x21,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x05, 0x63
		};
		// clang-format on

		char const req3[4] = {'r', 'e', 'q', '3'};
	}

	void rc4_init(const unsigned char* in, std::size_t len, rc4* state);
	std::size_t rc4_encrypt(unsigned char* out, std::size_t outlen, rc4* state);

#if defined TORRENT_USE_LIBCRYPTO && !defined TORRENT_USE_WOLFSSL

	namespace {

		// per-thread libcrypto state: scratch space for BIGNUM math and
		// constants derived from the prime, set up once and reused by
		// every key exchange on the thread
		struct openssl_context
		{
			openssl_context() = default;
			~openssl_context()
			{
				BN_MONT_CTX_free(montgomery);
				BN_CTX_free(ctx);
				BN_free(prime_minus_1);
				BN_free(prime);
			}
			openssl_context(openssl_context const&) = delete;
			openssl_context& operator=(openssl_context const&) = delete;

			// these are initialized in declaration order, so prime_minus_1
			// sees an already-initialized prime, and valid sees all four
			BIGNUM* const prime = BN_bin2bn(dh_prime_bytes, int(sizeof(dh_prime_bytes)), nullptr);
			BIGNUM* const prime_minus_1 = prime != nullptr ? BN_dup(prime) : nullptr;
			BN_CTX* const ctx = BN_CTX_new();
			BN_MONT_CTX* const montgomery = BN_MONT_CTX_new();
			bool const valid = prime != nullptr && prime_minus_1 != nullptr && ctx != nullptr
				&& montgomery != nullptr && BN_sub_word(prime_minus_1, 1) == 1
				&& BN_MONT_CTX_set(montgomery, prime, ctx) == 1;
		};

		// export a BIGNUM as a 96 byte big-endian string, padded with
		// leading zeroes. On failure "out" is left zeroed rather than
		// holding a partial or stale value. The size check is what keeps
		// BN_bn2bin (which takes no destination length) inside "out"
		bool export_bignum(BIGNUM const* value, std::array<char, 96>& out)
		{
			out.fill(0);
			int const size = int(BN_num_bytes(value));
			// a zero value would export as an all-zero key, which is never
			// a valid result of the modular exponentiation we do here
			if (size <= 0 || size > int(out.size()))
				return false;
			if (int(BN_bn2bin(value,
					reinterpret_cast<unsigned char*>(out.data()) + out.size() - std::size_t(size)))
				== size)
				return true;

			// BN_bn2bin may already have written part of the value
			out.fill(0);
			return false;
		}

#if !defined BOOST_NO_CXX11_THREAD_LOCAL
		// the thread's cached libcrypto state, set up on first use. If
		// setting it up failed (out of memory), it is torn down and set up
		// again on the next call, rather than leaving the thread unable to
		// perform key exchanges forever. This is deliberately not a
		// template, so there is exactly one context per thread
		openssl_context const* thread_context()
		{
			static thread_local std::optional<openssl_context> storage;
			if (!storage.has_value())
				storage.emplace();
			if (!storage->valid)
			{
				// don't hold on to a half-built context until the next call
				storage.reset();
				return nullptr;
			}
			return &*storage;
		}
#endif

		// runs f with the thread's libcrypto state (or a fresh one, on
		// compilers without thread_local), leaving the error queue the way
		// we found it. Returns false if the state could not be set up or
		// if f fails
		template <typename F>
		bool with_openssl_context(F f)
		{
			ERR_set_mark();
			auto const restore_errors = aux::scope_end([] { ERR_pop_to_mark(); });
#if defined BOOST_NO_CXX11_THREAD_LOCAL
			openssl_context const storage;
			return storage.valid && f(storage);
#else
			openssl_context const* const storage = thread_context();
			return storage != nullptr && f(*storage);
#endif
		}
	}

	// Set the prime P and the generator, generate local public key
	dh_key_exchange::dh_key_exchange()
	{
		// create local secret (random)
		aux::random_bytes({reinterpret_cast<char*>(m_dh_local_secret.data()),
			static_cast<std::ptrdiff_t>(m_dh_local_secret.size())});

		m_good = with_openssl_context([&](openssl_context const& storage) {
			// every BIGNUM below is owned by the context's stack frame, and
			// is only valid until the matching BN_CTX_end()
			BN_CTX_start(storage.ctx);
			auto const frame = aux::scope_end([&] { BN_CTX_end(storage.ctx); });

			BIGNUM* const generator = BN_CTX_get(storage.ctx);
			BIGNUM* const exponent = BN_CTX_get(storage.ctx);
			BIGNUM* const key = BN_CTX_get(storage.ctx);
			if (generator == nullptr || exponent == nullptr || key == nullptr)
				return false;

			if (BN_set_word(generator, 2) != 1)
				return false;
			if (BN_bin2bn(m_dh_local_secret.data(), int(m_dh_local_secret.size()), exponent)
				== nullptr)
				return false;

			// key = (2 ^ secret) % prime
			if (BN_mod_exp_mont(
					key, generator, exponent, storage.prime, storage.ctx, storage.montgomery)
				!= 1)
				return false;

			return export_bignum(key, m_dh_local_key);
		});
	}

	// compute shared secret given remote public key
	bool dh_key_exchange::compute_secret(std::uint8_t const* remote_pubkey)
	{
		TORRENT_ASSERT(remote_pubkey);

		// every failure path below shares one post-condition: no previous
		// exchange's secret is left readable through the accessors
		m_dh_shared_secret.fill(0);
		m_xor_mask.clear();

		// a previous call failed locally, or the key pair was never
		// generated. Either way this object cannot produce a shared secret
		if (!m_good)
			return false;

		bool degenerate = false;
		bool const ok = with_openssl_context([&](openssl_context const& storage) {
			// every BIGNUM below is owned by the context's stack frame, and
			// is only valid until the matching BN_CTX_end()
			BN_CTX_start(storage.ctx);
			auto const frame = aux::scope_end([&] { BN_CTX_end(storage.ctx); });

			BIGNUM* const pubkey = BN_CTX_get(storage.ctx);
			BIGNUM* const exponent = BN_CTX_get(storage.ctx);
			BIGNUM* const secret = BN_CTX_get(storage.ctx);
			if (pubkey == nullptr || exponent == nullptr || secret == nullptr)
				return false;

			if (BN_bin2bn(remote_pubkey, 96, pubkey) == nullptr)
				return false;

			// reject degenerate public keys. Any value outside [2, p-2]
			// produces a shared secret in a small subgroup (0, 1, or +/-1),
			// which effectively defeats the key exchange and would allow a
			// man-in-the-middle to fix the shared secret. 0 and 1 are the
			// only values below 2. This is the peer's mistake, not a local
			// failure
			if (BN_is_zero(pubkey) || BN_is_one(pubkey)
				|| BN_ucmp(pubkey, storage.prime_minus_1) >= 0)
			{
				degenerate = true;
				return false;
			}

			if (BN_bin2bn(m_dh_local_secret.data(), int(m_dh_local_secret.size()), exponent)
				== nullptr)
				return false;

			// shared_secret = (remote_pubkey ^ local_secret) % prime
			if (BN_mod_exp_mont(
					secret, pubkey, exponent, storage.prime, storage.ctx, storage.montgomery)
				!= 1)
				return false;

			return export_bignum(secret, m_dh_shared_secret);
		});
		if (!ok)
		{
			// a failure other than the peer sending a degenerate key is a
			// local crypto failure. Record it so the caller doesn't blame
			// the peer
			if (!degenerate)
				m_good = false;
			return false;
		}

		// calculate the xor mask for the obfuscated hash
		m_xor_mask = hasher(req3).update(m_dh_shared_secret).final();
		return true;
	}

#else

	namespace {

		namespace mp = boost::multiprecision;
		using key_t =
			mp::number<mp::cpp_int_backend<768, 768, mp::unsigned_magnitude, mp::unchecked, void>>;

		key_t make_dh_prime()
		{
			key_t ret;
			mp::import_bits(ret, std::begin(dh_prime_bytes), std::end(dh_prime_bytes));
			return ret;
		}

		key_t const dh_prime = make_dh_prime();

		// export a bignum as a 96 byte big-endian string, padded with
		// leading zeroes
		void export_key(key_t const& value, std::array<char, 96>& out)
		{
			auto* const begin = reinterpret_cast<std::uint8_t*>(out.data());
			std::uint8_t* const end = mp::export_bits(value, begin, 8);

			if (end < begin + 96)
			{
				int const len = int(end - begin);
#if defined __GNUC__ && __GNUC__ == 12
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif
				std::memmove(begin + 96 - len, begin, aux::numeric_cast<std::size_t>(len));
#if defined __GNUC__ && __GNUC__ == 12
#pragma GCC diagnostic pop
#endif
				std::memset(begin, 0, aux::numeric_cast<std::size_t>(96 - len));
			}
		}
	}

	// Set the prime P and the generator, generate local public key
	dh_key_exchange::dh_key_exchange()
	{
		// create local secret (random)
		aux::random_bytes({reinterpret_cast<char*>(m_dh_local_secret.data()),
			static_cast<std::ptrdiff_t>(m_dh_local_secret.size())});

		key_t secret;
		mp::import_bits(secret, m_dh_local_secret.begin(), m_dh_local_secret.end());

		// key = (2 ^ secret) % prime
		export_key(mp::powm(key_t(2), secret, dh_prime), m_dh_local_key);

		// key_t is a fixed precision, unchecked integer with no allocator, so
		// none of the operations above can fail. m_good is never cleared in
		// this backend, unlike the libcrypto one
		m_good = true;
	}

	// compute shared secret given remote public key
	bool dh_key_exchange::compute_secret(std::uint8_t const* remote_pubkey)
	{
		TORRENT_ASSERT(remote_pubkey);

		key_t pubkey;
		mp::import_bits(pubkey, remote_pubkey, remote_pubkey + 96);

		// every failure path below shares one post-condition: no previous
		// exchange's secret is left readable through the accessors
		m_dh_shared_secret.fill(0);
		m_xor_mask.clear();

		// reject degenerate public keys. Any value outside [2, p-2] produces
		// a shared secret in a small subgroup (0, 1, or +/-1), which
		// effectively defeats the key exchange and would allow a
		// man-in-the-middle to fix the shared secret.
		if (pubkey < key_t(2) || pubkey >= dh_prime - 1)
			return false;

		key_t secret;
		mp::import_bits(secret, m_dh_local_secret.begin(), m_dh_local_secret.end());

		// shared_secret = (remote_pubkey ^ local_secret) % prime
		export_key(mp::powm(pubkey, secret, dh_prime), m_dh_shared_secret);

		// calculate the xor mask for the obfuscated hash
		m_xor_mask = hasher(req3).update(m_dh_shared_secret).final();
		return true;
	}

#endif // TORRENT_USE_LIBCRYPTO

	std::tuple<int, span<span<char const>>>
	encryption_handler::encrypt(
		span<span<char>> iovec)
	{
		TORRENT_ASSERT(!m_send_barriers.empty());
		TORRENT_ASSERT(m_send_barriers.front().enc_handler);

		int to_process = m_send_barriers.front().next;

		span<span<char>> bufs;
		bool need_destruct = false;
		if (to_process != INT_MAX)
		{
			TORRENT_ALLOCA(abufs, span<char>, iovec.size());
			bufs = abufs;
			need_destruct = true;
			int num_bufs = 0;
			for (int i = 0; to_process > 0 && i < iovec.size(); ++i)
			{
				++num_bufs;
				int const size = int(iovec[i].size());
				if (to_process < size)
				{
					new (&bufs[i]) span<char>(
						iovec[i].data(), to_process);
					to_process = 0;
				}
				else
				{
					new (&bufs[i]) span<char>(iovec[i]);
					to_process -= size;
				}
			}
			bufs = bufs.first(num_bufs);
		}
		else
		{
			bufs = iovec;
		}

		int next_barrier = 0;
		span<span<char const>> out_iovec;
		if (!bufs.empty())
		{
			std::tie(next_barrier, out_iovec)
				= m_send_barriers.front().enc_handler->encrypt(bufs);
		}

		if (m_send_barriers.front().next != INT_MAX)
		{
			// to_process holds the difference between the size of the buffers
			// and the bytes left to the next barrier
			// if it's zero then pop the barrier
			// otherwise update the number of bytes remaining to the next barrier
			if (to_process == 0)
			{
				if (m_send_barriers.size() == 1)
				{
					// transitioning back to plaintext
					next_barrier = INT_MAX;
				}
				m_send_barriers.pop_front();
			}
			else
			{
				m_send_barriers.front().next = to_process;
			}
		}

#if TORRENT_USE_ASSERTS
		if (next_barrier != INT_MAX && next_barrier != 0)
		{
			int payload = 0;
			for (auto buf : bufs)
				payload += int(buf.size());

			int overhead = 0;
			for (auto buf : out_iovec)
				overhead += int(buf.size());
			TORRENT_ASSERT(overhead + payload == next_barrier);
		}
#endif
		if (need_destruct)
		{
			for (auto buf : bufs)
				buf.~span<char>();
		}
		return std::make_tuple(next_barrier, out_iovec);
	}

	int encryption_handler::decrypt(aux::crypto_receive_buffer& recv_buffer
		, std::size_t& bytes_transferred)
	{
		TORRENT_ASSERT(!is_recv_plaintext());
		int consume = 0;
		if (recv_buffer.crypto_packet_finished())
		{
			span<char> wr_buf = recv_buffer.mutable_buffer(int(bytes_transferred));
			int produce = 0;
			int packet_size = 0;
			std::tie(consume, produce, packet_size) = m_dec_handler->decrypt(wr_buf);
			TORRENT_ASSERT(packet_size || produce);
			TORRENT_ASSERT(packet_size >= 0);
			TORRENT_ASSERT(produce >= 0);
			bytes_transferred = std::size_t(produce);
			if (packet_size)
				recv_buffer.crypto_cut(consume, packet_size);
		}
		else
			bytes_transferred = 0;
		return consume;
	}

	bool encryption_handler::switch_send_crypto(std::shared_ptr<crypto_plugin> crypto
		, int pending_encryption)
	{
		bool place_barrier = false;
		if (!m_send_barriers.empty())
		{
			auto const end = std::prev(m_send_barriers.end());
			for (auto b = m_send_barriers.begin(); b != end; ++b)
				pending_encryption -= b->next;
			TORRENT_ASSERT(pending_encryption >= 0);
			m_send_barriers.back().next = pending_encryption;
		}
		else if (crypto)
			place_barrier = true;

		if (crypto)
			m_send_barriers.emplace_back(crypto, INT_MAX);

		return place_barrier;
	}

	void encryption_handler::switch_recv_crypto(std::shared_ptr<crypto_plugin> crypto
		, aux::crypto_receive_buffer& recv_buffer)
	{
		m_dec_handler = crypto;
		int packet_size = 0;
		if (crypto)
		{
			int consume = 0;
			int produce = 0;
			std::vector<span<char>> wr_buf;
			std::tie(consume, produce, packet_size) = crypto->decrypt(wr_buf);
			TORRENT_ASSERT(wr_buf.empty());
			TORRENT_ASSERT(consume == 0);
			TORRENT_ASSERT(produce == 0);
		}
		recv_buffer.crypto_reset(packet_size);
	}

	void rc4_handler::set_incoming_key(span<char const> key)
	{
		m_decrypt = true;
		rc4_init(reinterpret_cast<unsigned char const*>(key.data())
			, std::size_t(key.size()), &m_rc4_incoming);
		// Discard first 1024 bytes
		char buf[1024];
		span<char> vec(buf, sizeof(buf));
		decrypt(vec);
	}

	void rc4_handler::set_outgoing_key(span<char const> key)
	{
		m_encrypt = true;
		rc4_init(reinterpret_cast<unsigned char const*>(key.data())
			, std::size_t(key.size()), &m_rc4_outgoing);
		// Discard first 1024 bytes
		char buf[1024];
		span<char> vec(buf, sizeof(buf));
		encrypt(vec);
	}

	std::tuple<int, span<span<char const>>>
	rc4_handler::encrypt(span<span<char>> bufs)
	{
		span<span<char const>> empty;
		if (!m_encrypt) return std::make_tuple(0, empty);
		if (bufs.empty()) return std::make_tuple(0, empty);

		int bytes_processed = 0;
		for (auto& buf : bufs)
		{
			int const len = int(buf.size());

			TORRENT_ASSERT(len >= 0);
			if (len == 0) continue;

			auto* const pos = reinterpret_cast<unsigned char*>(buf.data());
			TORRENT_ASSERT(pos);

			bytes_processed += len;
			rc4_encrypt(pos, std::uint32_t(len), &m_rc4_outgoing);
		}
		return std::make_tuple(bytes_processed, empty);
	}

	std::tuple<int, int, int> rc4_handler::decrypt(span<span<char>> bufs)
	{
		if (!m_decrypt) return std::make_tuple(0, 0, 0);

		int bytes_processed = 0;
		for (auto& buf : bufs)
		{
			auto* const pos = reinterpret_cast<unsigned char*>(buf.data());
			int const len = int(buf.size());

			TORRENT_ASSERT(len >= 0);
			TORRENT_ASSERT(pos);

			bytes_processed += len;
			rc4_encrypt(pos, std::uint32_t(len), &m_rc4_incoming);
		}
		return std::make_tuple(0, bytes_processed, 0);
	}

// All this code is based on libTomCrypt (http://www.libtomcrypt.com/)
// this library is public domain and has been specially
// tailored for libtorrent by Arvid Norberg

	void rc4_init(unsigned char const* in, std::size_t len, rc4* state)
	{
		constexpr std::size_t key_size = 256;

		TORRENT_ASSERT(state != nullptr);
		TORRENT_ASSERT(len > 0);
		TORRENT_ASSERT(len <= key_size);
		if (len > key_size)
			len = key_size;

		// make RC4 perm and shuffle. the key is read directly out of "in"
		// (wrapping every keylen bytes), instead of first being copied byte by
		// byte into state->buf and then back out again
		for (int i = 0; i < int(key_size); ++i)
			state->buf[i] = std::uint32_t(i);

		// skip mixing the key in for len == 0, rather than dereferencing
		// "in" (an empty span's data() pointer isn't guaranteed to point
		// at valid memory). buf is left as the identity permutation --
		// not meaningful RC4 state, but safe
		if (len > 0)
		{
			int j = 0;
			for (int i = 0; i < int(key_size); ++i)
			{
				j = (j + int(state->buf[i]) + int(in[i % int(len)])) & 0xff;
				std::swap(state->buf[i], state->buf[j]);
			}
		}
		state->x = 0;
		state->y = 0;
	}

	std::size_t rc4_encrypt(unsigned char* out, std::size_t outlen, rc4* state)
	{
		TORRENT_ASSERT(out != nullptr);
		TORRENT_ASSERT(state != nullptr);

		std::size_t const n = outlen;
		auto x = std::uint8_t(state->x);
		auto y = std::uint8_t(state->y);
		std::uint32_t* const s = state->buf.data();

		auto const step = [&]() -> std::uint8_t {
			x = std::uint8_t(x + 1);
			y = std::uint8_t(y + s[x]);
			std::swap(s[x], s[y]);
			auto const idx = std::uint8_t(s[x] + s[y]);
			return std::uint8_t(s[idx]);
		};

		// generate 8 keystream bytes at a time into a local buffer, and XOR
		// them against the output in a single 64-bit operation, rather than
		// doing a separate byte-sized load/xor/store for every output byte
		while (outlen >= 8)
		{
			// write each keystream byte directly into keystream's own
			// storage (well-defined: any object's representation can be
			// written through an unsigned char pointer) instead of
			// staging it through a separate array and a second memcpy.
			// this preserves byte position exactly like memcpy does, so
			// it doesn't depend on host endianness
			std::uint64_t keystream;
			auto* const ks = reinterpret_cast<unsigned char*>(&keystream);
			for (int i = 0; i < 8; ++i)
				ks[i] = step();

			std::uint64_t block;
			std::memcpy(&block, out, 8);
			block ^= keystream;
			std::memcpy(out, &block, 8);

			out += 8;
			outlen -= 8;
		}

		while (outlen--)
			*out++ ^= step();

		state->x = x;
		state->y = y;
		return n;
	}

} // namespace libtorrent::aux

#endif // TORRENT_DISABLE_ENCRYPTION
