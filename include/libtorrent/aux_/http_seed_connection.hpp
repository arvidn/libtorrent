/*

Copyright (c) 2008-2010, 2012, 2014-2020, Arvid Norberg
Copyright (c) 2016, 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_HTTP_SEED_CONNECTION_HPP_INCLUDED
#define TORRENT_HTTP_SEED_CONNECTION_HPP_INCLUDED

#include <ctime>
#include <algorithm>
#include <string>
#include <cstdint>

#include "libtorrent/config.hpp"
#include "libtorrent/aux_/web_connection_base.hpp"
#include "libtorrent/piece_block_progress.hpp"

namespace libtorrent { struct peer_request; }

namespace libtorrent::aux {

	class TORRENT_EXTRA_EXPORT http_seed_connection
		: public web_connection_base
	{
	friend struct invariant_access;
	public:

		// this is the constructor where the we are the active part.
		// The peer_connection should handshake and verify that the
		// other end has the correct id
		http_seed_connection(peer_connection_args& pack
			, aux::web_seed_t& web);

		connection_type type() const override
		{ return connection_type::http_seed; }

		// called from the main loop when this connection has any
		// work to do.
		void on_receive(error_code const& error
			, std::size_t bytes_transferred) override;

		void on_connected() override;

		std::string const& url() const override { return m_url; }

		void get_specific_peer_info(peer_info& p) const override;
		void disconnect(error_code const& ec, operation_t op
			, disconnect_severity_t error = peer_connection_interface::normal) override;

		void write_request(peer_request const& r) override;

	private:

		// returns the block currently being
		// downloaded. And the progress of that
		// block. If the peer isn't downloading
		// a piece for the moment, the boost::optional
		// will be invalid.
		piece_block_progress downloading_piece_progress() const override;

		// this is const since it's used as a key in the web seed list in the torrent
		// if it's changed referencing back into that list will fail
		const std::string m_url;

		aux::web_seed_t* m_web;

		// the number of bytes left to receive of the response we're
		// currently parsing
		std::int64_t m_response_left;

		// this is the offset inside the current receive
		// buffer where the next chunk header will be.
		// this is updated for each chunk header that's
		// parsed. It does not necessarily point to a valid
		// offset in the receive buffer, if we haven't received
		// it yet. This offset never includes the HTTP header
		std::int64_t m_chunk_pos;

		// this is the number of bytes we've already received
		// from the next chunk header we're waiting for
		int m_partial_chunk_header;
	};
}

#endif // TORRENT_WEB_PEER_CONNECTION_HPP_INCLUDED
