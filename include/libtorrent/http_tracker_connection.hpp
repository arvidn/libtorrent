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

#include <string>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/shared_ptr.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/config.hpp"
#include "libtorrent/lazy_entry.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/config.hpp"

namespace libtorrent
{
	
	struct http_connection;
	class entry;
	class http_parser;
	class connection_queue;
	struct session_settings;
	namespace aux { struct session_impl; }

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
			, boost::weak_ptr<request_callback> c
			, aux::session_impl const& ses
			, proxy_settings const& ps
			, std::string const& password = "");

		void start();
		void close();

	private:

		boost::intrusive_ptr<http_tracker_connection> self()
		{ return boost::intrusive_ptr<http_tracker_connection>(this); }

		void on_filter(http_connection& c, std::list<tcp::endpoint>& endpoints);
		void on_connect(http_connection& c);
		void on_response(error_code const& ec, http_parser const& parser
			, char const* data, int size);

		virtual void on_timeout(error_code const& ec) {}

		void parse(int status_code, lazy_entry const& e);
		bool extract_peer_info(lazy_entry const& e, peer_entry& ret);

		tracker_manager& m_man;
		boost::shared_ptr<http_connection> m_tracker_connection;
		aux::session_impl const& m_ses;
		address m_tracker_ip;
		proxy_settings const& m_ps;
		connection_queue& m_cc;
		io_service& m_ios;
	};

}

#endif // TORRENT_HTTP_TRACKER_CONNECTION_HPP_INCLUDED

