/*

Copyright (c) 2003-2004, 2007, 2009, 2012-2020, Arvid Norberg
Copyright (c) 2004, Magnus Jonsson
Copyright (c) 2016, 2021, Alden Torres
Copyright (c) 2017, Steven Siloti
Copyright (c) 2017, 2019, Andrei Kurushin
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
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

#elif TORRENT_USE_CNG
#include "libtorrent/aux_/win_cng.hpp"

#elif TORRENT_USE_CRYPTOAPI
#include "libtorrent/aux_/win_crypto_provider.hpp"

#if !TORRENT_USE_CRYPTOAPI_SHA_512
#include "libtorrent/aux_/sha256.hpp"
#endif

#elif defined TORRENT_USE_LIBCRYPTO

extern "C" {
#include <openssl/sha.h>
}

#else
#include "libtorrent/aux_/sha1.hpp"
#include "libtorrent/aux_/sha256.hpp"
#endif
#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace lt {
TORRENT_CRYPTO_NAMESPACE

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
		hasher& operator=(hasher const&) &;

		// append the following bytes to what is being hashed
		hasher& update(span<char const> data);
		hasher& update(char const* data, int len);

		// returns the SHA-1 digest of the buffers previously passed to
		// update() and the hasher constructor.
		sha1_hash final();

		// restore the hasher state to be as if the hasher has just been
		// default constructed.
		void reset();

		// hidden
		~hasher();

	private:

#ifdef TORRENT_USE_LIBGCRYPT
		gcry_md_hd_t m_context;
#elif TORRENT_USE_COMMONCRYPTO
		CC_SHA1_CTX m_context;
#elif TORRENT_USE_CNG
		aux::cng_hash<aux::cng_sha1_algorithm> m_context;
#elif TORRENT_USE_CRYPTOAPI
		aux::crypt_hash<CALG_SHA1, PROV_RSA_FULL> m_context;
#elif defined TORRENT_USE_LIBCRYPTO
		SHA_CTX m_context;
#else
		aux::sha1_ctx m_context;
#endif
	};

	class TORRENT_EXPORT hasher256
	{
	public:
		hasher256();

		// this is the same as default constructing followed by a call to
		// ``update(data, len)``.
		hasher256(char const* data, int len);
		explicit hasher256(span<char const> data);
		hasher256(hasher256 const&);
		hasher256& operator=(hasher256 const&) &;

		// append the following bytes to what is being hashed
		hasher256& update(span<char const> data);
		hasher256& update(char const* data, int len);

		// returns the SHA-1 digest of the buffers previously passed to
		// update() and the hasher constructor.
		sha256_hash final();

		// restore the hasher state to be as if the hasher has just been
		// default constructed.
		void reset();

		~hasher256();

	private:
#ifdef TORRENT_USE_LIBGCRYPT
		gcry_md_hd_t m_context;
#elif TORRENT_USE_COMMONCRYPTO
		CC_SHA256_CTX m_context;
#elif TORRENT_USE_CNG
		aux::cng_hash<aux::cng_sha256_algorithm> m_context;
#elif TORRENT_USE_CRYPTOAPI_SHA_512
		aux::crypt_hash<CALG_SHA_256, PROV_RSA_AES> m_context;
#elif defined TORRENT_USE_LIBCRYPTO
		SHA256_CTX m_context;
#else
		aux::sha256_ctx m_context;
#endif
	};

TORRENT_CRYPTO_NAMESPACE_END
}

#endif // TORRENT_HASHER_HPP_INCLUDED
