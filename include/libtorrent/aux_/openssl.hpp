/*

Copyright (c) 2015, Arvid Norberg
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

#ifndef TORRENT_OPENSSL_HPP_INCLUDED
#define TORRENT_OPENSSL_HPP_INCLUDED

#ifdef TORRENT_USE_LIBCRYPTO

#include "libtorrent/config.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <openssl/opensslv.h> // for OPENSSL_VERSION_NUMBER
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#if defined __APPLE__ \
	&& MAC_OS_X_VERSION_MIN_REQUIRED >= 1070 \
	&& OPENSSL_VERSION_NUMBER <= 0x009081dfL
#define TORRENT_MACOS_DEPRECATED_LIBCRYPTO 1
#endif

#endif // TORRENT_USE_LIBCRYPTO

#ifdef TORRENT_USE_OPENSSL

#include "libtorrent/aux_/disable_warnings_push.hpp"

#ifdef TORRENT_WINDOWS
// because openssl includes winsock.h, we must include winsock2.h first
#include <winsock2.h>
#endif

#include <openssl/ssl.h>
#include <openssl/safestack.h> // for sk_GENERAL_NAME_value
#include <openssl/x509v3.h> // for GENERAL_NAME

#include <boost/asio/ssl.hpp>
#if defined TORRENT_BUILD_SIMULATOR
#include "simulator/simulator.hpp"
#endif

#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent {

namespace ssl {

#if defined TORRENT_BUILD_SIMULATOR
	using sim::asio::ssl::context;
	using sim::asio::ssl::stream_base;
	using sim::asio::ssl::stream;
#else
	using boost::asio::ssl::context;
	using boost::asio::ssl::stream_base;
	using boost::asio::ssl::stream;
#endif
} // ssl

namespace aux {

TORRENT_EXTRA_EXPORT void openssl_set_tlsext_hostname(SSL* s, char const* name);

#if OPENSSL_VERSION_NUMBER >= 0x90812f

TORRENT_EXTRA_EXPORT void openssl_set_tlsext_servername_callback(SSL_CTX* ctx
	, int (*servername_callback)(SSL*, int*, void*));

TORRENT_EXTRA_EXPORT void openssl_set_tlsext_servername_arg(SSL_CTX* ctx, void* userdata);

TORRENT_EXTRA_EXPORT int openssl_num_general_names(GENERAL_NAMES* gens);

TORRENT_EXTRA_EXPORT GENERAL_NAME* openssl_general_name_value(GENERAL_NAMES* gens, int i);

#endif // OPENSSL_VERSION_NUMBER

} // aux
} // libtorrent

#endif // TORRENT_USE_OPENSSL

#endif // TORRENT_OPENSSL_HPP_INCLUDED
