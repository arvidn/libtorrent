/*

Copyright (c) 2007, Un Shyam
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

#include <openssl/dh.h>
#include <openssl/engine.h>
#include <openssl/rc4.h>

#include "libtorrent/peer_id.hpp" // For sha1_hash
#include "libtorrent/assert.hpp"

namespace libtorrent
{
	class DH_key_exchange
	{
	public:
		DH_key_exchange ();
		~DH_key_exchange ();

		// Get local public key, always 96 bytes
		char const* get_local_key (void) const;

		// read remote_pubkey, generate and store shared secret in
		// m_dh_secret
		void compute_secret (const char* remote_pubkey);

		const char* get_secret (void) const;
		
	private:
		int get_local_key_size () const
		{
			TORRENT_ASSERT(m_DH);
			return BN_num_bytes (m_DH->pub_key);
		}

		DH* m_DH;
		static const unsigned char m_dh_prime[96];
		static const unsigned char m_dh_generator[1];

		char m_dh_local_key[96];
		char m_dh_secret[96];
	};
	
	class RC4_handler // Non copyable
	{
	public:
		// Input longkeys must be 20 bytes
		RC4_handler (const sha1_hash& rc4_local_longkey,
					 const sha1_hash& rc4_remote_longkey)
			
		{
			RC4_set_key (&m_local_key, 20,
						 reinterpret_cast<unsigned char const*>(rc4_local_longkey.begin()));
			RC4_set_key (&m_remote_key, 20,
						 reinterpret_cast<unsigned char const*>(rc4_remote_longkey.begin()));

			// Discard first 1024 bytes
			char buf[1024];
			encrypt (buf, 1024);
			decrypt (buf, 1024);
		};
		
		~RC4_handler () {};

		void encrypt (char* pos, int len)
		{
			TORRENT_ASSERT(len >= 0);
			TORRENT_ASSERT(pos);

			RC4 (&m_local_key, len, reinterpret_cast<unsigned char const*>(pos),
				 reinterpret_cast<unsigned char*>(pos));
		}

		void decrypt (char* pos, int len)
		{
			TORRENT_ASSERT(len >= 0);
			TORRENT_ASSERT(pos);

			RC4 (&m_remote_key, len, reinterpret_cast<unsigned char const*>(pos),
				 reinterpret_cast<unsigned char*>(pos));
		}

	private:
		RC4_KEY m_local_key; // Key to encrypt outgoing data
		RC4_KEY m_remote_key; // Key to decrypt incoming data
	};
	
} // namespace libtorrent

#endif // TORRENT_PE_CRYPTO_HPP_INCLUDED
#endif // TORRENT_DISABLE_ENCRYPTION
