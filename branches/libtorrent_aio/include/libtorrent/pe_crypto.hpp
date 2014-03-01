/*

Copyright (c) 2007-2014, Un Shyam & Arvid Norberg
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

#ifndef TORRENT_PE_CRYPTO_HPP_INCLUDED
#define TORRENT_PE_CRYPTO_HPP_INCLUDED

#include "libtorrent/config.hpp"

#ifdef TORRENT_USE_GCRYPT
#include <gcrypt.h>
#elif defined TORRENT_USE_OPENSSL
#include <openssl/rc4.h>
#else
// RC4 state from libtomcrypt
struct rc4 {
	int x, y;
	unsigned char buf[256];
};

void TORRENT_EXTRA_EXPORT rc4_init(const unsigned char* in, unsigned long len, rc4 *state);
unsigned long TORRENT_EXTRA_EXPORT rc4_encrypt(unsigned char *out, unsigned long outlen, rc4 *state);
#endif

#include "libtorrent/peer_id.hpp" // For sha1_hash
#include "libtorrent/assert.hpp"

namespace libtorrent
{
	class TORRENT_EXTRA_EXPORT dh_key_exchange
	{
	public:
		dh_key_exchange();
		bool good() const { return true; }

		// Get local public key, always 96 bytes
		char const* get_local_key() const;

		// read remote_pubkey, generate and store shared secret in
		// m_dh_shared_secret.
		int compute_secret(const char* remote_pubkey);

		char const* get_secret() const { return m_dh_shared_secret; }

		sha1_hash const& get_hash_xor_mask() const { return m_xor_mask; }
		
	private:

		int get_local_key_size() const
		{ return sizeof(m_dh_local_key); }

		char m_dh_local_key[96];
		char m_dh_local_secret[96];
		char m_dh_shared_secret[96];
		sha1_hash m_xor_mask;
	};

	struct encryption_handler
	{
		virtual void set_incoming_key(unsigned char const* key, int len) = 0;
		virtual void set_outgoing_key(unsigned char const* key, int len) = 0;
		virtual void encrypt(char* pos, int len) = 0;
		virtual void decrypt(char* pos, int len) = 0;
		virtual ~encryption_handler() {}
	};

	struct rc4_handler : encryption_handler
	{
	public:
		// Input longkeys must be 20 bytes
		rc4_handler()
			: m_encrypt(false)
			, m_decrypt(false)
		{
#ifdef TORRENT_USE_GCRYPT
			gcry_cipher_open(&m_rc4_incoming, GCRY_CIPHER_ARCFOUR, GCRY_CIPHER_MODE_STREAM, 0);
			gcry_cipher_open(&m_rc4_outgoing, GCRY_CIPHER_ARCFOUR, GCRY_CIPHER_MODE_STREAM, 0);
#endif
		};

		void set_incoming_key(unsigned char const* key, int len)
		{
			m_decrypt = true;
#ifdef TORRENT_USE_GCRYPT
			gcry_cipher_close(m_rc4_incoming);
			gcry_cipher_open(&m_rc4_incoming, GCRY_CIPHER_ARCFOUR, GCRY_CIPHER_MODE_STREAM, 0);
			gcry_cipher_setkey(m_rc4_incoming, key, len);
#elif defined TORRENT_USE_OPENSSL
			RC4_set_key(&m_remote_key, len, key);
#else
			rc4_init(key, len, &m_rc4_incoming);
#endif
			// Discard first 1024 bytes
			char buf[1024];
			decrypt(buf, 1024);
		}
		
		void set_outgoing_key(unsigned char const* key, int len)
		{
			m_encrypt = true;
#ifdef TORRENT_USE_GCRYPT
			gcry_cipher_close(m_rc4_outgoing);
			gcry_cipher_open(&m_rc4_outgoing, GCRY_CIPHER_ARCFOUR, GCRY_CIPHER_MODE_STREAM, 0);
			gcry_cipher_setkey(m_rc4_outgoing, key, len);
#elif defined TORRENT_USE_OPENSSL
			RC4_set_key(&m_local_key, len, key);
#else
			rc4_init(key, len, &m_rc4_outgoing);
#endif
			// Discard first 1024 bytes
			char buf[1024];
			encrypt(buf, 1024);
		}
		
		~rc4_handler()
		{
#ifdef TORRENT_USE_GCRYPT
			gcry_cipher_close(m_rc4_incoming);
			gcry_cipher_close(m_rc4_outgoing);
#endif
		};

		void encrypt(char* pos, int len)
		{
			if (!m_encrypt) return;

			TORRENT_ASSERT(len >= 0);
			TORRENT_ASSERT(pos);

#ifdef TORRENT_USE_GCRYPT
			gcry_cipher_encrypt(m_rc4_outgoing, pos, len, 0, 0);
#elif defined TORRENT_USE_OPENSSL
			RC4(&m_local_key, len, (const unsigned char*)pos, (unsigned char*)pos);
#else
			rc4_encrypt((unsigned char*)pos, len, &m_rc4_outgoing);
#endif
		}

		void decrypt(char* pos, int len)
		{
			if (!m_decrypt) return;

			TORRENT_ASSERT(len >= 0);
			TORRENT_ASSERT(pos);

#ifdef TORRENT_USE_GCRYPT
			gcry_cipher_decrypt(m_rc4_incoming, pos, len, 0, 0);
#elif defined TORRENT_USE_OPENSSL
			RC4(&m_remote_key, len, (const unsigned char*)pos, (unsigned char*)pos);
#else
			rc4_encrypt((unsigned char*)pos, len, &m_rc4_incoming);
#endif
		}

	private:
#ifdef TORRENT_USE_GCRYPT
		gcry_cipher_hd_t m_rc4_incoming;
		gcry_cipher_hd_t m_rc4_outgoing;
#elif defined TORRENT_USE_OPENSSL
		RC4_KEY m_local_key; // Key to encrypt outgoing data
		RC4_KEY m_remote_key; // Key to decrypt incoming data
#else
		rc4 m_rc4_incoming;
		rc4 m_rc4_outgoing;
#endif
		// determines whether or not encryption and decryption is enabled
		bool m_encrypt;
		bool m_decrypt;
	};

} // namespace libtorrent

#endif // TORRENT_PE_CRYPTO_HPP_INCLUDED
#endif // TORRENT_DISABLE_ENCRYPTION

