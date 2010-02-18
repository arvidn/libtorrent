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

	class TORRENT_EXPORT udp_tracker_connection: public tracker_connection
	{
	friend class tracker_manager;
	public:

		udp_tracker_connection(
			io_service& ios
			, connection_queue& cc
			, tracker_manager& man
			, tracker_request const& req
			, boost::weak_ptr<request_callback> c
			, aux::session_impl const& ses
			, proxy_settings const& ps);

		void start();
		void close();

	private:

		enum action_t
		{
			action_connect,
			action_announce,
			action_scrape,
			action_error
		};

		boost::intrusive_ptr<udp_tracker_connection> self()
		{ return boost::intrusive_ptr<udp_tracker_connection>(this); }

		void name_lookup(error_code const& error, udp::resolver::iterator i);
		void timeout(error_code const& error);

		void on_receive(error_code const& e, udp::endpoint const& ep
			, char const* buf, int size);
		void on_connect_response(char const* buf, int size);
		void on_announce_response(char const* buf, int size);
		void on_scrape_response(char const* buf, int size);

		void send_udp_connect();
		void send_udp_announce();
		void send_udp_scrape();

		virtual void on_timeout();

		tracker_manager& m_man;

		udp::resolver m_name_lookup;
		udp_socket m_socket;
		udp::endpoint m_target;
		std::list<udp::endpoint> m_endpoints;

		int m_transaction_id;
		aux::session_impl const& m_ses;
		int m_attempts;

		struct connection_cache_entry
		{
			boost::int64_t connection_id;
			ptime expires;
		};

		static std::map<address, connection_cache_entry> m_connection_cache;
		static boost::mutex m_cache_mutex;

		action_t m_state;
	};

}

#endif // TORRENT_UDP_TRACKER_CONNECTION_HPP_INCLUDED

