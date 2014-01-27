/*

Copyright (c) 2003, Arvid Norberg
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

#include <boost/cstdint.hpp>

#include "libtorrent/peer_id.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/assert.hpp"

#ifdef TORRENT_USE_GCRYPT
#include <gcrypt.h>
#elif defined TORRENT_USE_OPENSSL
extern "C"
{
#include <openssl/sha.h>
}
#else
// from sha1.cpp
struct TORRENT_EXTRA_EXPORT SHA_CTX
{
	boost::uint32_t state[5];
	boost::uint32_t count[2];
	boost::uint8_t buffer[64];
};

TORRENT_EXTRA_EXPORT void SHA1_Init(SHA_CTX* context);
TORRENT_EXTRA_EXPORT void SHA1_Update(SHA_CTX* context, boost::uint8_t const* data, boost::uint32_t len);
TORRENT_EXTRA_EXPORT void SHA1_Final(boost::uint8_t* digest, SHA_CTX* context);

#endif

namespace libtorrent
{
	class TORRENT_EXTRA_EXPORT hasher
	{
	public:

		hasher()
		{
#ifdef TORRENT_USE_GCRYPT
			gcry_md_open(&m_context, GCRY_MD_SHA1, 0);
#else
			SHA1_Init(&m_context);
#endif
		}
		hasher(const char* data, int len)
		{
			TORRENT_ASSERT(data != 0);
			TORRENT_ASSERT(len > 0);
#ifdef TORRENT_USE_GCRYPT
			gcry_md_open(&m_context, GCRY_MD_SHA1, 0);
			gcry_md_write(m_context, data, len);
#else
			SHA1_Init(&m_context);
			SHA1_Update(&m_context, reinterpret_cast<unsigned char const*>(data), len);
#endif
		}

#ifdef TORRENT_USE_GCRYPT
		hasher(hasher const& h)
		{
			gcry_md_copy(&m_context, h.m_context);
		}

		hasher& operator=(hasher const& h)
		{
			gcry_md_close(m_context);
			gcry_md_copy(&m_context, h.m_context);
			return *this;
		}
#endif

		hasher& update(std::string const& data) { update(data.c_str(), data.size()); return *this; }
		hasher& update(const char* data, int len)
		{
			TORRENT_ASSERT(data != 0);
			TORRENT_ASSERT(len > 0);
#ifdef TORRENT_USE_GCRYPT
			gcry_md_write(m_context, data, len);
#else
			SHA1_Update(&m_context, reinterpret_cast<unsigned char const*>(data), len);
#endif
			return *this;
		}

		sha1_hash final()
		{
			sha1_hash digest;
#ifdef TORRENT_USE_GCRYPT
			gcry_md_final(m_context);
			digest.assign((const char*)gcry_md_read(m_context, 0));
#else
			SHA1_Final(digest.begin(), &m_context);
#endif
			return digest;
		}

		void reset()
		{
#ifdef TORRENT_USE_GCRYPT
			gcry_md_reset(m_context);
#else
			SHA1_Init(&m_context);
#endif
		}

#ifdef TORRENT_USE_GCRYPT
		~hasher()
		{
			gcry_md_close(m_context);
		}
#endif

	private:

#ifdef TORRENT_USE_GCRYPT
		gcry_md_hd_t m_context;
#else
		SHA_CTX m_context;
#endif
	};
}

#endif // TORRENT_HASHER_HPP_INCLUDED

