/*

Copyright (c) 2009-2019, Arvid Norberg
Copyright (c) 2016, Steven Siloti
Copyright (c) 2016, Alden Torres
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

#include "libtorrent/config.hpp"
#include "libtorrent/aux_/socket_type.hpp"
#include "libtorrent/aux_/openssl.hpp"
#include "libtorrent/aux_/array.hpp"

#ifdef TORRENT_USE_OPENSSL
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/rfc2818_verification.hpp>

#endif

#include "libtorrent/debug.hpp"

namespace libtorrent {

	char const* socket_type_name(socket_type_t const s)
	{
		static aux::array<char const*, 9, socket_type_t> const names{{
			"TCP",
			"Socks5",
			"HTTP",
			"uTP",
#if TORRENT_USE_I2P
			"I2P",
#else
			"",
#endif
#ifdef TORRENT_USE_OPENSSL
			"SSL/TCP",
			"SSL/Socks5",
			"SSL/HTTP",
			"SSL/uTP"
#else
			"","","",""
#endif
		}};
		return names[s];
	}

namespace aux {

	struct is_ssl_visitor {
#ifdef TORRENT_USE_OPENSSL
		template <typename T>
		bool operator()(ssl_stream<T> const&) const { return true; }
#endif
		template <typename T>
		bool operator()(T const&) const { return false; }
	};

	bool is_ssl(socket_type const& s)
	{
		return boost::apply_visitor(is_ssl_visitor{}, s);
	}

	bool is_utp(socket_type const& s)
	{
		return boost::get<utp_stream>(&s)
#ifdef TORRENT_USE_OPENSSL
			|| boost::get<ssl_stream<utp_stream>>(&s)
#endif
			;
	}

#if TORRENT_USE_I2P
	bool is_i2p(socket_type const& s)
	{
		return boost::get<i2p_stream>(&s);
	}
#endif

	struct idx_visitor {
		socket_type_t operator()(tcp::socket const&) { return socket_type_t::tcp; }
		socket_type_t operator()(socks5_stream const&) { return socket_type_t::socks5; }
		socket_type_t operator()(http_stream const&) { return socket_type_t::http; }
		socket_type_t operator()(utp_stream const&) { return socket_type_t::utp; }
#if TORRENT_USE_I2P
		socket_type_t operator()(i2p_stream const&) { return socket_type_t::i2p; }
#endif
#ifdef TORRENT_USE_OPENSSL
		socket_type_t operator()(ssl_stream<tcp::socket> const&) { return socket_type_t::tcp_ssl; }
		socket_type_t operator()(ssl_stream<socks5_stream> const&) { return socket_type_t::socks5_ssl; }
		socket_type_t operator()(ssl_stream<http_stream> const&) { return socket_type_t::http_ssl; }
		socket_type_t operator()(ssl_stream<utp_stream> const&) { return socket_type_t::utp_ssl; }
#endif
	};

	socket_type_t socket_type_idx(socket_type const& s)
	{
		return boost::apply_visitor(idx_visitor{}, s);
	}

	char const* socket_type_name(socket_type const& s)
	{
		return socket_type_name(socket_type_idx(s));
	}

	struct set_close_reason_visitor {
		close_reason_t code_;
#ifdef TORRENT_USE_OPENSSL
		void operator()(ssl_stream<utp_stream>& s) const
		{ s.next_layer().set_close_reason(code_); }
#endif
		void operator()(utp_stream& s) const
		{ s.set_close_reason(code_); }
		template <typename T>
		void operator()(T const&) const {}
	};

	void set_close_reason(socket_type& s, close_reason_t code)
	{
		boost::apply_visitor(set_close_reason_visitor{code}, s);
	}

	struct get_close_reason_visitor {
#ifdef TORRENT_USE_OPENSSL
		close_reason_t operator()(ssl_stream<utp_stream>& s) const
		{ return s.next_layer().get_close_reason(); }
#endif
		close_reason_t operator()(utp_stream& s) const
		{ return s.get_close_reason(); }
		template <typename T>
		close_reason_t operator()(T const&) const { return close_reason_t::none; }
	};

	close_reason_t get_close_reason(socket_type const& s)
	{
		return boost::apply_visitor(get_close_reason_visitor{}, s);
	}

#ifdef TORRENT_USE_OPENSSL
	struct set_ssl_hostname_visitor
	{
		set_ssl_hostname_visitor(char const* h, error_code& ec) : hostname_(h), ec_(&ec) {}
		template <typename T>
		void operator()(ssl_stream<T>& s)
		{
			s.set_verify_callback(boost::asio::ssl::rfc2818_verification(hostname_), *ec_);
			ssl_ = s.native_handle();
			ctx_ = SSL_get_SSL_CTX(ssl_);
		}
		template <typename T>
		void operator()(T&) {}

		char const* hostname_;
		error_code* ec_;
		SSL* ssl_ = nullptr;
		SSL_CTX* ctx_ = nullptr;
	};
#endif

	void setup_ssl_hostname(socket_type& s, std::string const& hostname, error_code& ec)
	{
#ifdef TORRENT_USE_OPENSSL
		// for SSL connections, make sure to authenticate the hostname
		// of the certificate

		set_ssl_hostname_visitor visitor{hostname.c_str(), ec};
		boost::apply_visitor(visitor, s);

		if (visitor.ctx_)
		{
			aux::openssl_set_tlsext_servername_callback(visitor.ctx_, nullptr);
			aux::openssl_set_tlsext_servername_arg(visitor.ctx_, nullptr);
		}

		if (visitor.ssl_)
		{
			aux::openssl_set_tlsext_hostname(visitor.ssl_, hostname.c_str());
		}

#else
		TORRENT_UNUSED(ec);
		TORRENT_UNUSED(hostname);
		TORRENT_UNUSED(s);
#endif
	}

#ifdef TORRENT_USE_OPENSSL
	namespace {

	void nop(std::shared_ptr<void>) {}

	void on_close_socket(socket_type* s, std::shared_ptr<void>)
	{
		COMPLETE_ASYNC("on_close_socket");
		error_code ec;
		s->close(ec);
	}

	} // anonymous namespace
#endif

#ifdef TORRENT_USE_OPENSSL
	struct issue_async_shutdown_visitor
	{
		issue_async_shutdown_visitor(socket_type* s, std::shared_ptr<void>* h, boost::asio::const_buffer b)
			: holder_(h), sock_type_(s), buffer_(b) {}

		template <typename T>
		void operator()(ssl_stream<T>& s)
		{
			ADD_OUTSTANDING_ASYNC("on_close_socket");
			s.async_shutdown(std::bind(&nop, *holder_));
			s.async_write_some(buffer_, std::bind(&on_close_socket, sock_type_, *holder_));
		}
		template <typename T>
		void operator()(T& s)
		{
			error_code e;
			s.close(e);
		}
		std::shared_ptr<void>* holder_;
		socket_type* sock_type_;
		boost::asio::const_buffer buffer_;
	};
#endif

	// the second argument is a shared pointer to an object that
	// will keep the socket (s) alive for the duration of the async operation
	void async_shutdown(socket_type& s, std::shared_ptr<void> holder)
	{
#ifdef TORRENT_USE_OPENSSL
		// for SSL connections, first do an async_shutdown, before closing the socket

		static char const buffer[] = "";
		// chasing the async_shutdown by a write is a trick to close the socket as
		// soon as we've sent the close_notify, without having to wait to receive a
		// response from the other end
		// https://stackoverflow.com/questions/32046034/what-is-the-proper-way-to-securely-disconnect-an-asio-ssl-socket

		boost::apply_visitor(issue_async_shutdown_visitor{&s, &holder, boost::asio::buffer(buffer)}, s);
#else
		TORRENT_UNUSED(holder);
		error_code e;
		s.close(e);
#endif // TORRENT_USE_OPENSSL
	}
}
}
