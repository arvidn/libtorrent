/*
SHA-1 C++ conversion

original version:

SHA-1 in C
By Steve Reid <sreid@sea-to-sky.net>
100% Public Domain

changelog at the end of sha1.cpp
*/

#ifndef TORRENT_SHA1_HPP_INCLUDED
#define TORRENT_SHA1_HPP_INCLUDED

#include "libtorrent/config.hpp"

#if !defined TORRENT_USE_LIBGCRYPT \
	&& !TORRENT_USE_COMMONCRYPTO \
	&& !TORRENT_USE_CRYPTOAPI \
	&& !defined TORRENT_USE_LIBCRYPTO

#include <cstdint>

namespace libtorrent {

	struct sha1_ctx
	{
		std::uint32_t state[5];
		std::uint32_t count[2];
		std::uint8_t buffer[64];
	};

	// we don't want these to clash with openssl's libcrypto
	TORRENT_EXTRA_EXPORT void SHA1_init(sha1_ctx* context);
	TORRENT_EXTRA_EXPORT void SHA1_update(sha1_ctx* context
		, std::uint8_t const* data, size_t len);
	TORRENT_EXTRA_EXPORT void SHA1_final(std::uint8_t* digest, sha1_ctx* context);
}

#endif
#endif
