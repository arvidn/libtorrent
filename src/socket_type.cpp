/*

Copyright (c) 2009, 2012-2015, 2017-2020, Arvid Norberg
Copyright (c) 2016, 2020, Alden Torres
Copyright (c) 2016, 2020, Steven Siloti
Copyright (c) 2020, Paul-Louis Ageneau
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
#include "libtorrent/aux_/array.hpp"
#include "libtorrent/deadline_timer.hpp"
#include "libtorrent/ssl.hpp"
#include "libtorrent/debug.hpp"

namespace libtorrent {

	char const* socket_type_name(socket_type_t const s)
	{
		static aux::array<char const*, 9, socket_type_t> const names{{{
			"TCP",
			"Socks5",
			"HTTP",
			"uTP",
#if TORRENT_USE_I2P
			"I2P",
#else
			"",
#endif
#if TORRENT_USE_SSL
			"SSL/TCP",
			"SSL/Socks5",
			"SSL/HTTP",
			"SSL/uTP"
#else
			"","","",""
#endif
		}}};
		return names[s];
	}

namespace aux {

	struct is_ssl_visitor {
#if TORRENT_USE_SSL
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
#if TORRENT_USE_SSL
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
		socket_type_t operator()(tcp::socket const&) const { return socket_type_t::tcp; }
		socket_type_t operator()(socks5_stream const&) const { return socket_type_t::socks5; }
		socket_type_t operator()(http_stream const&) const { return socket_type_t::http; }
		socket_type_t operator()(utp_stream const&) const { return socket_type_t::utp; }
#if TORRENT_USE_I2P
		socket_type_t operator()(i2p_stream const&) const { return socket_type_t::i2p; }
#endif
#if TORRENT_USE_SSL
		socket_type_t operator()(ssl_stream<tcp::socket> const&) const { return socket_type_t::tcp_ssl; }
		socket_type_t operator()(ssl_stream<socks5_stream> const&) const { return socket_type_t::socks5_ssl; }
		socket_type_t operator()(ssl_stream<http_stream> const&) const { return socket_type_t::http_ssl; }
		socket_type_t operator()(ssl_stream<utp_stream> const&) const { return socket_type_t::utp_ssl; }
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
#if TORRENT_USE_SSL
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
#if TORRENT_USE_SSL
		close_reason_t operator()(ssl_stream<utp_stream> const& s) const
		{ return s.next_layer().get_close_reason(); }
#endif
		close_reason_t operator()(utp_stream const& s) const
		{ return s.get_close_reason(); }
		template <typename T>
		close_reason_t operator()(T const&) const { return close_reason_t::none; }
	};

	close_reason_t get_close_reason(socket_type const& s)
	{
		return boost::apply_visitor(get_close_reason_visitor{}, s);
	}

#if TORRENT_USE_SSL
	struct set_ssl_hostname_visitor
	{
		set_ssl_hostname_visitor(char const* h, error_code& ec) : hostname_(h), ec_(&ec) {}
		template <typename T>
		void operator()(ssl_stream<T>& s)
		{
			s.set_verify_callback(ssl::host_name_verification(hostname_), *ec_);
			ssl_ = s.handle();
			ctx_ = s.context_handle();
		}
		template <typename T>
		void operator()(T&) {}

		char const* hostname_;
		error_code* ec_;
		ssl::stream_handle_type ssl_ = nullptr;
		ssl::context_handle_type ctx_ = nullptr;
	};
#endif

	void setup_ssl_hostname(socket_type& s, std::string const& hostname, error_code& ec)
	{
#if TORRENT_USE_SSL
		// for SSL connections, make sure to authenticate the hostname
		// of the certificate

		set_ssl_hostname_visitor visitor{hostname.c_str(), ec};
		boost::apply_visitor(visitor, s);

		if (visitor.ctx_)
			ssl::set_server_name_callback(visitor.ctx_, nullptr, nullptr, ec);

		if (visitor.ssl_)
			ssl::set_host_name(visitor.ssl_, hostname, ec);
#else
		TORRENT_UNUSED(ec);
		TORRENT_UNUSED(hostname);
		TORRENT_UNUSED(s);
#endif
	}

#if TORRENT_USE_SSL

	struct socket_closer
	{
		socket_closer(io_context& ioc
			, std::shared_ptr<void> holder
			, socket_type* s)
			: h(std::move(holder))
			, t(std::make_shared<deadline_timer>(ioc))
			, sock(s)
		{
			t->expires_after(seconds(3));
			t->async_wait(*this);
		}

		void operator()(error_code const&)
		{
			COMPLETE_ASYNC("on_close_socket");
			error_code ec;
			sock->close(ec);
			t->cancel();
		}

		std::shared_ptr<void> h;
		std::shared_ptr<deadline_timer> t;
		socket_type* sock;
	};

	struct issue_async_shutdown_visitor
	{
		issue_async_shutdown_visitor(socket_type* s, std::shared_ptr<void> h)
			: holder_(std::move(h)), sock_type_(s) {}

		template <typename T>
		void operator()(ssl_stream<T>& s)
		{
			// we do this twice, because the socket_closer callback will be
			// called twice
			ADD_OUTSTANDING_ASYNC("on_close_socket");
			ADD_OUTSTANDING_ASYNC("on_close_socket");
			s.async_shutdown(socket_closer(static_cast<io_context&>(s.get_executor().context())
				, std::move(holder_), sock_type_));
		}
		template <typename T>
		void operator()(T& s)
		{
			error_code e;
			s.close(e);
		}
		std::shared_ptr<void> holder_;
		socket_type* sock_type_;
	};
#endif

	// the second argument is a shared pointer to an object that
	// will keep the socket (s) alive for the duration of the async operation
	void async_shutdown(socket_type& s, std::shared_ptr<void> holder)
	{
#if TORRENT_USE_SSL
		boost::apply_visitor(issue_async_shutdown_visitor{&s, std::move(holder)}, s);
#else
		TORRENT_UNUSED(holder);
		error_code e;
		s.close(e);
#endif // TORRENT_USE_SSL
	}
}
}
