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

#ifdef BOOST_NO_CXX11_HDR_RANDOM
#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/random/random_device.hpp>
#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_int_distribution.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"
#else
#include <random>
#endif

#if !TORRENT_THREADSAFE_STATIC
#include "libtorrent/thread.hpp"
#endif

namespace libtorrent
{
#ifdef BOOST_NO_CXX11_HDR_RANDOM
	using boost::random::random_device;
	using boost::random::mt19937;
	using boost::random::uniform_int_distribution;
#else
	using std::random_device;
	using std::mt19937;
	using std::uniform_int_distribution;
#endif

#ifdef TORRENT_BUILD_SIMULATOR

	boost::uint32_t random()
	{
		// make sure random numbers are deterministic. Seed with a fixed number
		static mt19937 random_engine(4040);
		return uniform_int_distribution<boost::uint32_t>(0, UINT_MAX)(random_engine);
	}

#else

#if !TORRENT_THREADSAFE_STATIC
	// because local statics are not atomic pre c++11
	// do it manually, probably at a higher cost
	namespace
	{
		static mutex random_device_mutex;
		static random_device* dev = NULL;
		static mt19937* rnd = NULL;
	}
#endif

	boost::uint32_t random()
	{
#if TORRENT_THREADSAFE_STATIC
		static random_device dev;
		static mt19937 random_engine(dev());
		return uniform_int_distribution<boost::uint32_t>(0, UINT_MAX)(random_engine);
#else
		mutex::scoped_lock l(random_device_mutex);

		if (dev == NULL)
		{
			dev = new random_device();
			rnd = new mt19937((*dev)());
		}
		return uniform_int_distribution<boost::uint32_t>(0, UINT_MAX)(*rnd);
#endif
	}

#endif // TORRENT_BUILD_SIMULATOR

	boost::uint32_t randint(int i)
	{
		return random() % i;
	}

}

