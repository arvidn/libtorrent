/*

Copyright (c) 2017, Alden Torres
Copyright (c) 2017-2020, Arvid Norberg
Copyright (c) 2017, 2019, Andrei Kurushin
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_WIN_CRYPTO_PROVIDER_HPP
#define TORRENT_WIN_CRYPTO_PROVIDER_HPP

#include "libtorrent/config.hpp"

#if TORRENT_USE_CRYPTOAPI
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/throw.hpp"
#include "libtorrent/aux_/windows.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <wincrypt.h>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace lt::aux {

	inline HCRYPTPROV crypt_acquire_provider(DWORD provider_type)
	{
		HCRYPTPROV ret;
		if (CryptAcquireContext(&ret, nullptr, nullptr, provider_type
			, CRYPT_VERIFYCONTEXT) == false)
		{
			throw_ex<system_error>(error_code(GetLastError(), system_category()));
		}
		return ret;
	}

	inline void crypt_gen_random(span<char> buffer)
	{
		static HCRYPTPROV provider = crypt_acquire_provider(PROV_RSA_FULL);
		if (!CryptGenRandom(provider, int(buffer.size())
			, reinterpret_cast<BYTE*>(buffer.data())))
		{
			throw_ex<system_error>(error_code(GetLastError(), system_category()));
		}
	}

	template <ALG_ID AlgId, DWORD ProviderType>
	struct crypt_hash
	{
		crypt_hash() { m_hash = create(); }
		crypt_hash(crypt_hash const& h) { m_hash = duplicate(h); }
		~crypt_hash() { CryptDestroyHash(m_hash); }

		crypt_hash& operator=(crypt_hash const& h) &
		{
			if (this == &h) return *this;
			HCRYPTHASH temp = duplicate(h);
			CryptDestroyHash(m_hash);
			m_hash = temp;
			return *this;
		}

		void reset()
		{
			HCRYPTHASH temp = create();
			CryptDestroyHash(m_hash);
			m_hash = temp;
		}

		void update(span<char const> data)
		{
			if (CryptHashData(m_hash, reinterpret_cast<BYTE const*>(data.data()), int(data.size()), 0) == false)
			{
				throw_ex<system_error>(error_code(GetLastError(), system_category()));
			}
		}

		void get_hash(char *digest, std::size_t digest_size)
		{
			DWORD size = DWORD(digest_size);
			if (CryptGetHashParam(m_hash, HP_HASHVAL
				, reinterpret_cast<BYTE*>(digest), &size, 0) == false)
			{
				throw_ex<system_error>(error_code(GetLastError(), system_category()));
			}
			TORRENT_ASSERT(size == DWORD(digest_size));
		}
	private:
		HCRYPTHASH create()
		{
			HCRYPTHASH ret;
			if (CryptCreateHash(get_provider(), AlgId, 0, 0, &ret) == false)
			{
				throw_ex<system_error>(error_code(GetLastError(), system_category()));
			}
			return ret;
		}

		HCRYPTHASH duplicate(crypt_hash const& h)
		{
			HCRYPTHASH ret;
			if (CryptDuplicateHash(h.m_hash, 0, 0, &ret) == false)
			{
				throw_ex<system_error>(error_code(GetLastError(), system_category()));
			}
			return ret;
		}

		HCRYPTPROV get_provider()
		{
			static HCRYPTPROV provider = crypt_acquire_provider(ProviderType);
			return provider;
		}

		HCRYPTHASH m_hash;
	};

} // namespace lt::aux

#endif // TORRENT_USE_CRYPTOAPI

#endif // TORRENT_WIN_CRYPTO_PROVIDER_HPP
