/*

Copyright (c) 2011-2012, 2014-2015, 2017-2020, Arvid Norberg
Copyright (c) 2016, 2019, 2021, Alden Torres
Copyright (c) 2017, 2019, Andrei Kurushin
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/config.hpp"
#include "libtorrent/aux_/random.hpp"
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

#elif TORRENT_USE_GETRANDOM

#include <sys/random.h>
// this is the fall-back in case getrandom() fails
#include "libtorrent/aux_/dev_random.hpp"

#elif TORRENT_USE_DEV_RANDOM
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

namespace lt::aux {

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
					std::uint32_t ret;
					crypto_random_bytes({reinterpret_cast<char*>(&ret), sizeof(ret)});
					return ret;
				}
			} dev;
#else
			static std::random_device dev;
#endif
#ifdef BOOST_NO_CXX11_THREAD_LOCAL
			static std::seed_seq seed({dev(), dev(), dev(), dev()});
			static std::mt19937 rng(seed);
#else
			thread_local static std::seed_seq seed({dev(), dev(), dev(), dev()});
			thread_local static std::mt19937 rng(seed);
#endif
#endif
			return rng;
		}

		void random_bytes(span<char> buffer)
		{
#ifdef TORRENT_BUILD_SIMULATOR
			// simulator
			for (auto& b : buffer) b = char(random(0xff));
#else
			std::generate(buffer.begin(), buffer.end(), [] { return char(random(0xff)); });
#endif
		}

		void crypto_random_bytes(span<char> buffer)
		{
#ifdef TORRENT_BUILD_SIMULATOR
			// In the simulator we want deterministic random numbers
			std::generate(buffer.begin(), buffer.end(), [] { return char(random(0xff)); });
#elif TORRENT_USE_CNG
			aux::cng_gen_random(buffer);
#elif TORRENT_USE_CRYPTOAPI
			// windows
			aux::crypt_gen_random(buffer);
#elif defined TORRENT_USE_LIBCRYPTO && !defined TORRENT_USE_WOLFSSL
// wolfSSL uses wc_RNG_GenerateBlock as the internal function for the
// openssl compatibility layer. This function API does not support
// an arbitrary buffer size (openssl does), it is limited by the
// constant RNG_MAX_BLOCK_LEN.
// TODO: improve calling RAND_bytes multiple times, using fallback for now

			// openssl
			int r = RAND_bytes(reinterpret_cast<unsigned char*>(buffer.data())
				, int(buffer.size()));
			if (r != 1) aux::throw_ex<system_error>(errors::no_entropy);
#elif TORRENT_USE_GETRANDOM
			ssize_t const r = ::getrandom(buffer.data(), static_cast<std::size_t>(buffer.size()), 0);
			if (r == ssize_t(buffer.size())) return;
			if (r == -1 && errno != ENOSYS) aux::throw_ex<system_error>(error_code(errno, generic_category()));
			static dev_random dev;
			dev.read(buffer);
#elif TORRENT_USE_DEV_RANDOM
			static dev_random dev;
			dev.read(buffer);
#else

#if TORRENT_BROKEN_RANDOM_DEVICE
			// even pseudo random numbers rely on being able to seed the random
			// generator
#error "no entropy source available"
#else
#ifdef TORRENT_I_WANT_INSECURE_RANDOM_NUMBERS
			std::generate(buffer.begin(), buffer.end(), [] { return char(random(0xff)); });
#else
#error "no secure entropy source available. If you really want insecure random numbers, define TORRENT_I_WANT_INSECURE_RANDOM_NUMBERS"
#endif
#endif

#endif
		}

	std::uint32_t random(std::uint32_t const max)
	{
#ifdef BOOST_NO_CXX11_THREAD_LOCAL
		std::lock_guard<std::mutex> l(rng_mutex);
#endif
		return std::uniform_int_distribution<std::uint32_t>(0, max)(aux::random_engine());
	}

}
