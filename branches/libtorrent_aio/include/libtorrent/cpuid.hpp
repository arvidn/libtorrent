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

#ifndef TORRENT_CPUID_HPP_INCLUDED
#define TORRENT_CPUID_HPP_INCLUDED

#if defined _M_AMD64 || defined _M_IX86 \
	|| defined __amd64__ || defined __i386 || defined __i386__ \
	|| defined __x86_64__
#define TORRENT_X86 1
#else
#define TORRENT_X86 0
#endif

#if defined __GNUC__ && TORRENT_X86
#include <cpuid.h>
#include <smmintrin.h>
#endif

#ifdef _MSC_VER
#include <intrin.h>
#include <nmmintrin.h>
#endif

namespace libtorrent
{
	inline void cpuid(unsigned int info[4], int type)
	{
#if TORRENT_X86
		// cpuinfo is x86 and amd64 specific
#ifdef _MSC_VER
		__cpuid((int*)info, type);

#elif defined __GNUC__
		__get_cpuid(type, &info[0], &info[1], &info[2], &info[3]);

#else
#warning "don't know how to get cpuinfo with this compiler"
		memset(info, 0, sizeof(info));
#endif

#else

		// for non-x86 and non-amd64, just return zeroes
		memset(info, 0, sizeof(info));

#endif // x86 or amd64
	}
}

#endif // TORRENT_CPUID_HPP_INCLUDED

