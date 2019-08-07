/*

Copyright (c) 2003-2016, Arvid Norberg, Alden Torres
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

#ifndef TORRENT_HASHER512_HPP_INCLUDED
#define TORRENT_HASHER512_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/span.hpp"

#include <cstdint>

#include "libtorrent/aux_/disable_warnings_push.hpp"
#ifdef TORRENT_USE_LIBGCRYPT
#include <gcrypt.h>

#elif TORRENT_USE_COMMONCRYPTO
#include <CommonCrypto/CommonDigest.h>

#elif TORRENT_USE_CRYPTOAPI_SHA_512
#include "libtorrent/aux_/win_crypto_provider.hpp"

#elif defined TORRENT_USE_LIBCRYPTO

extern "C" {
#include <openssl/sha.h>
}

#else
#include "libtorrent/sha512.hpp"
#endif

#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent {

	using sha512_hash = digest32<512>;

	// internal
	class TORRENT_EXPORT hasher512
	{
	// this is a SHA-512 hash class.
	//
	// You use it by first instantiating it, then call ``update()`` to feed it
	// with data. i.e. you don't have to keep the entire buffer of which you want to
	// create the hash in memory. You can feed the hasher parts of it at a time. When
	// You have fed the hasher with all the data, you call ``final()`` and it
	// will return the sha1-hash of the data.
	//
	// The constructor that takes a ``char const*`` and an integer will construct the
	// sha1 context and feed it the data passed in.
	//
	// If you want to reuse the hasher object once you have created a hash, you have to
	// call ``reset()`` to reinitialize it.
	//
	// The built-in software version of the sha512-algorithm is from LibTomCrypt
	public:

		hasher512();

		// this is the same as default constructing followed by a call to
		// ``update(data)``.
		explicit hasher512(span<char const> data);
		hasher512(hasher512 const&);
		hasher512& operator=(hasher512 const&) &;

		// append the following bytes to what is being hashed
		hasher512& update(span<char const> data);

		// store the SHA-512 digest of the buffers previously passed to
		// update() and the hasher constructor.
		sha512_hash final();

		// restore the hasher state to be as if the hasher has just been
		// default constructed.
		void reset();

		// hidden
		~hasher512();

	private:

#ifdef TORRENT_USE_LIBGCRYPT
		gcry_md_hd_t m_context;
#elif TORRENT_USE_COMMONCRYPTO
		CC_SHA512_CTX m_context;
#elif TORRENT_USE_CRYPTOAPI_SHA_512
		aux::crypt_hash<CALG_SHA_512, PROV_RSA_AES> m_context;
#elif defined TORRENT_USE_LIBCRYPTO
		SHA512_CTX m_context;
#else
		sha512_ctx m_context;
#endif
	};

}

#endif // TORRENT_HASHER512_HPP_INCLUDED
