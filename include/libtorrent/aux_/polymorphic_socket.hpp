/*

Copyright (c) 2019-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_POLYMORPHIC_SOCKET
#define TORRENT_POLYMORPHIC_SOCKET

#include "libtorrent/error_code.hpp"

#include <variant>

namespace lt::aux {

	template <typename T, typename... Rest>
	struct first_element
	{ using type = T; };

#define TORRENT_FWD_CALL(x) \
	return std::visit([&](auto& s){ return s.x; }, this->var())

	template <typename... Sockets>
	struct polymorphic_socket : std::variant<Sockets...>
	{
		using base = std::variant<Sockets...>;
		using first_socket = typename first_element<Sockets...>::type;
		using endpoint_type = typename first_socket::endpoint_type;
		using protocol_type = typename first_socket::protocol_type;

		using receive_buffer_size = typename first_socket::receive_buffer_size;
		using send_buffer_size = typename first_socket::send_buffer_size;

		using executor_type = typename first_socket::executor_type;

		base& var() { return static_cast<base&>(*this); }
		base const& var() const { return static_cast<base const&>(*this); }

		template <typename S>
		explicit polymorphic_socket(S s) : std::variant<Sockets...>(std::move(s))
		{
			static_assert(std::is_nothrow_move_constructible<S>::value
				, "should really be nothrow move contsructible, since it's part of a variant");
		}
		polymorphic_socket(polymorphic_socket&&) = default;
		~polymorphic_socket() = default;

		bool is_open() const
		{ TORRENT_FWD_CALL(is_open()); }

		void open(protocol_type const& p, error_code& ec)
		{ TORRENT_FWD_CALL(open(p, ec)); }

		void close(error_code& ec)
		{ TORRENT_FWD_CALL(close(ec)); }

		endpoint_type local_endpoint(error_code& ec) const
		{ TORRENT_FWD_CALL(local_endpoint(ec)); }

		endpoint_type remote_endpoint(error_code& ec) const
		{ TORRENT_FWD_CALL(remote_endpoint(ec)); }

		void bind(endpoint_type const& endpoint, error_code& ec)
		{ TORRENT_FWD_CALL(bind(endpoint, ec)); }

		std::size_t available(error_code& ec) const
		{ TORRENT_FWD_CALL(available(ec)); }

#ifndef BOOST_NO_EXCEPTIONS
		void open(protocol_type const& p)
		{ TORRENT_FWD_CALL(open(p)); }

		void close()
		{ TORRENT_FWD_CALL(close()); }

		endpoint_type local_endpoint() const
		{ TORRENT_FWD_CALL(local_endpoint()); }

		endpoint_type remote_endpoint() const
		{ TORRENT_FWD_CALL(remote_endpoint()); }

		void bind(endpoint_type const& endpoint)
		{ TORRENT_FWD_CALL(bind(endpoint)); }

		std::size_t available() const
		{ TORRENT_FWD_CALL(available()); }
#endif

		template <class Mutable_Buffers>
		std::size_t read_some(Mutable_Buffers const& buffers, error_code& ec)
		{ TORRENT_FWD_CALL(read_some(buffers, ec)); }

		template <class Mutable_Buffers, class Handler>
		void async_read_some(Mutable_Buffers const& buffers, Handler handler)
		{ TORRENT_FWD_CALL(async_read_some(buffers, std::move(handler))); }

		template <class Const_Buffers>
		std::size_t write_some(Const_Buffers const& buffers, error_code& ec)
		{ TORRENT_FWD_CALL(write_some(buffers, ec)); }

		template <class Const_Buffers, class Handler>
		void async_write_some(Const_Buffers const& buffers, Handler handler)
		{ TORRENT_FWD_CALL(async_write_some(buffers, std::move(handler))); }

		template <class Handler>
		void async_connect(endpoint_type const& endpoint, Handler handler)
		{ TORRENT_FWD_CALL(async_connect(endpoint, std::move(handler))); }

#ifndef BOOST_NO_EXCEPTIONS
		template <class IO_Control_Command>
		void io_control(IO_Control_Command& ioc)
		{ TORRENT_FWD_CALL(io_control(ioc)); }

		template <class Mutable_Buffers>
		std::size_t read_some(Mutable_Buffers const& buffers)
		{ TORRENT_FWD_CALL(read_some(buffers)); }
#endif

		template <class IO_Control_Command>
		void io_control(IO_Control_Command& ioc, error_code& ec)
		{ TORRENT_FWD_CALL(io_control(ioc, ec)); }

#ifndef BOOST_NO_EXCEPTIONS
		template <class SettableSocketOption>
		void set_option(SettableSocketOption const& opt)
		{ TORRENT_FWD_CALL(set_option(opt)); }
#endif

		template <class SettableSocketOption>
		void set_option(SettableSocketOption const& opt, error_code& ec)
		{ TORRENT_FWD_CALL(set_option(opt, ec)); }

		void non_blocking(bool b, error_code& ec)
		{ TORRENT_FWD_CALL(non_blocking(b, ec)); }

#ifndef BOOST_NO_EXCEPTIONS
		void non_blocking(bool b)
		{ TORRENT_FWD_CALL(non_blocking(b)); }
#endif

#ifndef BOOST_NO_EXCEPTIONS
		template <class GettableSocketOption>
		void get_option(GettableSocketOption& opt)
		{ TORRENT_FWD_CALL(get_option(opt)); }
#endif

		template <class GettableSocketOption>
		void get_option(GettableSocketOption& opt, error_code& ec)
		{ TORRENT_FWD_CALL(get_option(opt, ec)); }

		// explicitly disallow assignment, to silence msvc warning
		polymorphic_socket& operator=(polymorphic_socket const&) = delete;
		polymorphic_socket& operator=(polymorphic_socket&&) = default;
	};

#undef TORRENT_FWD_CALL

}

#endif // TORRENT_POLYMORPHIC_SOCKET
