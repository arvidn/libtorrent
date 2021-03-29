/*

Copyright (c) 2010, 2012, 2014-2020, Arvid Norberg
Copyright (c) 2016, 2020-2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef WEB_CONNECTION_BASE_HPP_INCLUDED
#define WEB_CONNECTION_BASE_HPP_INCLUDED

#include <deque>
#include <string>
#include <cstdint>

#include "libtorrent/aux_/peer_connection.hpp"
#include "libtorrent/aux_/torrent.hpp"
#include "libtorrent/peer_request.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/aux_/http_parser.hpp"

namespace lt::aux {

	class TORRENT_EXTRA_EXPORT web_connection_base
		: public peer_connection
	{
	friend struct invariant_access;
	public:

		// this is the constructor where the we are the active part.
		// The peer_connection should handshake and verify that the
		// other end has the correct id
		web_connection_base(peer_connection_args& pack
			, aux::web_seed_t const& web);

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

		aux::http_parser m_parser;

		int m_port;

		// the number of bytes into the receive buffer where
		// current read cursor is.
		int m_body_start;
	};
}

#endif // TORRENT_WEB_CONNECTION_BASE_HPP_INCLUDED
