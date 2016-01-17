/*

Copyright (c) 2014-2016, Arvid Norberg
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

#include "libtorrent/config.hpp"
#include <cstring>

#include "libtorrent/aux_/cpuid.hpp"

#if defined _MSC_VER && TORRENT_HAS_SSE
#include <intrin.h>
#include <nmmintrin.h>
#endif

#if TORRENT_HAS_SSE && defined __GNUC__
#include <cpuid.h>
#endif

namespace libtorrent { namespace aux
{
	namespace {

	// internal
	void cpuid(unsigned int info[4], int type)
	{
#if TORRENT_HAS_SSE && defined _MSC_VER
		__cpuid((int*)info, type);

#elif TORRENT_HAS_SSE && defined __GNUC__
		__get_cpuid(type, &info[0], &info[1], &info[2], &info[3]);
#else
		// for non-x86 and non-amd64, just return zeroes
		std::memset(&info[0], 0, sizeof(unsigned int) * 4);
#endif
	}

	bool supports_sse42()
	{
#if TORRENT_HAS_SSE
		unsigned int cpui[4];
		cpuid(cpui, 1);
		return cpui[2] & (1 << 20);
#else
		return false;
#endif
	}

	bool supports_mmx()
	{
#if TORRENT_HAS_SSE
		unsigned int cpui[4];
		cpuid(cpui, 1);
		return cpui[2] & (1 << 23);
#else
		return false;
#endif
	}

	} // anonymous namespace

	bool sse42_support = supports_sse42();
	bool mmx_support = supports_mmx();
} }


