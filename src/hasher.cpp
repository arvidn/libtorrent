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

#include "libtorrent/hasher.hpp"
#include "libtorrent/sha1.hpp"

namespace libtorrent
{
	hasher::hasher()
	{
#ifdef TORRENT_USE_GCRYPT
		gcry_md_open(&m_context, GCRY_MD_SHA1, 0);
#elif TORRENT_USE_COMMONCRYPTO
		CC_SHA1_Init(&m_context);
#elif defined TORRENT_USE_OPENSSL
		SHA1_Init(&m_context);
#else
		SHA1_init(&m_context);
#endif
	}

	hasher::hasher(const char* data, int len)
	{
		TORRENT_ASSERT(data != 0);
		TORRENT_ASSERT(len > 0);
#ifdef TORRENT_USE_GCRYPT
		gcry_md_open(&m_context, GCRY_MD_SHA1, 0);
		gcry_md_write(m_context, data, len);
#elif TORRENT_USE_COMMONCRYPTO
		CC_SHA1_Init(&m_context);
		CC_SHA1_Update(&m_context, reinterpret_cast<unsigned char const*>(data), len);
#elif defined TORRENT_USE_OPENSSL
		SHA1_Init(&m_context);
		SHA1_Update(&m_context, reinterpret_cast<unsigned char const*>(data), len);
#else
		SHA1_init(&m_context);
		SHA1_update(&m_context, reinterpret_cast<unsigned char const*>(data), len);
#endif
	}

#ifdef TORRENT_USE_GCRYPT
	hasher::hasher(hasher const& h)
	{
		gcry_md_copy(&m_context, h.m_context);
	}

	hasher& hasher::operator=(hasher const& h)
	{
		gcry_md_close(m_context);
		gcry_md_copy(&m_context, h.m_context);
		return *this;
	}
#endif

	hasher& hasher::update(const char* data, int len)
	{
		TORRENT_ASSERT(data != 0);
		TORRENT_ASSERT(len > 0);
#ifdef TORRENT_USE_GCRYPT
		gcry_md_write(m_context, data, len);
#elif TORRENT_USE_COMMONCRYPTO
		CC_SHA1_Update(&m_context, reinterpret_cast<unsigned char const*>(data), len);
#elif defined TORRENT_USE_OPENSSL
		SHA1_Update(&m_context, reinterpret_cast<unsigned char const*>(data), len);
#else
		SHA1_update(&m_context, reinterpret_cast<unsigned char const*>(data), len);
#endif
		return *this;
	}

	sha1_hash hasher::final()
	{
		sha1_hash digest;
#ifdef TORRENT_USE_GCRYPT
		gcry_md_final(m_context);
		digest.assign((const char*)gcry_md_read(m_context, 0));
#elif TORRENT_USE_COMMONCRYPTO
		CC_SHA1_Final(digest.begin(), &m_context);
#elif defined TORRENT_USE_OPENSSL
		SHA1_Final(digest.begin(), &m_context);
#else
		SHA1_final(digest.begin(), &m_context);
#endif
		return digest;
	}

	void hasher::reset()
	{
#ifdef TORRENT_USE_GCRYPT
		gcry_md_reset(m_context);
#elif TORRENT_USE_COMMONCRYPTO
		CC_SHA1_Init(&m_context);
#elif defined TORRENT_USE_OPENSSL
		SHA1_Init(&m_context);
#else
		SHA1_init(&m_context);
#endif
	}

	hasher::~hasher()
	{
#ifdef TORRENT_USE_GCRYPT
		gcry_md_close(m_context);
#endif
	}

}

