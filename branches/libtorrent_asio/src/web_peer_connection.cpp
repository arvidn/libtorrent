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

#include <vector>
#include <iostream>
#include <iomanip>
#include <limits>
#include <boost/bind.hpp>
#include <sstream>

#include "libtorrent/web_peer_connection.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/identify_client.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/version.hpp"

using namespace boost::posix_time;
using boost::bind;
using boost::shared_ptr;
using libtorrent::detail::session_impl;

namespace libtorrent
{
	web_peer_connection::web_peer_connection(
		detail::session_impl& ses
		, torrent* t
		, shared_ptr<stream_socket> s
		, tcp::endpoint const& remote
		, std::string const& url)
		: peer_connection(ses, t, s, remote)
		, m_url(url)
	{
		INVARIANT_CHECK;

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << "*** web_peer_connection\n";
#endif

		std::string protocol;
		boost::tie(protocol, m_host, m_port, m_path)
			= parse_url_components(url);
	}

	web_peer_connection::~web_peer_connection()
	{
	}
	
	boost::optional<piece_block_progress>
	web_peer_connection::downloading_piece_progress() const
	{
		// TODO: temporary implementation!!
		return boost::optional<piece_block_progress>();
/*

		buffer::const_interval recv_buffer = receive_buffer();
		// are we currently receiving a 'piece' message?
		if (m_state != read_packet
			|| (recv_buffer.end - recv_buffer.begin) < 9
			|| recv_buffer[0] != msg_piece)
			return boost::optional<piece_block_progress>();

		const char* ptr = recv_buffer.begin + 1;
		peer_request r;
		r.piece = detail::read_int32(ptr);
		r.start = detail::read_int32(ptr);
		r.length = packet_size() - 9;

		// is any of the piece message header data invalid?
		if (!verify_piece(r))

		piece_block_progress p;

		p.piece_index = r.piece;
		p.block_index = r.start / associated_torrent()->block_size();
		p.bytes_downloaded = recv_buffer.end - recv_buffer.begin - 9;
		p.full_block_bytes = r.length;

		return boost::optional<piece_block_progress>(p);
*/
	}

	void web_peer_connection::on_connected()
	{
		// this is always a seed
		incoming_bitfield(std::vector<bool>(
			associated_torrent()->torrent_file().num_pieces(), true));
		// it is always possible to request pieces
		incoming_unchoke();
		
		reset_recv_buffer(512*1024+1024);
	}

	void web_peer_connection::write_request(peer_request const& r)
	{
		INVARIANT_CHECK;

		assert(associated_torrent()->valid_metadata());

		bool single_file_request = false;
		if (!m_path.empty() && m_path[m_path.size() - 1] != '/')
			single_file_request = true;

		torrent_info const& info = associated_torrent()->torrent_file();
		
		// TODO: for now there's only support for single file torrents.
		// the receive function need to be able to put responses together
		// to form a single block in order to support multi-file torrents
		assert(info.num_files() == 1);

		std::string request;

		if (single_file_request)
		{
			request += "GET ";
			request += m_path;
			request += " HTTP/1.1\r\n";
			request += "Accept-Encoding: gzip\r\n";
			request += "Host: ";
			request += m_host;
			request += "\r\nUser-Agent: ";
			request += m_ses.m_http_settings.user_agent;
			request += "\r\nRange: bytes=";
			request += boost::lexical_cast<std::string>(r.piece
				* info.piece_length() + r.start);
			request += "-";
			request += boost::lexical_cast<std::string>(r.piece
				* info.piece_length() + r.start + r.length - 1);
			request += "\r\nConnection: keep-alive\r\n\r\n";
		}
		else
		{
			std::vector<file_slice> files = info.map_block(r.piece, r.start
				, r.length);

			for (std::vector<file_slice>::iterator i = files.begin();
				i != files.end(); ++i)
			{
				file_slice const& f = *i;

				request += "GET ";
				request += m_path;
				request += info.file_at(f.file_index).path.string();
				request += " HTTP/1.1\r\n";
				request += "Accept-Encoding: gzip\r\n";
				request += "Host: ";
				request += m_host;
				request += "\r\nUser-Agent: ";
				request += m_ses.m_http_settings.user_agent;
				request += "\r\nRange: bytes=";
				request += boost::lexical_cast<std::string>(f.offset);
				request += "-";
				request += boost::lexical_cast<std::string>(f.offset + f.size - 1);
				request += "\r\nConnection: keep-alive\r\n\r\n";
			}
		}

		send_buffer(request.c_str(), request.c_str() + request.size());
	}

	// --------------------------
	// RECEIVE DATA
	// --------------------------

	// throws exception when the client should be disconnected
	void web_peer_connection::on_receive(const asio::error& error
		, std::size_t bytes_transferred)
	{
		INVARIANT_CHECK;

		if (error)
		{
			return;
		}

		m_last_piece = boost::posix_time::second_clock::universal_time();

		for (;;)
		{
			buffer::const_interval recv_buffer = receive_buffer();
			int payload;
			int protocol;
			boost::tie(payload, protocol) = m_parser.incoming(recv_buffer);
			m_statistics.received_bytes(payload, protocol);

			if (m_parser.status_code() != 206 && m_parser.status_code() != -1)
			{
				// we should not try this server again.
				associated_torrent()->remove_url_seed(m_url);
				throw std::runtime_error("HTTP server does not support byte range requests");
			}

			if (!m_parser.finished()) break;

			peer_request r;
//			std::string debug = m_parser.header<std::string>("Content-Range");
			std::stringstream range_str(m_parser.header<std::string>("Content-Range"));
			size_type range_start;
			size_type range_end;
			char dummy;
			std::string bytes;
			range_str >> bytes >> range_start >> dummy >> range_end;
			if (!range_str)
			{
				// we should not try this server again.
				associated_torrent()->remove_url_seed(m_url);
				throw std::runtime_error("invalid range in HTTP response: " + range_str.str());
			}

			torrent_info const& info = associated_torrent()->torrent_file();

			r.piece = range_start / info.piece_length();
			r.start = range_start - r.piece * info.piece_length();
			r.length = range_end - range_start + 1;
			buffer::const_interval http_body = m_parser.get_body();
			incoming_piece(r, http_body.begin);
			cut_receive_buffer(http_body.end - recv_buffer.begin, 512*1024+1024);
		}
	}

	// --------------------------
	// SEND DATA
	// --------------------------

	// throws exception when the client should be disconnected
	void web_peer_connection::on_sent(asio::error const& error
		, std::size_t bytes_transferred)
	{
		INVARIANT_CHECK;

		if (error) return;
		m_statistics.sent_bytes(0, bytes_transferred);
	}


#ifndef NDEBUG
	void web_peer_connection::check_invariant() const
	{
/*
		assert(m_num_pieces == std::count(
			m_have_piece.begin()
			, m_have_piece.end()
			, true));
*/	}
#endif

}

