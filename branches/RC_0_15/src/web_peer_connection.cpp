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

#include "libtorrent/pch.hpp"

#include <vector>
#include <limits>
#include <boost/bind.hpp>
#include <stdlib.h>

#include "libtorrent/web_peer_connection.hpp"
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
	web_peer_connection::web_peer_connection(
		session_impl& ses
		, boost::weak_ptr<torrent> t
		, boost::shared_ptr<socket_type> s
		, tcp::endpoint const& remote
		, std::string const& url
		, policy::peer* peerinfo)
		: peer_connection(ses, t, s, remote, peerinfo)
		, m_url(url)
		, m_original_url(url)
		, m_first_request(true)
		, m_range_pos(0)
		, m_block_pos(0)
	{
		INVARIANT_CHECK;

		// we want large blocks as well, so
		// we can request more bytes at once
		request_large_blocks(true);

		// we only want left-over bandwidth
		set_priority(1);
		shared_ptr<torrent> tor = t.lock();
		TORRENT_ASSERT(tor);
		int blocks_per_piece = tor->torrent_file().piece_length() / tor->block_size();

		// we always prefer downloading 1 MB chunks
		// from web seeds
		prefer_whole_pieces((1024 * 1024) / tor->torrent_file().piece_length());
		
		// multiply with the blocks per piece since that many requests are
		// merged into one http request
		m_max_out_request_queue = ses.settings().urlseed_pipeline_size
			* blocks_per_piece;

		// since this is a web seed, change the timeout
		// according to the settings.
		set_timeout(ses.settings().urlseed_timeout);
#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << "*** web_peer_connection\n";
#endif

		std::string protocol;
		error_code ec;
		boost::tie(protocol, m_auth, m_host, m_port, m_path)
			= parse_url_components(url, ec);
		TORRENT_ASSERT(!ec);
		
		if (!m_auth.empty())
			m_auth = base64encode(m_auth);

		m_server_string = "URL seed @ ";
		m_server_string += m_host;
	}

	void web_peer_connection::start()
	{
		set_upload_only(true);
		if (is_disconnecting()) return;
		peer_connection::start();
	}

	web_peer_connection::~web_peer_connection()
	{}
	
	boost::optional<piece_block_progress>
	web_peer_connection::downloading_piece_progress() const
	{
		if (m_requests.empty())
			return boost::optional<piece_block_progress>();

		boost::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);

		piece_block_progress ret;

		ret.piece_index = m_requests.front().piece;
		ret.bytes_downloaded = m_block_pos % t->block_size();
		// this is used to make sure that the block_index stays within
		// bounds. If the entire piece is downloaded, the block_index
		// would otherwise point to one past the end
		int correction = m_block_pos ? -1 : 0;
		ret.block_index = (m_requests.front().start + m_block_pos + correction) / t->block_size();

		ret.full_block_bytes = t->block_size();
		const int last_piece = t->torrent_file().num_pieces() - 1;
		if (ret.piece_index == last_piece && ret.block_index
			== t->torrent_file().piece_size(last_piece) / t->block_size())
			ret.full_block_bytes = t->torrent_file().piece_size(last_piece) % t->block_size();
		return ret;
	}

	void web_peer_connection::on_connected()
	{
		boost::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);
	
		// this is always a seed
		incoming_have_all();

		// it is always possible to request pieces
		incoming_unchoke();

		reset_recv_buffer(t->block_size() + 1024);
	}

	void web_peer_connection::write_request(peer_request const& r)
	{
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);

		TORRENT_ASSERT(t->valid_metadata());

		bool single_file_request = t->torrent_file().num_files() == 1;

		if (!single_file_request)
		{
			// handle incorrect .torrent files which are multi-file
			// but have web seeds not ending with a slash
			if (m_path.empty() || m_path[m_path.size() - 1] != '/') m_path += "/";
			if (m_url.empty() || m_url[m_url.size() - 1] != '/') m_url += "/";
		}
		else
		{
			// handle .torrent files that don't include the filename in the url
			if (m_path.empty()) m_path += "/" + t->torrent_file().name();
			else if (m_path[m_path.size() - 1] == '/')
			{
				std::string tmp = t->torrent_file().files().at(0).path.string();
#ifdef TORRENT_WINDOWS
				convert_path_to_posix(tmp);
#endif
				m_path += tmp;
			}
			else if (!m_url.empty() && m_url[m_url.size() - 1] == '/')
			{
				std::string tmp = t->torrent_file().files().at(0).path.string();
#ifdef TORRENT_WINDOWS
				convert_path_to_posix(tmp);
#endif
				m_url += tmp;
			}
		}

		torrent_info const& info = t->torrent_file();
		
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

		if (single_file_request)
		{
			request += "GET ";
			// do not encode single file paths, they are 
			// assumed to be encoded in the torrent file
			request += using_proxy ? m_url : m_path;
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
			request += "\r\nRange: bytes=";
			request += to_string(size_type(r.piece) * info.piece_length() + r.start).elems;
			request += "-";
			request += to_string(size_type(r.piece) * info.piece_length() + r.start + r.length - 1).elems;
			if (m_first_request || using_proxy)
				request += "\r\nConnection: keep-alive";
			request += "\r\n\r\n";
			m_first_request = false;
			m_file_requests.push_back(0);
		}
		else
		{
			std::vector<file_slice> files = info.orig_files().map_block(r.piece, r.start
				, r.length);

			for (std::vector<file_slice>::iterator i = files.begin();
				i != files.end(); ++i)
			{
				file_slice const& f = *i;

				request += "GET ";
				if (using_proxy)
				{
					request += m_url;
					std::string path = info.orig_files().at(f.file_index).path.string();
					request += escape_path(path.c_str(), path.length());
				}
				else
				{
					std::string path = m_path;
					path += info.orig_files().at(f.file_index).path.string();
					request += escape_path(path.c_str(), path.length());
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
				request += "\r\nRange: bytes=";
				request += to_string(f.offset).elems;
				request += "-";
				request += to_string(f.offset + f.size - 1).elems;
				if (m_first_request || using_proxy)
					request += "\r\nConnection: keep-alive";
				request += "\r\n\r\n";
				m_first_request = false;
				TORRENT_ASSERT(f.file_index >= 0);
				m_file_requests.push_back(f.file_index);
			}
		}

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << request << "\n";
#endif

		send_buffer(request.c_str(), request.size(), message_type_request);
	}

	// --------------------------
	// RECEIVE DATA
	// --------------------------

	namespace
	{
		bool range_contains(peer_request const& range, peer_request const& req, int piece_size)
		{
			size_type range_start = size_type(range.piece) * piece_size + range.start;
			size_type req_start = size_type(req.piece) * piece_size + req.start;
			return range_start <= req_start
				&& range_start + range.length >= req_start + req.length;
		}
	}

	void web_peer_connection::on_receive(error_code const& error
		, std::size_t bytes_transferred)
	{
		INVARIANT_CHECK;

		if (error)
		{
			m_statistics.received_bytes(0, bytes_transferred);
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << "*** web_peer_connection error: "
				<< error.message() << "\n";
#endif
			return;
		}

		boost::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);

		for (;;)
		{
			buffer::const_interval recv_buffer = receive_buffer();

			int payload;
			int protocol;
			bool header_finished = m_parser.header_finished();
			if (!header_finished)
			{
				bool error = false;
				boost::tie(payload, protocol) = m_parser.incoming(recv_buffer, error);
				m_statistics.received_bytes(0, protocol);
				bytes_transferred -= protocol;

				if (error)
				{
					m_statistics.received_bytes(0, bytes_transferred);
#ifdef TORRENT_VERBOSE_LOGGING
					(*m_logger) << "*** " << std::string(recv_buffer.begin, recv_buffer.end) << "\n";
#endif
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
				if (!is_ok_status(m_parser.status_code()))
				{
					// temporarily unavailable, retry later
					t->retry_web_seed(m_original_url, web_seed_entry::url_seed);
					t->remove_web_seed(m_original_url, web_seed_entry::url_seed);
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

				m_body_start = m_parser.body_start();
				m_received_body = 0;
			}

			// we just completed reading the header
			if (!header_finished)
			{
#ifdef TORRENT_VERBOSE_LOGGING
				(*m_logger) << "*** STATUS: " << m_parser.status_code()
					<< " " << m_parser.message() << "\n";
				std::map<std::string, std::string> const& headers = m_parser.headers();
				for (std::map<std::string, std::string>::const_iterator i = headers.begin()
					, end(headers.end()); i != end; ++i)
					(*m_logger) << "   " << i->first << ": " << i->second << "\n";
#endif
				if (is_redirect(m_parser.status_code()))
				{
					// this means we got a redirection request
					// look for the location header
					std::string location = m_parser.header("location");
					m_statistics.received_bytes(0, bytes_transferred);

					if (location.empty())
					{
						// we should not try this server again.
						t->remove_web_seed(m_original_url, web_seed_entry::url_seed);
						disconnect(errors::missing_location, 2);
						return;
					}
					
					bool single_file_request = false;
					if (!m_path.empty() && m_path[m_path.size() - 1] != '/')
						single_file_request = true;

					// add the redirected url and remove the current one
					if (!single_file_request)
					{
						m_statistics.received_bytes(0, bytes_transferred);
						TORRENT_ASSERT(!m_file_requests.empty());
						int file_index = m_file_requests.front();

						torrent_info const& info = t->torrent_file();
						std::string path = info.orig_files().at(file_index).path.string();
						path = escape_path(path.c_str(), path.length());
						size_t i = location.rfind(path);
						if (i == std::string::npos)
						{
							t->remove_web_seed(m_original_url, web_seed_entry::url_seed);
							disconnect(errors::invalid_redirection, 2);
							return;
						}
						location.resize(i);
					}
					t->add_web_seed(location, web_seed_entry::url_seed);
					t->remove_web_seed(m_original_url, web_seed_entry::url_seed);
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

				m_body_start = m_parser.body_start();
				m_received_body = 0;
				m_range_pos = 0;
			}

			recv_buffer.begin += m_body_start;

			// we only received the header, no data
			if (recv_buffer.left() == 0) break;

			size_type range_start;
			size_type range_end;
			if (m_parser.status_code() == 206)
			{
				boost::tie(range_start, range_end) = m_parser.content_range();
				if (range_start < 0 || range_end < range_start)
				{
					m_statistics.received_bytes(0, bytes_transferred);
					// we should not try this server again.
					t->remove_web_seed(m_original_url, web_seed_entry::url_seed);
					disconnect(errors::invalid_range);
					return;
				}
				// the http range is inclusive
				range_end++;
			}
			else
			{
				range_start = 0;
				range_end = m_parser.content_length();
				if (range_end == -1)
				{
					m_statistics.received_bytes(0, bytes_transferred);
					// we should not try this server again.
					t->remove_web_seed(m_original_url, web_seed_entry::url_seed);
					disconnect(errors::no_content_length, 2);
					return;
				}
			}

			if (m_requests.empty() || m_file_requests.empty())
			{
				m_statistics.received_bytes(0, bytes_transferred);
				disconnect(errors::http_error, 2);
				return;
			}

			int left_in_response = range_end - range_start - m_range_pos;
			int payload_transferred = (std::min)(left_in_response, int(bytes_transferred));

			torrent_info const& info = t->torrent_file();

			peer_request front_request = m_requests.front();

			TORRENT_ASSERT(m_block_pos >= 0);

#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << "*** payload_transferred: " << payload_transferred
				<< " [" << front_request.piece << ":" << front_request.start
				<< " = " << front_request.length <<  "]\n";
#endif
			m_statistics.received_bytes(payload_transferred, 0);
			bytes_transferred -= payload_transferred;
			m_range_pos += payload_transferred;
			m_block_pos += payload_transferred;
			if (m_range_pos > range_end - range_start) m_range_pos = range_end - range_start;

#if 0
			std::cerr << "REQUESTS: m_requests: " << m_requests.size()
				<< " file_requests: " << m_file_requests.size() << std::endl;
#endif

			int file_index = m_file_requests.front();
			peer_request in_range = info.orig_files().map_file(file_index, range_start
				, int(range_end - range_start));

			size_type rs = size_type(in_range.piece) * info.piece_length() + in_range.start;
			size_type re = rs + in_range.length;
			size_type fs = size_type(front_request.piece) * info.piece_length() + front_request.start;
#if 0
			size_type fe = fs + front_request.length;

			std::cerr << "RANGE: r = (" << rs << ", " << re << " ) "
				"f = (" << fs << ", " << fe << ") "
				"file_index = " << file_index << " received_body = " << m_received_body << std::endl;
#endif

			// the http response body consists of 3 parts
			// 1. the middle of a block or the ending of a block
			// 2. a number of whole blocks
			// 3. the start of a block
			// in that order, these parts are parsed.

			bool range_overlaps_request = re > fs + int(m_piece.size());

			if (!range_overlaps_request)
			{
				incoming_piece_fragment((std::min)(payload_transferred
					, front_request.length - m_block_pos));
				m_statistics.received_bytes(0, bytes_transferred);
				// this means the end of the incoming request ends _before_ the
				// first expected byte (fs + m_piece.size())
				disconnect(errors::invalid_range, 2);
				return;
			}

			// if the request is contained in the range (i.e. the entire request
			// fits in the range) we should not start a partial piece, since we soon
			// will receive enough to call incoming_piece() and pass the read buffer
			// directly (in the next loop below).
			if (range_overlaps_request && !range_contains(in_range, front_request, info.piece_length()))
			{
				// the start of the next block to receive is stored
				// in m_piece. We need to append the rest of that
				// block from the http receive buffer and then
				// (if it completed) call incoming_piece() with
				// m_piece as buffer.
				
				int piece_size = int(m_piece.size());
				int copy_size = (std::min)((std::min)(front_request.length - piece_size
					, recv_buffer.left()), int(range_end - range_start - m_received_body));
				m_piece.resize(piece_size + copy_size);
				TORRENT_ASSERT(copy_size > 0);
				std::memcpy(&m_piece[0] + piece_size, recv_buffer.begin, copy_size);
				TORRENT_ASSERT(int(m_piece.size()) <= front_request.length);
				recv_buffer.begin += copy_size;
				m_received_body += copy_size;
				m_body_start += copy_size;
				TORRENT_ASSERT(m_received_body <= range_end - range_start);
				TORRENT_ASSERT(int(m_piece.size()) <= front_request.length);
				incoming_piece_fragment(copy_size);
				if (int(m_piece.size()) == front_request.length)
				{
					// each call to incoming_piece() may result in us becoming
					// a seed. If we become a seed, all seeds we're connected to
					// will be disconnected, including this web seed. We need to
					// check for the disconnect condition after the call.

					incoming_piece(front_request, &m_piece[0]);
					m_requests.pop_front();
					if (associated_torrent().expired()) return;
					TORRENT_ASSERT(m_block_pos >= front_request.length);
					m_block_pos -= front_request.length;
					cut_receive_buffer(m_body_start, t->block_size() + 1024);
					m_body_start = 0;
					recv_buffer = receive_buffer();
					TORRENT_ASSERT(m_received_body <= range_end - range_start);
					m_piece.clear();
					TORRENT_ASSERT(m_piece.empty());
				}
			}

			// report all received blocks to the bittorrent engine
			while (!m_requests.empty()
				&& range_contains(in_range, m_requests.front(), info.piece_length())
				&& m_block_pos >= m_requests.front().length)
			{
				peer_request r = m_requests.front();
				TORRENT_ASSERT(recv_buffer.left() >= r.length);

				incoming_piece_fragment(r.length);
				incoming_piece(r, recv_buffer.begin);
				m_requests.pop_front();
				if (associated_torrent().expired()) return;
				m_block_pos -= r.length;
				m_received_body += r.length;
				TORRENT_ASSERT(receive_buffer().begin + m_body_start == recv_buffer.begin);
				TORRENT_ASSERT(m_received_body <= range_end - range_start);
				cut_receive_buffer(r.length + m_body_start, t->block_size() + 1024);
				m_body_start = 0;
				recv_buffer = receive_buffer();
			}

			if (!m_requests.empty())
			{
				range_overlaps_request = in_range.start + in_range.length
					> m_requests.front().start + int(m_piece.size());

				if (in_range.start + in_range.length < m_requests.front().start + m_requests.front().length
					&& (m_received_body + recv_buffer.left() >= range_end - range_start))
				{
					int piece_size = int(m_piece.size());
					int copy_size = (std::min)((std::min)(m_requests.front().length - piece_size
						, recv_buffer.left()), int(range_end - range_start - m_received_body));
					TORRENT_ASSERT(copy_size >= 0);
					if (copy_size > 0)
					{
						m_piece.resize(piece_size + copy_size);
						std::memcpy(&m_piece[0] + piece_size, recv_buffer.begin, copy_size);
						recv_buffer.begin += copy_size;
						m_received_body += copy_size;
						m_body_start += copy_size;
					}
					TORRENT_ASSERT(m_received_body == range_end - range_start);
				}
			}

			TORRENT_ASSERT(m_received_body <= range_end - range_start);
			if (m_received_body == range_end - range_start)
			{
				cut_receive_buffer(recv_buffer.begin - receive_buffer().begin
					, t->block_size() + 1024);
				recv_buffer = receive_buffer();
				m_file_requests.pop_front();
				m_parser.reset();
				m_body_start = 0;
				m_received_body = 0;
				continue;
			}
			if (bytes_transferred == 0) break;
			TORRENT_ASSERT(payload_transferred > 0);
		}
		TORRENT_ASSERT(bytes_transferred == 0);
	}

	void web_peer_connection::get_specific_peer_info(peer_info& p) const
	{
		if (is_interesting()) p.flags |= peer_info::interesting;
		if (is_choked()) p.flags |= peer_info::choked;
		if (is_peer_interested()) p.flags |= peer_info::remote_interested;
		if (has_peer_choked()) p.flags |= peer_info::remote_choked;
		if (is_local()) p.flags |= peer_info::local_connection;
		if (!is_connecting() && m_server_string.empty())
			p.flags |= peer_info::handshake;
		if (is_connecting() && !is_queued()) p.flags |= peer_info::connecting;
		if (is_queued()) p.flags |= peer_info::queued;

		p.client = m_server_string;
		p.connection_type = peer_info::web_seed;
	}

	bool web_peer_connection::in_handshake() const
	{
		return m_server_string.empty();
	}

	void web_peer_connection::on_sent(error_code const& error
		, std::size_t bytes_transferred)
	{
		INVARIANT_CHECK;

		if (error) return;
		m_statistics.sent_bytes(0, bytes_transferred);
	}


#ifdef TORRENT_DEBUG
	void web_peer_connection::check_invariant() const
	{
/*
		TORRENT_ASSERT(m_num_pieces == std::count(
			m_have_piece.begin()
			, m_have_piece.end()
			, true));
*/	}
#endif

}

