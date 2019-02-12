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

#include "libtorrent/hasher512.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/aux_/openssl.hpp"

namespace libtorrent {

#ifdef TORRENT_MACOS_DEPRECATED_LIBCRYPTO
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

	hasher512::hasher512()
	{
#ifdef TORRENT_USE_LIBGCRYPT
		gcry_md_open(&m_context, GCRY_MD_SHA512, 0);
#elif TORRENT_USE_COMMONCRYPTO
		CC_SHA512_Init(&m_context);
#elif TORRENT_USE_CRYPTOAPI_SHA_512
#elif defined TORRENT_USE_LIBCRYPTO
		SHA512_Init(&m_context);
#else
		SHA512_init(&m_context);
#endif
	}

	hasher512::hasher512(span<char const> data)
		: hasher512()
	{
		update(data);
	}

#ifdef TORRENT_USE_LIBGCRYPT
	hasher512::hasher512(hasher512 const& h)
	{
		gcry_md_copy(&m_context, h.m_context);
	}

	hasher512& hasher512::operator=(hasher512 const& h) &
	{
		if (this == &h) return *this;
		gcry_md_close(m_context);
		gcry_md_copy(&m_context, h.m_context);
		return *this;
	}
#else
	hasher512::hasher512(hasher512 const&) = default;
	hasher512& hasher512::operator=(hasher512 const&) & = default;
#endif

	hasher512& hasher512::update(span<char const> data)
	{
		TORRENT_ASSERT(data.size() > 0);
#ifdef TORRENT_USE_LIBGCRYPT
		gcry_md_write(m_context, data.data(), static_cast<std::size_t>(data.size()));
#elif TORRENT_USE_COMMONCRYPTO
		CC_SHA512_Update(&m_context, reinterpret_cast<unsigned char const*>(data.data()), CC_LONG(data.size()));
#elif TORRENT_USE_CRYPTOAPI_SHA_512
		m_context.update(data);
#elif defined TORRENT_USE_LIBCRYPTO
		SHA512_Update(&m_context, reinterpret_cast<unsigned char const*>(data.data())
			, static_cast<std::size_t>(data.size()));
#else
		SHA512_update(&m_context, reinterpret_cast<unsigned char const*>(data.data())
			, static_cast<std::size_t>(data.size()));
#endif
		return *this;
	}

	sha512_hash hasher512::final()
	{
		sha512_hash digest;
#ifdef TORRENT_USE_LIBGCRYPT
		gcry_md_final(m_context);
		digest.assign(reinterpret_cast<char const*>(gcry_md_read(m_context, 0)));
#elif TORRENT_USE_COMMONCRYPTO
		CC_SHA512_Final(reinterpret_cast<unsigned char*>(digest.data()), &m_context);
#elif TORRENT_USE_CRYPTOAPI_SHA_512
		m_context.get_hash(digest.data(), digest.size());
#elif defined TORRENT_USE_LIBCRYPTO
		SHA512_Final(reinterpret_cast<unsigned char*>(digest.data()), &m_context);
#else
		SHA512_final(reinterpret_cast<unsigned char*>(digest.data()), &m_context);
#endif
		return digest;
	}

	void hasher512::reset()
	{
#ifdef TORRENT_USE_LIBGCRYPT
		gcry_md_reset(m_context);
#elif TORRENT_USE_COMMONCRYPTO
		CC_SHA512_Init(&m_context);
#elif TORRENT_USE_CRYPTOAPI_SHA_512
		m_context.reset();
#elif defined TORRENT_USE_LIBCRYPTO
		SHA512_Init(&m_context);
#else
		SHA512_init(&m_context);
#endif
	}

#if defined TORRENT_USE_LIBGCRYPT
	hasher512::~hasher512()
	{
		gcry_md_close(m_context);
	}
#else
	hasher512::~hasher512() = default;
#endif

#ifdef TORRENT_MACOS_DEPRECATED_LIBCRYPTO
#pragma clang diagnostic pop
#endif
}
