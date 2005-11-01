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

#include <cassert>
#include <boost/cstdint.hpp>

#include "libtorrent/peer_id.hpp"
#include "libtorrent/config.hpp"

// from sha1.cpp
struct TORRENT_EXPORT SHA1_CTX
{
	boost::uint32_t state[5];
	boost::uint32_t count[2];
	boost::uint8_t buffer[64];
};

TORRENT_EXPORT void SHA1Init(SHA1_CTX* context);
TORRENT_EXPORT void SHA1Update(SHA1_CTX* context, boost::uint8_t const* data, boost::uint32_t len);
TORRENT_EXPORT void SHA1Final(SHA1_CTX* context, boost::uint8_t* digest);

extern "C"
{
	// from zlib/adler32.c
	unsigned long adler32(unsigned long adler, const char* data, unsigned int len);
}

namespace libtorrent
{

	class adler32_crc
	{
	public:
		adler32_crc(): m_adler(adler32(0, 0, 0)) {}

		void update(const char* data, int len)
		{
			assert(data != 0);
			assert(len > 0);
			m_adler = adler32(m_adler, data, len);
		}
		unsigned long final() const { return m_adler; }
		void reset() { m_adler = adler32(0, 0, 0); }

	private:

		unsigned long m_adler;

	};

	class hasher
	{
	public:

		hasher() { SHA1Init(&m_context); }
		hasher(const char* data, int len)
		{
			SHA1Init(&m_context);
			assert(data != 0);
			assert(len > 0);
			SHA1Update(&m_context, reinterpret_cast<unsigned char const*>(data), len);
		}
		void update(const char* data, int len)
		{
			assert(data != 0);
			assert(len > 0);
			SHA1Update(&m_context, reinterpret_cast<unsigned char const*>(data), len);
		}

		sha1_hash final()
		{
			sha1_hash digest;
			SHA1Final(&m_context, digest.begin());
			return digest;
		}

		void reset() { SHA1Init(&m_context); }

	private:

		SHA1_CTX m_context;

	};
}

#endif // TORRENT_HASHER_HPP_INCLUDED
