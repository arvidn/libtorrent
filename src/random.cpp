/*

Copyright (c) 2011-2018, Arvid Norberg
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
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/openssl.hpp"
#include "libtorrent/aux_/throw.hpp"

#if defined BOOST_NO_CXX11_THREAD_LOCAL
#include <mutex>
#endif

#if TORRENT_BROKEN_RANDOM_DEVICE
#include "libtorrent/time.hpp"
#include <atomic>
#endif

#if TORRENT_USE_CRYPTOAPI
#include "libtorrent/aux_/win_crypto_provider.hpp"

#elif defined TORRENT_USE_LIBCRYPTO

#include "libtorrent/aux_/disable_warnings_push.hpp"
extern "C" {
#include <openssl/rand.h>
#include <openssl/err.h>
}
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#endif

#if TORRENT_USE_DEV_RANDOM
#include "libtorrent/aux_/dev_random.hpp"
#endif

#ifdef BOOST_NO_CXX11_THREAD_LOCAL
namespace {
	// if the random number generator can't be thread local, just protect it with
	// a mutex. Not ideal, but hopefully not too many people are affected by old
	// systems
	std::mutex rng_mutex;
}
#endif

namespace libtorrent {
namespace aux {

		std::mt19937& random_engine()
		{
#ifdef TORRENT_BUILD_SIMULATOR
			// make sure random numbers are deterministic. Seed with a fixed number
			static std::mt19937 rng(0x82daf973);
#else

#if TORRENT_BROKEN_RANDOM_DEVICE
			struct {
				std::uint32_t operator()() const
				{
					static std::atomic<std::uint32_t> seed{static_cast<std::uint32_t>(duration_cast<microseconds>(
						std::chrono::high_resolution_clock::now().time_since_epoch()).count())};
					return seed++;
				}
			} dev;
#else
			static std::random_device dev;
#endif
#ifdef BOOST_NO_CXX11_THREAD_LOCAL
			static std::mt19937 rng(dev());
#else
			thread_local static std::mt19937 rng(dev());
#endif
#endif
			return rng;
		}

		void random_bytes(span<char> buffer)
		{
#ifdef TORRENT_BUILD_SIMULATOR
			// simulator

			for (auto& b : buffer) b = char(random(0xff));

#elif TORRENT_USE_CRYPTOAPI
			// windows

			aux::crypt_gen_random(buffer);

#elif TORRENT_USE_DEV_RANDOM
			// /dev/random

			static dev_random dev;
			dev.read(buffer);

#elif defined TORRENT_USE_LIBCRYPTO
			// openssl

			int r = RAND_bytes(reinterpret_cast<unsigned char*>(buffer.data())
				, int(buffer.size()));
			if (r != 1) aux::throw_ex<system_error>(errors::no_entropy);
#else
			// fallback

			std::generate(buffer.begin(), buffer.end(), [] { return char(random(0xff)); });
#endif
		}
	}

	std::uint32_t random(std::uint32_t const max)
	{
#ifdef BOOST_NO_CXX11_THREAD_LOCAL
		std::lock_guard<std::mutex> l(rng_mutex);
#endif
		return std::uniform_int_distribution<std::uint32_t>(0, max)(aux::random_engine());
	}
}
