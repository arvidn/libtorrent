/*

Copyright (c) 2011-2012, 2014-2015, 2017-2020, Arvid Norberg
Copyright (c) 2016, 2019, Alden Torres
Copyright (c) 2017, 2019, Andrei Kurushin
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/config.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/throw.hpp"

#if defined BOOST_NO_CXX11_THREAD_LOCAL
#include <mutex>
#endif

#if TORRENT_BROKEN_RANDOM_DEVICE
#include "libtorrent/time.hpp"
#include <atomic>
#endif

#if TORRENT_USE_CNG
#include "libtorrent/aux_/win_cng.hpp"

#elif TORRENT_USE_CRYPTOAPI
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

			std::generate(buffer.begin(), buffer.end(), [] { return char(random(0xff)); });

#elif TORRENT_USE_CNG
			aux::cng_gen_random(buffer);
#elif TORRENT_USE_CRYPTOAPI
			// windows

			aux::crypt_gen_random(buffer);

#elif TORRENT_USE_DEV_RANDOM
			// /dev/random

			static dev_random dev;
			dev.read(buffer);

#elif defined TORRENT_USE_LIBCRYPTO

#if defined TORRENT_USE_WOLFSSL
// wolfSSL uses wc_RNG_GenerateBlock as the internal function for the
// openssl compatibility layer. This function API does not support
// an arbitrary buffer size (openssl does), it is limited by the
// constant RNG_MAX_BLOCK_LEN.
// TODO: improve calling RAND_bytes multiple times, using fallback for now
			std::generate(buffer.begin(), buffer.end(), [] { return char(random(0xff)); });
#else // TORRENT_USE_WOLFSSL
			// openssl

			int r = RAND_bytes(reinterpret_cast<unsigned char*>(buffer.data())
				, int(buffer.size()));
			if (r != 1) aux::throw_ex<system_error>(errors::no_entropy);
#endif

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
