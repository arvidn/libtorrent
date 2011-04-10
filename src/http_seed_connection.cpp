/*

Copyright (c) 2008, Arvid Norberg
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

#include "libtorrent/pch.hpp"

#include <vector>
#include <limits>
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

using boost::shared_ptr;
using libtorrent::aux::session_impl;

namespace libtorrent
{
	http_seed_connection::http_seed_connection(
		session_impl& ses
		, boost::weak_ptr<torrent> t
		, boost::shared_ptr<socket_type> s
		, tcp::endpoint const& remote
		, std::string const& url
		, policy::peer* peerinfo)
		: peer_connection(ses, t, s, remote, peerinfo)
		, m_url(url)
		, m_first_request(true)
		, m_response_left(0)
		, m_body_start(0)
	{
		INVARIANT_CHECK;

		prefer_whole_pieces(1);

		// we only want left-over bandwidth
		set_priority(1);
		shared_ptr<torrent> tor = t.lock();
		TORRENT_ASSERT(tor);
		int blocks_per_piece = tor->torrent_file().piece_length() / tor->block_size();

		// multiply with the blocks per piece since that many requests are
		// merged into one http request
		m_max_out_request_queue = ses.settings().urlseed_pipeline_size
			* blocks_per_piece;

		// since this is a web seed, change the timeout
		// according to the settings.
		set_timeout(ses.settings().urlseed_timeout);
#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << "*** http_seed_connection\n";
#endif

		std::string protocol;
		error_code ec;
		boost::tie(protocol, m_auth, m_host, m_port, m_path)
			= parse_url_components(url, ec);
		TORRENT_ASSERT(!ec);
		
		if (!m_auth.empty())
			m_auth = base64encode(m_auth);

		m_server_string = "HTTP seed @ ";
		m_server_string += m_host;
	}

	void http_seed_connection::start()
	{
		set_upload_only(true);
		if (is_disconnecting()) return;
		peer_connection::start();
	}

	http_seed_connection::~http_seed_connection()
	{}
	
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
			int receive_buffer_size = receive_buffer().left() - m_parser.body_start();
			TORRENT_ASSERT(receive_buffer_size < t->block_size());
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

	void http_seed_connection::on_connected()
	{
		boost::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);
	
		// this is always a seed
		incoming_have_all();

		// it is always possible to request pieces
		incoming_unchoke();

		reset_recv_buffer(t->block_size() + 1024);
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

		proxy_settings const& ps = m_ses.web_seed_proxy();
		bool using_proxy = ps.type == proxy_settings::http
			|| ps.type == proxy_settings::http_pw;

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
		request += "Host: ";
		request += m_host;
		if (m_first_request)
		{
			request += "\r\nUser-Agent: ";
			request += m_ses.settings().user_agent;
		}
		if (!m_auth.empty())
		{
			request += "\r\nAuthorization: Basic ";
			request += m_auth;
		}
		if (ps.type == proxy_settings::http_pw)
		{
			request += "\r\nProxy-Authorization: Basic ";
			request += base64encode(ps.username + ":" + ps.password);
		}
		if (using_proxy)
		{
			request += "\r\nProxy-Connection: keep-alive";
		}
		if (m_first_request || using_proxy)
			request += "\r\nConnection: keep-alive";
		request += "\r\n\r\n";
		m_first_request = false;

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << request << "\n";
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
			m_statistics.received_bytes(0, bytes_transferred);
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << "*** http_seed_connection error: "
				<< error.message() << "\n";
#endif
			return;
		}

		boost::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);

		for (;;)
		{
			buffer::const_interval recv_buffer = receive_buffer();

			if (bytes_transferred == 0) break;
			TORRENT_ASSERT(recv_buffer.left() > 0);

			TORRENT_ASSERT(!m_requests.empty());
			if (m_requests.empty())
			{
				m_statistics.received_bytes(0, bytes_transferred);
				disconnect(errors::http_error, 2);
				return;
			}

			peer_request front_request = m_requests.front();

			int payload = 0;
			int protocol = 0;
			bool header_finished = m_parser.header_finished();
			if (!header_finished)
			{
				bool error = false;
				boost::tie(payload, protocol) = m_parser.incoming(recv_buffer, error);
				m_statistics.received_bytes(0, protocol);
				bytes_transferred -= protocol;
				if (payload > front_request.length) payload = front_request.length;

				if (error)
				{
					m_statistics.received_bytes(0, bytes_transferred);
					disconnect(errors::http_parse_error, 2);
					return;
				}

				TORRENT_ASSERT(recv_buffer.left() == 0 || *recv_buffer.begin == 'H');
			
				TORRENT_ASSERT(recv_buffer.left() <= packet_size());
				
				// this means the entire status line hasn't been received yet
				if (m_parser.status_code() == -1)
				{
					TORRENT_ASSERT(payload == 0);
					TORRENT_ASSERT(bytes_transferred == 0);
					break;
				}

				// if the status code is not one of the accepted ones, abort
				if (!is_ok_status(m_parser.status_code()) && m_parser.status_code() != 503)
				{
					int retry_time = atoi(m_parser.header("retry-after").c_str());
					if (retry_time <= 0) retry_time = 5 * 60;
					// temporarily unavailable, retry later
					t->retry_web_seed(m_url, web_seed_entry::http_seed, retry_time);
					t->remove_web_seed(m_url, web_seed_entry::http_seed);
					std::string error_msg = to_string(m_parser.status_code()).elems
						+ (" " + m_parser.message());
					if (m_ses.m_alerts.should_post<url_seed_alert>())
					{
						m_ses.m_alerts.post_alert(url_seed_alert(t->get_handle(), url()
							, error_msg));
					}
					m_statistics.received_bytes(0, bytes_transferred);
					disconnect(errors::http_error, 1);
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
					m_statistics.received_bytes(0, bytes_transferred);

					if (location.empty())
					{
						// we should not try this server again.
						t->remove_web_seed(m_url, web_seed_entry::http_seed);
						disconnect(errors::missing_location, 2);
						return;
					}
					
					// add the redirected url and remove the current one
					t->add_web_seed(location, web_seed_entry::http_seed);
					t->remove_web_seed(m_url, web_seed_entry::http_seed);
					disconnect(errors::redirecting, 2);
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
					m_statistics.received_bytes(0, bytes_transferred);
					// we should not try this server again.
					t->remove_web_seed(m_url, web_seed_entry::http_seed);
					disconnect(errors::no_content_length, 2);
					return;
				}
				if (m_response_left != front_request.length)
				{
					m_statistics.received_bytes(0, bytes_transferred);
					// we should not try this server again.
					t->remove_web_seed(m_url, web_seed_entry::http_seed);
					disconnect(errors::invalid_range, 2);
					return;
				}
				if (payload > m_response_left) payload = m_response_left;
				m_body_start = m_parser.body_start();
				m_response_left -= payload;
				m_statistics.received_bytes(payload, 0);
				incoming_piece_fragment(payload);
			}
			else
			{
				payload = bytes_transferred;
				if (payload > m_response_left) payload = m_response_left;
				if (payload > front_request.length) payload = front_request.length;
				m_statistics.received_bytes(payload, 0);
				incoming_piece_fragment(payload);
				m_response_left -= payload;
			}
			recv_buffer.begin += m_body_start;

			if (m_parser.status_code() == 503)
			{
				if (!m_parser.finished()) return;

				int retry_time = atol(std::string(recv_buffer.begin, recv_buffer.end).c_str());
				if (retry_time <= 0) retry_time = 60;
#ifdef TORRENT_VERBOSE_LOGGING
				(*m_logger) << time_now_string() << ": retrying in " << retry_time << " seconds\n";
#endif

				m_statistics.received_bytes(0, bytes_transferred);
				// temporarily unavailable, retry later
				t->retry_web_seed(m_url, web_seed_entry::http_seed, retry_time);
				t->remove_web_seed(m_url, web_seed_entry::http_seed);
				disconnect(errors::http_error, 1);
				return;
			}

			// we only received the header, no data
			if (recv_buffer.left() == 0) break;

			if (recv_buffer.left() < front_request.length) break;

			m_requests.pop_front();
			incoming_piece(front_request, recv_buffer.begin);
			if (associated_torrent().expired()) return;
			cut_receive_buffer(m_body_start + front_request.length, t->block_size() + 1024);
			bytes_transferred -= payload;
			m_body_start = 0;
			if (m_response_left > 0) continue;
			TORRENT_ASSERT(m_response_left == 0);
			m_parser.reset();
		}
	}

	void http_seed_connection::get_specific_peer_info(peer_info& p) const
	{
		if (is_interesting()) p.flags |= peer_info::interesting;
		if (is_choked()) p.flags |= peer_info::choked;
		p.flags |= peer_info::local_connection;
		if (!is_connecting() && m_server_string.empty())
			p.flags |= peer_info::handshake;
		if (is_connecting() && !is_queued()) p.flags |= peer_info::connecting;
		if (is_queued()) p.flags |= peer_info::queued;

		p.client = m_server_string;
		p.connection_type = peer_info::http_seed;
	}

	bool http_seed_connection::in_handshake() const
	{
		return m_server_string.empty();
	}

	void http_seed_connection::on_sent(error_code const& error
		, std::size_t bytes_transferred)
	{
		INVARIANT_CHECK;

		if (error) return;
		m_statistics.sent_bytes(0, bytes_transferred);
	}


#ifdef TORRENT_DEBUG
	void http_seed_connection::check_invariant() const
	{
/*
		TORRENT_ASSERT(m_num_pieces == std::count(
			m_have_piece.begin()
			, m_have_piece.end()
			, true));
*/	}
#endif

}

