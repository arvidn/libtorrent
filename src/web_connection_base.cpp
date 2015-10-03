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
#include <limits>
#include <boost/bind.hpp>
#include <stdlib.h>

#include "libtorrent/web_connection_base.hpp"
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

using boost::shared_ptr;
using libtorrent::aux::session_impl;

namespace libtorrent
{
	web_connection_base::web_connection_base(
		session_impl& ses
		, boost::weak_ptr<torrent> t
		, boost::shared_ptr<socket_type> s
		, tcp::endpoint const& remote
		, web_seed_entry& web)
		: peer_connection(ses, t, s, remote, &web.peer_info)
		, m_parser(http_parser::dont_parse_chunks)
		, m_external_auth(web.auth)
		, m_extra_headers(web.extra_headers)
		, m_first_request(true)
		, m_ssl(false)
		, m_body_start(0)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(is_outgoing());

		// we only want left-over bandwidth
		set_priority(1);

		// since this is a web seed, change the timeout
		// according to the settings.
		set_timeout(ses.settings().urlseed_timeout);

		std::string protocol;
		error_code ec;
		boost::tie(protocol, m_basic_auth, m_host, m_port, m_path)
			= parse_url_components(web.url, ec);
		TORRENT_ASSERT(!ec);

		if (m_port == -1 && protocol == "http")
			m_port = 80;

#ifdef TORRENT_USE_OPENSSL
		if (protocol == "https")
		{
			m_ssl = true;
			if (m_port == -1) m_port = 443;
		}
#endif

		if (!m_basic_auth.empty())
			m_basic_auth = base64encode(m_basic_auth);

		m_server_string = "URL seed @ ";
		m_server_string += m_host;
	}

	void web_connection_base::start()
	{
		set_upload_only(true);
		if (is_disconnecting()) return;
		peer_connection::start();
	}

	web_connection_base::~web_connection_base()
	{}

	void web_connection_base::on_connected()
	{
		boost::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);
	
		// this is always a seed
		incoming_have_all();

		// it is always possible to request pieces
		incoming_unchoke();

		reset_recv_buffer(t->block_size() + 1024);
	}

	void web_connection_base::add_headers(std::string& request
		, proxy_settings const& ps, bool using_proxy) const
	{
		request += "Host: ";
		request += m_host;
		if (m_first_request || m_ses.settings().always_send_user_agent) {
			request += "\r\nUser-Agent: ";
			request += m_ses.settings().user_agent;
		}
		if (!m_external_auth.empty()) {
			request += "\r\nAuthorization: ";
			request += m_external_auth;
		} else if (!m_basic_auth.empty()) {
			request += "\r\nAuthorization: Basic ";
			request += m_basic_auth;
		}
		if (ps.type == proxy_settings::http_pw) {
			request += "\r\nProxy-Authorization: Basic ";
			request += base64encode(ps.username + ":" + ps.password);
		}
		for (web_seed_entry::headers_t::const_iterator it = m_extra_headers.begin();
		     it != m_extra_headers.end(); ++it) {
		  request += "\r\n";
		  request += it->first;
		  request += ": ";
		  request += it->second;
		}
		if (using_proxy) {
			request += "\r\nProxy-Connection: keep-alive";
		}
		if (m_first_request || using_proxy) {
			request += "\r\nConnection: keep-alive";
		}
	}

	// --------------------------
	// RECEIVE DATA
	// --------------------------

	void web_connection_base::get_specific_peer_info(peer_info& p) const
	{
		if (is_interesting()) p.flags |= peer_info::interesting;
		if (is_choked()) p.flags |= peer_info::choked;
		if (!is_connecting() && m_server_string.empty())
			p.flags |= peer_info::handshake;
		if (is_connecting() && !is_queued()) p.flags |= peer_info::connecting;
		if (is_queued()) p.flags |= peer_info::queued;

		p.client = m_server_string;
	}

	bool web_connection_base::in_handshake() const
	{
		return m_server_string.empty();
	}

	void web_connection_base::on_sent(error_code const& error
		, std::size_t bytes_transferred)
	{
		INVARIANT_CHECK;

		if (error) return;
		m_statistics.sent_bytes(0, bytes_transferred);
	}


#if TORRENT_USE_INVARIANT_CHECKS
	void web_connection_base::check_invariant() const
	{
/*
		TORRENT_ASSERT(m_num_pieces == std::count(
			m_have_piece.begin()
			, m_have_piece.end()
			, true));
*/	}
#endif

}

