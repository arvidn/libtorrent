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

#ifndef TORRENT_HTTP_TRACKER_CONNECTION_HPP_INCLUDED
#define TORRENT_HTTP_TRACKER_CONNECTION_HPP_INCLUDED

#include <vector>
#include <string>
#include <utility>
#include <ctime>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/shared_ptr.hpp>
#include <boost/cstdint.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/lexical_cast.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/socket.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/session_settings.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/peer.hpp"
#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/buffer.hpp"
#include "libtorrent/socket_type.hpp"
#include "libtorrent/connection_queue.hpp"
#include "libtorrent/http_parser.hpp"

namespace libtorrent
{
	
	class TORRENT_EXPORT http_tracker_connection
		: public tracker_connection
	{
	friend class tracker_manager;
	public:

		http_tracker_connection(
			io_service& ios
			, connection_queue& cc
			, tracker_manager& man
			, tracker_request const& req
			, std::string const& hostname
			, unsigned short port
			, std::string request
			, address bind_infc
			, boost::weak_ptr<request_callback> c
			, session_settings const& stn
			, proxy_settings const& ps
			, std::string const& password = "");

		void close();

	private:

		boost::intrusive_ptr<http_tracker_connection> self()
		{ return boost::intrusive_ptr<http_tracker_connection>(this); }

		void on_response();
		
		void init_send_buffer(
			std::string const& hostname
			, std::string const& request);

		void name_lookup(asio::error_code const& error, tcp::resolver::iterator i);
		void connect(int ticket, tcp::endpoint target_address);
		void connected(asio::error_code const& error);
		void sent(asio::error_code const& error);
		void receive(asio::error_code const& error
			, std::size_t bytes_transferred);

		virtual void on_timeout();

		void parse(const entry& e);
		bool extract_peer_info(const entry& e, peer_entry& ret);

		tracker_manager& m_man;
		http_parser m_parser;

		tcp::resolver m_name_lookup;
		int m_port;
		socket_type m_socket;
		int m_recv_pos;
		std::vector<char> m_buffer;
		std::string m_send_buffer;

		session_settings const& m_settings;
		proxy_settings const& m_proxy;
		std::string m_password;

		bool m_timed_out;

		int m_connection_ticket;
		connection_queue& m_cc;
	};

}

#endif // TORRENT_HTTP_TRACKER_CONNECTION_HPP_INCLUDED

