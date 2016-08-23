/*

Copyright (c) 2003-2016, Arvid Norberg
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

#include "libtorrent/hasher.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/assert.hpp"

#if TORRENT_USE_CRYPTOAPI
namespace
{
	HCRYPTPROV make_crypt_provider()
	{
		using namespace libtorrent;

		HCRYPTPROV ret;
		if (CryptAcquireContext(&ret, nullptr, nullptr, PROV_RSA_FULL
			, CRYPT_VERIFYCONTEXT) == false)
		{
#ifndef BOOST_NO_EXCEPTIONS
			throw system_error(error_code(GetLastError(), system_category()));
#else
			std::terminate();
#endif
		}
		return ret;
	}

	HCRYPTPROV get_crypt_provider()
	{
		static HCRYPTPROV prov = make_crypt_provider();
		return prov;
	}
}
#endif

namespace libtorrent
{
	hasher::hasher()
	{
#ifdef TORRENT_USE_LIBGCRYPT
		gcry_md_open(&m_context, GCRY_MD_SHA1, 0);
#elif TORRENT_USE_COMMONCRYPTO
		CC_SHA1_Init(&m_context);
#elif TORRENT_USE_CRYPTOAPI
		if (CryptCreateHash(get_crypt_provider(), CALG_SHA1, 0, 0, &m_context) == false)
		{
#ifndef BOOST_NO_EXCEPTIONS
			throw system_error(error_code(GetLastError(), system_category()));
#else
			std::terminate();
#endif
		}
#elif defined TORRENT_USE_LIBCRYPTO
		SHA1_Init(&m_context);
#else
		SHA1_init(&m_context);
#endif
	}

	hasher::hasher(span<char const> data)
		: hasher()
	{
		update(data);
	}

	hasher::hasher(char const* data, int len)
		: hasher()
	{
		TORRENT_ASSERT(len > 0);
		update({data, size_t(len)});
	}

#ifdef TORRENT_USE_LIBGCRYPT
	hasher::hasher(hasher const& h)
	{
		gcry_md_copy(&m_context, h.m_context);
	}

	hasher& hasher::operator=(hasher const& h)
	{
		if (this == &h) return;
		gcry_md_close(m_context);
		gcry_md_copy(&m_context, h.m_context);
		return *this;
	}
#elif TORRENT_USE_CRYPTOAPI
	hasher::hasher(hasher const& h)
	{
		if (CryptDuplicateHash(h.m_context, 0, 0, &m_context) == false)
		{
#ifndef BOOST_NO_EXCEPTIONS
			throw system_error(error_code(GetLastError(), system_category()));
#else
			std::terminate();
#endif
		}
	}

	hasher& hasher::operator=(hasher const& h)
	{
		if (this == &h) return *this;
		CryptDestroyHash(m_context);
		if (CryptDuplicateHash(h.m_context, 0, 0, &m_context) == false)
		{
#ifndef BOOST_NO_EXCEPTIONS
			throw system_error(error_code(GetLastError(), system_category()));
#else
			std::terminate();
#endif
		}
		return *this;
	}
#else
	hasher::hasher(hasher const&) = default;
	hasher& hasher::operator=(hasher const&) = default;
#endif

	hasher& hasher::update(char const* data, int len)
	{
		return update({data, size_t(len)});
	}

	hasher& hasher::update(span<char const> data)
	{
		TORRENT_ASSERT(!data.empty());
#ifdef TORRENT_USE_LIBGCRYPT
		gcry_md_write(m_context, data.data(), data.size());
#elif TORRENT_USE_COMMONCRYPTO
		CC_SHA1_Update(&m_context, reinterpret_cast<unsigned char const*>(data.data()), data.size());
#elif TORRENT_USE_CRYPTOAPI
		if (CryptHashData(m_context, reinterpret_cast<BYTE const*>(data.data()), int(data.size()), 0) == false)
		{
#ifndef BOOST_NO_EXCEPTIONS
			throw system_error(error_code(GetLastError(), system_category()));
#else
			std::terminate();
#endif
		}
#elif defined TORRENT_USE_LIBCRYPTO
		SHA1_Update(&m_context, reinterpret_cast<unsigned char const*>(data.data()), data.size());
#else
		SHA1_update(&m_context, reinterpret_cast<unsigned char const*>(data.data()), data.size());
#endif
		return *this;
	}

	sha1_hash hasher::final()
	{
		sha1_hash digest;
#ifdef TORRENT_USE_LIBGCRYPT
		gcry_md_final(m_context);
		digest.assign((char const*)gcry_md_read(m_context, 0));
#elif TORRENT_USE_COMMONCRYPTO
		CC_SHA1_Final(reinterpret_cast<unsigned char*>(digest.data()), &m_context);
#elif TORRENT_USE_CRYPTOAPI

		DWORD size = DWORD(digest.size());
		if (CryptGetHashParam(m_context, HP_HASHVAL
			, reinterpret_cast<BYTE*>(digest.data()), &size, 0) == false)
		{
#ifndef BOOST_NO_EXCEPTIONS
			throw system_error(error_code(GetLastError(), system_category()));
#else
			std::terminate();
#endif
		}
		TORRENT_ASSERT(size == digest.size());
#elif defined TORRENT_USE_LIBCRYPTO
		SHA1_Final(reinterpret_cast<unsigned char*>(digest.data()), &m_context);
#else
		SHA1_final(reinterpret_cast<unsigned char*>(digest.data()), &m_context);
#endif
		return digest;
	}

	void hasher::reset()
	{
#ifdef TORRENT_USE_LIBGCRYPT
		gcry_md_reset(m_context);
#elif TORRENT_USE_COMMONCRYPTO
		CC_SHA1_Init(&m_context);
#elif TORRENT_USE_CRYPTOAPI
		CryptDestroyHash(m_context);
		if (CryptCreateHash(get_crypt_provider(), CALG_SHA1, 0, 0, &m_context) == false)
		{
#ifndef BOOST_NO_EXCEPTIONS
			throw system_error(error_code(GetLastError(), system_category()));
#else
			std::terminate();
#endif
		}
#elif defined TORRENT_USE_LIBCRYPTO
		SHA1_Init(&m_context);
#else
		SHA1_init(&m_context);
#endif
	}

	hasher::~hasher()
	{
#if TORRENT_USE_CRYPTOAPI
		CryptDestroyHash(m_context);
#elif defined TORRENT_USE_LIBGCRYPT
		gcry_md_close(m_context);
#endif
	}
}
