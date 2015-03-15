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
#include <boost/cstdint.hpp>

namespace libtorrent
{

	struct TORRENT_EXTRA_EXPORT sha_ctx
	{
		boost::uint32_t state[5];
		boost::uint32_t count[2];
		boost::uint8_t buffer[64];
	};

	// we don't want these to clash with openssl's libcrypto
	TORRENT_EXTRA_EXPORT void SHA1_init(sha_ctx* context);
	TORRENT_EXTRA_EXPORT void SHA1_update(sha_ctx* context
		, boost::uint8_t const* data
		, boost::uint32_t len);
	TORRENT_EXTRA_EXPORT void SHA1_final(boost::uint8_t* digest, sha_ctx* context);
}

#endif

