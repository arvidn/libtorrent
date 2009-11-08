/*

Copyright (c) 2007, Un Shyam & Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#ifndef TORRENT_DISABLE_ENCRYPTION

#include <algorithm>

#if defined TORRENT_USE_GCRYPT
#include <gcrypt.h>
#elif defined TORRENT_USE_OPENSSL
#include <openssl/bn.h>
#include <openssl/rand.h>
#endif

#include "libtorrent/pe_crypto.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/assert.hpp"

namespace libtorrent
{
	namespace
	{
		const unsigned char dh_prime[96] = {
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xC9, 0x0F, 0xDA, 0xA2,
			0x21, 0x68, 0xC2, 0x34, 0xC4, 0xC6, 0x62, 0x8B, 0x80, 0xDC, 0x1C, 0xD1,
			0x29, 0x02, 0x4E, 0x08, 0x8A, 0x67, 0xCC, 0x74, 0x02, 0x0B, 0xBE, 0xA6,
			0x3B, 0x13, 0x9B, 0x22, 0x51, 0x4A, 0x08, 0x79, 0x8E, 0x34, 0x04, 0xDD,
			0xEF, 0x95, 0x19, 0xB3, 0xCD, 0x3A, 0x43, 0x1B, 0x30, 0x2B, 0x0A, 0x6D,
			0xF2, 0x5F, 0x14, 0x37, 0x4F, 0xE1, 0x35, 0x6D, 0x6D, 0x51, 0xC2, 0x45,
			0xE4, 0x85, 0xB5, 0x76, 0x62, 0x5E, 0x7E, 0xC6, 0xF4, 0x4C, 0x42, 0xE9,
			0xA6, 0x3A, 0x36, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x05, 0x63
		};
	}

	// Set the prime P and the generator, generate local public key
	dh_key_exchange::dh_key_exchange()
	{
#ifdef TORRENT_USE_GCRYPT
		// create local key
		gcry_randomize(m_dh_local_secret, sizeof(m_dh_local_secret), GCRY_STRONG_RANDOM);

		// build gcrypt big ints from the prime and the secret
		gcry_mpi_t prime = 0;
		gcry_mpi_t secret = 0;
		gcry_mpi_t key = 0;
		gcry_error_t e;

		e = gcry_mpi_scan(&prime, GCRYMPI_FMT_USG, dh_prime, sizeof(dh_prime), 0);
		if (e) goto get_out;
		e = gcry_mpi_scan(&secret, GCRYMPI_FMT_USG, m_dh_local_secret, sizeof(m_dh_local_secret), 0);
		if (e) goto get_out;

		key = gcry_mpi_new(8);

		// generator is 2
		gcry_mpi_set_ui(key, 2);
		// key = (2 ^ secret) % prime
		gcry_mpi_powm(key, key, secret, prime);

		// key is now our local key
		size_t written;
		gcry_mpi_print(GCRYMPI_FMT_USG, (unsigned char*)m_dh_local_key
			, sizeof(m_dh_local_key), &written, key);
		if (written < 96)
		{
			memmove(m_dh_local_key + (sizeof(m_dh_local_key) - written), m_dh_local_key, written);
			memset(m_dh_local_key, 0, sizeof(m_dh_local_key) - written);
		}

get_out:
		if (key) gcry_mpi_release(key);
		if (prime) gcry_mpi_release(prime);
		if (secret) gcry_mpi_release(secret);

#elif defined TORRENT_USE_OPENSSL
		// create local key
		RAND_bytes((unsigned char*)m_dh_local_secret, sizeof(m_dh_local_secret));

		BIGNUM* prime = 0;
		BIGNUM* secret = 0;
		BIGNUM* key = 0;
		BN_CTX* ctx = 0;
		int size;

		prime = BN_bin2bn(dh_prime, sizeof(dh_prime), 0);
		if (prime == 0) goto get_out;
		secret = BN_bin2bn((unsigned char*)m_dh_local_secret, sizeof(m_dh_local_secret), 0);
		if (secret == 0) goto get_out;

		key = BN_new();
		if (key == 0) goto get_out;
		// generator is 2
		BN_set_word(key, 2);

		ctx = BN_CTX_new();
		if (ctx == 0) goto get_out;
		BN_mod_exp(key, key, secret, prime, ctx);
		BN_CTX_free(ctx);

		// print key to m_dh_local_key
		size = BN_num_bytes(key);
		memset(m_dh_local_key, 0, sizeof(m_dh_local_key) - size);
		BN_bn2bin(key, (unsigned char*)m_dh_local_key + sizeof(m_dh_local_key) - size);

get_out:
		if (key) BN_free(key);
		if (secret) BN_free(secret);
		if (prime) BN_free(prime);
#else
#error you must define TORRENT_USE_OPENSSL or TORRENT_USE_GCRYPT
#endif
	}

	char const* dh_key_exchange::get_local_key() const
	{
		return m_dh_local_key;
	}	


	// compute shared secret given remote public key
	int dh_key_exchange::compute_secret(char const* remote_pubkey)
	{
		TORRENT_ASSERT(remote_pubkey);
		int ret = 0;
#ifdef TORRENT_USE_GCRYPT

		gcry_mpi_t prime = 0;
		gcry_mpi_t remote_key = 0;
		gcry_mpi_t secret = 0;
		size_t written;
		gcry_error_t e;

		e = gcry_mpi_scan(&prime, GCRYMPI_FMT_USG, dh_prime, sizeof(dh_prime), 0);
		if (e != 0) { ret = 1; goto get_out; }
		e = gcry_mpi_scan(&remote_key, GCRYMPI_FMT_USG, remote_pubkey, 96, 0);
		if (e != 0) { ret = 1; goto get_out; }
		e = gcry_mpi_scan(&secret, GCRYMPI_FMT_USG, (unsigned char const*)m_dh_local_secret
			, sizeof(m_dh_local_secret), 0);
		if (e != 0) { ret = 1; goto get_out; }

		gcry_mpi_powm(remote_key, remote_key, secret, prime);

		// remote_key is now the shared secret
		e = gcry_mpi_print(GCRYMPI_FMT_USG, (unsigned char*)m_dh_shared_secret
			, sizeof(m_dh_shared_secret), &written, remote_key);
		if (e != 0) { ret = 1; goto get_out; }

		if (written < 96)
		{
			memmove(m_dh_shared_secret, m_dh_shared_secret
				+ (sizeof(m_dh_shared_secret) - written), written);
			memset(m_dh_shared_secret, 0, sizeof(m_dh_shared_secret) - written);
		}

get_out:
		if (prime) gcry_mpi_release(prime);
		if (remote_key) gcry_mpi_release(remote_key);
		if (secret) gcry_mpi_release(secret);

#elif defined TORRENT_USE_OPENSSL

		BIGNUM* prime = 0;
		BIGNUM* secret = 0;
		BIGNUM* remote_key = 0;
		BN_CTX* ctx = 0;
		int size;

		prime = BN_bin2bn(dh_prime, sizeof(dh_prime), 0);
		if (prime == 0) { ret = 1; goto get_out; }
		secret = BN_bin2bn((unsigned char*)m_dh_local_secret, sizeof(m_dh_local_secret), 0);
		if (secret == 0) { ret = 1; goto get_out; }
		remote_key = BN_bin2bn((unsigned char*)remote_pubkey, 96, 0);
		if (remote_key == 0) { ret = 1; goto get_out; }

		ctx = BN_CTX_new();
		if (ctx == 0) { ret = 1; goto get_out; }
		BN_mod_exp(remote_key, remote_key, secret, prime, ctx);
		BN_CTX_free(ctx);

		// remote_key is now the shared secret
		size = BN_num_bytes(remote_key);
		memset(m_dh_shared_secret, 0, sizeof(m_dh_shared_secret) - size);
		BN_bn2bin(remote_key, (unsigned char*)m_dh_shared_secret + sizeof(m_dh_shared_secret) - size);

get_out:
		BN_free(remote_key);
		BN_free(secret);
		BN_free(prime);
#else
#error you must define TORRENT_USE_OPENSSL or TORRENT_USE_GCRYPT
#endif

		// calculate the xor mask for the obfuscated hash
		hasher h;
		h.update("req3", 4);
		h.update(m_dh_shared_secret, sizeof(m_dh_shared_secret));
		m_xor_mask = h.final();
		return ret;
	}

} // namespace libtorrent

#endif // #ifndef TORRENT_DISABLE_ENCRYPTION

