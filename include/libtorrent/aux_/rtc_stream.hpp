/*

Copyright (c) 2020, Paul-Louis Ageneau
Copyright (c) 2020, Arvid Norberg
Copyright (c) 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_RTC_STREAM_HPP_INCLUDED
#define TORRENT_RTC_STREAM_HPP_INCLUDED

#include "libtorrent/config.hpp"

#if TORRENT_USE_RTC

#include "libtorrent/aux_/packet_buffer.hpp"
#include "libtorrent/aux_/throw.hpp"
#include "libtorrent/close_reason.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/io_bytes.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/span.hpp"

#include <functional>
#include <memory>
#include <list>

#ifndef BOOST_NO_EXCEPTIONS
#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/system/system_error.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"
#endif

namespace rtc {
	class PeerConnection;
	class DataChannel;
}

namespace lt::aux {

struct TORRENT_EXTRA_EXPORT rtc_stream_init
{
	std::shared_ptr<rtc::PeerConnection> peer_connection;
	std::shared_ptr<rtc::DataChannel> data_channel;
};

struct TORRENT_EXTRA_EXPORT rtc_stream_impl : std::enable_shared_from_this<rtc_stream_impl>
{
	using endpoint_type = tcp::socket::endpoint_type;
	using protocol_type = tcp::socket::protocol_type;

	rtc_stream_impl(io_context& ioc, rtc_stream_init init);
	~rtc_stream_impl() = default;
	rtc_stream_impl& operator=(rtc_stream_impl const&) = delete;
	rtc_stream_impl(rtc_stream_impl const&) = delete;
	rtc_stream_impl& operator=(rtc_stream_impl&&) = delete;
	rtc_stream_impl(rtc_stream_impl&&) = delete;

	void init();
	void close();

	bool is_open() const;
	std::size_t available() const;

	endpoint_type local_endpoint(error_code& ec) const;
	endpoint_type remote_endpoint(error_code& ec) const;

	void cancel_handlers(error_code const& ec);

	bool has_read_handler() const { return bool(m_read_handler); }
	bool has_write_handler() const { return bool(m_write_handler); }
	template <class Handler>
	void set_read_handler(Handler handler) { m_read_handler = std::move(handler); }
	template <class Handler>
	void set_write_handler(Handler handler) { m_write_handler = std::move(handler); }

	template <class Mutable_Buffer>
	std::size_t add_read_buffer(Mutable_Buffer const& buffer) {
		if(buffer.size() == 0) return 0;
		m_read_buffer.push_back(buffer);
		m_read_buffer_size += buffer.size();
		return buffer.size();
	}

	template <class Const_Buffer>
	std::size_t add_write_buffer(Const_Buffer const& buffer) {
		if(buffer.size() == 0) return 0;
		m_write_buffer.push_back(buffer);
		m_write_buffer_size += buffer.size();
		return buffer.size();
	}

	void issue_read();
	void issue_write();
	std::size_t read_some(error_code& ec);
	std::size_t write_some(error_code& ec);
	void clear_read_buffers();
	void clear_write_buffers();

private:
	void on_available(error_code const& ec);
	void on_buffered_low(error_code const& ec);
	bool ensure_open();

	std::size_t incoming_data(span<char const> data);
	std::pair<std::size_t, bool> write_data(std::size_t size);

	io_context& m_io_context;
	std::shared_ptr<rtc::PeerConnection> m_peer_connection;
	std::shared_ptr<rtc::DataChannel> m_data_channel;

	std::function<void(error_code const&, std::size_t)> m_read_handler;
	std::function<void(error_code const&, std::size_t)> m_write_handler;
	std::list<boost::asio::const_buffer> m_write_buffer;
	std::list<boost::asio::mutable_buffer> m_read_buffer;
	std::size_t m_write_buffer_size = 0;
	std::size_t m_read_buffer_size = 0;

	std::vector<char> m_incoming;
};

// This is the user-level stream interface to WebRTC DataChannels.
struct TORRENT_EXTRA_EXPORT rtc_stream
{
	using lowest_layer_type = rtc_stream;
	using endpoint_type = rtc_stream_impl::endpoint_type;
	using protocol_type = rtc_stream_impl::protocol_type;

	using executor_type = tcp::socket::executor_type;
	executor_type get_executor() { return m_io_context.get_executor(); }

	rtc_stream(io_context& ioc, rtc_stream_init init);
	~rtc_stream();
	rtc_stream& operator=(rtc_stream const&) = delete;
	rtc_stream(rtc_stream const&) = delete;
	rtc_stream& operator=(rtc_stream&&) noexcept = delete;
	rtc_stream(rtc_stream&&) noexcept = default;

	lowest_layer_type& lowest_layer() { return *this; }

#ifndef BOOST_NO_EXCEPTIONS
	template <class IO_Control_Command>
	void io_control(IO_Control_Command&) {}
#endif

	template <class IO_Control_Command>
	void io_control(IO_Control_Command&, error_code&) {}

#ifndef BOOST_NO_EXCEPTIONS
	void non_blocking(bool) {}
#endif

	void non_blocking(bool, error_code&) {}

#ifndef BOOST_NO_EXCEPTIONS
	void bind(endpoint_type const&) {}
#endif

	void bind(endpoint_type const&, error_code&) {}

#ifndef BOOST_NO_EXCEPTIONS
	template <class SettableSocketOption>
	void set_option(SettableSocketOption const&) {}
#endif

	template <class SettableSocketOption>
	void set_option(SettableSocketOption const&, error_code&) {}

#ifndef BOOST_NO_EXCEPTIONS
	template <class GettableSocketOption>
	void get_option(GettableSocketOption&) {}
#endif

	template <class GettableSocketOption>
	void get_option(GettableSocketOption&, error_code&) {}

	template <class Protocol>
	void open(Protocol const&, error_code&)
	{ /* dummy */ }

	template <class Protocol>
	void open(Protocol const&)
	{ /* dummy */ }

	void cancel() { if(m_impl) m_impl->cancel_handlers(boost::asio::error::operation_aborted); }
	void cancel(error_code&) { cancel(); }

	void close() { if(m_impl) m_impl->close(); }
	void close(error_code const&) { close(); }

	close_reason_t get_close_reason() { return close_reason_t::none; }

	bool is_open() const { return m_impl && m_impl->is_open(); }

	endpoint_type local_endpoint() const { error_code ec; return local_endpoint(ec); }
	endpoint_type local_endpoint(error_code& ec) const
	{
		if(!m_impl) { ec = boost::asio::error::not_connected; return endpoint_type{}; }
		return m_impl->local_endpoint(ec);
	}

	endpoint_type remote_endpoint() const { error_code ec; return remote_endpoint(ec); }
	endpoint_type remote_endpoint(error_code& ec) const
    {
		if(!m_impl) { ec = boost::asio::error::not_connected; return endpoint_type{}; }
		return m_impl->remote_endpoint(ec);
	}

	std::size_t available() const { return m_impl ? m_impl->available() : 0; }
	std::size_t available(error_code&) const { return available(); }

	template <class Handler>
	void async_connect(endpoint_type const&, Handler const& handler)
	{ /* dummy */ handler(error_code{}); }

	template <class Mutable_Buffers, class Handler>
	void async_read_some(Mutable_Buffers const& buffers, Handler const& handler)
	{
		if (!is_open())
		{
			post(m_io_context, std::bind(handler, boost::asio::error::not_connected, std::size_t(0)));
			return;
		}

		TORRENT_ASSERT(!m_impl->has_read_handler());
		if (m_impl->has_read_handler())
		{
			post(m_io_context, std::bind(handler, boost::asio::error::operation_not_supported, std::size_t(0)));
			return;
		}

		std::size_t size = 0;
		for (auto it = buffer_sequence_begin(buffers)
			, end(buffer_sequence_end(buffers)); it != end; ++it)
			size += m_impl->add_read_buffer(*it);

		if (size == 0)
		{
			// if we're reading 0 bytes, post handler immediately
			post(m_io_context, std::bind(handler, error_code{}, std::size_t(0)));
			return;
		}

		m_impl->set_read_handler(handler);
		m_impl->issue_read();
	}

	template <class Const_Buffers, class Handler>
	void async_write_some(Const_Buffers const& buffers, Handler const& handler)
	{
		if (!is_open())
		{
			post(m_io_context, std::bind(handler
				, boost::asio::error::not_connected, std::size_t(0)));
			return;
		}

		TORRENT_ASSERT(!m_impl->has_write_handler());
		if (m_impl->has_write_handler())
		{
			post(m_io_context, std::bind(handler
				, boost::asio::error::operation_not_supported, std::size_t(0)));
			return;
		}

		std::size_t size = 0;
		for (auto it = buffer_sequence_begin(buffers)
			, end(buffer_sequence_end(buffers)); it != end; ++it)
			size += m_impl->add_write_buffer(*it);

		if (size == 0)
		{
			// if we're writing 0 bytes, post handler immediately
			post(m_io_context, std::bind(handler, error_code{}, std::size_t(0)));
			return;
		}

		m_impl->set_write_handler(handler);
		m_impl->issue_write();
	}

	template <class Mutable_Buffers>
	std::size_t read_some(Mutable_Buffers const& buffers, error_code& ec)
	{
		if (!is_open())
		{
			ec = boost::asio::error::not_connected;
			return 0;
		}

		if (available() == 0)
		{
			ec = boost::asio::error::would_block;
			return 0;
		}

		TORRENT_ASSERT(!m_impl->has_read_handler());

		for (auto it = buffer_sequence_begin(buffers)
			, end(buffer_sequence_end(buffers)); it != end; ++it)
			m_impl->add_read_buffer(*it);

		std::size_t ret = m_impl->read_some(ec);
		m_impl->clear_read_buffers();
		return ret;
	}

	template <class Const_Buffers>
	std::size_t write_some(Const_Buffers const& buffers, error_code& ec)
	{
		if (!is_open())
		{
			ec = boost::asio::error::not_connected;
			return 0;
		}

		TORRENT_ASSERT(!m_impl->has_write_handler());

		for (auto i = buffer_sequence_begin(buffers)
			, end(buffer_sequence_end(buffers)); i != end; ++i)
		{
			m_impl->add_write_buffer(i->data(), i->size());
		}

		std::size_t ret = m_impl->write_some(ec);
		m_impl->clear_read_buffers();
		return ret;
	}

#ifndef BOOST_NO_EXCEPTIONS
	template <class Mutable_Buffers>
	std::size_t read_some(Mutable_Buffers const& buffers)
	{
		error_code ec;
		std::size_t ret = read_some(buffers, ec);
		if (ec)
			aux::throw_ex<system_error>(ec);
		return ret;
	}

	template <class Const_Buffers>
	std::size_t write_some(Const_Buffers const& buffers)
	{
		error_code ec;
		std::size_t ret = write_some(buffers, ec);
		if (ec)
			boost::throw_exception(boost::system::system_error(ec));
		return ret;
	}
#endif

private:
	io_context& m_io_context;
	std::shared_ptr<rtc_stream_impl> m_impl;
};

TORRENT_EXTRA_EXPORT rtc_stream::endpoint_type rtc_parse_endpoint(std::string const& addr, error_code& ec);

}

#endif // TORRENT_USE_RTC

#endif
