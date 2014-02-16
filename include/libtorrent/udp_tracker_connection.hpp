/*

Copyright (c) 2003, Arvid Norberg
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
#include <ctime>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/shared_ptr.hpp>
#include <boost/cstdint.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/udp_socket.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/session_settings.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/peer.hpp"
#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/config.hpp"

namespace libtorrent
{
	namespace aux { struct session_impl; }

	class TORRENT_EXTRA_EXPORT udp_tracker_connection: public tracker_connection
	{
	friend class tracker_manager;
	public:

		udp_tracker_connection(
			io_service& ios
			, connection_queue& cc
			, tracker_manager& man
			, tracker_request const& req
			, boost::weak_ptr<request_callback> c
			, aux::session_impl& ses
			, proxy_settings const& ps);

		void start();
		void close();

#if !defined TORRENT_VERBOSE_LOGGING \
	&& !defined TORRENT_LOGGING \
	&& !defined TORRENT_ERROR_LOGGING
	// necessary for logging member offsets
	private:
#endif

		enum action_t
		{
			action_connect,
			action_announce,
			action_scrape,
			action_error
		};

		boost::intrusive_ptr<udp_tracker_connection> self()
		{ return boost::intrusive_ptr<udp_tracker_connection>(this); }

		void name_lookup(error_code const& error, tcp::resolver::iterator i);
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

//		tracker_manager& m_man;

		bool m_abort;
		std::string m_hostname;
		udp::endpoint m_target;
		std::list<tcp::endpoint> m_endpoints;

		int m_transaction_id;
		aux::session_impl& m_ses;
		int m_attempts;

		struct connection_cache_entry
		{
			boost::int64_t connection_id;
			ptime expires;
		};

		static std::map<address, connection_cache_entry> m_connection_cache;
		static mutex m_cache_mutex;

		action_t m_state;

		proxy_settings m_proxy;
	};

}

#endif // TORRENT_UDP_TRACKER_CONNECTION_HPP_INCLUDED

