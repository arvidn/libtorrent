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

#ifndef TORRENT_HTTP_TRACKER_CONNECTION_HPP_INCLUDED
#define TORRENT_HTTP_TRACKER_CONNECTION_HPP_INCLUDED

#include <string>
#include <vector>

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/shared_ptr.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/config.hpp"
#include "libtorrent/lazy_entry.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/i2p_stream.hpp"
#include "libtorrent/error_code.hpp"

namespace libtorrent
{
	
	struct http_connection;
	class entry;
	class http_parser;
	struct bdecode_node;
	struct peer_entry;

	namespace aux { struct session_settings; }

	class TORRENT_EXTRA_EXPORT http_tracker_connection
		: public tracker_connection
	{
	friend class tracker_manager;
	public:

		http_tracker_connection(
			io_service& ios
			, tracker_manager& man
			, tracker_request const& req
			, boost::weak_ptr<request_callback> c);

		void start();
		void close();

	private:

		boost::shared_ptr<http_tracker_connection> shared_from_this()
		{
			return boost::static_pointer_cast<http_tracker_connection>(
				tracker_connection::shared_from_this());
		}

		void on_filter(http_connection& c, std::vector<tcp::endpoint>& endpoints);
		void on_connect(http_connection& c);
		void on_response(error_code const& ec, http_parser const& parser
			, char const* data, int size);

		virtual void on_timeout(error_code const&) {}

		tracker_manager& m_man;
		boost::shared_ptr<http_connection> m_tracker_connection;
		address m_tracker_ip;
	};

	TORRENT_EXTRA_EXPORT tracker_response parse_tracker_response(
		char const* data, int size, error_code& ec
		, int flags, sha1_hash scrape_ih);

	TORRENT_EXTRA_EXPORT bool extract_peer_info(bdecode_node const& info
		, peer_entry& ret, error_code& ec);
}

#endif // TORRENT_HTTP_TRACKER_CONNECTION_HPP_INCLUDED

