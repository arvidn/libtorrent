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

#include <vector>
#include <boost/limits.hpp>
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
#include "libtorrent/parse_url.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/aux_/session_interface.hpp"
#include "libtorrent/alert_manager.hpp" // for alert_manageralert_manager
#include "libtorrent/aux_/escape_string.hpp" // for is_hex

using boost::shared_ptr;

namespace libtorrent
{
enum
{
	request_size_overhead = 5000
};

struct disk_interface;

web_peer_connection::web_peer_connection(peer_connection_args const& pack
	, web_seed_t& web)
	: web_connection_base(pack, web)
	, m_url(web.url)
	, m_web(&web)
	, m_received_body(0)
	, m_range_pos(0)
	, m_chunk_pos(0)
	, m_block_pos(0)
	, m_partial_chunk_header(0)
	, m_num_responses(0)
{
	INVARIANT_CHECK;

	if (!m_settings.get_bool(settings_pack::report_web_seed_downloads))
		ignore_stats(true);

	shared_ptr<torrent> tor = pack.tor.lock();
	TORRENT_ASSERT(tor);

	// we always prefer downloading 1 MiB chunks
	// from web seeds, or whole pieces if pieces
	// are larger than a MiB
	int preferred_size = 1024 * 1024;

	// if the web server is known not to support keep-alive.
	// request even larger blocks at a time
	if (!web.supports_keepalive) preferred_size *= 4;

	prefer_contiguous_blocks((std::max)(preferred_size / tor->block_size(), 1));
	
	// we want large blocks as well, so
	// we can request more bytes at once
	// this setting will merge adjacent requests
	// into single larger ones
	request_large_blocks(true);

#ifdef TORRENT_LOGGING
	peer_log("*** web_peer_connection %s", m_url.c_str());
#endif
}

void web_peer_connection::on_connected()
{
	incoming_have_all();
	if (m_web->restart_request.piece != -1)
	{
		// increase the chances of requesting the block
		// we have partial data for already, to finish it
		incoming_suggest(m_web->restart_request.piece);
	}
	web_connection_base::on_connected();
}

void web_peer_connection::disconnect(error_code const& ec
	, operation_t op, int error)
{
	if (is_disconnecting()) return;

	if (op == op_sock_write && ec == boost::system::errc::broken_pipe)
	{
#ifdef TORRENT_LOGGING
		// a write operation failed with broken-pipe. This typically happens
		// with HTTP 1.0 servers that close their incoming channel of the TCP
		// stream whenever they're done reading one full request. Instead of
		// us bailing out and failing the entire request just because our
		// write-end was closed, ignore it and keep reading until the read-end
		// also is closed.
		peer_log("*** WRITE-DIRECTION CLOSED");
#endif

		// prevent the peer from trying to send anything more
		m_send_buffer.clear();
		m_recv_buffer.free_disk_buffer();

		// when the web server closed our write-end of the socket (i.e. its
		// read-end), if it's an HTTP 1.0 server. we will stop sending more
		// requests. We'll close the connection once we receive the last bytes,
		// and our read end is closed as well.
		incoming_choke();
		return;
	}

	if (op == op_connect && m_web && !m_web->endpoints.empty())
	{
		// we failed to connect to this IP. remove it so that the next attempt
		// uses the next IP in the list.
		m_web->endpoints.erase(m_web->endpoints.begin());
	}

	boost::shared_ptr<torrent> t = associated_torrent().lock();

	if (!m_requests.empty() && !m_file_requests.empty()
		&& !m_piece.empty() && m_web)
	{
#ifdef TORRENT_LOGGING
		peer_log("*** SAVE-RESTART-DATA: [ data: %d req: %d off: %d ]"
			, int(m_piece.size()), int(m_requests.front().piece)
			, int(m_requests.front().start));
#endif
		m_web->restart_request = m_requests.front();
		if (!m_web->restart_piece.empty())
		{
			// we're about to replace a different restart piece
			// buffer. So it was wasted download
			if (t) t->add_redundant_bytes(m_web->restart_piece.size()
				, torrent::piece_closing);
		}
		m_web->restart_piece.swap(m_piece);

		// we have to do this to not count this data as redundant. The
		// upper layer will call downloading_piece_progress and assume
		// it's all wasted download. Since we're saving it here, it isn't.
		m_requests.clear();
		m_block_pos = 0;
	}

	if (m_web && !m_web->supports_keepalive && error == 0)
	{
		// if the web server doesn't support keepalive and we were
		// disconnected as a graceful EOF, reconnect right away
		if (t) t->session().get_io_service().post(
			boost::bind(&torrent::maybe_connect_web_seeds, t));
	}
	peer_connection::disconnect(ec, op, error);
	if (t) t->disconnect_web_seed(this);
}
	
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
	TORRENT_ASSERT(ret.block_index < int(piece_block::invalid.block_index));
	TORRENT_ASSERT(ret.piece_index < int(piece_block::invalid.piece_index));

	ret.full_block_bytes = t->block_size();
	const int last_piece = t->torrent_file().num_pieces() - 1;
	if (ret.piece_index == last_piece && ret.block_index
		== t->torrent_file().piece_size(last_piece) / t->block_size())
		ret.full_block_bytes = t->torrent_file().piece_size(last_piece) % t->block_size();
	return ret;
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
			std::string tmp = t->torrent_file().files().file_path(0);
#ifdef TORRENT_WINDOWS
			convert_path_to_posix(tmp);
#endif
			m_path += tmp;
		}
		else if (!m_url.empty() && m_url[m_url.size() - 1] == '/')
		{
			std::string tmp = t->torrent_file().files().file_path(0);
#ifdef TORRENT_WINDOWS
			convert_path_to_posix(tmp);
#endif
			m_url += tmp;
		}
	}

	torrent_info const& info = t->torrent_file();
	peer_request req = r;
	
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

#ifdef TORRENT_LOGGING
		peer_log("==> REQUESTING [ piece: %d start: %d len: %d ]"
			, pr.piece, pr.start, pr.length);
#endif

		if (m_web->restart_request == m_requests.front())
		{
			m_piece.swap(m_web->restart_piece);
			m_block_pos += m_piece.size();
			peer_request& front = m_requests.front();
			TORRENT_ASSERT(front.length > int(m_piece.size()));

#ifdef TORRENT_LOGGING
			peer_log("*** RESTART-DATA: [ data: %d req: (%d, %d) ]"
				, int(m_piece.size()), int(front.piece), int(front.start)
				, int (front.start + front.length - 1));
#endif

			req.start += m_piece.size();
			req.length -= m_piece.size();

			// just to keep the accounting straight for the upper layer.
			// it doesn't know we just re-wrote the request
			incoming_piece_fragment(m_piece.size());
			m_web->restart_request.piece = -1;
		}

#if 0
			std::cerr << this << " REQ: p: " << pr.piece << " " << pr.start << std::endl;
#endif
		size -= pr.length;
	}

	int proxy_type = m_settings.get_int(settings_pack::proxy_type);
	bool using_proxy = (proxy_type == settings_pack::http
		|| proxy_type == settings_pack::http_pw) && !m_ssl;

	if (single_file_request)
	{
		request += "GET ";
		// do not encode single file paths, they are 
		// assumed to be encoded in the torrent file
		request += using_proxy ? m_url : m_path;
		request += " HTTP/1.1\r\n";
		add_headers(request, m_settings, using_proxy);
		request += "\r\nRange: bytes=";
		request += to_string(boost::int64_t(req.piece) * info.piece_length()
			+ req.start).elems;
		request += "-";
		request += to_string(boost::int64_t(req.piece) * info.piece_length()
			+ req.start + req.length - 1).elems;
		request += "\r\n\r\n";
		m_first_request = false;
		m_file_requests.push_back(0);
	}
	else
	{
		if (!t->need_loaded())
		{
			disconnect(errors::torrent_aborted, op_bittorrent);
			return;
		}

		std::vector<file_slice> files = info.orig_files().map_block(req.piece, req.start
			, req.length);

		for (std::vector<file_slice>::iterator i = files.begin();
			i != files.end(); ++i)
		{
			file_slice const& f = *i;
			if (info.orig_files().pad_file_at(f.file_index))
			{
				m_file_requests.push_back(f.file_index);
				continue;
			}

			request += "GET ";
			if (using_proxy)
			{
				// m_url is already a properly escaped URL
				// with the correct slashes. Don't encode it again
				request += m_url;
				std::string path = info.orig_files().file_path(f.file_index);
#ifdef TORRENT_WINDOWS
				convert_path_to_posix(path);
#endif
				request += escape_path(path.c_str(), path.length());
			}
			else
			{
				// m_path is already a properly escaped URL
				// with the correct slashes. Don't encode it again
				request += m_path;

				std::string path = info.orig_files().file_path(f.file_index);
#ifdef TORRENT_WINDOWS
				convert_path_to_posix(path);
#endif
				request += escape_path(path.c_str(), path.length());
			}
			request += " HTTP/1.1\r\n";
			add_headers(request, m_settings, using_proxy);
			request += "\r\nRange: bytes=";
			request += to_string(f.offset).elems;
			request += "-";
			request += to_string(f.offset + f.size - 1).elems;
			request += "\r\n\r\n";
			m_first_request = false;

#if 0
			std::cerr << this << " SEND-REQUEST: f: " << f.file_index
				<< " s: " << f.offset
				<< " e: " << (f.offset + f.size - 1) << std::endl;
#endif
			TORRENT_ASSERT(f.file_index >= 0);
			m_file_requests.push_back(f.file_index);
		}
	}

#ifdef TORRENT_LOGGING
	peer_log("==> %s", request.c_str());
#endif

	// in case the first file on this series of requests is a padfile
	// we need to handle it right now, and pretend that we got a response
	// with zeros.
	buffer::const_interval recv_buffer = m_recv_buffer.get();
	handle_padfile(recv_buffer);
	if (associated_torrent().expired()) return;

	send_buffer(request.c_str(), request.size(), message_type_request);
}

// --------------------------
// RECEIVE DATA
// --------------------------

namespace
{
	bool range_contains(peer_request const& range, peer_request const& req, int piece_size)
	{
		boost::int64_t range_start = boost::int64_t(range.piece) * piece_size + range.start;
		boost::int64_t req_start = boost::int64_t(req.piece) * piece_size + req.start;
		return range_start <= req_start
			&& range_start + range.length >= req_start + req.length;
	}
}

bool web_peer_connection::maybe_harvest_block()
{
	peer_request const& front_request = m_requests.front();

	if (int(m_piece.size()) < front_request.length) return false;
	TORRENT_ASSERT(int(m_piece.size() == front_request.length));

	// each call to incoming_piece() may result in us becoming
	// a seed. If we become a seed, all seeds we're connected to
	// will be disconnected, including this web seed. We need to
	// check for the disconnect condition after the call.

	boost::shared_ptr<torrent> t = associated_torrent().lock();
	TORRENT_ASSERT(t);
	buffer::const_interval recv_buffer = m_recv_buffer.get();

	incoming_piece(front_request, &m_piece[0]);
#ifdef TORRENT_LOGGING
	peer_log("<== POP REQUEST [ piece: %d start: %d len: %d ]"
		, front_request.piece, front_request.start, front_request.length);
#endif
	m_requests.pop_front();
	if (associated_torrent().expired()) return false;
	TORRENT_ASSERT(m_block_pos >= front_request.length);
	m_block_pos -= front_request.length;
	m_recv_buffer.cut(m_body_start, t->block_size() + request_size_overhead);
	m_body_start = 0;
	recv_buffer = m_recv_buffer.get();
//		TORRENT_ASSERT(m_received_body <= range_end - range_start);
	m_piece.clear();
	TORRENT_ASSERT(m_piece.empty());
	return true;
}

bool web_peer_connection::received_invalid_data(int index, bool single_peer)
{
	if (!single_peer) return peer_connection::received_invalid_data(index, single_peer);

	// when a web seed fails a hash check, do the following:
	// 1. if the whole piece only overlaps a single file, mark that file as not
	//    have for this peer
	// 2. if the piece overlaps more than one file, mark the piece as not have
	//    for this peer
	// 3. if it's a single file torrent, just ban it right away
	// this handles the case where web seeds may have some files updated but not other

	boost::shared_ptr<torrent> t = associated_torrent().lock();
	file_storage const& fs = t->torrent_file().files();

	// single file torrent
	if (fs.num_files() == 1) return peer_connection::received_invalid_data(index, single_peer);

	std::vector<file_slice> files = fs.map_block(index, 0, fs.piece_size(index));

	if (files.size() == 1)
	{
		// assume the web seed has a different copy of this specific file
		// than what we expect, and pretend not to have it.
		int fi = files[0].file_index;
		int first_piece = fs.file_offset(fi) / fs.piece_length();
		// one past last piece
		int end_piece = int((fs.file_offset(fi) + fs.file_size(fi) + 1) / fs.piece_length());
		for (int i = first_piece; i < end_piece; ++i)
			incoming_dont_have(i);
	}
	else
	{
		incoming_dont_have(index);
	}

	peer_connection::received_invalid_data(index, single_peer);

	// if we don't think we have any of the files, allow banning the web seed
	if (num_have_pieces() == 0) return true;

	// don't disconnect, we won't request anything from this file again
	return false;
}

void web_peer_connection::on_receive(error_code const& error
	, std::size_t bytes_transferred)
{
	INVARIANT_CHECK;

#ifdef TORRENT_DEBUG
	TORRENT_ASSERT(statistics().last_payload_downloaded()
		+ statistics().last_protocol_downloaded() + bytes_transferred < size_t(INT_MAX));
	int dl_target = statistics().last_payload_downloaded()
		+ statistics().last_protocol_downloaded() + bytes_transferred;
#endif

	if (error)
	{
		received_bytes(0, bytes_transferred);
#ifdef TORRENT_LOGGING
		peer_log("*** web_peer_connection error: %s", error.message().c_str());
#endif
#ifdef TORRENT_DEBUG
		TORRENT_ASSERT(statistics().last_payload_downloaded()
			+ statistics().last_protocol_downloaded()
			== dl_target);
#endif
		return;
	}

	boost::shared_ptr<torrent> t = associated_torrent().lock();
	TORRENT_ASSERT(t);

	for (;;)
	{
#ifdef TORRENT_DEBUG
		TORRENT_ASSERT(statistics().last_payload_downloaded()
			+ statistics().last_protocol_downloaded() + int(bytes_transferred)
			== dl_target);
#endif

		buffer::const_interval recv_buffer = m_recv_buffer.get();

		int payload;
		int protocol;
		bool header_finished = m_parser.header_finished();
		if (!header_finished)
		{
			bool failed = false;
			boost::tie(payload, protocol) = m_parser.incoming(recv_buffer, failed);
			received_bytes(0, protocol);
			TORRENT_ASSERT(int(bytes_transferred) >= protocol);
			bytes_transferred -= protocol;

			if (failed)
			{
				received_bytes(0, bytes_transferred);
#ifdef TORRENT_LOGGING
				peer_log("*** %s", std::string(recv_buffer.begin, recv_buffer.end).c_str());
#endif
				disconnect(errors::http_parse_error, op_bittorrent, 2);
#ifdef TORRENT_DEBUG
				TORRENT_ASSERT(statistics().last_payload_downloaded()
					+ statistics().last_protocol_downloaded()
					== dl_target);
#endif
				return;
			}

			TORRENT_ASSERT(recv_buffer.left() == 0 || *recv_buffer.begin == 'H');
		
			TORRENT_ASSERT(recv_buffer.left() <= m_recv_buffer.packet_size());
			
			// this means the entire status line hasn't been received yet
			if (m_parser.status_code() == -1)
			{
				TORRENT_ASSERT(payload == 0);
				TORRENT_ASSERT(bytes_transferred == 0);
#ifdef TORRENT_DEBUG
				TORRENT_ASSERT(statistics().last_payload_downloaded()
					+ statistics().last_protocol_downloaded() + int(bytes_transferred)
					== dl_target);
#endif
				break;
			}

			if (!m_parser.header_finished())
			{
				TORRENT_ASSERT(payload == 0);
				TORRENT_ASSERT(bytes_transferred == 0);
#ifdef TORRENT_DEBUG
				TORRENT_ASSERT(statistics().last_payload_downloaded()
					+ statistics().last_protocol_downloaded() + int(bytes_transferred)
					== dl_target);
#endif
				break;
			}

			m_body_start = m_parser.body_start();
			m_received_body = 0;
		}

		// we just completed reading the header
		if (!header_finished)
		{
			++m_num_responses;

			if (m_parser.connection_close())
			{
				incoming_choke();
				if (m_num_responses == 1)
					m_web->supports_keepalive = false;
			}

#ifdef TORRENT_LOGGING
			peer_log("*** STATUS: %d %s", m_parser.status_code(), m_parser.message().c_str());
			std::multimap<std::string, std::string> const& headers = m_parser.headers();
			for (std::multimap<std::string, std::string>::const_iterator i = headers.begin()
				, end(headers.end()); i != end; ++i)
				peer_log("   %s: %s", i->first.c_str(), i->second.c_str());
#endif
			// if the status code is not one of the accepted ones, abort
			if (!is_ok_status(m_parser.status_code()))
			{
				// TODO: 2 just make this peer not have the pieces
				// associated with the file we just requested. Only
				// when it doesn't have any of the file do the following
				int retry_time = atoi(m_parser.header("retry-after").c_str());
				if (retry_time <= 0) retry_time = m_settings.get_int(settings_pack::urlseed_wait_retry);
				// temporarily unavailable, retry later
				t->retry_web_seed(this, retry_time);
				std::string error_msg = to_string(m_parser.status_code()).elems
					+ (" " + m_parser.message());
				if (t->alerts().should_post<url_seed_alert>())
				{
					t->alerts().post_alert(url_seed_alert(t->get_handle(), m_url
						, error_msg));
				}
				received_bytes(0, bytes_transferred);
				disconnect(error_code(m_parser.status_code(), get_http_category()), op_bittorrent, 1);
#ifdef TORRENT_DEBUG
				TORRENT_ASSERT(statistics().last_payload_downloaded()
					+ statistics().last_protocol_downloaded()
					== dl_target);
#endif
				return;
			}
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
					m_web = NULL;
					TORRENT_ASSERT(is_disconnecting());
#ifdef TORRENT_DEBUG
					TORRENT_ASSERT(statistics().last_payload_downloaded()
						+ statistics().last_protocol_downloaded()
						== dl_target);
#endif
					return;
				}
				
				bool single_file_request = false;
				if (!m_path.empty() && m_path[m_path.size() - 1] != '/')
					single_file_request = true;

				// add the redirected url and remove the current one
				if (!single_file_request)
				{
					TORRENT_ASSERT(!m_file_requests.empty());
					int file_index = m_file_requests.front();

						if (!t->need_loaded())
						{
							disconnect(errors::torrent_aborted, op_bittorrent);
							return;
						}
// TODO: 2 create a mapping of file-index to redirection URLs. Use that to form
// URLs instead. Support to reconnect to a new server without destructing this
// peer_connection
						torrent_info const& info = t->torrent_file();
						std::string path = info.orig_files().file_path(file_index);
#ifdef TORRENT_WINDOWS
						convert_path_to_posix(path);
#endif
						path = escape_path(path.c_str(), path.length());
						size_t i = location.rfind(path);
						if (i == std::string::npos)
						{
							t->remove_web_seed(this, errors::invalid_redirection, op_bittorrent, 2);
							m_web = NULL;
							TORRENT_ASSERT(is_disconnecting());
#ifdef TORRENT_DEBUG
							TORRENT_ASSERT(statistics().last_payload_downloaded()
								+ statistics().last_protocol_downloaded()
								== dl_target);
#endif
							return;
						}
						location.resize(i);
					}
					else
					{
						location = resolve_redirect_location(m_url, location);
					}

#ifdef TORRENT_LOGGING
					peer_log("*** LOCATION: %s", location.c_str());
#endif
					t->add_web_seed(location, web_seed_entry::url_seed, m_external_auth, m_extra_headers);
					t->remove_web_seed(this, errors::redirecting, op_bittorrent, 2);
					m_web = NULL;
					TORRENT_ASSERT(is_disconnecting());
#ifdef TORRENT_DEBUG
					TORRENT_ASSERT(statistics().last_payload_downloaded()
						+ statistics().last_protocol_downloaded()
						== dl_target);
#endif
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
			if (recv_buffer.left() == 0)
			{
#ifdef TORRENT_DEBUG
				TORRENT_ASSERT(statistics().last_payload_downloaded()
					+ statistics().last_protocol_downloaded()
					== dl_target);
#endif
				break;
			}

			boost::int64_t range_start;
			boost::int64_t range_end;
			if (m_parser.status_code() == 206)
			{
				boost::tie(range_start, range_end) = m_parser.content_range();
				if (range_start < 0 || range_end < range_start)
				{
					received_bytes(0, bytes_transferred);
					// we should not try this server again.
					t->remove_web_seed(this, errors::invalid_range, op_bittorrent);
					m_web = NULL;
					TORRENT_ASSERT(is_disconnecting());
#ifdef TORRENT_DEBUG
					TORRENT_ASSERT(statistics().last_payload_downloaded()
						+ statistics().last_protocol_downloaded()
						== dl_target);
#endif
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
					received_bytes(0, bytes_transferred);
					// we should not try this server again.
					t->remove_web_seed(this, errors::no_content_length, op_bittorrent, 2);
					m_web = NULL;
					TORRENT_ASSERT(is_disconnecting());
#ifdef TORRENT_DEBUG
					TORRENT_ASSERT(statistics().last_payload_downloaded()
						+ statistics().last_protocol_downloaded()
						== dl_target);
#endif
					return;
				}
			}

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
					TORRENT_ASSERT(int(bytes_transferred) >= chunk_start.left() - m_partial_chunk_header);
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
					TORRENT_ASSERT(int(bytes_transferred) >= header_size - m_partial_chunk_header);
					bytes_transferred -= header_size - m_partial_chunk_header;
					received_bytes(0, header_size - m_partial_chunk_header);
					m_partial_chunk_header = 0;
					TORRENT_ASSERT(chunk_size != 0 || chunk_start.left() <= header_size || chunk_start.begin[header_size] == 'H');
					// cut out the chunk header from the receive buffer
					TORRENT_ASSERT(m_body_start + m_chunk_pos < INT_MAX);
					m_recv_buffer.cut(header_size, t->block_size() + request_size_overhead, int(m_body_start + m_chunk_pos));
					recv_buffer = m_recv_buffer.get();
					recv_buffer.begin += m_body_start;
					m_chunk_pos += chunk_size;
					if (chunk_size == 0)
					{
#ifdef TORRENT_DEBUG
						chunk_start = recv_buffer;
						chunk_start.begin += m_chunk_pos;
						TORRENT_ASSERT(chunk_start.left() == 0 || chunk_start.begin[0] == 'H');
#endif
						m_chunk_pos = -1;
					}
					// if all of hte receive buffer was just consumed as chunk
					// header, we're done
					if (bytes_transferred == 0) return;
				}
			}

			if (m_requests.empty() || m_file_requests.empty())
			{
				received_bytes(0, bytes_transferred);
				disconnect(errors::http_error, op_bittorrent, 2);
#ifdef TORRENT_DEBUG
				TORRENT_ASSERT(statistics().last_payload_downloaded()
					+ statistics().last_protocol_downloaded()
					== dl_target);
#endif
				return;
			}

			boost::int64_t left_in_response = range_end - range_start - m_range_pos;
			int payload_transferred = int((std::min)(left_in_response, boost::int64_t(bytes_transferred)));

			torrent_info const& info = t->torrent_file();

			peer_request front_request = m_requests.front();

			TORRENT_ASSERT(m_block_pos >= 0);

#ifdef TORRENT_LOGGING
			peer_log("*** payload_transferred: %d [ %d:%d = %d ]"
				, payload_transferred, front_request.piece
				, front_request.start, front_request.length);
#endif
			received_bytes(payload_transferred, 0);
			TORRENT_ASSERT(int(bytes_transferred) >= payload_transferred);
			bytes_transferred -= payload_transferred;
			m_range_pos += payload_transferred;
			m_block_pos += payload_transferred;
			if (m_range_pos > range_end - range_start) m_range_pos = range_end - range_start;

			if (!t->need_loaded())
			{
				disconnect(errors::torrent_aborted, op_bittorrent);
				return;
			}
			int file_index = m_file_requests.front();
			peer_request in_range = info.orig_files().map_file(file_index, range_start
				, int(range_end - range_start));

			// request start
			boost::int64_t rs = boost::int64_t(in_range.piece) * info.piece_length() + in_range.start;
			// request end
			boost::int64_t re = rs + in_range.length;
			// file start
			boost::int64_t fs = boost::int64_t(front_request.piece) * info.piece_length() + front_request.start;

			// the http response body consists of 3 parts
			// 1. the middle of a block or the ending of a block
			// 2. a number of whole blocks
			// 3. the start of a block
			// in that order, these parts are parsed.

			bool range_overlaps_request = re >= fs + int(m_piece.size());

			if (!range_overlaps_request)
			{
				// this means the end of the incoming request ends _before_ the
				// first expected byte (fs + m_piece.size())

				incoming_piece_fragment((std::min)(payload_transferred
					, front_request.length - m_block_pos));
				received_bytes(0, bytes_transferred);

#ifdef TORRENT_LOGGING
				std::vector<file_slice> sl = info.orig_files().map_block(
					front_request.piece, front_request.start, front_request.start
						+ front_request.length);
				peer_log("INVALID HTTP RESPONSE [ in=(%d, %d-%d) expected=(%d, %d-%d) piece: %d ]"
					, file_index, range_start, range_end, sl[0].file_index
					, sl[0].offset, sl[0].offset + sl[0].size, front_request.piece);
#endif
				disconnect(errors::invalid_range, op_bittorrent, 2);
				return;
			}

			// if the request is contained in the range (i.e. the entire request
			// fits in the range) we should not start a partial piece, since we soon
			// will receive enough to call incoming_piece() and pass the read buffer
			// directly (in the next loop below).
			if (range_overlaps_request
				&& !range_contains(in_range, front_request, info.piece_length()))
			{
				// the start of the next block to receive is stored
				// in m_piece. We need to append the rest of that
				// block from the http receive buffer and then
				// (if it completed) call incoming_piece() with
				// m_piece as buffer.
				
				int piece_size = int(m_piece.size());
				int copy_size = (std::min)((std::min)(front_request.length - piece_size
					, recv_buffer.left()), int(range_end - range_start - m_received_body));
				if (copy_size > m_chunk_pos && m_chunk_pos > 0) copy_size = m_chunk_pos;
				if (copy_size > 0)
				{
					TORRENT_ASSERT(int(m_piece.size()) == m_received_in_piece);
					m_piece.resize(piece_size + copy_size);
					std::memcpy(&m_piece[0] + piece_size, recv_buffer.begin, copy_size);
					TORRENT_ASSERT(int(m_piece.size()) <= front_request.length);
					recv_buffer.begin += copy_size;
					m_received_body += copy_size;
					m_body_start += copy_size;
					if (m_chunk_pos > 0)
					{
						TORRENT_ASSERT(m_chunk_pos >= copy_size);
						m_chunk_pos -= copy_size;
					}
					TORRENT_ASSERT(m_received_body <= range_end - range_start);
					TORRENT_ASSERT(int(m_piece.size()) <= front_request.length);
					incoming_piece_fragment(copy_size);
					TORRENT_ASSERT(int(m_piece.size()) == m_received_in_piece);
				}

				if (maybe_harvest_block())
					recv_buffer = m_recv_buffer.get();
				if (associated_torrent().expired()) return;
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

#ifdef TORRENT_LOGGING
				peer_log("<== POP REQUEST [ piece: %d start: %d len: %d ]"
					, r.piece, r.start, r.length);
#endif
				m_requests.pop_front();
				if (associated_torrent().expired()) return;
				TORRENT_ASSERT(m_block_pos >= r.length);
				m_block_pos -= r.length;
				m_received_body += r.length;
				TORRENT_ASSERT(m_recv_buffer.get().begin + m_body_start == recv_buffer.begin);
				TORRENT_ASSERT(m_received_body <= range_end - range_start);
				m_recv_buffer.cut(m_body_start + r.length, t->block_size() + request_size_overhead);
				if (m_chunk_pos > 0)
				{
					TORRENT_ASSERT(m_chunk_pos >= r.length);
					m_chunk_pos -= r.length;
				}
				m_body_start = 0;
				recv_buffer = m_recv_buffer.get();
			}

			if (!m_requests.empty())
			{
				if (in_range.start + in_range.length < m_requests.front().start + m_requests.front().length
					&& (m_received_body + recv_buffer.left() >= range_end - range_start))
				{
					int piece_size = int(m_piece.size());
					int copy_size = (std::min)((std::min)(m_requests.front().length - piece_size
						, recv_buffer.left()), int(range_end - range_start - m_received_body));
					TORRENT_ASSERT(copy_size >= 0);
					if (copy_size > 0)
					{
						TORRENT_ASSERT(int(m_piece.size()) == m_received_in_piece);
						m_piece.resize(piece_size + copy_size);
						std::memcpy(&m_piece[0] + piece_size, recv_buffer.begin, copy_size);
						recv_buffer.begin += copy_size;
						m_received_body += copy_size;
						m_body_start += copy_size;
						incoming_piece_fragment(copy_size);
						TORRENT_ASSERT(int(m_piece.size()) == m_received_in_piece);
					}
					TORRENT_ASSERT(m_received_body == range_end - range_start);
				}
			}

			TORRENT_ASSERT(m_received_body <= range_end - range_start);
			// if we're in chunked encoding mode, we have to wait for the complete
			// tail header before we can consider have received the block, otherwise
			// we'll get out of sync with the next http response. m_chunk_pos is set
			// to -1 when the tail header has been received
			if (m_received_body == range_end - range_start
				&& (!m_parser.chunked_encoding() || m_chunk_pos == -1))
			{
				int size_to_cut = recv_buffer.begin - m_recv_buffer.get().begin;

				TORRENT_ASSERT(m_recv_buffer.get().left() < size_to_cut + 1
					|| m_recv_buffer.get()[size_to_cut] == 'H');

				m_recv_buffer.cut(size_to_cut, t->block_size() + request_size_overhead);
				if (m_chunk_pos > 0)
				{
					TORRENT_ASSERT(m_chunk_pos >= size_to_cut);
					m_chunk_pos -= size_to_cut;
				}
				recv_buffer = m_recv_buffer.get();
				m_file_requests.pop_front();
				m_parser.reset();
				m_body_start = 0;
				m_received_body = 0;
				m_chunk_pos = 0;
				m_partial_chunk_header = 0;
				
				if (!t->need_loaded())
				{
					disconnect(errors::torrent_aborted, op_bittorrent);
					return;
				}

				handle_padfile(recv_buffer);
				if (associated_torrent().expired()) return;
				continue;
			}

			if (bytes_transferred == 0 || payload_transferred == 0)
			{
#ifdef TORRENT_DEBUG
				TORRENT_ASSERT(statistics().last_payload_downloaded()
					+ statistics().last_protocol_downloaded()
					== dl_target);
#endif
				break;
			}
			TORRENT_ASSERT(payload_transferred > 0);
		}
		TORRENT_ASSERT(bytes_transferred == 0);
#ifdef TORRENT_DEBUG
		TORRENT_ASSERT(statistics().last_payload_downloaded()
			+ statistics().last_protocol_downloaded() == dl_target);
#endif
	}

	void web_peer_connection::get_specific_peer_info(peer_info& p) const
	{
		web_connection_base::get_specific_peer_info(p);
		p.flags |= peer_info::local_connection;
		p.connection_type = peer_info::web_seed;
	}

	void web_peer_connection::handle_padfile(buffer::const_interval& recv_buffer)
	{
		boost::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);
		torrent_info const& info = t->torrent_file();

		while (!m_file_requests.empty()
			&& info.orig_files().pad_file_at(m_file_requests.front()))
		{
			// the next file is a pad file. We didn't actually send
			// a request for this since it most likely doesn't exist on
			// the web server anyway. Just pretend that we received a
			// bunch of zeroes here and pop it again
			int file_index = m_file_requests.front();
			m_file_requests.pop_front();
			boost::int64_t file_size = info.orig_files().file_size(file_index);

			peer_request front_request = m_requests.front();

			TORRENT_ASSERT(m_block_pos < front_request.length);
			int pad_size = int((std::min)(file_size, boost::int64_t(front_request.length - m_block_pos)));

			// insert zeroes to represent the pad file
			m_piece.resize(m_piece.size() + size_t(pad_size), 0);
			m_block_pos += pad_size;
			incoming_piece_fragment(pad_size);

			if (maybe_harvest_block())
				recv_buffer = m_recv_buffer.get();
			if (associated_torrent().expired()) return;
		}
	}
}

