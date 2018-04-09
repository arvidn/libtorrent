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

#include "libtorrent/config.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <vector>
#include <boost/limits.hpp>
#include <boost/bind.hpp>
#include <stdlib.h>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

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
#include "libtorrent/aux_/escape_string.hpp" // for escape_path
#include "libtorrent/hex.hpp" // for is_hex

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
	, m_chunk_pos(0)
	, m_partial_chunk_header(0)
	, m_num_responses(0)
{
	INVARIANT_CHECK;

	if (!m_settings.get_bool(settings_pack::report_web_seed_downloads))
		ignore_stats(true);

	shared_ptr<torrent> tor = pack.tor.lock();
	TORRENT_ASSERT(tor);

	// if the web server is known not to support keep-alive. request 4MiB
	// but we want to have at least piece size to prevent block based requests
	int const min_size = std::max((web.supports_keepalive ? 1 : 4) * 1024 * 1024,
		tor->torrent_file().piece_length());

	// we prefer downloading large chunks from web seeds,
	// but still want to be able to split requests
	int const preferred_size = std::max(min_size, m_settings.get_int(settings_pack::urlseed_max_request_bytes));

	prefer_contiguous_blocks(preferred_size / tor->block_size());

	boost::shared_ptr<torrent> t = associated_torrent().lock();
	bool const single_file_request = t->torrent_file().num_files() == 1;

	if (!single_file_request)
	{
		// handle incorrect .torrent files which are multi-file
		// but have web seeds not ending with a slash
		if (m_path.empty() || m_path[m_path.size()-1] != '/') m_path += '/';
		if (m_url.empty() || m_url[m_url.size()-1] != '/') m_url += '/';
	}
	else
	{
		// handle .torrent files that don't include the filename in the url
		if (m_path.empty()) m_path += '/';
		if (m_path[m_path.size()-1] == '/')
		{
			std::string const& name = t->torrent_file().name();
			m_path += escape_string(name.c_str(), name.size());
		}

		if (!m_url.empty() && m_url[m_url.size() - 1] == '/')
		{
			std::string tmp = t->torrent_file().files().file_path(0);
#ifdef TORRENT_WINDOWS
			convert_path_to_posix(tmp);
#endif
			tmp = escape_path(tmp.c_str(), tmp.size());
			m_url += tmp;
		}
	}

	// we want large blocks as well, so
	// we can request more bytes at once
	// this setting will merge adjacent requests
	// into single larger ones
	request_large_blocks(true);

#ifndef TORRENT_DISABLE_LOGGING
	peer_log(peer_log_alert::info, "URL", "web_peer_connection %s", m_url.c_str());
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
#ifndef TORRENT_DISABLE_LOGGING
		// a write operation failed with broken-pipe. This typically happens
		// with HTTP 1.0 servers that close their incoming channel of the TCP
		// stream whenever they're done reading one full request. Instead of
		// us bailing out and failing the entire request just because our
		// write-end was closed, ignore it and keep reading until the read-end
		// also is closed.
		peer_log(peer_log_alert::info, "WRITE_DIRECTION", "CLOSED");
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
#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::info, "SAVE_RESTART_DATA"
			, "data: %d req: %d off: %d"
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
	}

	if (m_web && !m_web->supports_keepalive && error == 0)
	{
		// if the web server doesn't support keepalive and we were
		// disconnected as a graceful EOF, reconnect right away
		if (t) get_io_service().post(
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
	ret.bytes_downloaded = m_piece.size();
	// this is used to make sure that the block_index stays within
	// bounds. If the entire piece is downloaded, the block_index
	// would otherwise point to one past the end
	int correction = m_piece.size() ? -1 : 0;
	ret.block_index = (m_requests.front().start + m_piece.size() + correction) / t->block_size();
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

#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::outgoing_message, "REQUESTING", "piece: %d start: %d len: %d"
			, pr.piece, pr.start, pr.length);
#endif

		if (m_web->restart_request == m_requests.front())
		{
			m_piece.swap(m_web->restart_piece);
			peer_request& front = m_requests.front();
			TORRENT_ASSERT(front.length > int(m_piece.size()));

#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "RESTART_DATA", "data: %d req: (%d, %d) size: %d"
				, int(m_piece.size()), int(front.piece), int(front.start)
				, int (front.start + front.length - 1));
#else
			TORRENT_UNUSED(front);
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

	bool const single_file_request = t->torrent_file().num_files() == 1;
	int const proxy_type = m_settings.get_int(settings_pack::proxy_type);
	bool const using_proxy = (proxy_type == settings_pack::http
		|| proxy_type == settings_pack::http_pw) && !m_ssl;

	// the number of pad files that have been "requested". In case we _only_
	// request padfiles, we can't rely on handling them in the on_receive()
	// callback (because we won't receive anything), instead we have to post a
	// pretend read callback where we can deliver the zeroes for the partfile
	int num_pad_files = 0;

	// TODO: 2 do we really need a special case here? wouldn't the multi-file
	// case handle single file torrents correctly too?
	if (single_file_request)
	{
		file_request_t file_req;
		file_req.file_index = 0;
		file_req.start = boost::int64_t(req.piece) * info.piece_length()
			+ req.start;
		file_req.length = req.length;

		request += "GET ";
		// do not encode single file paths, they are
		// assumed to be encoded in the torrent file
		request += using_proxy ? m_url : m_path;
		request += " HTTP/1.1\r\n";
		add_headers(request, m_settings, using_proxy);
		request += "\r\nRange: bytes=";
		request += to_string(file_req.start).elems;
		request += "-";
		request += to_string(file_req.start + file_req.length - 1).elems;
		request += "\r\n\r\n";
		m_first_request = false;

		m_file_requests.push_back(file_req);
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

			file_request_t file_req;
			file_req.file_index = f.file_index;
			file_req.start = f.offset;
			file_req.length = f.size;

			if (info.orig_files().pad_file_at(f.file_index))
			{
				m_file_requests.push_back(file_req);
				++num_pad_files;
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

			m_file_requests.push_back(file_req);
		}
	}

	if (num_pad_files == int(m_file_requests.size()))
	{
		get_io_service().post(boost::bind(
			&web_peer_connection::on_receive_padfile,
			boost::static_pointer_cast<web_peer_connection>(self())));
		return;
	}

#ifndef TORRENT_DISABLE_LOGGING
	peer_log(peer_log_alert::outgoing_message, "REQUEST", "%s", request.c_str());
#endif

	send_buffer(request.c_str(), request.size(), message_type_request);
}

namespace {

	std::string get_peer_name(http_parser const& p, std::string const& host)
	{
		std::string ret = "URL seed @ ";
		ret += host;

		std::string const& server_version = p.header("server");
		if (!server_version.empty())
		{
			ret += " (";
			ret += server_version;
			ret += ")";
		}
		return ret;
	}

	boost::tuple<boost::int64_t, boost::int64_t> get_range(
		http_parser const& parser, error_code& ec)
	{
		boost::int64_t range_start;
		boost::int64_t range_end;
		if (parser.status_code() == 206)
		{
			boost::tie(range_start, range_end) = parser.content_range();
			if (range_start < 0 || range_end < range_start)
			{
				ec = errors::invalid_range;
				range_start = 0;
				range_end = 0;
			}
			else
			{
				// the http range is inclusive
				range_end++;
			}
		}
		else
		{
			range_start = 0;
			range_end = parser.content_length();
			if (range_end < 0)
			{
				range_end = 0;
				ec = errors::no_content_length;
			}
		}
		return boost::tuple<boost::int64_t, boost::int64_t>(range_start, range_end);
	}
}

// --------------------------
// RECEIVE DATA
// --------------------------

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

void web_peer_connection::on_receive_padfile()
{
	handle_padfile();
}

void web_peer_connection::handle_error(int bytes_left)
{
	boost::shared_ptr<torrent> t = associated_torrent().lock();
	TORRENT_ASSERT(t);

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
		t->alerts().emplace_alert<url_seed_alert>(t->get_handle(), m_url
			, error_msg);
	}
	received_bytes(0, bytes_left);
	disconnect(error_code(m_parser.status_code(), http_category()), op_bittorrent, 1);
	return;
}

void web_peer_connection::handle_redirect(int bytes_left)
{
	// this means we got a redirection request
	// look for the location header
	std::string location = m_parser.header("location");
	received_bytes(0, bytes_left);

	boost::shared_ptr<torrent> t = associated_torrent().lock();
	TORRENT_ASSERT(t);

	if (location.empty())
	{
		// we should not try this server again.
		t->remove_web_seed(this, errors::missing_location, op_bittorrent, 2);
		m_web = NULL;
		TORRENT_ASSERT(is_disconnecting());
		return;
	}

	bool const single_file_request = !m_path.empty()
		&& m_path[m_path.size() - 1] != '/';

	// add the redirected url and remove the current one
	if (!single_file_request)
	{
		TORRENT_ASSERT(!m_file_requests.empty());
		int const file_index = m_file_requests.front().file_index;

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
			return;
		}
		location.resize(i);
	}
	else
	{
		location = resolve_redirect_location(m_url, location);
	}

#ifndef TORRENT_DISABLE_LOGGING
	peer_log(peer_log_alert::info, "LOCATION", "%s", location.c_str());
#endif
	t->add_web_seed(location, web_seed_entry::url_seed, m_external_auth, m_extra_headers);
	t->remove_web_seed(this, errors::redirecting, op_bittorrent, 2);
	m_web = NULL;
	TORRENT_ASSERT(is_disconnecting());
	return;
}

void web_peer_connection::on_receive(error_code const& error
	, std::size_t bytes_transferred)
{
	INVARIANT_CHECK;

	if (error)
	{
		received_bytes(0, bytes_transferred);
#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::info, "ERROR"
			, "web_peer_connection error: %s", error.message().c_str());
#endif
		return;
	}

	boost::shared_ptr<torrent> t = associated_torrent().lock();
	TORRENT_ASSERT(t);

	// in case the first file on this series of requests is a padfile
	// we need to handle it right now
	buffer::const_interval recv_buffer = m_recv_buffer.get();
	handle_padfile();
	if (associated_torrent().expired()) return;

	for (;;)
	{
		int payload;
		int protocol;
		bool header_finished = m_parser.header_finished();
		if (!header_finished)
		{
			bool failed = false;
			boost::tie(payload, protocol) = m_parser.incoming(recv_buffer, failed);
			received_bytes(0, protocol);
			TORRENT_ASSERT(int(recv_buffer.left()) >= protocol);

			if (failed)
			{
				received_bytes(0, recv_buffer.left());
#ifndef TORRENT_DISABLE_LOGGING
				peer_log(peer_log_alert::info, "RECEIVE_BYTES"
					, "%s", std::string(recv_buffer.begin, recv_buffer.end).c_str());
#endif
				disconnect(errors::http_parse_error, op_bittorrent, 2);
				return;
			}

			TORRENT_ASSERT(recv_buffer.left() == 0 || *recv_buffer.begin == 'H');
			TORRENT_ASSERT(recv_buffer.left() <= m_recv_buffer.packet_size());

			// this means the entire status line hasn't been received yet
			if (m_parser.status_code() == -1)
			{
				TORRENT_ASSERT(payload == 0);
				break;
			}

			if (!m_parser.header_finished())
			{
				TORRENT_ASSERT(payload == 0);
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

#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "STATUS"
				, "%d %s", m_parser.status_code(), m_parser.message().c_str());
			std::multimap<std::string, std::string> const& headers = m_parser.headers();
			for (std::multimap<std::string, std::string>::const_iterator i = headers.begin()
				, end(headers.end()); i != end; ++i)
				peer_log(peer_log_alert::info, "STATUS", "   %s: %s", i->first.c_str(), i->second.c_str());
#endif

			// if the status code is not one of the accepted ones, abort
			if (!is_ok_status(m_parser.status_code()))
			{
				handle_error(recv_buffer.left());
				return;
			}

			if (is_redirect(m_parser.status_code()))
			{
				handle_redirect(recv_buffer.left());
				return;
			}

			m_server_string = get_peer_name(m_parser, m_host);

			recv_buffer.begin += m_body_start;

			m_body_start = m_parser.body_start();
			m_received_body = 0;
		}

		// we only received the header, no data
		if (recv_buffer.left() == 0) break;

		// ===================================
		// ======= RESPONSE BYTE RANGE =======
		// ===================================

		// despite the HTTP range being inclusive, range_start and range_end are
		// exclusive to fit better into C++. i.e. range_end points one byte past
		// the end of the payload
		boost::int64_t range_start;
		boost::int64_t range_end;
		error_code ec;
		boost::tie(range_start, range_end) = get_range(m_parser, ec);
		if (ec)
		{
			received_bytes(0, recv_buffer.left());
			// we should not try this server again.
			t->remove_web_seed(this, ec, op_bittorrent, 2);
			m_web = NULL;
			TORRENT_ASSERT(is_disconnecting());
			return;
		}

		TORRENT_ASSERT(!m_file_requests.empty());
		file_request_t const& file_req = m_file_requests.front();
		if (range_start != file_req.start
			|| range_end != file_req.start + file_req.length)
		{
			// the byte range in the http response is different what we expected
			received_bytes(0, recv_buffer.left());

#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::incoming, "INVALID HTTP RESPONSE"
				, "in=(%d, %" PRId64 "-%" PRId64 ") expected=(%d, %" PRId64 "-%" PRId64 ") ]"
				, file_req.file_index, range_start, range_end
				, file_req.file_index, file_req.start, file_req.start + file_req.length - 1);
#endif
			disconnect(errors::invalid_range, op_bittorrent, 2);
			return;
		}

		if (m_parser.chunked_encoding())
		{

			// =========================
			// === CHUNKED ENCODING  ===
			// =========================

			while (m_chunk_pos >= 0 && recv_buffer.left() > 0)
			{
				// first deliver any payload we have in the buffer so far, ahead of
				// the next chunk header.
				if (m_chunk_pos > 0)
				{
					int const copy_size = (std::min)(m_chunk_pos, recv_buffer.left());
					TORRENT_ASSERT(copy_size > 0);

					if (m_received_body + copy_size > file_req.length)
					{
						// the byte range in the http response is different what we expected
						received_bytes(0, recv_buffer.left());

#ifndef TORRENT_DISABLE_LOGGING
						peer_log(peer_log_alert::incoming, "INVALID HTTP RESPONSE"
							, "received body: %d request size: %d"
							, m_received_body, file_req.length);
#endif
						disconnect(errors::invalid_range, op_bittorrent, 2);
						return;
					}
					incoming_payload(recv_buffer.begin, copy_size);

					recv_buffer.begin += copy_size;
					m_chunk_pos -= copy_size;

					if (recv_buffer.left() == 0) goto done;
				}

				TORRENT_ASSERT(m_chunk_pos == 0);

				int header_size = 0;
				boost::int64_t chunk_size = 0;
				buffer::const_interval chunk_start = recv_buffer;
				chunk_start.begin += m_chunk_pos;
				TORRENT_ASSERT(chunk_start.begin[0] == '\r'
					|| detail::is_hex(chunk_start.begin, 1));
				bool ret = m_parser.parse_chunk_header(chunk_start, &chunk_size, &header_size);
				if (!ret)
				{
					received_bytes(0, chunk_start.left() - m_partial_chunk_header);
					m_partial_chunk_header = chunk_start.left();
					goto done;
				}
#ifndef TORRENT_DISABLE_LOGGING
				peer_log(peer_log_alert::info, "CHUNKED_ENCODING"
					, "parsed chunk: %" PRId64 " header_size: %d"
					, chunk_size, header_size);
#endif
				received_bytes(0, header_size - m_partial_chunk_header);
				m_partial_chunk_header = 0;
				TORRENT_ASSERT(chunk_size != 0
					|| chunk_start.left() <= header_size || chunk_start.begin[header_size] == 'H');
				TORRENT_ASSERT(m_body_start + m_chunk_pos < INT_MAX);
				m_chunk_pos += chunk_size;
				recv_buffer.begin += header_size;

				// a chunk size of zero means the request is complete. Make sure the
				// number of payload bytes we've received matches the number we
				// requested. If that's not the case, we got an invalid response.
				if (chunk_size == 0)
				{
					TORRENT_ASSERT_VAL(m_chunk_pos == 0, m_chunk_pos);

#ifdef TORRENT_DEBUG
					chunk_start = recv_buffer;
					chunk_start.begin += m_chunk_pos;
					TORRENT_ASSERT(chunk_start.left() == 0 || chunk_start.begin[0] == 'H');
#endif
					m_chunk_pos = -1;

					TORRENT_ASSERT(m_received_body <= file_req.length);
					if (m_received_body != file_req.length)
					{
						// the byte range in the http response is different what we expected
						received_bytes(0, recv_buffer.left());

#ifndef TORRENT_DISABLE_LOGGING
						peer_log(peer_log_alert::incoming, "INVALID HTTP RESPONSE"
							, "received body: %d request size: %d"
							, m_received_body, file_req.length);
#endif
						disconnect(errors::invalid_range, op_bittorrent, 2);
						return;
					}
					// we just completed an HTTP file request. pop it from m_file_requests
					m_file_requests.pop_front();
					m_parser.reset();
					m_body_start = 0;
					m_received_body = 0;
					m_chunk_pos = 0;
					m_partial_chunk_header = 0;

					// in between each file request, there may be an implicit
					// pad-file request
					handle_padfile();
					break;
				}

				// if all of the receive buffer was just consumed as chunk
				// header, we're done
				if (recv_buffer.left() == 0) goto done;
			}
		}
		else
		{
			// this is the simple case, where we don't have chunked encoding
			TORRENT_ASSERT(m_received_body <= file_req.length);
			int const copy_size = (std::min)(file_req.length - m_received_body
				, recv_buffer.left());
			incoming_payload(recv_buffer.begin, copy_size);
			recv_buffer.begin += copy_size;

			TORRENT_ASSERT(m_received_body <= file_req.length);
			if (m_received_body == file_req.length)
			{
				// we just completed an HTTP file request. pop it from m_file_requests
				m_file_requests.pop_front();
				m_parser.reset();
				m_body_start = 0;
				m_received_body = 0;
				m_chunk_pos = 0;
				m_partial_chunk_header = 0;

				// in between each file request, there may be an implicit
				// pad-file request
				handle_padfile();
			}
		}

		if (recv_buffer.left() == 0) break;
	}
done:

	// now, remove all the bytes we've processed from the receive buffer
	m_recv_buffer.cut(recv_buffer.begin - m_recv_buffer.get().begin
		, t->block_size() + request_size_overhead);
}

void web_peer_connection::incoming_payload(char const* buf, int len)
{
	received_bytes(len, 0);
	m_received_body += len;

	if (is_disconnecting()) return;

#ifndef TORRENT_DISABLE_LOGGING
	peer_log(peer_log_alert::incoming_message, "INCOMING_PAYLOAD", "%d bytes", len);
#endif

	// deliver all complete bittorrent requests to the bittorrent engine
	while (len > 0)
	{
		if (m_requests.empty()) return;

		TORRENT_ASSERT(!m_requests.empty());
		peer_request const& front_request = m_requests.front();
		int const piece_size = int(m_piece.size());
		int const copy_size = (std::min)(front_request.length - piece_size, len);

		// m_piece may not hold more than the response to the next BT request
		TORRENT_ASSERT(front_request.length > piece_size);

		// copy_size is the number of bytes we need to add to the end of m_piece
		// to not exceed the size of the next bittorrent request to be delivered.
		// m_piece can only hold the response for a single BT request at a time
		m_piece.resize(piece_size + copy_size);
		std::memcpy(&m_piece[0] + piece_size, buf, copy_size);
		len -= copy_size;
		buf += copy_size;

		// keep peer stats up-to-date
		incoming_piece_fragment(copy_size);

		TORRENT_ASSERT(front_request.length >= piece_size);
		if (int(m_piece.size()) == front_request.length)
		{
			boost::shared_ptr<torrent> t = associated_torrent().lock();
			TORRENT_ASSERT(t);

#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::incoming_message, "POP_REQUEST"
				, "piece: %d start: %d len: %d"
				, front_request.piece, front_request.start, front_request.length);
#endif

			// Make a copy of the request and pop it off the queue before calling
			// incoming_piece because that may lead to a call to disconnect()
			// which will clear the request queue and invalidate any references
			// to the request
			peer_request const front_request_copy = front_request;
			m_requests.pop_front();

			incoming_piece(front_request_copy, &m_piece[0]);

			m_piece.clear();
		}
	}
}

void web_peer_connection::incoming_zeroes(int len)
{
#ifndef TORRENT_DISABLE_LOGGING
	peer_log(peer_log_alert::incoming_message, "INCOMING_ZEROES", "%d bytes", len);
#endif

	// deliver all complete bittorrent requests to the bittorrent engine
	while (len > 0)
	{
		TORRENT_ASSERT(!m_requests.empty());
		peer_request const& front_request = m_requests.front();
		int const piece_size = int(m_piece.size());
		int const copy_size = (std::min)(front_request.length - piece_size, len);

		// m_piece may not hold more than the response to the next BT request
		TORRENT_ASSERT(front_request.length > piece_size);

		// copy_size is the number of bytes we need to add to the end of m_piece
		// to not exceed the size of the next bittorrent request to be delivered.
		// m_piece can only hold the response for a single BT request at a time
		m_piece.resize(piece_size + copy_size, 0);
		len -= copy_size;

		// keep peer stats up-to-date
		incoming_piece_fragment(copy_size);

		maybe_harvest_piece();
	}
}

void web_peer_connection::maybe_harvest_piece()
{
	peer_request const& front_request = m_requests.front();
	TORRENT_ASSERT(front_request.length >= int(m_piece.size()));
	if (int(m_piece.size()) != front_request.length) return;

	boost::shared_ptr<torrent> t = associated_torrent().lock();
	TORRENT_ASSERT(t);

#ifndef TORRENT_DISABLE_LOGGING
	peer_log(peer_log_alert::incoming_message, "POP_REQUEST"
		, "piece: %d start: %d len: %d"
		, front_request.piece, front_request.start, front_request.length);
#endif
	m_requests.pop_front();

	incoming_piece(front_request, &m_piece[0]);
	m_piece.clear();
}

void web_peer_connection::get_specific_peer_info(peer_info& p) const
{
	web_connection_base::get_specific_peer_info(p);
	p.flags |= peer_info::local_connection;
	p.connection_type = peer_info::web_seed;
}

void web_peer_connection::handle_padfile()
{
	if (m_file_requests.empty()) return;
	if (m_requests.empty()) return;

	boost::shared_ptr<torrent> t = associated_torrent().lock();
	TORRENT_ASSERT(t);
	torrent_info const& info = t->torrent_file();

	while (!m_file_requests.empty()
		&& info.orig_files().pad_file_at(m_file_requests.front().file_index))
	{
		// the next file is a pad file. We didn't actually send
		// a request for this since it most likely doesn't exist on
		// the web server anyway. Just pretend that we received a
		// bunch of zeroes here and pop it again
		boost::int64_t file_size = m_file_requests.front().length;

		// in theory the pad file can span multiple bocks, hence the loop
		while (file_size > 0)
		{
			peer_request const front_request = m_requests.front();
			TORRENT_ASSERT(m_piece.size() < front_request.length);

			int pad_size = int((std::min)(file_size
					, boost::int64_t(front_request.length - m_piece.size())));
			TORRENT_ASSERT(pad_size > 0);
			file_size -= pad_size;

			incoming_zeroes(pad_size);

#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "HANDLE_PADFILE"
				, "file: %d start: %" PRId64 " len: %d"
				, m_file_requests.front().file_index
				, m_file_requests.front().start
				, m_file_requests.front().length);
#endif
		}

		m_file_requests.pop_front();
	}
}

} // libtorrent namespace

