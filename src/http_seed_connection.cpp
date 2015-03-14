/*

Copyright (c) 2008-2014, Arvid Norberg
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
#include <boost/limits.hpp>
#include <boost/bind.hpp>

#include "libtorrent/http_seed_connection.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/identify_client.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/version.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/parse_url.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/aux_/escape_string.hpp" // for is_hex

using boost::shared_ptr;
using libtorrent::aux::session_impl;

namespace libtorrent
{
	http_seed_connection::http_seed_connection(peer_connection_args const& pack
		, web_seed_t& web)
		: web_connection_base(pack, web)
		, m_url(web.url)
		, m_web(&web)
		, m_response_left(0)
		, m_chunk_pos(0)
		, m_partial_chunk_header(0)
	{
		INVARIANT_CHECK;

		if (!m_settings.get_bool(settings_pack::report_web_seed_downloads))
			ignore_stats(true);

		shared_ptr<torrent> tor = pack.tor.lock();
		TORRENT_ASSERT(tor);
		int blocks_per_piece = tor->torrent_file().piece_length() / tor->block_size();

		// multiply with the blocks per piece since that many requests are
		// merged into one http request
		max_out_request_queue(m_settings.get_int(settings_pack::urlseed_pipeline_size)
			* blocks_per_piece);

		prefer_contiguous_blocks(blocks_per_piece);

#ifdef TORRENT_LOGGING
		peer_log("*** http_seed_connection");
#endif
	}

	void http_seed_connection::disconnect(error_code const& ec
		, operation_t op, int error)
	{
		if (is_disconnecting()) return;

		if (op == op_connect && m_web && !m_web->endpoints.empty())
		{
			// we failed to connect to this IP. remove it so that the next attempt
			// uses the next IP in the list.
			m_web->endpoints.erase(m_web->endpoints.begin());
		}

		boost::shared_ptr<torrent> t = associated_torrent().lock();
		peer_connection::disconnect(ec, op, error);
		if (t) t->disconnect_web_seed(this);
	}
	
	boost::optional<piece_block_progress>
	http_seed_connection::downloading_piece_progress() const
	{
		if (m_requests.empty())
			return boost::optional<piece_block_progress>();

		boost::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);

		piece_block_progress ret;

		peer_request const& pr = m_requests.front();
		ret.piece_index = pr.piece;
		if (!m_parser.header_finished())
		{
			ret.bytes_downloaded = 0;
		}
		else
		{
			int receive_buffer_size = m_recv_buffer.get().left() - m_parser.body_start();
			// TODO: 1 in chunked encoding mode, this assert won't hold.
			// the chunk headers should be subtracted from the receive_buffer_size
			TORRENT_ASSERT_VAL(receive_buffer_size <= t->block_size(), receive_buffer_size);
			ret.bytes_downloaded = t->block_size() - receive_buffer_size;
		}
		// this is used to make sure that the block_index stays within
		// bounds. If the entire piece is downloaded, the block_index
		// would otherwise point to one past the end
		int correction = ret.bytes_downloaded ? -1 : 0;
		ret.block_index = (pr.start + ret.bytes_downloaded + correction) / t->block_size();
		ret.full_block_bytes = t->block_size();
		const int last_piece = t->torrent_file().num_pieces() - 1;
		if (ret.piece_index == last_piece && ret.block_index
			== t->torrent_file().piece_size(last_piece) / t->block_size())
			ret.full_block_bytes = t->torrent_file().piece_size(last_piece) % t->block_size();
		return ret;
	}

	void http_seed_connection::write_request(peer_request const& r)
	{
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);

		TORRENT_ASSERT(t->valid_metadata());
		// http_seeds don't support requesting more than one piece
		// at a time
		TORRENT_ASSERT(r.length <= t->torrent_file().piece_size(r.piece));

		std::string request;
		request.reserve(400);

		int size = r.length;
		const int block_size = t->block_size();
		const int piece_size = t->torrent_file().piece_length();
		peer_request pr;
		while (size > 0)
		{
			int request_offset = r.start + r.length - size;
			pr.start = request_offset % piece_size;
			pr.length = (std::min)(block_size, size);
			pr.piece = r.piece + request_offset / piece_size;
			m_requests.push_back(pr);
			size -= pr.length;
		}

		int proxy_type = m_settings.get_int(settings_pack::proxy_type);
		bool using_proxy = (proxy_type == settings_pack::http
			|| proxy_type == settings_pack::http_pw) && !m_ssl;

		request += "GET ";
		request += using_proxy ? m_url : m_path;
		request += "?info_hash=";
		request += escape_string((char const*)&t->torrent_file().info_hash()[0], 20);
		request += "&piece=";
		request += to_string(r.piece).elems;

		// if we're requesting less than an entire piece we need to
		// add ranges
		if (r.start > 0 || r.length != t->torrent_file().piece_size(r.piece))
		{
			request += "&ranges=";
			request += to_string(r.start).elems;
			request += "-";
			// ranges are inclusive, just like HTTP
			request += to_string(r.start + r.length - 1).elems;
		}

		request += " HTTP/1.1\r\n";
		add_headers(request, m_settings, using_proxy);
		request += "\r\n\r\n";
		m_first_request = false;

#ifdef TORRENT_LOGGING
		peer_log("==> %s", request.c_str());
#endif

		send_buffer(request.c_str(), request.size(), message_type_request);
	}

	// --------------------------
	// RECEIVE DATA
	// --------------------------

	void http_seed_connection::on_receive(error_code const& error
		, std::size_t bytes_transferred)
	{
		INVARIANT_CHECK;

		if (error)
		{
			received_bytes(0, bytes_transferred);
#ifdef TORRENT_LOGGING
			peer_log("*** http_seed_connection error: %s", error.message().c_str());
#endif
			return;
		}

		boost::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);

		for (;;)
		{
			buffer::const_interval recv_buffer = m_recv_buffer.get();

			if (bytes_transferred == 0) break;
			TORRENT_ASSERT(recv_buffer.left() > 0);

			TORRENT_ASSERT(!m_requests.empty());
			if (m_requests.empty())
			{
				received_bytes(0, bytes_transferred);
				disconnect(errors::http_error, op_bittorrent, 2);
				return;
			}

			peer_request front_request = m_requests.front();

			bool header_finished = m_parser.header_finished();
			if (!header_finished)
			{
				bool parse_error = false;
				int protocol = 0;
				int payload = 0;
				boost::tie(payload, protocol) = m_parser.incoming(recv_buffer, parse_error);
				received_bytes(0, protocol);
				bytes_transferred -= protocol;
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
				if (payload > front_request.length) payload = front_request.length;
#endif

				if (parse_error)
				{
					received_bytes(0, bytes_transferred);
					disconnect(errors::http_parse_error, op_bittorrent, 2);
					return;
				}

				TORRENT_ASSERT(recv_buffer.left() == 0 || *recv_buffer.begin == 'H');
			
				TORRENT_ASSERT(recv_buffer.left() <= m_recv_buffer.packet_size());
				
				// this means the entire status line hasn't been received yet
				if (m_parser.status_code() == -1)
				{
					TORRENT_ASSERT(payload == 0);
					TORRENT_ASSERT(bytes_transferred == 0);
					break;
				}

				// if the status code is not one of the accepted ones, abort
				if (!is_ok_status(m_parser.status_code()))
				{
					int retry_time = atoi(m_parser.header("retry-after").c_str());
					if (retry_time <= 0) retry_time = 5 * 60;
					// temporarily unavailable, retry later
					t->retry_web_seed(this, retry_time);

					std::string error_msg = to_string(m_parser.status_code()).elems
						+ (" " + m_parser.message());
					if (t->alerts().should_post<url_seed_alert>())
					{
						t->alerts().post_alert(url_seed_alert(t->get_handle(), url()
							, error_msg));
					}
					received_bytes(0, bytes_transferred);
					disconnect(error_code(m_parser.status_code(), get_http_category()), op_bittorrent, 1);
					return;
				}
				if (!m_parser.header_finished())
				{
					TORRENT_ASSERT(payload == 0);
					TORRENT_ASSERT(bytes_transferred == 0);
					break;
				}
			}

			// we just completed reading the header
			if (!header_finished)
			{
				if (is_redirect(m_parser.status_code()))
				{
					// this means we got a redirection request
					// look for the location header
					std::string location = m_parser.header("location");
					received_bytes(0, bytes_transferred);

					if (location.empty())
					{
						// we should not try this server again.
						t->remove_web_seed(this, errors::missing_location, op_bittorrent, 2);
						return;
					}
					
					// add the redirected url and remove the current one
					t->add_web_seed(location, web_seed_entry::http_seed);
					t->remove_web_seed(this, errors::redirecting, op_bittorrent, 2);
					return;
				}

				std::string const& server_version = m_parser.header("server");
				if (!server_version.empty())
				{
					m_server_string = "URL seed @ ";
					m_server_string += m_host;
					m_server_string += " (";
					m_server_string += server_version;
					m_server_string += ")";
				}

				m_response_left = atol(m_parser.header("content-length").c_str());
				if (m_response_left == -1)
				{
					received_bytes(0, bytes_transferred);
					// we should not try this server again.
					t->remove_web_seed(this, errors::no_content_length, op_bittorrent, 2);
					return;
				}
				if (m_response_left != front_request.length)
				{
					received_bytes(0, bytes_transferred);
					// we should not try this server again.
					t->remove_web_seed(this, errors::invalid_range, op_bittorrent, 2);
					return;
				}
				m_body_start = m_parser.body_start();
			}

			recv_buffer.begin += m_body_start;

			// =========================
			// === CHUNKED ENCODING  ===
			// =========================
			while (m_parser.chunked_encoding()
				&& m_chunk_pos >= 0
				&& m_chunk_pos < recv_buffer.left())
			{
				int header_size = 0;
				boost::int64_t chunk_size = 0;
				buffer::const_interval chunk_start = recv_buffer;
				chunk_start.begin += m_chunk_pos;
				TORRENT_ASSERT(chunk_start.begin[0] == '\r' || is_hex(chunk_start.begin, 1));
				bool ret = m_parser.parse_chunk_header(chunk_start, &chunk_size, &header_size);
				if (!ret)
				{
					TORRENT_ASSERT(bytes_transferred >= size_t(chunk_start.left() - m_partial_chunk_header));
					bytes_transferred -= chunk_start.left() - m_partial_chunk_header;
					received_bytes(0, chunk_start.left() - m_partial_chunk_header);
					m_partial_chunk_header = chunk_start.left();
					if (bytes_transferred == 0) return;
					break;
				}
				else
				{
#ifdef TORRENT_LOGGING
					peer_log("*** parsed chunk: %d header_size: %d", chunk_size, header_size);
#endif
					TORRENT_ASSERT(bytes_transferred >= size_t(header_size - m_partial_chunk_header));
					bytes_transferred -= header_size - m_partial_chunk_header;

					received_bytes(0, header_size - m_partial_chunk_header);
					m_partial_chunk_header = 0;
					TORRENT_ASSERT(chunk_size != 0 || chunk_start.left() <= header_size || chunk_start.begin[header_size] == 'H');
					// cut out the chunk header from the receive buffer
					TORRENT_ASSERT(m_chunk_pos + m_body_start < INT_MAX);
					m_recv_buffer.cut(header_size, t->block_size() + 1024, int(m_chunk_pos + m_body_start));
					recv_buffer = m_recv_buffer.get();
					recv_buffer.begin += m_body_start;
					m_chunk_pos += chunk_size;
					if (chunk_size == 0)
					{
						TORRENT_ASSERT(m_recv_buffer.get().left() < m_chunk_pos + m_body_start + 1
							|| m_recv_buffer.get()[int(m_chunk_pos + m_body_start)] == 'H'
							|| (m_parser.chunked_encoding() && m_recv_buffer.get()[int(m_chunk_pos + m_body_start)] == '\r'));
						m_chunk_pos = -1;
					}
				}
			}

			int payload = bytes_transferred;
			if (payload > m_response_left) payload = int(m_response_left);
			if (payload > front_request.length) payload = front_request.length;
			received_bytes(payload, 0);
			incoming_piece_fragment(payload);
			m_response_left -= payload;

			if (m_parser.status_code() == 503)
			{
				if (!m_parser.finished()) return;

				int retry_time = atol(std::string(recv_buffer.begin, recv_buffer.end).c_str());
				if (retry_time <= 0) retry_time = 60;
#ifdef TORRENT_LOGGING
				peer_log("*** retrying in %d seconds", retry_time);
#endif

				received_bytes(0, bytes_transferred);
				// temporarily unavailable, retry later
				t->retry_web_seed(this, retry_time);
				disconnect(error_code(m_parser.status_code(), get_http_category()), op_bittorrent, 1);
				return;
			}


			// we only received the header, no data
			if (recv_buffer.left() == 0) break;

			if (recv_buffer.left() < front_request.length) break;

			// if the response is chunked, we need to receive the last
			// terminating chunk and the tail headers before we can proceed
			if (m_parser.chunked_encoding() && m_chunk_pos >= 0) break;

			m_requests.pop_front();
			incoming_piece(front_request, recv_buffer.begin);
			if (associated_torrent().expired()) return;

			int size_to_cut = m_body_start + front_request.length;
			TORRENT_ASSERT(m_recv_buffer.get().left() < size_to_cut + 1
				|| m_recv_buffer.get()[size_to_cut] == 'H'
				|| (m_parser.chunked_encoding() && m_recv_buffer.get()[size_to_cut] == '\r'));

			m_recv_buffer.cut(size_to_cut, t->block_size() + 1024);
			if (m_response_left == 0) m_chunk_pos = 0;
			else m_chunk_pos -= front_request.length;
			bytes_transferred -= payload;
			m_body_start = 0;
			if (m_response_left > 0) continue;
			TORRENT_ASSERT(m_response_left == 0);
			m_parser.reset();
		}
	}

	void http_seed_connection::get_specific_peer_info(peer_info& p) const
	{
		web_connection_base::get_specific_peer_info(p);
		p.flags |= peer_info::local_connection;
		p.connection_type = peer_info::http_seed;
	}

}

