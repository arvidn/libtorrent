/*

Copyright (c) 2004-2008, 2010, 2012, 2014-2017, 2019-2020, Arvid Norberg
Copyright (c) 2016, 2020-2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_UDP_TRACKER_CONNECTION_HPP_INCLUDED
#define TORRENT_UDP_TRACKER_CONNECTION_HPP_INCLUDED

#include <vector>
#include <string>
#include <utility>
#include <mutex>
#include <cstdint>
#include <memory>
#include <map>

#include "libtorrent/aux_/udp_socket.hpp"
#include "libtorrent/aux_/tracker_manager.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/span.hpp"

namespace lt::aux {

	class TORRENT_EXTRA_EXPORT udp_tracker_connection: public tracker_connection
	{
	friend class tracker_manager;
	public:

		udp_tracker_connection(
			io_context& ios
			, tracker_manager& man
			, tracker_request const& req
			, std::weak_ptr<request_callback> c);

		void start() override;
		void close() override;

		std::uint32_t transaction_id() const { return m_transaction_id; }

	private:

		enum class action_t : std::uint8_t
		{
			connect,
			announce,
			scrape,
			error
		};

		std::shared_ptr<udp_tracker_connection> shared_from_this()
		{
			return std::static_pointer_cast<udp_tracker_connection>(
				tracker_connection::shared_from_this());
		}

		void update_transaction_id();

		void name_lookup(error_code const& error
			, std::vector<address> const& addresses, int port);
		void start_announce();

		bool on_receive(udp::endpoint const& ep, span<char const> buf);
		bool on_receive_hostname(char const* hostname, span<char const> buf);
		bool on_connect_response(span<char const> buf);
		bool on_announce_response(span<char const> buf);
		bool on_scrape_response(span<char const> buf);

		// wraps tracker_connection::fail
		void fail(error_code const& ec
			, operation_t op
			, char const* msg = ""
			, seconds32 interval = seconds32(0)
			, seconds32 min_interval = seconds32(30));

		void send_udp_connect();
		void send_udp_announce();
		void send_udp_scrape();

		void on_timeout(error_code const& ec) override;

		std::string m_hostname;
		std::vector<tcp::endpoint> m_endpoints;

		struct connection_cache_entry
		{
			std::int64_t connection_id;
			time_point expires;
		};

		static std::map<address, connection_cache_entry> m_connection_cache;
		static std::mutex m_cache_mutex;

		udp::endpoint m_target;

		std::uint32_t m_transaction_id;
		int m_attempts;

		action_t m_state;

		bool m_abort;
	};

}

#endif // TORRENT_UDP_TRACKER_CONNECTION_HPP_INCLUDED
