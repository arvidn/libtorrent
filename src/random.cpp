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
#include "libtorrent/error_code.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

#if TORRENT_USE_CRYPTOAPI
#include <windows.h>
#include <wincrypt.h>

#elif defined TORRENT_USE_LIBCRYPTO
extern "C" {
#include <openssl/rand.h>
#include <openssl/err.h>
}

#endif

#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent
{
	namespace aux
	{
		std::mt19937& random_engine()
		{
#ifdef TORRENT_BUILD_SIMULATOR
			// make sure random numbers are deterministic. Seed with a fixed number
			static std::mt19937 rng(0x82daf973);
#else
			static std::random_device dev;
			static std::mt19937 rng(dev());
#endif
			return rng;
		}

		void random_bytes(span<char> buffer)
		{
#if TORRENT_USE_CRYPTOAPI
			HCRYPTPROV prov;

			if (!CryptAcquireContext(&prov, NULL, NULL
				, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
			{
#ifndef BOOST_NO_EXCEPTIONS
				throw system_error(error_code(GetLastError(), system_category()));
#else
				std::terminate();
#endif
			}

			if (!CryptGenRandom(prov, int(buffer.size())
				, reinterpret_cast<BYTE*>(buffer.data())))
			{
				CryptReleaseContext(prov, 0);
#ifndef BOOST_NO_EXCEPTIONS
				throw system_error(error_code(GetLastError(), system_category()));
#else
				std::terminate();
#endif
			}

			CryptReleaseContext(prov, 0);
#elif defined TORRENT_USE_LIBCRYPTO
			int r = RAND_bytes(reinterpret_cast<unsigned char*>(buffer.data())
				, int(buffer.size()));
			if (r != 1)
			{
#ifndef BOOST_NO_EXCEPTIONS
				throw system_error(error_code(ERR_get_error(), system_category()));
#else
				std::terminate();
#endif
			}
#else
			std::uint32_t s = random(0xffffffff);
			std::independent_bits_engine<std::mt19937, 8, std::uint8_t> generator(s);
			std::generate(buffer.begin(), buffer.end(), std::ref(generator));
#endif
		}
	}

	std::uint32_t random(std::uint32_t max)
	{
		return std::uniform_int_distribution<std::uint32_t>(0, max)(aux::random_engine());
	}
}
