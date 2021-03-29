/*

Copyright (c) 2019 Paul-Louis Ageneau
Copyright (c) 2020, Arvid Norberg
Copyright (c) 2020, Paul-Louis Ageneau
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/config.hpp"

#if TORRENT_USE_RTC

#include "libtorrent/aux_/rtc_stream.hpp"
#include "libtorrent/error.hpp"
#include "libtorrent/span.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <rtc/rtc.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace lt::aux {

namespace ip = boost::asio::ip;
namespace errc = boost::system::errc;

using boost::asio::const_buffer;
using boost::asio::mutable_buffer;

rtc_stream_impl::rtc_stream_impl(io_context& ioc, rtc_stream_init init)
	: m_io_context(ioc)
	, m_peer_connection(std::move(init.peer_connection))
	, m_data_channel(std::move(init.data_channel))
{

}

void rtc_stream_impl::init()
{
	auto weak_this = weak_from_this();

	m_data_channel->onAvailable([this, weak_this]()
	{
		// Warning: this is called from another thread
		auto self = weak_this.lock();
		if (!self) return;

		post(m_io_context, std::bind(&rtc_stream_impl::on_available
			, std::move(self)
			, error_code{}
		));
	});

	m_data_channel->setBufferedAmountLowThreshold(0);
	m_data_channel->onBufferedAmountLow([this, weak_this]()
	{
		// Warning: this is called from another thread
		auto self = weak_this.lock();
		if (!self) return;

		post(m_io_context, std::bind(&rtc_stream_impl::on_buffered_low
			, std::move(self)
			, error_code{}
		));
	});

	m_data_channel->onClosed([this, weak_this]()
	{
		// Warning: this is called from another thread
		auto self = weak_this.lock();
		if (!self) return;

		post(m_io_context, std::bind(&rtc_stream_impl::cancel_handlers
			, std::move(self)
			, boost::asio::error::connection_reset
		));
	});
}

void rtc_stream_impl::close()
{
	if (m_data_channel && !m_data_channel->isClosed())
		m_data_channel->close();

	cancel_handlers(boost::asio::error::operation_aborted);
}

void rtc_stream_impl::on_available(error_code const& ec)
{
	if (!m_read_handler) return;

	if (ec)
	{
		clear_read_buffers();
		post(m_io_context, std::bind(std::exchange(m_read_handler, nullptr), ec, 0));
		return;
	}

	// Fulfil pending read
	issue_read();
}

void rtc_stream_impl::on_buffered_low(error_code const& ec)
{
	if (!m_write_handler) return;

	std::size_t const bytes_written = ec ? 0 : m_write_buffer_size;

	m_write_buffer.clear();
	m_write_buffer_size = 0;
	post(m_io_context, std::bind(std::exchange(m_write_handler, nullptr), ec, bytes_written));
}

bool rtc_stream_impl::is_open() const
{
	return m_data_channel && m_data_channel->isOpen();
}

std::size_t rtc_stream_impl::available() const
{
	return m_incoming.size() + (m_data_channel ? m_data_channel->availableAmount() : 0);
}

rtc_stream::endpoint_type rtc_stream_impl::remote_endpoint(error_code& ec) const
{
	if (!is_open())
	{
		ec = boost::asio::error::not_connected;
		return {};
	}

	auto const addr = m_peer_connection->remoteAddress();
	if (!addr)
	{
		ec = boost::asio::error::operation_not_supported;
		return {};
	}

	return rtc_parse_endpoint(*addr, ec);
}

rtc_stream::endpoint_type rtc_stream_impl::local_endpoint(error_code& ec) const
{
	if (!is_open())
	{
		ec = boost::asio::error::not_connected;
		return {};
	}

	auto addr = m_peer_connection->localAddress();
	if (!addr)
	{
		ec = boost::asio::error::operation_not_supported;
		return {};
	}

	return rtc_parse_endpoint(*addr, ec);
}

void rtc_stream_impl::cancel_handlers(error_code const& ec)
{
	TORRENT_ASSERT(ec);

	if(auto handler = std::exchange(m_read_handler, nullptr))
		post(m_io_context, std::bind(handler, ec, 0));

	if(auto handler = std::exchange(m_write_handler, nullptr))
		post(m_io_context, std::bind(handler, ec, 0));

	m_read_buffer.clear();
	m_read_buffer_size = 0;

	m_write_buffer.clear();
	m_write_buffer_size = 0;

}

bool rtc_stream_impl::ensure_open()
{
	if (is_open()) return true;

	cancel_handlers(boost::asio::error::not_connected);
	return false;
}


void rtc_stream_impl::issue_read()
{
	TORRENT_ASSERT(m_read_handler);
	TORRENT_ASSERT(m_read_buffer_size > 0);

	if (!ensure_open()) return;

	error_code ec;
	std::size_t bytes_read = read_some(ec);
	if (ec || bytes_read > 0) // error or immediate read
	{
		clear_read_buffers();
		post(m_io_context, std::bind(std::exchange(m_read_handler, nullptr), ec, bytes_read));
	}
}

void rtc_stream_impl::issue_write()
{
	TORRENT_ASSERT(m_write_handler);
	TORRENT_ASSERT(m_write_buffer_size > 0);

	if (!ensure_open()) return;

	std::size_t const max_message_size = m_data_channel->maxMessageSize();
	std::size_t bytes_written = 0;
	bool is_buffered = false;
	while (!m_write_buffer.empty())
	{
		std::size_t bytes;
		std::tie(bytes, is_buffered) = write_data(max_message_size);
		bytes_written += bytes;
	}

	TORRENT_ASSERT(bytes_written == m_write_buffer_size);

	if (!is_buffered)
	{
		m_write_buffer.clear();
		m_write_buffer_size = 0;
		post(m_io_context, std::bind(std::exchange(m_write_handler, nullptr), error_code{}, bytes_written));
	}
}

std::size_t rtc_stream_impl::read_some(error_code& ec)
{
	if (!is_open())
	{
		ec = boost::asio::error::not_connected;
		return 0;
	}

	std::size_t bytes_read = 0;

	if (!m_incoming.empty())
	{
		std::size_t const copied = incoming_data(span<char const>{m_incoming.data(), long(m_incoming.size())});
		bytes_read += copied;
		if (copied < m_incoming.size())
		{
			m_incoming.erase(m_incoming.begin(), m_incoming.begin() + long(copied));
			return bytes_read;
		}

		m_incoming.clear();
	}

	while (!m_read_buffer.empty() && m_incoming.empty() && !ec)
	{
		auto message = m_data_channel->receive();
		if (!message) break;

		std::visit(rtc::overloaded
		{
			[&](rtc::binary const& bin)
			{
				char const *data = reinterpret_cast<char const*>(bin.data());
				std::size_t const size = bin.size();
				std::size_t const copied = incoming_data(span<char const>{data, long(size)});
				bytes_read += copied;
				if (copied < size)
					m_incoming.assign(data + copied, data + size);
			},
			[&](rtc::string const&)
			{
				ec = errc::make_error_code(errc::bad_message);
			}
		}
		, *message);
	}

	return bytes_read;
}

std::size_t rtc_stream_impl::write_some(error_code& ec)
{
	if (!is_open())
	{
		ec = boost::asio::error::not_connected;
		return 0;
	}

	if (m_data_channel->bufferedAmount() > 0)
	{
		ec = boost::asio::error::would_block;
		return 0;
	}

	std::size_t const max_message_size = m_data_channel->maxMessageSize();
	std::size_t bytes_written = 0;
	bool is_buffered = false;
	while (!m_write_buffer.empty() && !is_buffered)
	{
		std::size_t bytes = 0;
		std::tie(bytes, is_buffered) = write_data(max_message_size);
		bytes_written += bytes;
	}

	return bytes_written;
}

void rtc_stream_impl::clear_read_buffers()
{
	m_read_buffer.clear();
	m_read_buffer_size = 0;
}

void rtc_stream_impl::clear_write_buffers()
{
	m_write_buffer.clear();
	m_write_buffer_size = 0;
}

std::size_t rtc_stream_impl::incoming_data(span<char const> data)
{
	std::size_t bytes_read = 0;
	auto target = m_read_buffer.begin();
	while (target != m_read_buffer.end() && data.size() > 0)
	{
		std::size_t to_copy = std::min(std::size_t(data.size()), target->size());
		std::memcpy(target->data(), data.data(), to_copy);
		data = data.subspan(long(to_copy));
		(*target)+= to_copy;
		TORRENT_ASSERT(m_read_buffer_size >= to_copy);
		m_read_buffer_size -= to_copy;
		bytes_read += to_copy;
		if (target->size() == 0) target = m_read_buffer.erase(target);
	}
	return bytes_read;
}

std::pair<std::size_t, bool> rtc_stream_impl::write_data(std::size_t size)
{
	std::size_t total = 0;
	auto target = m_write_buffer.begin();
	while (target != m_write_buffer.end())
	{
		total += target->size();
		if (total >= size) break;
		++target;
	}

	if (total > size)
	{
		TORRENT_ASSERT(target != m_write_buffer.end());
		std::size_t const left = total - size;
		std::size_t const to_copy = target->size() - left;
		m_write_buffer.insert(target, boost::asio::const_buffer(target->data(), to_copy));
		(*target) += to_copy;
		total = size;
	}

	bool is_buffered = !m_data_channel->sendBuffer(m_write_buffer.begin(), target);
	m_write_buffer.erase(m_write_buffer.begin(), target);
	return std::make_pair(total, is_buffered);
}

rtc_stream::rtc_stream(io_context& ioc, rtc_stream_init init)
	  : m_io_context(ioc)
	  , m_impl(std::make_shared<rtc_stream_impl>(ioc, std::move(init)))
{
	m_impl->init();
}

rtc_stream::~rtc_stream()
{
	if (m_impl) m_impl->close();
}

rtc_stream::endpoint_type rtc_parse_endpoint(std::string const& addr, error_code& ec)
{
	// The format is always "address:port" without brackets around the address,
	// therefore splitting on the last ':' works to separate address and port.
	std::string host;
	std::uint16_t port;
	try {
		std::size_t const pos = addr.find_last_of(':');
		if (pos == std::string::npos) throw std::exception();

		host = addr.substr(0, pos);
		port = std::uint16_t(std::stoul(addr.substr(pos+1)));
	}
	catch(...) {
		ec = errors::parse_failed;
		return {};
	}

	return rtc_stream_impl::endpoint_type(ip::make_address(host, ec), port);
}

}

#endif // TORRENT_USE_RTC
