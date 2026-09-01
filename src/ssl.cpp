/*

Copyright (c) 2018, Alexandre Janniaux
Copyright (c) 2020-2021, Arvid Norberg
Copyright (c) 2020, Paul-Louis Ageneau
Copyright (c) 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/ssl.hpp"

#if TORRENT_USE_SSL

#include <array>
#include "libtorrent/aux_/scope_end.hpp"

#ifdef TORRENT_USE_OPENSSL
#include <openssl/x509v3.h> // for GENERAL_NAME
#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS
#include <openssl/evp.h> // for EVP_sha256, EVP_MAX_MD_SIZE
#endif
#endif

#ifdef TORRENT_USE_GNUTLS
#include <gnutls/x509.h>
#endif

namespace libtorrent::aux::ssl {

sha256_hash set_trust_certificate(native_context_type nc, string_view pem, error_code& ec)
{
#if defined TORRENT_USE_OPENSSL
	// create a new X.509 certificate store
	X509_STORE* cert_store = X509_STORE_new();
	if (!cert_store)
	{
		ec = error_code(int(ERR_get_error()), error::get_ssl_category());
		return {};
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
		return {};
	}
	auto se = aux::scope_end([&] { X509_free(cert); });

	// add cert to cert_store
	X509_STORE_add_cert(cert_store, cert);

	// and replace the default cert store with ours
	SSL_CTX_set_cert_store(nc, cert_store);

	sha256_hash fingerprint;
#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS
	// fingerprint the same parsed certificate used for the trust store,
	// rather than re-parsing the PEM text
	std::array<unsigned char, EVP_MAX_MD_SIZE> md;
	unsigned int md_len = 0;
	if (X509_digest(cert, EVP_sha256(), md.data(), &md_len) != 1)
	{
		ec = error_code(int(ERR_get_error()), error::get_ssl_category());
		return {};
	}
	TORRENT_ASSERT(md_len == static_cast<unsigned int>(sha256_hash::size()));
	fingerprint.assign(reinterpret_cast<char const*>(md.data()));
#endif
	return fingerprint;

#elif defined TORRENT_USE_GNUTLS
	gnutls_datum_t ca;
	ca.data = reinterpret_cast<unsigned char*>(const_cast<char*>(pem.data()));
	ca.size = unsigned(pem.size());

	// parse a single certificate, rather than using
	// gnutls_certificate_set_x509_trust_mem() directly, so the certificate
	// that's trusted is the exact same one that's fingerprinted below. A
	// PEM blob containing more than one certificate only has its first
	// certificate trusted, matching the OpenSSL branch above.
	gnutls_x509_crt_t crt;
	int ret = gnutls_x509_crt_init(&crt);
	if (ret < 0)
	{
		ec = error_code(ret, error::get_ssl_category());
		return {};
	}
	auto se = aux::scope_end([&] { gnutls_x509_crt_deinit(crt); });

	ret = gnutls_x509_crt_import(crt, &ca, GNUTLS_X509_FMT_PEM);
	if (ret < 0)
	{
		ec = error_code(ret, error::get_ssl_category());
		return {};
	}

	ret = gnutls_certificate_set_x509_trust(nc, &crt, 1);
	if (ret < 0)
	{
		ec = error_code(ret, error::get_ssl_category());
		return {};
	}

	sha256_hash fingerprint;
#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS
	std::array<unsigned char, 32> buf;
	size_t buf_size = buf.size();
	ret = gnutls_x509_crt_get_fingerprint(crt, GNUTLS_DIG_SHA256, buf.data(), &buf_size);
	if (ret < 0)
	{
		ec = error_code(ret, error::get_ssl_category());
		return {};
	}
	fingerprint.assign(reinterpret_cast<char const*>(buf.data()));
#endif
	return fingerprint;
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
			OPENSSL_init_crypto(OPENSSL_INIT_ADD_ALL_CIPHERS | OPENSSL_INIT_ADD_ALL_DIGESTS, nullptr);
		}
	} global;
}
#endif

} // libtorrent::aux::ssl

#endif
