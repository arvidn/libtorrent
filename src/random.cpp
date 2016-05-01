/*

Copyright (c) 2011-2016, Arvid Norberg
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
#include "libtorrent/random.hpp"
#include "libtorrent/assert.hpp"

#include <random>

namespace libtorrent
{
	using std::random_device;
	using std::mt19937;
	using std::uniform_int_distribution;

#ifdef TORRENT_BUILD_SIMULATOR

	boost::uint32_t random()
	{
		// make sure random numbers are deterministic. Seed with a fixed number
		static mt19937 random_engine(4040);
		return uniform_int_distribution<boost::uint32_t>(0, UINT_MAX)(random_engine);
	}

#else

	boost::uint32_t random()
	{
		// TODO: versions prior to msvc-14 (visual studio 2015) do
		// not generate thread safe initialization of statics
		static random_device dev;
		static mt19937 random_engine(dev());
		return uniform_int_distribution<boost::uint32_t>(0, UINT_MAX)(random_engine);
	}

#endif // TORRENT_BUILD_SIMULATOR

	boost::uint32_t randint(int i)
	{
		return random() % i;
	}

}

