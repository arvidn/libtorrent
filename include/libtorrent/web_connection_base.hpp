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

#ifndef WEB_CONNECTION_BASE_HPP_INCLUDED
#define WEB_CONNECTION_BASE_HPP_INCLUDED

#include <ctime>
#include <algorithm>
#include <deque>
#include <string>
#include <cstdint>

#include "libtorrent/peer_connection.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/peer_request.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/http_parser.hpp"

namespace libtorrent {

	class torrent;

	class TORRENT_EXTRA_EXPORT web_connection_base
		: public peer_connection
	{
	friend class invariant_access;
	public:

		// this is the constructor where the we are the active part.
		// The peer_connection should handshake and verify that the
		// other end has the correct id
		web_connection_base(peer_connection_args const& pack
			, web_seed_t& web);

		int timeout() const override;
		void start() override;

		~web_connection_base() override;

		// called from the main loop when this connection has any
		// work to do.
		void on_sent(error_code const& error
			, std::size_t bytes_transferred) override;

		virtual std::string const& url() const = 0;

		bool in_handshake() const override;

		peer_id our_pid() const override { return peer_id(); }

		// the following functions appends messages
		// to the send buffer
		void write_choke() override {}
		void write_unchoke() override {}
		void write_interested() override {}
		void write_not_interested() override {}
		void write_request(peer_request const&) override = 0;
		void write_cancel(peer_request const&) override {}
		void write_have(piece_index_t) override {}
		void write_dont_have(piece_index_t) override {}
		void write_piece(peer_request const&, disk_buffer_holder) override
		{ TORRENT_ASSERT_FAIL(); }
		void write_keepalive() override {}
		void on_connected() override;
		void write_reject_request(peer_request const&) override {}
		void write_allow_fast(piece_index_t) override {}
		void write_suggest(piece_index_t) override {}
		void write_bitfield() override {}
		void write_upload_only(bool) override {}

#if TORRENT_USE_INVARIANT_CHECKS
		void check_invariant() const;
#endif

		void get_specific_peer_info(peer_info& p) const override;

	protected:

		virtual void add_headers(std::string& request
			, aux::session_settings const& sett, bool using_proxy) const;

		// the first request will contain a little bit more data
		// than subsequent ones, things that aren't critical are left
		// out to save bandwidth.
		bool m_first_request;

		// true if we're using ssl
		bool m_ssl;

		// this has one entry per bittorrent request
		std::deque<peer_request> m_requests;

		std::string m_server_string;
		std::string m_basic_auth;
		std::string m_host;
		std::string m_path;

		std::string m_external_auth;
		web_seed_entry::headers_t m_extra_headers;

		http_parser m_parser;

		int m_port;

		// the number of bytes into the receive buffer where
		// current read cursor is.
		int m_body_start;
	};
}

#endif // TORRENT_WEB_CONNECTION_BASE_HPP_INCLUDED
