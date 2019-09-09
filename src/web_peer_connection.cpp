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

#include <functional>
#include <cstdlib>
#include <cstdio> // for snprintf
#include <cinttypes> // for PRId64 et.al.

#include "libtorrent/web_peer_connection.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/parse_url.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/aux_/session_interface.hpp"
#include "libtorrent/alert_manager.hpp" // for alert_manager
#include "libtorrent/aux_/escape_string.hpp" // for escape_path
#include "libtorrent/hex.hpp" // for is_hex
#include "libtorrent/torrent.hpp"
#include "libtorrent/http_parser.hpp"

namespace libtorrent {

constexpr int request_size_overhead = 5000;

std::string escape_file_path(file_storage const& storage, file_index_t index);

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

	std::shared_ptr<torrent> tor = pack.tor.lock();
	TORRENT_ASSERT(tor);

	// if the web server is known not to support keep-alive. request 4MiB
	// but we want to have at least piece size to prevent block based requests
	int const min_size = std::max((web.supports_keepalive ? 1 : 4) * 1024 * 1024,
		tor->torrent_file().piece_length());

	// we prefer downloading large chunks from web seeds,
	// but still want to be able to split requests
	int const preferred_size = std::max(min_size, m_settings.get_int(settings_pack::urlseed_max_request_bytes));

	prefer_contiguous_blocks(preferred_size / tor->block_size());

	std::shared_ptr<torrent> t = associated_torrent().lock();
	bool const single_file_request = t->torrent_file().num_files() == 1;

	if (!single_file_request)
	{
		// handle incorrect .torrent files which are multi-file
		// but have web seeds not ending with a slash
		ensure_trailing_slash(m_path);
		ensure_trailing_slash(m_url);
	}
	else
	{
		// handle .torrent files that don't include the filename in the url
		if (m_path.empty()) m_path += '/';
		if (m_path[m_path.size() - 1] == '/')
		{
			m_path += escape_string(t->torrent_file().name());
		}

		if (!m_url.empty() && m_url[m_url.size() - 1] == '/')
		{
			m_url += escape_file_path(t->torrent_file().files(), file_index_t(0));
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

std::string escape_file_path(file_storage const& storage, file_index_t index)
{
	std::string new_path { storage.file_path(index) };
#ifdef TORRENT_WINDOWS
	convert_path_to_posix(new_path);
#endif
	return escape_path(new_path);
}

void web_peer_connection::on_connected()
{
	if (m_web->have_files.empty())
	{
		incoming_have_all();
	}
	else if (m_web->have_files.none_set())
	{
		incoming_have_none();
		m_web->interesting = false;
#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::info, "WEB-SEED", "have no files, not interesting. %s", m_url.c_str());
#endif
	}
	else
	{
		std::shared_ptr<torrent> t = associated_torrent().lock();

		// only advertise pieces that are contained within the files we have as
		// indicated by m_web->have_files AND padfiles!
		// it's important to include pieces that may overlap many files, as long
		// as we have all those files, so instead of starting with a clear bitfield
		// and setting the pieces corresponding to files we have, we do it the
		// other way around. Start with assuming we have all files, and clear
		// pieces overlapping with files we *don't* have.
		typed_bitfield<piece_index_t> have;
		file_storage const& fs = t->torrent_file().files();
		have.resize(fs.num_pieces(), true);
		for (auto const i : fs.file_range())
		{
			// if we have the file, no need to do anything
			if (m_web->have_files.get_bit(i) || fs.pad_file_at(i)) continue;

			auto const range = aux::file_piece_range_inclusive(fs, i);
			for (piece_index_t k = std::get<0>(range); k < std::get<1>(range); ++k)
				have.clear_bit(k);
		}
		if (have.none_set())
		{
			incoming_have_none();
			m_web->interesting = false;
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "WEB-SEED", "have no pieces, not interesting. %s", m_url.c_str());
#endif
		}
		else
		{
			incoming_bitfield(have);
		}
	}

	// TODO: 3 this should be an optional<piece_index_t>, piece index -1 should
	// not be allowed
	if (m_web->restart_request.piece != piece_index_t(-1))
	{
		// increase the chances of requesting the block
		// we have partial data for already, to finish it
		incoming_suggest(m_web->restart_request.piece);
	}
	web_connection_base::on_connected();
}

void web_peer_connection::disconnect(error_code const& ec
	, operation_t op, disconnect_severity_t const error)
{
	if (is_disconnecting()) return;

	if (op == operation_t::sock_write && ec == boost::system::errc::broken_pipe)
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

		// when the web server closed our write-end of the socket (i.e. its
		// read-end), if it's an HTTP 1.0 server. we will stop sending more
		// requests. We'll close the connection once we receive the last bytes,
		// and our read end is closed as well.
		incoming_choke();
		return;
	}

	if (op == operation_t::connect && m_web && !m_web->endpoints.empty())
	{
		// we failed to connect to this IP. remove it so that the next attempt
		// uses the next IP in the list.
		m_web->endpoints.erase(m_web->endpoints.begin());
	}

	if (ec == errors::uninteresting_upload_peer && m_web)
	{
		// if this is an "ephemeral" web seed, it means it was added by receiving
		// an HTTP redirect. If we disconnect because we're not interested in any
		// of its pieces, mark it as uninteresting, to avoid reconnecting to it
		// repeatedly
		if (m_web->ephemeral) m_web->interesting = false;

		// if the web seed is not ephemeral, but we're still not interested. That
		// implies that all files either have failed with 404 or with a
		// redirection to a different web server.
		m_web->retry = std::max(m_web->retry, aux::time_now32()
			+ seconds32(m_settings.get_int(settings_pack::urlseed_wait_retry)));
		TORRENT_ASSERT(m_web->retry > aux::time_now32());
	}

	std::shared_ptr<torrent> t = associated_torrent().lock();

	if (!m_requests.empty() && !m_file_requests.empty()
		&& !m_piece.empty() && m_web)
	{
#ifndef TORRENT_DISABLE_LOGGING
		if (should_log(peer_log_alert::info))
		{
			peer_log(peer_log_alert::info, "SAVE_RESTART_DATA"
				, "data: %d req: %d off: %d"
				, int(m_piece.size()), int(m_requests.front().piece)
				, m_requests.front().start);
		}
#endif
		m_web->restart_request = m_requests.front();
		if (!m_web->restart_piece.empty())
		{
			// we're about to replace a different restart piece
			// buffer. So it was wasted download
			if (t) t->add_redundant_bytes(int(m_web->restart_piece.size())
				, waste_reason::piece_closing);
		}
		m_web->restart_piece.swap(m_piece);

		// we have to do this to not count this data as redundant. The
		// upper layer will call downloading_piece_progress and assume
		// it's all wasted download. Since we're saving it here, it isn't.
		m_requests.clear();
	}

	if (m_web && !m_web->supports_keepalive && error == peer_connection_interface::normal)
	{
		// if the web server doesn't support keepalive and we were
		// disconnected as a graceful EOF, reconnect right away
		if (t) get_io_service().post(
			std::bind(&torrent::maybe_connect_web_seeds, t));
	}

	if (error >= failure)
	{
		m_web->retry = std::max(m_web->retry, aux::time_now32()
			+ seconds32(m_settings.get_int(settings_pack::urlseed_wait_retry)));
	}

	peer_connection::disconnect(ec, op, error);
	if (t) t->disconnect_web_seed(this);
}

piece_block_progress web_peer_connection::downloading_piece_progress() const
{
	if (m_requests.empty()) return {};

	std::shared_ptr<torrent> t = associated_torrent().lock();
	TORRENT_ASSERT(t);

	piece_block_progress ret;

	ret.piece_index = m_requests.front().piece;
	ret.bytes_downloaded = int(m_piece.size());
	// this is used to make sure that the block_index stays within
	// bounds. If the entire piece is downloaded, the block_index
	// would otherwise point to one past the end
	int correction = m_piece.empty() ? 0 : -1;
	ret.block_index = (m_requests.front().start + int(m_piece.size()) + correction) / t->block_size();
	TORRENT_ASSERT(ret.block_index < int(piece_block::invalid.block_index));
	TORRENT_ASSERT(ret.piece_index < piece_block::invalid.piece_index);

	ret.full_block_bytes = t->block_size();
	piece_index_t const last_piece = t->torrent_file().last_piece();
	if (ret.piece_index == last_piece && ret.block_index
		== t->torrent_file().piece_size(last_piece) / t->block_size())
	{
		ret.full_block_bytes = t->torrent_file().piece_size(last_piece) % t->block_size();
	}
	return ret;
}

void web_peer_connection::write_request(peer_request const& r)
{
	INVARIANT_CHECK;

	std::shared_ptr<torrent> t = associated_torrent().lock();
	TORRENT_ASSERT(t);

	TORRENT_ASSERT(t->valid_metadata());

	torrent_info const& info = t->torrent_file();
	peer_request req = r;

	std::string request;
	request.reserve(400);

	int size = r.length;
	const int block_size = t->block_size();
	const int piece_size = t->torrent_file().piece_length();
	peer_request pr{};

	while (size > 0)
	{
		int request_offset = r.start + r.length - size;
		pr.start = request_offset % piece_size;
		pr.length = std::min(block_size, size);
		pr.piece = piece_index_t(static_cast<int>(r.piece) + request_offset / piece_size);
		m_requests.push_back(pr);

		if (m_web->restart_request == m_requests.front())
		{
			m_piece.swap(m_web->restart_piece);
			peer_request const& front = m_requests.front();
			TORRENT_ASSERT(front.length > int(m_piece.size()));

#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "RESTART_DATA",
				"data: %d req: (%d, %d) size: %d"
					, int(m_piece.size()), static_cast<int>(front.piece), front.start
					, front.start + front.length - 1);
#else
			TORRENT_UNUSED(front);
#endif

			req.start += int(m_piece.size());
			req.length -= int(m_piece.size());

			// just to keep the accounting straight for the upper layer.
			// it doesn't know we just re-wrote the request
			incoming_piece_fragment(int(m_piece.size()));
			m_web->restart_request.piece = piece_index_t(-1);
		}

#if 0
			std::cerr << this << " REQ: p: " << pr.piece << " " << pr.start << std::endl;
#endif
		size -= pr.length;
	}

#ifndef TORRENT_DISABLE_LOGGING
	peer_log(peer_log_alert::outgoing_message, "REQUESTING", "(piece: %d start: %d) - (piece: %d end: %d)"
		, static_cast<int>(r.piece), r.start
		, static_cast<int>(pr.piece), pr.start + pr.length);
#endif

	bool const single_file_request = t->torrent_file().num_files() == 1;
	int const proxy_type = m_settings.get_int(settings_pack::proxy_type);
	bool const using_proxy = (proxy_type == settings_pack::http
		|| proxy_type == settings_pack::http_pw) && !m_ssl;

	// the number of pad files that have been "requested". In case we _only_
	// request padfiles, we can't rely on handling them in the on_receive()
	// callback (because we won't receive anything), instead we have to post a
	// pretend read callback where we can deliver the zeroes for the partfile
	int num_pad_files = 0;

	// TODO: 3 do we really need a special case here? wouldn't the multi-file
	// case handle single file torrents correctly too?
	if (single_file_request)
	{
		file_request_t file_req;
		file_req.file_index = file_index_t(0);
		file_req.start = std::int64_t(static_cast<int>(req.piece)) * info.piece_length()
			+ req.start;
		file_req.length = req.length;

		request += "GET ";
		// do not encode single file paths, they are
		// assumed to be encoded in the torrent file
		request += using_proxy ? m_url : m_path;
		request += " HTTP/1.1\r\n";
		add_headers(request, m_settings, using_proxy);
		request += "\r\nRange: bytes=";
		request += to_string(file_req.start).data();
		request += "-";
		request += to_string(file_req.start + file_req.length - 1).data();
		request += "\r\n\r\n";
		m_first_request = false;

		m_file_requests.push_back(file_req);
	}
	else
	{
		std::vector<file_slice> files = info.orig_files().map_block(req.piece, req.start
			, req.length);

		for (auto const &f : files)
		{
			file_request_t file_req;
			file_req.file_index = f.file_index;
			file_req.start = f.offset;
			file_req.length = int(f.size);

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
			}

			auto redirection = m_web->redirects.find(f.file_index);
			if (redirection != m_web->redirects.end())
			{
				auto const& redirect = redirection->second;
				// in case of http proxy "request" already contains m_url with trailing slash, so let's skip dup slash
				bool const trailing_slash = using_proxy && !redirect.empty() && redirect[0] == '/';
				request.append(redirect, trailing_slash, std::string::npos);
			}
			else
			{
				if (!using_proxy)
				{
					// m_path is already a properly escaped URL
					// with the correct slashes. Don't encode it again
					request += m_path;
				}

				request += escape_file_path(info.orig_files(), f.file_index);
			}
			request += " HTTP/1.1\r\n";
			add_headers(request, m_settings, using_proxy);
			request += "\r\nRange: bytes=";
			request += to_string(f.offset).data();
			request += "-";
			request += to_string(f.offset + f.size - 1).data();
			request += "\r\n\r\n";
			m_first_request = false;

#if 0
			std::cerr << this << " SEND-REQUEST: f: " << f.file_index
				<< " s: " << f.offset
				<< " e: " << (f.offset + f.size - 1) << std::endl;
#endif
			// TODO: 3 file_index_t should not allow negative values
			TORRENT_ASSERT(f.file_index >= file_index_t(0));

			m_file_requests.push_back(file_req);
		}
	}

	if (num_pad_files == int(m_file_requests.size()))
	{
		get_io_service().post(std::bind(
			&web_peer_connection::on_receive_padfile,
			std::static_pointer_cast<web_peer_connection>(self())));
		return;
	}

#ifndef TORRENT_DISABLE_LOGGING
	peer_log(peer_log_alert::outgoing_message, "REQUEST", "%s", request.c_str());
#endif

	send_buffer(request);
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

	std::tuple<std::int64_t, std::int64_t> get_range(
		http_parser const& parser, error_code& ec)
	{
		std::int64_t range_start;
		std::int64_t range_end;
		if (parser.status_code() == 206)
		{
			std::tie(range_start, range_end) = parser.content_range();
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
		return std::make_tuple(range_start, range_end);
	}
}

// --------------------------
// RECEIVE DATA
// --------------------------

bool web_peer_connection::received_invalid_data(piece_index_t const index, bool single_peer)
{
	if (!single_peer) return peer_connection::received_invalid_data(index, single_peer);

	// when a web seed fails a hash check, do the following:
	// 1. if the whole piece only overlaps a single file, mark that file as not
	//    have for this peer
	// 2. if the piece overlaps more than one file, mark the piece as not have
	//    for this peer
	// 3. if it's a single file torrent, just ban it right away
	// this handles the case where web seeds may have some files updated but not other

	std::shared_ptr<torrent> t = associated_torrent().lock();
	file_storage const& fs = t->torrent_file().files();

	// single file torrent
	if (fs.num_files() == 1) return peer_connection::received_invalid_data(index, single_peer);

	std::vector<file_slice> files = fs.map_block(index, 0, fs.piece_size(index));

	if (files.size() == 1)
	{
		// assume the web seed has a different copy of this specific file
		// than what we expect, and pretend not to have it.
		auto const range = file_piece_range_inclusive(fs, files[0].file_index);
		for (piece_index_t i = std::get<0>(range); i != std::get<1>(range); ++i)
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

void web_peer_connection::handle_error(int const bytes_left)
{
	std::shared_ptr<torrent> t = associated_torrent().lock();
	TORRENT_ASSERT(t);

	// TODO: 2 just make this peer not have the pieces
	// associated with the file we just requested. Only
	// when it doesn't have any of the file do the following
	// pad files will make it complicated

	// temporarily unavailable, retry later
	t->retry_web_seed(this, m_parser.header_duration("retry-after"));
	if (t->alerts().should_post<url_seed_alert>())
	{
		std::string const error_msg = to_string(m_parser.status_code()).data()
			+ (" " + m_parser.message());
		t->alerts().emplace_alert<url_seed_alert>(t->get_handle(), m_url
			, error_msg);
	}
	received_bytes(0, bytes_left);
	disconnect(error_code(m_parser.status_code(), http_category()), operation_t::bittorrent, failure);
}

void web_peer_connection::handle_redirect(int const bytes_left)
{
	// this means we got a redirection request
	// look for the location header
	std::string location = m_parser.header("location");
	received_bytes(0, bytes_left);

	std::shared_ptr<torrent> t = associated_torrent().lock();
	TORRENT_ASSERT(t);

	if (location.empty())
	{
		// we should not try this server again.
		t->remove_web_seed_conn(this, errors::missing_location, operation_t::bittorrent, peer_error);
		m_web = nullptr;
		TORRENT_ASSERT(is_disconnecting());
		return;
	}

	bool const single_file_request = !m_path.empty()
		&& m_path[m_path.size() - 1] != '/';

	// add the redirected url and remove the current one
	if (!single_file_request)
	{
		TORRENT_ASSERT(!m_file_requests.empty());
		file_index_t const file_index = m_file_requests.front().file_index;

		location = resolve_redirect_location(m_url, location);
#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::info, "LOCATION", "%s", location.c_str());
#endif
		// TODO: 3 this could be made more efficient for the case when we use an
		// HTTP proxy. Then we wouldn't need to add new web seeds to the torrent,
		// we could just make the redirect table contain full URLs.
		std::string redirect_base;
		std::string redirect_path;
		error_code ec;
		std::tie(redirect_base, redirect_path) = split_url(location, ec);

		if (ec)
		{
			// we should not try this server again.
			disconnect(errors::missing_location, operation_t::bittorrent, failure);
			return;
		}

		// add_web_seed won't add duplicates. If we have already added an entry
		// with this URL, we'll get back the existing entry

		// "ephemeral" flag should be set to avoid "web_seed_t" saving in resume data.
		// E.g. original "web_seed_t" request url points to "http://example1.com/file1" and
		// web server responses with redirect location "http://example2.com/subpath/file2".
		// "handle_redirect" process this location to create new "web_seed_t"
		// with base url=="http://example2.com/" and redirects[0]=="/subpath/file2").
		// If we try to load resume with such "web_seed_t" then "web_peer_connection" will send
		// request with wrong path "http://example2.com/file1" (cause "redirects" map is not serialized in resume)
		web_seed_t* web = t->add_web_seed(redirect_base, web_seed_entry::url_seed
			, m_external_auth, m_extra_headers, torrent::ephemeral);
		web->have_files.resize(t->torrent_file().num_files(), false);

		// the new web seed we're adding only has this file for now
		// we may add more files later
		web->redirects[file_index] = redirect_path;
		if (web->have_files.get_bit(file_index) == false)
		{
			web->have_files.set_bit(file_index);

			if (web->peer_info.connection != nullptr)
			{
				auto* pc = static_cast<peer_connection*>(web->peer_info.connection);

				// we just learned that this host has this file, and we're currently
				// connected to it. Make it advertise that it has this file to the
				// bittorrent engine
				file_storage const& fs = t->torrent_file().files();
				auto const range = aux::file_piece_range_inclusive(fs, file_index);
				for (piece_index_t i = std::get<0>(range); i < std::get<1>(range); ++i)
					pc->incoming_have(i);
			}
			// we just learned about another file this web server has, make sure
			// it's marked interesting to enable connecting to it
			web->interesting = true;
		}

		// we don't have this file on this server. Don't ask for it again
		m_web->have_files.resize(t->torrent_file().num_files(), true);
		if (m_web->have_files[file_index])
		{
			m_web->have_files.clear_bit(file_index);
#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::info, "MISSING_FILE", "redirection | file: %d"
				, static_cast<int>(file_index));
#endif
		}
		disconnect(errors::redirecting, operation_t::bittorrent, normal);
	}
	else
	{
		location = resolve_redirect_location(m_url, location);
#ifndef TORRENT_DISABLE_LOGGING
		peer_log(peer_log_alert::info, "LOCATION", "%s", location.c_str());
#endif
		t->add_web_seed(location, web_seed_entry::url_seed, m_external_auth
			, m_extra_headers, torrent::ephemeral);

		// this web seed doesn't have any files. Don't try to request from it
		// again this session
		m_web->have_files.resize(t->torrent_file().num_files(), false);
		disconnect(errors::redirecting, operation_t::bittorrent, normal);
		m_web = nullptr;
		TORRENT_ASSERT(is_disconnecting());
	}
}

void web_peer_connection::on_receive(error_code const& error
	, std::size_t bytes_transferred)
{
	INVARIANT_CHECK;

	if (error)
	{
		received_bytes(0, int(bytes_transferred));
#ifndef TORRENT_DISABLE_LOGGING
		if (should_log(peer_log_alert::info))
		{
			peer_log(peer_log_alert::info, "ERROR"
				, "web_peer_connection error: %s", error.message().c_str());
		}
#endif
		return;
	}

	std::shared_ptr<torrent> t = associated_torrent().lock();
	TORRENT_ASSERT(t);

	// in case the first file on this series of requests is a padfile
	// we need to handle it right now
	span<char const> recv_buffer = m_recv_buffer.get();
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
			std::tie(payload, protocol) = m_parser.incoming(recv_buffer, failed);
			received_bytes(0, protocol);
			TORRENT_ASSERT(int(recv_buffer.size()) >= protocol);

			if (failed)
			{
				received_bytes(0, int(recv_buffer.size()));
#ifndef TORRENT_DISABLE_LOGGING
				if (should_log(peer_log_alert::info))
				{
					peer_log(peer_log_alert::info, "RECEIVE_BYTES"
						, "%*s", int(recv_buffer.size()), recv_buffer.data());
				}
#endif
				disconnect(errors::http_parse_error, operation_t::bittorrent, peer_error);
				return;
			}

			TORRENT_ASSERT(recv_buffer.empty() || recv_buffer[0] == 'H');
			TORRENT_ASSERT(int(recv_buffer.size()) <= m_recv_buffer.packet_size());

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
			if (should_log(peer_log_alert::info))
			{
				peer_log(peer_log_alert::info, "STATUS"
					, "%d %s", m_parser.status_code(), m_parser.message().c_str());
				std::multimap<std::string, std::string> const& headers = m_parser.headers();
				for (auto const &i : headers)
					peer_log(peer_log_alert::info, "STATUS", "   %s: %s", i.first.c_str(), i.second.c_str());
			}
#endif

			// if the status code is not one of the accepted ones, abort
			if (!is_ok_status(m_parser.status_code()))
			{
				if (!m_file_requests.empty())
				{
					file_request_t const& file_req = m_file_requests.front();
					m_web->have_files.resize(t->torrent_file().num_files(), true);
					m_web->have_files.clear_bit(file_req.file_index);

#ifndef TORRENT_DISABLE_LOGGING
					peer_log(peer_log_alert::info, "MISSING_FILE", "http-code: %d | file: %d"
						, m_parser.status_code(), static_cast<int>(file_req.file_index));
#endif
				}
				handle_error(int(recv_buffer.size()));
				return;
			}

			if (is_redirect(m_parser.status_code()))
			{
				handle_redirect(int(recv_buffer.size()));
				return;
			}

			m_server_string = get_peer_name(m_parser, m_host);

			recv_buffer = recv_buffer.subspan(m_body_start);

			m_body_start = m_parser.body_start();
			m_received_body = 0;
		}

		// we only received the header, no data
		if (recv_buffer.empty()) break;

		// ===================================
		// ======= RESPONSE BYTE RANGE =======
		// ===================================

		// despite the HTTP range being inclusive, range_start and range_end are
		// exclusive to fit better into C++. i.e. range_end points one byte past
		// the end of the payload
		std::int64_t range_start;
		std::int64_t range_end;
		error_code ec;
		std::tie(range_start, range_end) = get_range(m_parser, ec);
		if (ec)
		{
			received_bytes(0, int(recv_buffer.size()));
			// we should not try this server again.
			t->remove_web_seed_conn(this, ec, operation_t::bittorrent, peer_error);
			m_web = nullptr;
			TORRENT_ASSERT(is_disconnecting());
			return;
		}

		TORRENT_ASSERT(!m_file_requests.empty());
		file_request_t const& file_req = m_file_requests.front();
		if (range_start != file_req.start
			|| range_end != file_req.start + file_req.length)
		{
			// the byte range in the http response is different what we expected
			received_bytes(0, int(recv_buffer.size()));

#ifndef TORRENT_DISABLE_LOGGING
			if (should_log(peer_log_alert::incoming))
			{
				peer_log(peer_log_alert::incoming, "INVALID HTTP RESPONSE"
					, "in=(%d, %" PRId64 "-%" PRId64 ") expected=(%d, %" PRId64 "-%" PRId64 ") ]"
					, static_cast<int>(file_req.file_index), range_start, range_end
					, static_cast<int>(file_req.file_index), file_req.start, file_req.start + file_req.length - 1);
			}
#endif
			disconnect(errors::invalid_range, operation_t::bittorrent, peer_error);
			return;
		}

		if (m_parser.chunked_encoding())
		{

			// =========================
			// === CHUNKED ENCODING  ===
			// =========================

			while (m_chunk_pos >= 0 && !recv_buffer.empty())
			{
				// first deliver any payload we have in the buffer so far, ahead of
				// the next chunk header.
				if (m_chunk_pos > 0)
				{
					int const copy_size = std::min(m_chunk_pos, int(recv_buffer.size()));
					TORRENT_ASSERT(copy_size > 0);

					if (m_received_body + copy_size > file_req.length)
					{
						// the byte range in the http response is different what we expected
						received_bytes(0, int(recv_buffer.size()));

#ifndef TORRENT_DISABLE_LOGGING
						peer_log(peer_log_alert::incoming, "INVALID HTTP RESPONSE"
							, "received body: %d request size: %d"
							, m_received_body, file_req.length);
#endif
						disconnect(errors::invalid_range, operation_t::bittorrent, peer_error);
						return;
					}
					incoming_payload(recv_buffer.data(), copy_size);

					recv_buffer = recv_buffer.subspan(copy_size);
					m_chunk_pos -= copy_size;

					if (recv_buffer.empty()) goto done;
				}

				TORRENT_ASSERT(m_chunk_pos == 0);

				int header_size = 0;
				std::int64_t chunk_size = 0;
				span<char const> chunk_start = recv_buffer.subspan(m_chunk_pos);
				TORRENT_ASSERT(chunk_start[0] == '\r'
					|| aux::is_hex({chunk_start.data(), 1}));
				bool const ret = m_parser.parse_chunk_header(chunk_start, &chunk_size, &header_size);
				if (!ret)
				{
					received_bytes(0, int(chunk_start.size()) - m_partial_chunk_header);
					m_partial_chunk_header = int(chunk_start.size());
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
					|| int(chunk_start.size()) <= header_size || chunk_start[header_size] == 'H');
				TORRENT_ASSERT(m_body_start + m_chunk_pos < INT_MAX);
				m_chunk_pos += int(chunk_size);
				recv_buffer = recv_buffer.subspan(header_size);

				// a chunk size of zero means the request is complete. Make sure the
				// number of payload bytes we've received matches the number we
				// requested. If that's not the case, we got an invalid response.
				if (chunk_size == 0)
				{
					TORRENT_ASSERT_VAL(m_chunk_pos == 0, m_chunk_pos);

#if TORRENT_USE_ASSERTS
					span<char const> chunk = recv_buffer.subspan(m_chunk_pos);
					TORRENT_ASSERT(chunk.size() == 0 || chunk[0] == 'H');
#endif
					m_chunk_pos = -1;

					TORRENT_ASSERT(m_received_body <= file_req.length);
					if (m_received_body != file_req.length)
					{
						// the byte range in the http response is different what we expected
						received_bytes(0, int(recv_buffer.size()));

#ifndef TORRENT_DISABLE_LOGGING
						peer_log(peer_log_alert::incoming, "INVALID HTTP RESPONSE"
							, "received body: %d request size: %d"
							, m_received_body, file_req.length);
#endif
						disconnect(errors::invalid_range, operation_t::bittorrent, peer_error);
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
				if (recv_buffer.empty()) goto done;
			}
		}
		else
		{
			// this is the simple case, where we don't have chunked encoding
			TORRENT_ASSERT(m_received_body <= file_req.length);
			int const copy_size = std::min(file_req.length - m_received_body
				, int(recv_buffer.size()));
			incoming_payload(recv_buffer.data(), copy_size);
			recv_buffer = recv_buffer.subspan(copy_size);

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

		if (recv_buffer.empty()) break;
	}
done:

	// now, remove all the bytes we've processed from the receive buffer
	m_recv_buffer.cut(int(recv_buffer.data() - m_recv_buffer.get().begin())
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
		int const copy_size = std::min(front_request.length - piece_size, len);

		// m_piece may not hold more than the response to the next BT request
		TORRENT_ASSERT(front_request.length > piece_size);

		// copy_size is the number of bytes we need to add to the end of m_piece
		// to not exceed the size of the next bittorrent request to be delivered.
		// m_piece can only hold the response for a single BT request at a time
		m_piece.resize(piece_size + copy_size);
		std::memcpy(m_piece.data() + piece_size, buf, aux::numeric_cast<std::size_t>(copy_size));
		len -= copy_size;
		buf += copy_size;

		// keep peer stats up-to-date
		incoming_piece_fragment(copy_size);

		TORRENT_ASSERT(front_request.length >= piece_size);
		if (int(m_piece.size()) == front_request.length)
		{
			std::shared_ptr<torrent> t = associated_torrent().lock();
			TORRENT_ASSERT(t);

#ifndef TORRENT_DISABLE_LOGGING
			peer_log(peer_log_alert::incoming_message, "POP_REQUEST"
				, "piece: %d start: %d len: %d"
				, static_cast<int>(front_request.piece), front_request.start, front_request.length);
#endif

			// Make a copy of the request and pop it off the queue before calling
			// incoming_piece because that may lead to a call to disconnect()
			// which will clear the request queue and invalidate any references
			// to the request
			peer_request const front_request_copy = front_request;
			m_requests.pop_front();

			incoming_piece(front_request_copy, m_piece.data());

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
		int const copy_size = std::min(front_request.length - piece_size, len);

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

	std::shared_ptr<torrent> t = associated_torrent().lock();
	TORRENT_ASSERT(t);

#ifndef TORRENT_DISABLE_LOGGING
	peer_log(peer_log_alert::incoming_message, "POP_REQUEST"
		, "piece: %d start: %d len: %d"
		, static_cast<int>(front_request.piece)
		, front_request.start, front_request.length);
#endif
	m_requests.pop_front();

	incoming_piece(front_request, m_piece.data());
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

	std::shared_ptr<torrent> t = associated_torrent().lock();
	TORRENT_ASSERT(t);
	torrent_info const& info = t->torrent_file();

	while (!m_file_requests.empty()
		&& info.orig_files().pad_file_at(m_file_requests.front().file_index))
	{
		// the next file is a pad file. We didn't actually send
		// a request for this since it most likely doesn't exist on
		// the web server anyway. Just pretend that we received a
		// bunch of zeroes here and pop it again
		std::int64_t file_size = m_file_requests.front().length;

		// in theory the pad file can span multiple bocks, hence the loop
		while (file_size > 0)
		{
			peer_request const front_request = m_requests.front();
			TORRENT_ASSERT(int(m_piece.size()) < front_request.length);

			int pad_size = int(std::min(file_size
					, front_request.length - std::int64_t(m_piece.size())));
			TORRENT_ASSERT(pad_size > 0);
			file_size -= pad_size;

			incoming_zeroes(pad_size);

#ifndef TORRENT_DISABLE_LOGGING
			if (should_log(peer_log_alert::info))
			{
				peer_log(peer_log_alert::info, "HANDLE_PADFILE"
					, "file: %d start: %" PRId64 " len: %d"
					, static_cast<int>(m_file_requests.front().file_index)
					, m_file_requests.front().start
					, m_file_requests.front().length);
			}
#endif
		}

		m_file_requests.pop_front();
	}
}

} // libtorrent namespace
