/*

Copyright (c) 2013-2014, 2016-2022, Arvid Norberg
Copyright (c) 2016, 2018, 2021, Alden Torres
Copyright (c) 2017, 2019, Andrei Kurushin
Copyright (c) 2017, Steven Siloti
Copyright (c) 2020, Paul-Louis Ageneau
Copyright (c) 2021, Mike Tzou
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/hasher.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/assert.hpp"

#if defined TORRENT_USE_LIBCRYPTO
#include <openssl/opensslv.h> // for OPENSSL_VERSION_NUMBER
#endif

namespace libtorrent {

TORRENT_CRYPTO_NAMESPACE

	hasher::hasher()
	{
#ifdef TORRENT_USE_LIBGCRYPT
		gcry_md_open(&m_context, GCRY_MD_SHA1, 0);
#elif TORRENT_USE_COMMONCRYPTO
		CC_SHA1_Init(&m_context);
#elif TORRENT_USE_CNG
#elif TORRENT_USE_CRYPTOAPI
#elif defined TORRENT_USE_LIBCRYPTO
		m_context = EVP_MD_CTX_new();
		EVP_DigestInit_ex(m_context, EVP_sha1(), nullptr);
#else
		aux::SHA1_init(&m_context);
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
		update({data, len});
	}

#ifdef TORRENT_USE_LIBGCRYPT
	hasher::hasher(hasher const& h)
	{
		gcry_md_copy(&m_context, h.m_context);
	}

	hasher& hasher::operator=(hasher const& h) &
	{
		if (this == &h) return *this;
		gcry_md_close(m_context);
		gcry_md_copy(&m_context, h.m_context);
		return *this;
	}
#elif defined TORRENT_USE_LIBCRYPTO
	hasher::hasher(hasher const& h)
		: hasher()
	{
		EVP_MD_CTX_copy_ex(m_context, h.m_context);
	}

	hasher& hasher::operator=(hasher const& h) &
	{
		if (this == &h) return *this;
		EVP_MD_CTX_copy_ex(m_context, h.m_context);
		return *this;
	}
#else
	hasher::hasher(hasher const&) = default;
	hasher& hasher::operator=(hasher const&) & = default;
#endif

#if defined TORRENT_USE_LIBCRYPTO
	hasher::hasher(hasher&& h)
	{
		std::swap(m_context, h.m_context);
	}

	hasher& hasher::operator=(hasher&& h) &
	{
		if (this == &h) return *this;
		std::swap(m_context, h.m_context);
		return *this;
	}
#else
	hasher::hasher(hasher&&) = default;
	hasher& hasher::operator=(hasher&&) & = default;
#endif

	hasher& hasher::update(char const* data, int len)
	{
		return update({data, len});
	}

	hasher& hasher::update(span<char const> data)
	{
		TORRENT_ASSERT(data.size() > 0);
#ifdef TORRENT_USE_LIBGCRYPT
		gcry_md_write(m_context, data.data(), static_cast<std::size_t>(data.size()));
#elif TORRENT_USE_COMMONCRYPTO
		CC_SHA1_Update(&m_context, reinterpret_cast<unsigned char const*>(data.data()), CC_LONG(data.size()));
#elif TORRENT_USE_CNG
		m_context.update(data);
#elif TORRENT_USE_CRYPTOAPI
		m_context.update(data);
#elif defined TORRENT_USE_LIBCRYPTO
		EVP_DigestUpdate(m_context, reinterpret_cast<unsigned char const*>(data.data())
			, static_cast<std::size_t>(data.size()));
#else
		aux::SHA1_update(&m_context, reinterpret_cast<unsigned char const*>(data.data())
			, static_cast<std::size_t>(data.size()));
#endif
		return *this;
	}

	sha1_hash hasher::final()
	{
		sha1_hash digest;
#ifdef TORRENT_USE_LIBGCRYPT
		gcry_md_final(m_context);
		digest.assign(reinterpret_cast<char const*>(gcry_md_read(m_context, 0)));
#elif TORRENT_USE_COMMONCRYPTO
		CC_SHA1_Final(reinterpret_cast<unsigned char*>(digest.data()), &m_context);
#elif TORRENT_USE_CNG
		m_context.get_hash(digest.data(), digest.size());
#elif TORRENT_USE_CRYPTOAPI
		m_context.get_hash(digest.data(), digest.size());
#elif defined TORRENT_USE_LIBCRYPTO
		EVP_DigestFinal_ex(m_context, reinterpret_cast<unsigned char*>(digest.data()), nullptr);
#else
		aux::SHA1_final(reinterpret_cast<unsigned char*>(digest.data()), &m_context);
#endif
		return digest;
	}

	void hasher::reset()
	{
#ifdef TORRENT_USE_LIBGCRYPT
		gcry_md_reset(m_context);
#elif TORRENT_USE_COMMONCRYPTO
		CC_SHA1_Init(&m_context);
#elif TORRENT_USE_CNG
		m_context.reset();
#elif TORRENT_USE_CRYPTOAPI
		m_context.reset();
#elif defined TORRENT_USE_LIBCRYPTO
		EVP_DigestInit_ex(m_context, EVP_sha1(), nullptr);
#else
		aux::SHA1_init(&m_context);
#endif
	}

	hasher::~hasher()
	{
#if defined TORRENT_USE_LIBGCRYPT
		gcry_md_close(m_context);
#elif defined TORRENT_USE_LIBCRYPTO
		if (m_context) EVP_MD_CTX_free(m_context);
#endif
	}

	hasher256::hasher256()
	{
#ifdef TORRENT_USE_LIBGCRYPT
		gcry_md_open(&m_context, GCRY_MD_SHA256, 0);
#elif TORRENT_USE_COMMONCRYPTO
		CC_SHA256_Init(&m_context);
#elif TORRENT_USE_CNG
#elif TORRENT_USE_CRYPTOAPI_SHA_512
#elif defined TORRENT_USE_LIBCRYPTO
		m_context = EVP_MD_CTX_new();
		EVP_DigestInit_ex(m_context, EVP_sha256(), nullptr);
#else
		aux::SHA256_init(m_context);
#endif
	}

	hasher256::hasher256(span<char const> data)
		: hasher256()
	{
		update(data);
	}

	hasher256::hasher256(char const* data, int len)
		: hasher256()
	{
		TORRENT_ASSERT(len > 0);
		update({ data, len });
	}

#ifdef TORRENT_USE_LIBGCRYPT
	hasher256::hasher256(hasher256 const& h)
	{
		gcry_md_copy(&m_context, h.m_context);
	}

	hasher256& hasher256::operator=(hasher256 const& h) &
	{
		if (this == &h) return *this;
		gcry_md_close(m_context);
		gcry_md_copy(&m_context, h.m_context);
		return *this;
	}
#elif defined TORRENT_USE_LIBCRYPTO
	hasher256::hasher256(hasher256 const& h)
		: hasher256()
	{
		EVP_MD_CTX_copy_ex(m_context, h.m_context);
	}

	hasher256& hasher256::operator=(hasher256 const& h) &
	{
		if (this == &h) return *this;
		EVP_MD_CTX_copy_ex(m_context, h.m_context);
		return *this;
	}
#else
	hasher256::hasher256(hasher256 const&) = default;
	hasher256& hasher256::operator=(hasher256 const&) & = default;
#endif

#if defined TORRENT_USE_LIBCRYPTO
	hasher256::hasher256(hasher256&& h)
	{
		std::swap(m_context, h.m_context);
	}

	hasher256& hasher256::operator=(hasher256&& h) &
	{
		if (this == &h) return *this;
		std::swap(m_context, h.m_context);
		return *this;
	}
#else
	hasher256::hasher256(hasher256&&) = default;
	hasher256& hasher256::operator=(hasher256&&) & = default;
#endif

	hasher256& hasher256::update(char const* data, int len)
	{
		return update({ data, len });
	}

	hasher256& hasher256::update(span<char const> data)
	{
		TORRENT_ASSERT(!data.empty());
#ifdef TORRENT_USE_LIBGCRYPT
		gcry_md_write(m_context, data.data(), data.size());
#elif TORRENT_USE_COMMONCRYPTO
		CC_SHA256_Update(&m_context, reinterpret_cast<unsigned char const*>(data.data()), CC_LONG(data.size()));
#elif TORRENT_USE_CNG
		m_context.update(data);
#elif TORRENT_USE_CRYPTOAPI_SHA_512
		m_context.update(data);
#elif defined TORRENT_USE_LIBCRYPTO
		EVP_DigestUpdate(m_context, reinterpret_cast<unsigned char const*>(data.data())
			, static_cast<std::size_t>(data.size()));
#else
		aux::SHA256_update(m_context, reinterpret_cast<unsigned char const*>(data.data())
			, static_cast<std::size_t>(data.size()));
#endif
		return *this;
	}

	sha256_hash hasher256::final()
	{
		sha256_hash digest;
#ifdef TORRENT_USE_LIBGCRYPT
		gcry_md_final(m_context);
		digest.assign((char const*)gcry_md_read(m_context, 0));
#elif TORRENT_USE_COMMONCRYPTO
		CC_SHA256_Final(reinterpret_cast<unsigned char*>(digest.data()), &m_context);
#elif TORRENT_USE_CNG
		m_context.get_hash(digest.data(), digest.size());
#elif TORRENT_USE_CRYPTOAPI_SHA_512
		m_context.get_hash(digest.data(), digest.size());
#elif defined TORRENT_USE_LIBCRYPTO
		EVP_DigestFinal_ex(m_context, reinterpret_cast<unsigned char*>(digest.data()), nullptr);
#else
		aux::SHA256_final(reinterpret_cast<unsigned char*>(digest.data()), m_context);
#endif
		return digest;
	}

	void hasher256::reset()
	{
#ifdef TORRENT_USE_LIBGCRYPT
		gcry_md_reset(m_context);
#elif TORRENT_USE_COMMONCRYPTO
		CC_SHA256_Init(&m_context);
#elif TORRENT_USE_CNG
		m_context.reset();
#elif TORRENT_USE_CRYPTOAPI_SHA_512
		m_context.reset();
#elif defined TORRENT_USE_LIBCRYPTO
		EVP_DigestInit_ex(m_context, EVP_sha256(), nullptr);
#else
		aux::SHA256_init(m_context);
#endif
	}

	hasher256::~hasher256()
	{
#if defined TORRENT_USE_LIBGCRYPT
		gcry_md_close(m_context);
#elif defined TORRENT_USE_LIBCRYPTO
		if (m_context) EVP_MD_CTX_free(m_context);
#endif
	}

TORRENT_CRYPTO_NAMESPACE_END

}
