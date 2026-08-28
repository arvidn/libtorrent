/*

Copyright (c) 2007, Un Shyam
Copyright (c) 2007-2009, 2011-2012, 2014-2021, Arvid Norberg
Copyright (c) 2016-2017, 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_PE_CRYPTO_HPP_INCLUDED
#define TORRENT_PE_CRYPTO_HPP_INCLUDED

#if !defined TORRENT_DISABLE_ENCRYPTION

#include "libtorrent/config.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#if defined TORRENT_USE_LIBCRYPTO && !defined TORRENT_USE_WOLFSSL
extern "C"
{
#include <openssl/bn.h>
}
#else
#include <boost/multiprecision/cpp_int.hpp>
#endif
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/aux_/receive_buffer.hpp"
#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/extensions.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/aux_/array.hpp"
#include "libtorrent/aux_/throw.hpp"
#include "libtorrent/error_code.hpp"

#include <list>
#include <array>
#include <cstdint>
#include <memory>

namespace libtorrent::aux {

#if !defined TORRENT_USE_LIBCRYPTO || defined TORRENT_USE_WOLFSSL
	namespace mp = boost::multiprecision;
#endif

	// RC4 state from libtomcrypt. buf holds a permutation of 0-255, stored
	// widened to 32 bits per entry so every access is a whole-register
	// load/store.
	struct rc4 {
		int x = 0;
		int y = 0;
		aux::array<std::uint32_t, 256> buf;
	};

	// TODO: 3 dh_key_exchange should probably move into its own file

	// Diffie-Hellman key exchange for BitTorrent Message Stream Encryption
	// (MSE/PE, see BEP unofficial spec). MSE is deep-packet-inspection
	// obfuscation, not a confidentiality guarantee against a targeted
	// adversary: it has no authentication, so it is trivially defeated by an
	// active man-in-the-middle, and RC4 is not a secure cipher by modern
	// standards. Accordingly the local secret exponent is deliberately
	// generated with aux::random_bytes() (a fast, non-cryptographic PRNG)
	// rather than aux::crypto_random_bytes(): a stronger PRNG would not
	// change what MSE actually protects against, and there is no plan to
	// harden this into a real cryptographic channel.
	class TORRENT_EXTRA_EXPORT dh_key_exchange
	{
	public:
		dh_key_exchange();

		// Get local public key, DH-exported to a fixed-width 96 byte big
		// endian buffer.
		std::array<char, 96> const& get_local_key() const { return m_dh_local_key; }

		// read remote_pubkey, generate and store shared secret in
		// m_dh_shared_secret. Returns false if the remote public key is
		// degenerate (i.e. outside the range [2, p-2]) and the shared
		// secret was not computed.
		bool compute_secret(std::uint8_t const* remote_pubkey);

		std::array<char, 96> const& get_secret() const { return m_dh_shared_secret; }

		sha1_hash const& get_hash_xor_mask() const { return m_xor_mask; }

	private:
#if defined TORRENT_USE_LIBCRYPTO && !defined TORRENT_USE_WOLFSSL
		// Move-only RAII wrapper around a libcrypto BIGNUM. Used internally
		// for the DH modular exponentiation. Throws system_error
		// (error_code errors::no_memory) if the underlying BIGNUM
		// allocation failed, so a successfully constructed key_t always
		// wraps a valid BIGNUM.
		class key_t
		{
		public:
			key_t()
				: key_t(BN_new())
			{}
			explicit key_t(::BIGNUM* b)
				: m_b(b, &BN_free)
			{
				if (!m_b)
					aux::throw_ex<system_error>(errors::no_memory);
			}
			key_t(key_t const&) = delete;
			key_t(key_t&&) noexcept = default;
			key_t& operator=(key_t const&) = delete;
			key_t& operator=(key_t&&) noexcept = default;
			~key_t() = default;

			::BIGNUM* get() { return m_b.get(); }
			::BIGNUM const* get() const { return m_b.get(); }

		private:
			std::unique_ptr<::BIGNUM, void (*)(::BIGNUM*)> m_b;
		};

		// the DH generator (also the lower bound of the valid public-key range)
		static key_t const& two();
		// upper bound (exclusive) of the valid public-key range
		static key_t const& dh_prime_minus_1();

		// scratch space for BN_mod_exp(), reused across calls rather than
		// allocated fresh each time, since BN_CTX internally pools the
		// temporary BIGNUMs it hands out. One instance per thread
		// (thread_local), since a BN_CTX is not safe to use concurrently
		// from multiple threads. Falls back to a single instance guarded by a
		// mutex where thread_local isn't available.
		static ::BN_CTX* ctx();
		static ::BN_MONT_CTX* mont();
#else
		// also used for TORRENT_USE_WOLFSSL builds: wolfSSL's OpenSSL
		// compatibility layer does not reliably provide the same BN_*
		// function names/semantics as real libcrypto.
		using key_t =
			mp::number<mp::cpp_int_backend<768, 768, mp::unsigned_magnitude, mp::unchecked, void>>;
#endif

		static key_t const& dh_prime();
		static std::array<char, 96> export_key(key_t const& k);

		key_t m_dh_local_secret;
		std::array<char, 96> m_dh_local_key;
		// only valid after a successful call to compute_secret()
		std::array<char, 96> m_dh_shared_secret{};
		sha1_hash m_xor_mask;
	};

	struct TORRENT_EXTRA_EXPORT encryption_handler
	{
		std::tuple<int, span<span<char const>>>
		encrypt(span<span<char>> iovec);

		int decrypt(aux::crypto_receive_buffer& recv_buffer
			, std::size_t& bytes_transferred);

		bool switch_send_crypto(std::shared_ptr<crypto_plugin> crypto
			, int pending_encryption);

		void switch_recv_crypto(std::shared_ptr<crypto_plugin> crypto
			, aux::crypto_receive_buffer& recv_buffer);

		bool is_send_plaintext() const
		{
			return m_send_barriers.empty() || m_send_barriers.back().next != INT_MAX;
		}

		bool is_recv_plaintext() const
		{
			return m_dec_handler.get() == nullptr;
		}

	private:
		struct barrier
		{
			barrier(std::shared_ptr<crypto_plugin> plugin, int n)
				: enc_handler(plugin), next(n) {}
			std::shared_ptr<crypto_plugin> enc_handler;
			// number of bytes to next barrier
			int next;
		};
		std::list<barrier> m_send_barriers;
		std::shared_ptr<crypto_plugin> m_dec_handler;
	};

	struct TORRENT_EXTRA_EXPORT rc4_handler : crypto_plugin
	{
	public:
		rc4_handler() = default;

		// Input keys must be 20 bytes
		void set_incoming_key(span<char const> key) override;
		void set_outgoing_key(span<char const> key) override;

		std::tuple<int, span<span<char const>>>
		encrypt(span<span<char>> buf) override;

		std::tuple<int, int, int> decrypt(span<span<char>> buf) override;

	private:
		rc4 m_rc4_incoming;
		rc4 m_rc4_outgoing;

		// determines whether or not encryption and decryption is enabled
		bool m_encrypt = false;
		bool m_decrypt = false;
	};

} // namespace libtorrent::aux

#endif // TORRENT_DISABLE_ENCRYPTION

#endif // TORRENT_PE_CRYPTO_HPP_INCLUDED
