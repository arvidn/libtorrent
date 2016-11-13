/*

Copyright (c) 2003-2016, Arvid Norberg
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

#ifndef TORRENT_HASHER_HPP_INCLUDED
#define TORRENT_HASHER_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/span.hpp"

#include <cstdint>

#include "libtorrent/aux_/disable_warnings_push.hpp"
#ifdef TORRENT_USE_LIBGCRYPT
#include <gcrypt.h>

#elif TORRENT_USE_COMMONCRYPTO
#include <CommonCrypto/CommonDigest.h>

#elif TORRENT_USE_CRYPTOAPI
#include <windows.h>
#include <wincrypt.h>

#elif defined TORRENT_USE_LIBCRYPTO

extern "C" {
#include <openssl/sha.h>
}

#else
#include "libtorrent/sha1.hpp"
#endif
#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent
{
	// this is a SHA-1 hash class.
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
	// The built-in software version of sha1-algorithm was implemented
	// by Steve Reid and released as public domain.
	// For more info, see ``src/sha1.cpp``.
	class TORRENT_EXPORT hasher
	{
	public:
		hasher();

		// this is the same as default constructing followed by a call to
		// ``update(data, len)``.
		hasher(char const* data, int len);
		explicit hasher(span<char const> data);
		hasher(hasher const&);
		hasher& operator=(hasher const&);

		// append the following bytes to what is being hashed
		hasher& update(span<char const> data);
		hasher& update(char const* data, int len);

		// returns the SHA-1 digest of the buffers previously passed to
		// update() and the hasher constructor.
		sha1_hash final();

		// restore the hasher state to be as if the hasher has just been
		// default constructed.
		void reset();

		~hasher();

	private:
#ifdef TORRENT_USE_LIBGCRYPT
		gcry_md_hd_t m_context;
#elif TORRENT_USE_COMMONCRYPTO
		CC_SHA1_CTX m_context;
#elif TORRENT_USE_CRYPTOAPI
		HCRYPTHASH m_context;
#elif defined TORRENT_USE_LIBCRYPTO
		SHA_CTX m_context;
#else
		sha1_ctx m_context;
#endif
	};
}

#endif // TORRENT_HASHER_HPP_INCLUDED
