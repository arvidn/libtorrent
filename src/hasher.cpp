/*

Copyright (c) 2003-2018, Arvid Norberg
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

#include "libtorrent/hasher.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/aux_/openssl.hpp"

namespace libtorrent {

#ifdef TORRENT_MACOS_DEPRECATED_LIBCRYPTO
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

	hasher::hasher()
	{
#ifdef TORRENT_USE_LIBGCRYPT
		gcry_md_open(&m_context, GCRY_MD_SHA1, 0);
#elif TORRENT_USE_COMMONCRYPTO
		CC_SHA1_Init(&m_context);
#elif TORRENT_USE_CRYPTOAPI
#elif defined TORRENT_USE_LIBCRYPTO
		SHA1_Init(&m_context);
#else
		SHA1_init(&m_context);
#endif
	}

	hasher::hasher(span<char const> data)
		: hasher()
	{
		update(data);
	}

	hasher::hasher(char const* data, int len)
		: hasher()
	{
		TORRENT_ASSERT(len > 0);
		update({data, len});
	}

#ifdef TORRENT_USE_LIBGCRYPT
	hasher::hasher(hasher const& h)
	{
		gcry_md_copy(&m_context, h.m_context);
	}

	hasher& hasher::operator=(hasher const& h) &
	{
		if (this == &h) return *this;
		gcry_md_close(m_context);
		gcry_md_copy(&m_context, h.m_context);
		return *this;
	}
#else
	hasher::hasher(hasher const&) = default;
	hasher& hasher::operator=(hasher const&) & = default;
#endif

	hasher& hasher::update(char const* data, int len)
	{
		return update({data, len});
	}

	hasher& hasher::update(span<char const> data)
	{
		TORRENT_ASSERT(data.size() > 0);
#ifdef TORRENT_USE_LIBGCRYPT
		gcry_md_write(m_context, data.data(), static_cast<std::size_t>(data.size()));
#elif TORRENT_USE_COMMONCRYPTO
		CC_SHA1_Update(&m_context, reinterpret_cast<unsigned char const*>(data.data()), CC_LONG(data.size()));
#elif TORRENT_USE_CRYPTOAPI
		m_context.update(data);
#elif defined TORRENT_USE_LIBCRYPTO
		SHA1_Update(&m_context, reinterpret_cast<unsigned char const*>(data.data())
			, static_cast<std::size_t>(data.size()));
#else
		SHA1_update(&m_context, reinterpret_cast<unsigned char const*>(data.data())
			, static_cast<std::size_t>(data.size()));
#endif
		return *this;
	}

	sha1_hash hasher::final()
	{
		sha1_hash digest;
#ifdef TORRENT_USE_LIBGCRYPT
		gcry_md_final(m_context);
		digest.assign(reinterpret_cast<char const*>(gcry_md_read(m_context, 0)));
#elif TORRENT_USE_COMMONCRYPTO
		CC_SHA1_Final(reinterpret_cast<unsigned char*>(digest.data()), &m_context);
#elif TORRENT_USE_CRYPTOAPI
		m_context.get_hash(digest.data(), digest.size());
#elif defined TORRENT_USE_LIBCRYPTO
		SHA1_Final(reinterpret_cast<unsigned char*>(digest.data()), &m_context);
#else
		SHA1_final(reinterpret_cast<unsigned char*>(digest.data()), &m_context);
#endif
		return digest;
	}

	void hasher::reset()
	{
#ifdef TORRENT_USE_LIBGCRYPT
		gcry_md_reset(m_context);
#elif TORRENT_USE_COMMONCRYPTO
		CC_SHA1_Init(&m_context);
#elif TORRENT_USE_CRYPTOAPI
		m_context.reset();
#elif defined TORRENT_USE_LIBCRYPTO
		SHA1_Init(&m_context);
#else
		SHA1_init(&m_context);
#endif
	}

	hasher::~hasher()
	{
#if defined TORRENT_USE_LIBGCRYPT
		gcry_md_close(m_context);
#endif
	}

#ifdef TORRENT_MACOS_DEPRECATED_LIBCRYPTO
#pragma clang diagnostic pop
#endif
}
