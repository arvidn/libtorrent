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
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/cstdint.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/socket.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/http_proxy.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/peer.hpp"
#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/async_gethostbyname.hpp"

namespace libtorrent
{
	class udp_tracker_connection: public tracker_connection
	{
	friend class tracker_manager;
	public:

		udp_tracker_connection(
			tracker_request const& req
			, std::string const& hostname
			, unsigned short port
			, boost::weak_ptr<request_callback> c
			, const http_proxy& http_proxy);

		virtual bool tick();
		virtual bool send_finished() const;
		virtual tracker_request const& tracker_req() const
		{ return m_request; }

	private:

		enum action_t
		{
			connect,
			announce,
			scrape,
			error
		};

		void send_udp_connect();
		void send_udp_announce();
		void send_udp_scrape();
		bool parse_connect_response(const char* buf, int ret);
		bool parse_announce_response(const char* buf, int ret);
		bool parse_scrape_response(const char* buf, int ret);

		dns_lookup m_name_lookup;
		boost::shared_ptr<socket> m_socket;

		// used for time outs
		boost::posix_time::ptime m_request_time;

		tracker_request m_request;
		int m_transaction_id;
		boost::int64_t m_connection_id;
		const http_proxy& m_http_proxy;
		int m_attempts;
	};

}

#endif // TORRENT_UDP_TRACKER_CONNECTION_HPP_INCLUDED
