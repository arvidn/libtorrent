/*

Copyright (c) 2003-2014, Arvid Norberg
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

#ifndef TORRENT_WEB_PEER_CONNECTION_HPP_INCLUDED
#define TORRENT_WEB_PEER_CONNECTION_HPP_INCLUDED

#include <ctime>
#include <algorithm>
#include <vector>
#include <deque>
#include <string>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/smart_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/noncopyable.hpp>
#include <boost/array.hpp>
#include <boost/optional.hpp>
#include <boost/cstdint.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/config.hpp"
#include "libtorrent/web_connection_base.hpp"
#include "libtorrent/disk_buffer_holder.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/piece_block_progress.hpp"
#include "libtorrent/http_parser.hpp"

namespace libtorrent
{
	class torrent;

	namespace detail
	{
		struct session_impl;
	}

	class TORRENT_EXTRA_EXPORT web_peer_connection
		: public web_connection_base
	{
	friend class invariant_access;
	public:

		// this is the constructor where the we are the active part.
		// The peer_conenction should handshake and verify that the
		// other end has the correct id
		web_peer_connection(
			aux::session_impl& ses
			, boost::weak_ptr<torrent> t
			, boost::shared_ptr<socket_type> s
			, tcp::endpoint const& remote
			, web_seed_entry& web);

		virtual void on_connected();

		virtual int type() const { return peer_connection::url_seed_connection; }

		// called from the main loop when this connection has any
		// work to do.
		void on_receive(error_code const& error
			, std::size_t bytes_transferred);
			
		std::string const& url() const { return m_url; }
		
		virtual void get_specific_peer_info(peer_info& p) const;
		virtual void disconnect(error_code const& ec, int error = 0);

		virtual void write_request(peer_request const& r);

		virtual bool received_invalid_data(int index, bool single_peer);

	private:

		bool maybe_harvest_block();

		// returns the block currently being
		// downloaded. And the progress of that
		// block. If the peer isn't downloading
		// a piece for the moment, the boost::optional
		// will be invalid.
		boost::optional<piece_block_progress> downloading_piece_progress() const;

		void handle_padfile(buffer::const_interval& recv_buffer);

		// this has one entry per http-request
		// (might be more than the bt requests)
		std::deque<int> m_file_requests;

		std::string m_url;
	
		web_seed_entry& m_web;
			
		// this is used for intermediate storage of pieces
		// that are received in more than one HTTP response
		// TODO: 1 if we make this be a disk_buffer_holder instead
		// we would save a copy sometimes
		// use allocate_disk_receive_buffer and release_disk_receive_buffer
		std::vector<char> m_piece;
		
		// the number of bytes received in the current HTTP
		// response. used to know where in the buffer the
		// next response starts
		size_type m_received_body;

		// position in the current range response
		size_type m_range_pos;

		// the position in the current block
		int m_block_pos;

		// this is the offset inside the current receive
		// buffer where the next chunk header will be.
		// this is updated for each chunk header that's
		// parsed. It does not necessarily point to a valid
		// offset in the receive buffer, if we haven't received
		// it yet. This offset never includes the HTTP header
		size_type m_chunk_pos;

		// this is the number of bytes we've already received
		// from the next chunk header we're waiting for
		int m_partial_chunk_header;

		// the number of responses we've received so far on
		// this connection
		int m_num_responses;
	};
}

#endif // TORRENT_WEB_PEER_CONNECTION_HPP_INCLUDED

