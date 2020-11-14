/*

Copyright (c) 2019, Andrei Kurushin
Copyright (c) 2020, Arvid Norberg
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

#ifndef TORRENT_WIN_CNG_HPP
#define TORRENT_WIN_CNG_HPP

#include <vector>

#include "libtorrent/config.hpp"

#if TORRENT_USE_CNG
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/throw.hpp"
#include "libtorrent/aux_/windows.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <bcrypt.h>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent { namespace aux {

	inline void throw_ntstatus_error(char const* name, NTSTATUS status) {
		throw_ex<std::system_error>(status, system_category(), name);
	}

	inline BCRYPT_ALG_HANDLE cng_open_algorithm_handle(LPCWSTR alg_name)
	{
		BCRYPT_ALG_HANDLE algorithm_handle{ 0 };
		NTSTATUS status =
			BCryptOpenAlgorithmProvider(&algorithm_handle, alg_name, nullptr, 0);
		if (status < 0) {
			throw_ntstatus_error("BCryptOpenAlgorithmProvider", status);
		}
		return algorithm_handle;
	}

	inline DWORD cng_get_algorithm_object_size(
		BCRYPT_ALG_HANDLE algorithm_handle)
	{
		DWORD object_size{ 0 };
		DWORD data_size{ 0 };
		NTSTATUS status = BCryptGetProperty(algorithm_handle,
			BCRYPT_OBJECT_LENGTH, (PBYTE)&object_size, sizeof(DWORD),
			&data_size, 0);
		if (status < 0) {
			throw_ntstatus_error("BCryptGetProperty BCRYPT_OBJECT_LENGTH",
				status);
		}

		return object_size;
	}

	inline void cng_gen_random(span<char> buffer)
	{
		static BCRYPT_ALG_HANDLE algorithm_handle =
			cng_open_algorithm_handle(BCRYPT_RNG_ALGORITHM);

		NTSTATUS status = BCryptGenRandom(algorithm_handle,
			reinterpret_cast<PUCHAR>(buffer.data()),
			static_cast<ULONG>(buffer.size()), 0);
		if (status < 0) {
			throw_ntstatus_error("BCryptGenRandom", status);
		}
	}

	template <typename AlgId>
	struct cng_hash
	{
		cng_hash() { create(); }
		cng_hash(cng_hash const& h) { duplicate(h); }
		~cng_hash()
		{
			destroy();
		}

		cng_hash& operator=(cng_hash const& h) &
		{
			if (this == &h) return *this;
			if (m_hash == h.m_hash) return *this;
			destroy();
			duplicate(h);
			return *this;
		}

		void reset()
		{
			destroy();
			create();
		}

		void update(span<char const> data)
		{
			NTSTATUS status = BCryptHashData(
				m_hash,
				(PUCHAR)(data.data()),
				static_cast<ULONG>(data.size()), 0);
			if (status < 0) {
				throw_ntstatus_error("BCryptHashData", status);
			}
		}

		void get_hash(char *digest, std::size_t digest_size)
		{
			NTSTATUS status = BCryptFinishHash(m_hash,
				reinterpret_cast<PUCHAR>(digest),
				static_cast<ULONG>(digest_size), 0);
			if (status < 0) {
				throw_ntstatus_error("BCryptFinishHash", status);
			}
		}
	private:
		void create()
		{
			NTSTATUS status = BCryptCreateHash(get_algorithm_handle(),
				&m_hash, m_hash_object.data(), m_hash_object.size(),
				nullptr, 0, 0);
			if (status < 0) {
				throw_ntstatus_error("BCryptCreateHash", status);
			}
		}

		void destroy()
		{
			NTSTATUS status = BCryptDestroyHash(m_hash);
			if (status < 0) {
				throw_ntstatus_error("BCryptDestroyHash", status);
			}
		}

		void duplicate(cng_hash const& h)
		{
			NTSTATUS status = BCryptDuplicateHash(h.m_hash,
				&m_hash, m_hash_object.data(), m_hash_object.size(), 0);
			if (status < 0) {
				throw_ntstatus_error("BCryptDuplicateHash", status);
			}
		}

		BCRYPT_ALG_HANDLE get_algorithm_handle()
		{
			static BCRYPT_ALG_HANDLE algorithm_handle =
					cng_open_algorithm_handle(AlgId::name);
			return algorithm_handle;
		}

		std::size_t get_algorithm_object_size()
		{
			static std::size_t object_size =
				static_cast<std::size_t>(
					cng_get_algorithm_object_size(get_algorithm_handle()));
			return object_size;
		}

		using hash_object_t = std::vector<UCHAR>;

		BCRYPT_HASH_HANDLE m_hash;
		hash_object_t m_hash_object
			= hash_object_t(get_algorithm_object_size());
	};

	struct cng_sha1_algorithm {
		static constexpr LPCWSTR name = BCRYPT_SHA1_ALGORITHM;
	};

	struct cng_sha256_algorithm {
		static constexpr LPCWSTR name = BCRYPT_SHA256_ALGORITHM;
	};

	struct cng_sha512_algorithm {
		static constexpr LPCWSTR name = BCRYPT_SHA512_ALGORITHM;
	};

} // namespace aux
} // namespace libtorrent

#endif // TORRENT_USE_CNG

#endif // TORRENT_WIN_CNG_HPP
