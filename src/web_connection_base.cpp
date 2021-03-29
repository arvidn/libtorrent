/*

Copyright (c) 2010, 2013-2017, 2019-2020, Arvid Norberg
Copyright (c) 2016, Andrei Kurushin
Copyright (c) 2017, Steven Siloti
Copyright (c) 2019-2021, Alden Torres
Copyright (c) 2020, Paul-Louis Ageneau
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/config.hpp"

#include <limits>
#include <cstdlib>

#include "libtorrent/aux_/web_connection_base.hpp"
#include "libtorrent/aux_/invariant_check.hpp"
#include "libtorrent/aux_/parse_url.hpp"
#include "libtorrent/peer_info.hpp"

namespace lt::aux {

	web_connection_base::web_connection_base(
		peer_connection_args& pack
		, aux::web_seed_t const& web)
		: peer_connection(pack)
		, m_first_request(true)
		, m_ssl(false)
		, m_external_auth(web.auth)
		, m_extra_headers(web.extra_headers)
		, m_parser(aux::http_parser::dont_parse_chunks)
		, m_body_start(0)
	{
		TORRENT_ASSERT(&web.peer_info == pack.peerinfo);
		// when going through a proxy, we don't necessarily have an endpoint here,
		// since the proxy might be resolving the hostname, not us
		TORRENT_ASSERT(web.endpoints.empty() || web.endpoints.front() == pack.endp);

		INVARIANT_CHECK;

		TORRENT_ASSERT(is_outgoing());

		TORRENT_ASSERT(!m_torrent.lock()->is_upload_only());

		// we only want left-over bandwidth
		// TODO: introduce a web-seed default class which has a low download priority

		std::string protocol;
		error_code ec;
		std::tie(protocol, m_basic_auth, m_host, m_port, m_path)
			= parse_url_components(web.url, ec);
		TORRENT_ASSERT(!ec);

		if (m_port == -1 && protocol == "http")
			m_port = 80;

#if TORRENT_USE_SSL
		if (protocol == "https")
		{
			m_ssl = true;
			if (m_port == -1) m_port = 443;
		}
#endif

		if (!m_basic_auth.empty())
			m_basic_auth = base64encode(m_basic_auth);

		m_server_string = m_host;
	}

	int web_connection_base::timeout() const
	{
		// since this is a web seed, change the timeout
		// according to the settings.
		return m_settings.get_int(settings_pack::urlseed_timeout);
	}

	void web_connection_base::start()
	{
		// avoid calling torrent::set_seed because it calls torrent::check_invariant
		// which fails because the m_num_connecting count is not consistent until
		// after we call peer_connection::start
		m_upload_only = true;
		peer_connection::start();
		// disconnect_if_redundant must be called after start to keep
		// m_num_connecting consistent
		disconnect_if_redundant();
	}

	web_connection_base::~web_connection_base() = default;

	void web_connection_base::on_connected()
	{
		auto t = associated_torrent().lock();
		TORRENT_ASSERT(t);

		// it is always possible to request pieces
		incoming_unchoke();

		m_recv_buffer.reset(t->block_size() + 1024);
	}

	void web_connection_base::add_headers(std::string& request
		, aux::session_settings const& sett, bool const using_proxy) const
	{
		request += "Host: ";
		request += m_host;
		if ((m_first_request || m_settings.get_bool(settings_pack::always_send_user_agent))
			&& !m_settings.get_bool(settings_pack::anonymous_mode))
		{
			request += "\r\nUser-Agent: ";
			request += m_settings.get_str(settings_pack::user_agent);
		}
		if (!m_external_auth.empty())
		{
			request += "\r\nAuthorization: ";
			request += m_external_auth;
		}
		else if (!m_basic_auth.empty())
		{
			request += "\r\nAuthorization: Basic ";
			request += m_basic_auth;
		}
		if (sett.get_int(settings_pack::proxy_type) == settings_pack::http_pw)
		{
			request += "\r\nProxy-Authorization: Basic ";
			request += base64encode(sett.get_str(settings_pack::proxy_username)
				+ ":" + sett.get_str(settings_pack::proxy_password));
		}
		for (auto const& h : m_extra_headers)
		{
			request += "\r\n";
			request += h.first;
			request += ": ";
			request += h.second;
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
		if (is_connecting()) p.flags |= peer_info::connecting;

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
		sent_bytes(0, int(bytes_transferred));
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
