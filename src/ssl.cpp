/*

Copyright (c) 2020, Paul-Louis Ageneau
Copyright (c) 2018, Alexandre Janniaux
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

#include "libtorrent/ssl.hpp"

#if TORRENT_USE_SSL

#ifdef TORRENT_USE_OPENSSL
#include <openssl/x509v3.h> // for GENERAL_NAME
#endif

#ifdef TORRENT_USE_GNUTLS
#include <gnutls/x509.h>
#endif

namespace libtorrent {
namespace ssl {

void set_trust_certificate(native_context_type nc, string_view pem, error_code &ec)
{
#if defined TORRENT_USE_OPENSSL
	// create a new X.509 certificate store
	X509_STORE* cert_store = X509_STORE_new();
	if (!cert_store)
	{
		ec = error_code(int(ERR_get_error()), error::get_ssl_category());
		return;
	}

	// wrap the PEM certificate in a BIO, for openssl to read
	BIO* bp = BIO_new_mem_buf(
			const_cast<char*>(pem.data()),
			int(pem.size()));

	// parse the certificate into OpenSSL's internal representation
	X509* cert = PEM_read_bio_X509_AUX(bp, nullptr, nullptr, nullptr);
	BIO_free(bp);

	if (!cert)
	{
		X509_STORE_free(cert_store);
		ec = error_code(int(ERR_get_error()), error::get_ssl_category());
		return;
	}

	// add cert to cert_store
	X509_STORE_add_cert(cert_store, cert);
	X509_free(cert);

	// and lastly, replace the default cert store with ours
	SSL_CTX_set_cert_store(nc, cert_store);

#elif defined TORRENT_USE_GNUTLS
    gnutls_datum_t ca;
    ca.data = reinterpret_cast<unsigned char*>(const_cast<char*>(pem.data()));
    ca.size = unsigned(pem.size());

	// Warning: returns the number of certificates processed or a negative error code on error
	int ret = gnutls_certificate_set_x509_trust_mem(nc, &ca, GNUTLS_X509_FMT_PEM);
	if(ret < 0)
		ec = error_code(ret, error::get_ssl_category());
#endif
}

void set_server_name_callback(context_handle_type c, server_name_callback_type cb, void* arg, error_code& ec)
{
#if defined TORRENT_USE_OPENSSL
	TORRENT_UNUSED(ec);
#if defined __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#pragma clang diagnostic ignored "-Wcast-function-type-strict"
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#endif
	SSL_CTX_set_tlsext_servername_callback(c, cb);
	SSL_CTX_set_tlsext_servername_arg(c, arg);
#if defined __clang__
#pragma clang diagnostic pop
#endif

#elif defined TORRENT_USE_GNUTLS
	if(cb)
		c->set_server_name_callback(
				[cb, arg](stream_base& s, std::string const& name)
				{
					return cb(&s, name, arg);
				}
				, ec);
	else
		c->set_server_name_callback(nullptr);
#endif
}

void set_host_name(stream_handle_type s, std::string const& name, error_code& ec)
{
#if defined TORRENT_USE_OPENSSL
	TORRENT_UNUSED(ec);
#if defined __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#pragma clang diagnostic ignored "-Wcast-qual"
#endif
	SSL_set_tlsext_host_name(s, name.c_str());
#if defined __clang__
#pragma clang diagnostic pop
#endif

#elif defined TORRENT_USE_GNUTLS
	s->set_host_name(name, ec);
#endif
}

void set_context(stream_handle_type s, context_handle_type c)
{
#if defined TORRENT_USE_OPENSSL
	SSL_set_SSL_CTX(s, c);
	SSL_set_verify(s
		, SSL_CTX_get_verify_mode(c)
		, SSL_CTX_get_verify_callback(c));
#elif defined TORRENT_USE_GNUTLS
	s->set_context(*c);
#endif
}

bool has_context(stream_handle_type s, context_handle_type c)
{
	context_handle_type stream_ctx = get_context(s);
#if defined TORRENT_USE_OPENSSL
	return stream_ctx == c;
#elif defined TORRENT_USE_GNUTLS
	return stream_ctx->native_handle() == c->native_handle();
#endif
}

context_handle_type get_context(stream_handle_type s)
{
#if defined TORRENT_USE_OPENSSL
	return SSL_get_SSL_CTX(s);
#elif defined TORRENT_USE_GNUTLS
	return &s->get_context();
#endif
}

#if defined TORRENT_USE_OPENSSL
namespace {
	struct lifecycle
	{
		lifecycle()
		{
			// this is needed for openssl < 1.0 to decrypt keys created by openssl 1.0+
#if !defined(OPENSSL_API_COMPAT) || (OPENSSL_API_COMPAT < 0x10100000L)
			OpenSSL_add_all_algorithms();
#else
			OPENSSL_init_crypto(OPENSSL_INIT_ADD_ALL_CIPHERS | OPENSSL_INIT_ADD_ALL_DIGESTS, nullptr);
#endif
		}

		~lifecycle()
		{
// by openssl changelog at https://www.openssl.org/news/changelog.html
// Changes between 1.0.2h and 1.1.0  [25 Aug 2016]
// - Most global cleanup functions are no longer required because they are handled
//   via auto-deinit. Affected function CRYPTO_cleanup_all_ex_data()
#if !defined(OPENSSL_API_COMPAT) || OPENSSL_API_COMPAT < 0x10100000L
#ifdef TORRENT_MACOS_DEPRECATED_LIBCRYPTO
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
			// openssl requires this to clean up internal structures it allocates
			CRYPTO_cleanup_all_ex_data();
#ifdef TORRENT_MACOS_DEPRECATED_LIBCRYPTO
#pragma clang diagnostic pop
#endif
#endif
		}
	} global;
}
#endif

} // ssl
} // libtorrent
#endif

