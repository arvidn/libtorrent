/*

Copyright (c) 2014, Arvid Norberg
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

#include "libtorrent/crc32c.hpp"
#include "libtorrent/cpuid.hpp"

#include <boost/crc.hpp>

namespace libtorrent
{
	bool supports_sse42()
	{
#if defined _MSC_VER || defined __GNUC__
		unsigned int cpui[4];
		cpuid(cpui, 1);
		return cpui[2] & (1 << 20);
#else
		return false;
#endif
	}

	static bool sse42_support = supports_sse42();

	boost::uint32_t crc32c_32(boost::uint32_t v)
	{
#if (defined _MSC_VER || defined __GNUC__) && TORRENT_X86 && defined __SSE4_1__
		if (sse42_support)
		{
			boost::uint32_t ret = 0xffffffff;
			return _mm_crc32_u32(ret, v) ^ 0xffffffff;
		}
#endif

		boost::crc_optimal<32, 0x1EDC6F41, 0xFFFFFFFF, 0xFFFFFFFF, true, true> crc;
		crc.process_bytes(&v, 4);
		return crc.checksum();
	}

	boost::uint32_t crc32c(boost::uint64_t const* buf, int num_words)
	{
#if (defined _MSC_VER || defined __GNUC__) \
	&& TORRENT_X86 && defined __SSE4_1__
		if (sse42_support)
		{
#if defined __LP64__ || defined _M_AMD64
			boost::uint64_t ret = 0xffffffff;
			for (int i = 0; i < num_words; ++i)
				ret = _mm_crc32_u64(ret, buf[i]);
			return boost::uint32_t(ret) ^ 0xffffffff;
#else
			boost::uint32_t ret = 0xffffffff;
			boost::uint32_t const* buf0 = reinterpret_cast<boost::uint32_t const*>(buf);
			for (int i = 0; i < num_words; ++i)
			{
				ret = _mm_crc32_u32(ret, buf0[i*2]);
				ret = _mm_crc32_u32(ret, buf0[i*2+1]);
			}
			return ret ^ 0xffffffff;
#endif // amd64 or x86
		}
#endif // x86 or amd64 and gcc or msvc
	
		boost::crc_optimal<32, 0x1EDC6F41, 0xFFFFFFFF, 0xFFFFFFFF, true, true> crc;
		crc.process_bytes(buf, num_words * 8);
		return crc.checksum();
	}
}


