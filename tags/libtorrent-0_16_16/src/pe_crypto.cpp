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

#include <boost/cstdint.hpp>
#include <algorithm>

#if defined TORRENT_USE_GCRYPT
#include <gcrypt.h>
#elif defined TORRENT_USE_OPENSSL
#include <openssl/bn.h>
#include "libtorrent/random.hpp"
#elif defined TORRENT_USE_TOMMATH
extern "C" {
#include "libtorrent/tommath.h"
}
#include "libtorrent/random.hpp"
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
		for (int i = 0; i < sizeof(m_dh_local_secret); ++i)
			m_dh_local_secret[i] = random();

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
#elif defined TORRENT_USE_TOMMATH
		// create local key
		for (int i = 0; i < int(sizeof(m_dh_local_secret)); ++i)
			m_dh_local_secret[i] = random();

		mp_int prime;
		mp_int secret;
		mp_int key;
		int e;
		int size;

		mp_init(&prime);
		mp_init(&secret);
		mp_init(&key);

		e = mp_read_unsigned_bin(&prime, dh_prime, sizeof(dh_prime));
		if (e) goto get_out;
		e = mp_read_unsigned_bin(&secret, (unsigned char*)m_dh_local_secret, sizeof(m_dh_local_secret));
		if (e) goto get_out;

		// generator is 2
		mp_set_int(&key, 2);
		// key = (2 ^ secret) % prime
		e = mp_exptmod(&key, &secret, &prime, &key);
		if (e) goto get_out;

		// key is now our local key
		size = mp_unsigned_bin_size(&key);
		memset(m_dh_local_key, 0, sizeof(m_dh_local_key) - size);
		mp_to_unsigned_bin(&key, (unsigned char*)m_dh_local_key + sizeof(m_dh_local_key) - size);

get_out:
		mp_clear(&key);
		mp_clear(&prime);
		mp_clear(&secret);
#else
#error you must define which bigint library to use
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
#elif defined TORRENT_USE_TOMMATH
		mp_int prime;
		mp_int secret;
		mp_int remote_key;
		int size;
		int e;

		mp_init(&prime);
		mp_init(&secret);
		mp_init(&remote_key);

		e = mp_read_unsigned_bin(&prime, dh_prime, sizeof(dh_prime));
		if (e) { ret = 1; goto get_out; }
		e = mp_read_unsigned_bin(&secret, (unsigned char*)m_dh_local_secret, sizeof(m_dh_local_secret));
		if (e) { ret = 1; goto get_out; }
		e = mp_read_unsigned_bin(&remote_key, (unsigned char*)remote_pubkey, 96);
		if (e) { ret = 1; goto get_out; }

		e = mp_exptmod(&remote_key, &secret, &prime, &remote_key);
		if (e) goto get_out;

		// remote_key is now the shared secret
		size = mp_unsigned_bin_size(&remote_key);
		memset(m_dh_shared_secret, 0, sizeof(m_dh_shared_secret) - size);
		mp_to_unsigned_bin(&remote_key, (unsigned char*)m_dh_shared_secret + sizeof(m_dh_shared_secret) - size);

get_out:
		mp_clear(&remote_key);
		mp_clear(&secret);
		mp_clear(&prime);
#else
#error you must define which bigint library to use
#endif

		// calculate the xor mask for the obfuscated hash
		hasher h;
		h.update("req3", 4);
		h.update(m_dh_shared_secret, sizeof(m_dh_shared_secret));
		m_xor_mask = h.final();
		return ret;
	}

} // namespace libtorrent

#if !defined TORRENT_USE_OPENSSL && !defined TORRENT_USE_GCRYPT

// All this code is based on libTomCrypt (http://www.libtomcrypt.com/)
// this library is public domain and has been specially
// tailored for libtorrent by Arvid Norberg

void rc4_init(const unsigned char* in, unsigned long len, rc4 *state)
{
	unsigned char key[256], tmp, *s;
	int keylen, x, y, j;

	TORRENT_ASSERT(state != 0);
	TORRENT_ASSERT(len <= 256);

	state->x = 0;
	while (len--) {
		state->buf[state->x++] = *in++;
	}

	/* extract the key */
	s = state->buf;
	memcpy(key, s, 256);
	keylen = state->x;

	/* make RC4 perm and shuffle */
	for (x = 0; x < 256; x++) {
		s[x] = x;
	}

	for (j = x = y = 0; x < 256; x++) {
		y = (y + state->buf[x] + key[j++]) & 255;
		if (j == keylen) {
			j = 0; 
		}
		tmp = s[x]; s[x] = s[y]; s[y] = tmp;
	}
	state->x = 0;
	state->y = 0;
}

unsigned long rc4_encrypt(unsigned char *out, unsigned long outlen, rc4 *state)
{
	unsigned char x, y, *s, tmp;
	unsigned long n;

	TORRENT_ASSERT(out != 0);
	TORRENT_ASSERT(state != 0);

	n = outlen;
	x = state->x;
	y = state->y;
	s = state->buf;
	while (outlen--) {
		x = (x + 1) & 255;
		y = (y + s[x]) & 255;
		tmp = s[x]; s[x] = s[y]; s[y] = tmp;
		tmp = (s[x] + s[y]) & 255;
		*out++ ^= s[tmp];
	}
	state->x = x;
	state->y = y;
	return n;
}

#endif

#endif // #ifndef TORRENT_DISABLE_ENCRYPTION

