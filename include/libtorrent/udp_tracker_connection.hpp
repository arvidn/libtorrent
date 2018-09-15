/*

Copyright (c) 2003-2018, Arvid Norberg
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

#ifndef TORRENT_UDP_TRACKER_CONNECTION_HPP_INCLUDED
#define TORRENT_UDP_TRACKER_CONNECTION_HPP_INCLUDED

#include <vector>
#include <string>
#include <utility>
#include <mutex>
#include <cstdint>
#include <memory>
#include <map>

#include "libtorrent/udp_socket.hpp"
#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/span.hpp"

namespace libtorrent {

	class TORRENT_EXTRA_EXPORT udp_tracker_connection: public tracker_connection
	{
	friend class tracker_manager;
	public:

		udp_tracker_connection(
			io_service& ios
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
