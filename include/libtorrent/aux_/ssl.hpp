/*

Copyright (c) 2020, Paul-Louis Ageneau
Copyright (c) 2018, Alexandre Janniaux
Copyright (c) 2020, Steven Siloti
Copyright (c) 2020, Arvid Norberg
Copyright (c) 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_SSL_HPP_INCLUDED
#define TORRENT_SSL_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/string_view.hpp"
#include "libtorrent/aux_/export.hpp"

#if TORRENT_USE_SSL

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/asio.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/system/error_code.hpp>

#ifndef BOOST_NO_EXCEPTIONS
#include <boost/system/system_error.hpp>
#endif

#ifdef TORRENT_USE_OPENSSL
#include <openssl/opensslv.h> // for OPENSSL_VERSION_NUMBER
#if OPENSSL_VERSION_NUMBER < 0x1000000fL
#error OpenSSL too old, use a recent version with SNI support
#endif
#ifdef TORRENT_WINDOWS
// because openssl includes winsock.h, we must include winsock2.h first
#include <winsock2.h>
#endif
#include <openssl/ssl.h>
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <boost/asio/ssl.hpp>
#endif

#ifdef TORRENT_USE_GNUTLS
#include <gnutls/gnutls.h>
#include <boost/asio/gnutls.hpp>
#endif

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#ifdef TORRENT_BUILD_SIMULATOR
#include "simulator/simulator.hpp"
#endif

#include <string>
#include <cstddef>
#include <functional>
#include <exception>

namespace lt::aux::ssl {

using error_code = boost::system::error_code;

#if defined TORRENT_USE_OPENSSL
#if defined TORRENT_BUILD_SIMULATOR
	using sim::asio::ssl::context;
	using sim::asio::ssl::stream_base;
	using sim::asio::ssl::stream;
#else
	using boost::asio::ssl::context;
	using boost::asio::ssl::stream_base;
	using boost::asio::ssl::stream;
#endif
using boost::asio::ssl::verify_context;
#if BOOST_VERSION >= 107300
using boost::asio::ssl::host_name_verification;
#else
using host_name_verification = boost::asio::ssl::rfc2818_verification;
#endif

using native_context_type = SSL_CTX*;
using native_stream_type = SSL*;
using context_handle_type = native_context_type;
using stream_handle_type = native_stream_type;

typedef int (*server_name_callback_type)(SSL* s, int*, void* arg);

#elif defined TORRENT_USE_GNUTLS
using boost::asio::gnutls::context;
using boost::asio::gnutls::stream_base;
using boost::asio::gnutls::stream;
using boost::asio::gnutls::verify_context;
using boost::asio::gnutls::host_name_verification;

using native_context_type = context::native_handle_type;
using native_stream_type = stream_base::native_handle_type;
using context_handle_type = context*;
using stream_handle_type = stream_base*;

typedef bool (*server_name_callback_type)(stream_handle_type handle, std::string const& name, void* arg);

#endif

namespace error {

#if defined TORRENT_USE_OPENSSL
using boost::asio::error::get_ssl_category;
using boost::asio::ssl::error::get_stream_category;
#elif defined TORRENT_USE_GNUTLS
using boost::asio::gnutls::error::get_ssl_category;
using boost::asio::gnutls::error::get_stream_category;
#endif

}

inline context_handle_type get_handle(context &c)
{
#if defined TORRENT_USE_OPENSSL
	return c.native_handle();
#elif defined TORRENT_USE_GNUTLS
	return &c;
#endif
}

template<typename T>
stream_handle_type get_handle(stream<T>& s)
{
#if defined TORRENT_USE_OPENSSL
	return s.native_handle();
#elif defined TORRENT_USE_GNUTLS
	return &s;
#endif
}

template<typename T>
context_handle_type get_context_handle(stream<T>& s)
{
#if defined TORRENT_USE_OPENSSL
	return SSL_get_SSL_CTX(s.native_handle());
#elif defined TORRENT_USE_GNUTLS
	return &s.get_context();
#endif
}

TORRENT_EXTRA_EXPORT void set_trust_certificate(native_context_type nc, string_view pem, error_code &ec);

TORRENT_EXTRA_EXPORT void set_server_name_callback(context_handle_type c, server_name_callback_type cb, void* arg, error_code& ec);
TORRENT_EXTRA_EXPORT void set_host_name(stream_handle_type s, std::string const& name, error_code& ec);

TORRENT_EXTRA_EXPORT void set_context(stream_handle_type s, context_handle_type c);
TORRENT_EXTRA_EXPORT bool has_context(stream_handle_type s, context_handle_type c);
TORRENT_EXTRA_EXPORT context_handle_type get_context(stream_handle_type s);

} // lt::aux:ssl

#endif // TORRENT_USE_SSL

#endif
