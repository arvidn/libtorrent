/*

Copyright (c) 2003-2016, Arvid Norberg
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

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <vector>
#include <string>
#include <utility>
#include <ctime>

#include <boost/shared_ptr.hpp>
#include <boost/cstdint.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/udp_socket.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/session_settings.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/peer.hpp"
#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/config.hpp"

namespace libtorrent
{
	class TORRENT_EXTRA_EXPORT udp_tracker_connection: public tracker_connection
	{
	friend class tracker_manager;
	public:

		udp_tracker_connection(
			io_service& ios
			, tracker_manager& man
			, tracker_request const& req
			, boost::weak_ptr<request_callback> c);

		void start();
		void close();

		boost::uint32_t transaction_id() const { return m_transaction_id; }

	private:

		enum action_t
		{
			action_connect,
			action_announce,
			action_scrape,
			action_error
		};

		boost::shared_ptr<udp_tracker_connection> shared_from_this()
		{
			return boost::static_pointer_cast<udp_tracker_connection>(
				tracker_connection::shared_from_this());
		}

		void update_transaction_id();

		void name_lookup(error_code const& error
			, std::vector<address> const& addresses, int port);
		void timeout(error_code const& error);
		void start_announce();

		bool on_receive(error_code const& e, udp::endpoint const& ep
			, char const* buf, int size);
		bool on_receive_hostname(error_code const& e, char const* hostname
			, char const* buf, int size);
		bool on_connect_response(char const* buf, int size);
		bool on_announce_response(char const* buf, int size);
		bool on_scrape_response(char const* buf, int size);

		// wraps tracker_connection::fail
		void fail(error_code const& ec, int code = -1
			, char const* msg = "", int interval = 0, int min_interval = 0);

		void send_udp_connect();
		void send_udp_announce();
		void send_udp_scrape();

		virtual void on_timeout(error_code const& ec);

		udp::endpoint pick_target_endpoint() const;

		std::string m_hostname;
		std::vector<tcp::endpoint> m_endpoints;

		struct connection_cache_entry
		{
			boost::int64_t connection_id;
			time_point expires;
		};

		static std::map<address, connection_cache_entry> m_connection_cache;
		static mutex m_cache_mutex;

		udp::endpoint m_target;

		boost::uint32_t m_transaction_id;
		int m_attempts;

		// action_t
		boost::uint8_t m_state;

		bool m_abort;
	};

}

#endif // TORRENT_UDP_TRACKER_CONNECTION_HPP_INCLUDED

