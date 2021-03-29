/*

Copyright (c) 2009, 2012-2015, 2017-2020, Arvid Norberg
Copyright (c) 2016, 2020, Steven Siloti
Copyright (c) 2016, 2020-2021, Alden Torres
Copyright (c) 2020, Paul-Louis Ageneau
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/config.hpp"
#include "libtorrent/aux_/socket_type.hpp"
#include "libtorrent/aux_/array.hpp"
#include "libtorrent/aux_/deadline_timer.hpp"
#include "libtorrent/aux_/ssl.hpp"
#include "libtorrent/aux_/debug.hpp"

namespace lt {

	char const* socket_type_name(socket_type_t const s)
	{
		static aux::array<char const*, 10, socket_type_t> const names{{{
			"TCP",
			"Socks5",
			"HTTP",
			"uTP",
#if TORRENT_USE_I2P
			"I2P",
#else
			"",
#endif
#if TORRENT_USE_RTC
			"RTC",
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
		return std::visit(is_ssl_visitor{}, s.var());
	}

	bool is_utp(socket_type const& s)
	{
		return std::get_if<utp_stream>(&s)
#if TORRENT_USE_SSL
			|| std::get_if<ssl_stream<utp_stream>>(&s)
#endif
			;
	}

#if TORRENT_USE_I2P
	bool is_i2p(socket_type const& s)
	{
		return std::get_if<i2p_stream>(&s);
	}
#endif

#if TORRENT_USE_RTC
	bool is_rtc(socket_type const& s)
	{
		return std::get_if<rtc_stream>(&s);
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
#if TORRENT_USE_RTC
		socket_type_t operator()(rtc_stream const&) { return socket_type_t::rtc; }
#endif
#if TORRENT_USE_SSL
		socket_type_t operator()(ssl_stream<tcp::socket> const&) { return socket_type_t::tcp_ssl; }
		socket_type_t operator()(ssl_stream<socks5_stream> const&) { return socket_type_t::socks5_ssl; }
		socket_type_t operator()(ssl_stream<http_stream> const&) { return socket_type_t::http_ssl; }
		socket_type_t operator()(ssl_stream<utp_stream> const&) { return socket_type_t::utp_ssl; }
#endif
	};

	socket_type_t socket_type_idx(socket_type const& s)
	{
		return std::visit(idx_visitor{}, s.var());
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
		std::visit(set_close_reason_visitor{code}, s.var());
	}

	struct get_close_reason_visitor {
#if TORRENT_USE_SSL
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
		return std::visit(get_close_reason_visitor{}, s.var());
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
		std::visit(visitor, s.var());

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
		std::visit(issue_async_shutdown_visitor{&s, std::move(holder)}, s.var());
#else
		TORRENT_UNUSED(holder);
		error_code e;
		s.close(e);
#endif // TORRENT_USE_SSL
	}
}
}
