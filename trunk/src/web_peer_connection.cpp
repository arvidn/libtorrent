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
		, boost::weak_ptr<torrent> t
		, boost::shared_ptr<stream_socket> s
		, tcp::endpoint const& remote
		, std::string const& url)
		: peer_connection(ses, t, s, remote)
		, m_url(url)
		, m_first_request(true)
	{
		INVARIANT_CHECK;

		// since this is a web seed, change the timeout
		// according to the settings.
		set_timeout(ses.m_settings.urlseed_timeout);
#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << "*** web_peer_connection\n";
#endif

		std::string protocol;
		boost::tie(protocol, m_host, m_port, m_path)
			= parse_url_components(url);
			
		m_server_string = "URL seed @ ";
		m_server_string += m_host;
	}

	web_peer_connection::~web_peer_connection()
	{}
	
	boost::optional<piece_block_progress>
	web_peer_connection::downloading_piece_progress() const
	{
		if (!m_parser.header_finished() || m_requests.empty())
			return boost::optional<piece_block_progress>();

		boost::shared_ptr<torrent> t = associated_torrent().lock();
		assert(t);

		int body_start = m_parser.body_start();
		buffer::const_interval recv_buffer = receive_buffer();
		assert(body_start <= recv_buffer.left());
		piece_block_progress ret;

		ret.piece_index = m_requests.front().piece;
		ret.block_index = m_requests.front().start / t->block_size();
		ret.bytes_downloaded = recv_buffer.left() - body_start;
		ret.full_block_bytes = m_requests.front().length;
		return ret;
	}

	void web_peer_connection::on_connected()
	{
		boost::shared_ptr<torrent> t = associated_torrent().lock();
		assert(t);
	
		// this is always a seed
		incoming_bitfield(std::vector<bool>(
			t->torrent_file().num_pieces(), true));
		// it is always possible to request pieces
		incoming_unchoke();
		
		reset_recv_buffer(512*1024+1024);
	}

	void web_peer_connection::write_request(peer_request const& r)
	{
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = associated_torrent().lock();
		assert(t);

		assert(t->valid_metadata());

		bool single_file_request = false;
		if (!m_path.empty() && m_path[m_path.size() - 1] != '/')
			single_file_request = true;

		torrent_info const& info = t->torrent_file();
		
		std::string request;

		m_requests.push_back(r);

		bool using_proxy = false;
		if (!m_ses.m_settings.proxy_ip.empty())
			using_proxy = true;

		if (single_file_request)
		{
			request += "GET ";
			if (using_proxy) request += m_url;
			else request += escape_path(m_path.c_str(), m_path.length());
			request += " HTTP/1.1\r\n";
			request += "Host: ";
			request += m_host;
			if (m_first_request)
			{
				request += "\r\nUser-Agent: ";
				request += escape_string(m_ses.m_settings.user_agent.c_str()
					, m_ses.m_settings.user_agent.size());
			}
			if (using_proxy && !m_ses.m_settings.proxy_login.empty())
			{
				request += "\r\nProxy-Authorization: Basic ";
				request += base64encode(m_ses.m_settings.proxy_login + ":"
					+ m_ses.m_settings.proxy_password);
			}
			if (using_proxy)
			{
				request += "\r\nProxy-Connection: keep-alive";
			}
			request += "\r\nRange: bytes=";
			request += boost::lexical_cast<std::string>(r.piece
				* info.piece_length() + r.start);
			request += "-";
			request += boost::lexical_cast<std::string>(r.piece
				* info.piece_length() + r.start + r.length - 1);
			if (m_first_request || using_proxy)
				request += "\r\nConnection: keep-alive";
			request += "\r\n\r\n";
			m_first_request = false;
			m_file_requests.push_back(0);
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
				if (using_proxy)
				{
					request += m_url;
					std::string path = info.file_at(f.file_index).path.string();
					request += escape_path(path.c_str(), path.length());
				}
				else
				{
					std::string path = m_path;
					path += info.file_at(f.file_index).path.string();
					request += escape_path(path.c_str(), path.length());
				}
				request += " HTTP/1.1\r\n";
				request += "Host: ";
				request += m_host;
				if (m_first_request)
				{
					request += "\r\nUser-Agent: ";
					request += escape_string(m_ses.m_settings.user_agent.c_str()
						, m_ses.m_settings.user_agent.size());
				}
				if (using_proxy && !m_ses.m_settings.proxy_login.empty())
				{
					request += "\r\nProxy-Authorization: Basic ";
					request += base64encode(m_ses.m_settings.proxy_login + ":"
						+ m_ses.m_settings.proxy_password);
				}
				if (using_proxy)
				{
					request += "\r\nProxy-Connection: keep-alive";
				}
				request += "\r\nRange: bytes=";
				request += boost::lexical_cast<std::string>(f.offset);
				request += "-";
				request += boost::lexical_cast<std::string>(f.offset + f.size - 1);
				if (m_first_request || using_proxy)
					request += "\r\nConnection: keep-alive";
				request += "\r\n\r\n";
				m_first_request = false;
				m_file_requests.push_back(f.file_index);
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

		boost::shared_ptr<torrent> t = associated_torrent().lock();
		assert(t);

		incoming_piece_fragment();

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
				t->remove_url_seed(m_url);
				if (m_parser.status_code() == 404)
					throw std::runtime_error("File not found on server");
				throw std::runtime_error("HTTP server does not support byte range requests");
			}

			if (!m_parser.finished()) break;

			std::string server_version = m_parser.header<std::string>("Server");
			if (!server_version.empty())
			{
				m_server_string = "URL seed @ ";
				m_server_string += m_host;
				m_server_string += " (";
				m_server_string += server_version;
				m_server_string += ")";
			}

			std::stringstream range_str(m_parser.header<std::string>("Content-Range"));
			size_type range_start;
			size_type range_end;
			char dummy;
			std::string bytes;
			range_str >> bytes >> range_start >> dummy >> range_end;
			if (!range_str)
			{
				// we should not try this server again.
				t->remove_url_seed(m_url);
				throw std::runtime_error("invalid range in HTTP response: " + range_str.str());
			}
			// the http range is inclusive
			range_end++;

			torrent_info const& info = t->torrent_file();

			if (m_requests.empty() || m_file_requests.empty())
				throw std::runtime_error("unexpected HTTP response");

			int file_index = m_file_requests.front();
			m_file_requests.pop_front();

			peer_request r = info.map_file(file_index, range_start
				, range_end - range_start);

			buffer::const_interval http_body = m_parser.get_body();
						
			if (r == m_requests.front())
			{
				m_requests.pop_front();
				incoming_piece(r, http_body.begin);
				cut_receive_buffer(http_body.end - recv_buffer.begin, 512*1024+1024);
				return;
			}

			if (!m_piece.empty())
			{
				// this is not the first partial request we get
				if (m_intermediate_piece.start + m_intermediate_piece.length != r.start
					|| m_intermediate_piece.piece != r.piece)
				{
					throw std::runtime_error("invalid range in HTTP response");
				}
			}
			else
			{
				// this is the first part of a partial request
				if (r.start != m_requests.front().start
					|| r.piece != m_requests.front().piece)
				{
					throw std::runtime_error("invalid range in HTTP response");
				}
				m_intermediate_piece.piece = r.piece;
				m_intermediate_piece.start = r.start;
				m_intermediate_piece.length = 0;
			}

			m_piece.reserve(info.piece_length());
			std::copy(http_body.begin, http_body.end, back_inserter(m_piece));
			m_intermediate_piece.length += r.length;
			if (m_intermediate_piece.length == m_requests.front().length)
			{
				assert(m_requests.front() == m_intermediate_piece);
				assert(int(m_piece.size()) == m_intermediate_piece.length);
				m_requests.pop_front();
				incoming_piece(m_intermediate_piece, &m_piece[0]);
				m_piece.clear();
			}
			else if (m_intermediate_piece.length > m_requests.front().length)
			{
				throw std::runtime_error("too large HTTP response body");
			}

			cut_receive_buffer(http_body.end - recv_buffer.begin, 512*1024+1024);
		}
	}

	// --------------------------
	// SEND DATA
	// --------------------------

	void web_peer_connection::get_peer_info(peer_info& p) const
	{
		assert(!associated_torrent().expired());

		p.down_speed = statistics().download_rate();
		p.up_speed = statistics().upload_rate();
		p.payload_down_speed = statistics().download_payload_rate();
		p.payload_up_speed = statistics().upload_payload_rate();
		p.pid = pid();
		p.ip = remote();

		p.total_download = statistics().total_payload_download();
		p.total_upload = statistics().total_payload_upload();

		if (m_ul_bandwidth_quota.given == std::numeric_limits<int>::max())
			p.upload_limit = -1;
		else
			p.upload_limit = m_ul_bandwidth_quota.given;

		if (m_dl_bandwidth_quota.given == std::numeric_limits<int>::max())
			p.download_limit = -1;
		else
			p.download_limit = m_dl_bandwidth_quota.given;

		p.load_balancing = total_free_upload();

		p.download_queue_length = (int)download_queue().size();
		p.upload_queue_length = (int)upload_queue().size();

		if (boost::optional<piece_block_progress> ret = downloading_piece_progress())
		{
			p.downloading_piece_index = ret->piece_index;
			p.downloading_block_index = ret->block_index;
			p.downloading_progress = ret->bytes_downloaded;
			p.downloading_total = ret->full_block_bytes;
		}
		else
		{
			p.downloading_piece_index = -1;
			p.downloading_block_index = -1;
			p.downloading_progress = 0;
			p.downloading_total = 0;
		}

		p.flags = 0;
		if (is_interesting()) p.flags |= peer_info::interesting;
		if (is_choked()) p.flags |= peer_info::choked;
		if (is_peer_interested()) p.flags |= peer_info::remote_interested;
		if (has_peer_choked()) p.flags |= peer_info::remote_choked;
		if (is_local()) p.flags |= peer_info::local_connection;
		if (!is_connecting() && m_server_string.empty())
			p.flags |= peer_info::handshake;
		if (is_connecting() && !is_queued()) p.flags |= peer_info::connecting;
		if (is_queued()) p.flags |= peer_info::queued;
		
		p.pieces = get_bitfield();
		p.seed = is_seed();

		p.client = m_server_string;
		p.connection_type = peer_info::web_seed;
	}

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

