/*

Copyright (c) 2025, libtorrent project
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

#include "libtorrent/curl_tracker_connection.hpp"

#ifdef TORRENT_USE_LIBCURL

#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/settings_pack.hpp"

namespace libtorrent {

curl_tracker_connection::curl_tracker_connection(
	io_context& ios
	, aux::tracker_manager& man
	, aux::tracker_request req
	, std::weak_ptr<aux::request_callback> c
	, std::shared_ptr<aux::curl_thread_manager> curl_mgr)
	: aux::http_tracker_connection(ios, man, std::move(req), c)
	, m_curl_thread_manager(std::move(curl_mgr))
{
	aux::session_settings const& session_sett = m_man.settings();
	settings_pack sett;

	sett.set_bool(settings_pack::enable_http2_trackers,
		session_sett.get_bool(settings_pack::enable_http2_trackers));
	sett.set_int(settings_pack::tracker_completion_timeout,
		session_sett.get_int(settings_pack::tracker_completion_timeout));
	sett.set_int(settings_pack::tracker_receive_timeout,
		session_sett.get_int(settings_pack::tracker_receive_timeout));
	sett.set_int(settings_pack::max_tracker_response_size,
		session_sett.get_int(settings_pack::max_tracker_response_size));
	sett.set_bool(settings_pack::tracker_ssl_verify_peer,
		session_sett.get_bool(settings_pack::tracker_ssl_verify_peer));
	sett.set_bool(settings_pack::tracker_ssl_verify_host,
		session_sett.get_bool(settings_pack::tracker_ssl_verify_host));
	sett.set_int(settings_pack::tracker_min_tls_version,
		session_sett.get_int(settings_pack::tracker_min_tls_version));

	sett.set_int(settings_pack::proxy_type,
		session_sett.get_int(settings_pack::proxy_type));
	sett.set_str(settings_pack::proxy_hostname,
		session_sett.get_str(settings_pack::proxy_hostname));
	sett.set_int(settings_pack::proxy_port,
		session_sett.get_int(settings_pack::proxy_port));
	sett.set_str(settings_pack::proxy_username,
		session_sett.get_str(settings_pack::proxy_username));
	sett.set_str(settings_pack::proxy_password,
		session_sett.get_str(settings_pack::proxy_password));
	sett.set_bool(settings_pack::proxy_tracker_connections,
		session_sett.get_bool(settings_pack::proxy_tracker_connections));
	sett.set_bool(settings_pack::proxy_hostnames,
		session_sett.get_bool(settings_pack::proxy_hostnames));

	sett.set_str(settings_pack::user_agent,
		session_sett.get_str(settings_pack::user_agent));
	sett.set_int(settings_pack::connections_limit,
		session_sett.get_int(settings_pack::connections_limit));

	m_client = std::make_unique<aux::curl_tracker_client>(
		ios,
		tracker_req().url,
		sett,
		m_curl_thread_manager);
}

void curl_tracker_connection::start()
{
	if (m_started) return;
	m_started = true;

	auto self = shared_from_this();

	if (tracker_req().kind & aux::tracker_request::scrape_request)
	{
		m_client->scrape(tracker_req(),
			[self](error_code const& ec, aux::tracker_response const& resp)
			{
				self->on_response(ec, resp);
			});
	}
	else
	{
		m_client->announce(tracker_req(),
			[self](error_code const& ec, aux::tracker_response const& resp)
			{
				self->on_response(ec, resp);
			});
	}

	aux::session_settings const& settings = m_man.settings();
	int const timeout = settings.get_int(settings_pack::tracker_completion_timeout);
	int const read_timeout = settings.get_int(settings_pack::tracker_receive_timeout);
	set_timeout(timeout, read_timeout);

	sent_bytes(0);
}

void curl_tracker_connection::close()
{
	if (m_client)
	{
		m_client->close();
	}
	cancel();
	m_man.remove_request(static_cast<aux::http_tracker_connection const*>(this));
}

void curl_tracker_connection::on_timeout([[maybe_unused]] error_code const& ec)
{
	if (!m_client) return;

	m_client->close();
	fail(errors::timed_out, operation_t::bittorrent, "tracker request timed out");
	close();
}

void curl_tracker_connection::on_response(error_code const& ec, aux::tracker_response const& resp)
{
	if (cancelled()) return;

	cancel();

	if (ec)
	{
		fail(ec, operation_t::bittorrent, ec.message().c_str());
		close();
		return;
	}

	if (auto cb = requester(); !cb)
	{
		close();
		return;
	}
	else
	{
		received_bytes(0); // We don't have exact byte count from curl

		if (tracker_req().kind & aux::tracker_request::scrape_request)
		{
			cb->tracker_scrape_response(tracker_req(), resp.complete,
				resp.incomplete, resp.downloaded, resp.downloaders);
		}
		else
		{
			// For announce, we need to provide the tracker IP and IP list
			// Since curl doesn't give us this directly, we'll use empty values
			address tracker_ip;
			std::list<address> ip_list;

			cb->tracker_response(tracker_req(), tracker_ip, ip_list, resp);
		}
	}

	close();
}

} // namespace libtorrent

#endif // TORRENT_USE_LIBCURL
