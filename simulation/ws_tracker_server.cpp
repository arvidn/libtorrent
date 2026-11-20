/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "ws_tracker_server.hpp"

#if TORRENT_USE_RTC

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/json.hpp>
#if defined TORRENT_LINKING_SHARED
#include <boost/json/src.hpp>
#endif
#include "libtorrent/aux_/disable_warnings_pop.hpp"

sim_ws_tracker::sim_ws_tracker(
	sim::asio::io_context& ios, int port, int const announces_before_reply, bool const keep_alive)
	: m_acceptor(ios)
	, m_ws(ios)
	, m_announces_before_reply(announces_before_reply)
	, m_keep_alive(keep_alive)
{
	boost::system::error_code ec;
	m_acceptor.open(sim_tcp::v4(), ec);
	m_acceptor.bind(sim_tcp::endpoint(sim::asio::ip::address_v4::any(), std::uint16_t(port)), ec);
	m_acceptor.listen(1, ec);

	m_acceptor.async_accept(m_ws.next_layer(), [this](boost::system::error_code const& accept_ec) {
		if (accept_ec)
			return;
		boost::system::error_code ec;
		auto const ep = m_ws.next_layer().remote_endpoint(ec);
		if (!ec)
			m_connections.push_back(ep.address());
		if (m_keep_alive)
			m_acceptor.close(ec);
		m_ws.async_accept([this](boost::system::error_code const& handshake_ec) {
			if (handshake_ec)
				return;
			read_next();
		});
	});
}

std::string sim_ws_tracker::extract_info_hash(std::string const& msg)
{
	boost::system::error_code ec;
	auto const val = boost::json::parse(msg, ec);
	if (ec || !val.is_object())
		return {};
	auto const& obj = val.as_object();
	auto const it = obj.find("info_hash");
	if (it == obj.end() || !it->value().is_string())
		return {};
	return std::string(it->value().as_string());
}

void sim_ws_tracker::read_next()
{
	m_buffer.consume(m_buffer.size());
	m_ws.async_read(m_buffer, [this](boost::system::error_code const& ec, std::size_t) {
		if (ec)
		{
			m_closed = true;
			return;
		}

		m_received.push_back(extract_info_hash(boost::beast::buffers_to_string(m_buffer.data())));
		if (int(m_received.size()) < m_announces_before_reply)
		{
			read_next();
			return;
		}

		m_response = "{\"info_hash\":\"" + (m_keep_alive ? m_received.back() : m_received.front())
			+ "\",\"interval\":120,\"min_interval\":60}";
		m_ws.async_write(boost::asio::buffer(m_response),
			[this](boost::system::error_code const& ec, std::size_t) {
				if (!ec && m_keep_alive)
				{
					read_next();
					return;
				}
				boost::system::error_code ignore;
				m_ws.next_layer().close(ignore);
				m_closed = true;
			});
	});
}

#endif // TORRENT_USE_RTC
