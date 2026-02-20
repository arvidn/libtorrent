/*

Copyright (c) 2026, Arvid Norberg
Copyright (c) 2026, The Baron Vladimir Harkonnen
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/curl_tracker_request.hpp"

#if TORRENT_USE_CURL
#include "libtorrent/aux_/curl_tracker_manager.hpp"
#include "libtorrent/aux_/http_tracker_connection.hpp"
#include "libtorrent/aux_/parse_url.hpp"
#include "libtorrent/aux_/tracker_manager.hpp"
#include "libtorrent/session_settings.hpp"
#include "libtorrent/settings_pack.hpp"

namespace libtorrent::aux {
curl_tracker_request::curl_tracker_request(
	curl_tracker_manager& owner,
	tracker_request&& req,
	std::weak_ptr<request_callback> c)
	: m_params(std::make_unique<const tracker_request>(std::move(req)))
	, m_owner(owner)
	, m_request(owner.settings().get_int(settings_pack::max_http_recv_buffer_size))
	, m_callback(std::move(c))
{
}

bool curl_tracker_request::is_stopped_event() const noexcept
{
	return m_params->event == event_t::stopped;
}

curl_tracker_request::error_type curl_tracker_request::initialize_request()
{
	error_type result;
	m_request.set_defaults();
	m_request.set_private_data(this);

	if (!m_params->outgoing_socket)
	{
		result.ec = errors::invalid_listen_socket;
		result.op = operation_t::get_interface;
		result.message = "outgoing socket was closed";
		return result;
	}

	const auto& settings = m_owner.settings();

	constexpr bool i2p = false;
	const std::string url = build_tracker_url(*m_params, settings, i2p, result.ec);
	if (result.ec)
		return result;
	m_request.set_url(url);

	const auto bind_device = m_params->outgoing_socket.device();
	const auto bind_address = m_params->outgoing_socket.get_local_endpoint().address();
	if (!m_request.bind(bind_device, bind_address))
	{
		result.ec = errors::invalid_listen_socket;
		result.op = operation_t::get_interface;
		result.message = "could not bind to device '"+ bind_device +"' with ip '"+ bind_address.to_string() +"'";
		return result;
	}

	// in anonymous mode we omit the user agent to mitigate fingerprinting of
	// the client. Private torrents is an exception because some private
	// trackers may require the user agent
	bool const anon_user = settings.get_bool(settings_pack::anonymous_mode) && !m_params->private_torrent;
	m_request.set_user_agent(anon_user ? "curl/7.81.0" : settings.get_str(settings_pack::user_agent));

	int const timeout = m_params->event == event_t::stopped
							? settings.get_int(settings_pack::stop_tracker_timeout)
							: settings.get_int(settings_pack::tracker_completion_timeout);
	m_request.set_timeout(seconds32(timeout));

	if (!settings.get_bool(settings_pack::validate_https_trackers))
	{
		m_request.set_ssl_verify_host(false);
		m_request.set_ssl_verify_peer(false);
	}

	m_request.set_ssrf_mitigation(settings.get_bool(settings_pack::ssrf_mitigation));
	m_request.set_ip_filter(m_params->filter);

	auto [protocol, auth, hostname, port, path]
			= parse_url_components(url, result.ec);

	if (result.ec)
	{
		result.op = operation_t::parse_address;
		return result;
	}

	if (!settings.get_bool(settings_pack::allow_idna) && is_idna(hostname))
	{
		result.ec = errors::blocked_by_idna;
		return result;
	}

#if TORRENT_ABI_VERSION == 1
	if (auth.empty())
	{
		m_request.set_userpwd(m_params->auth);
	}
#endif

	proxy_settings ps(settings);
	if (ps.proxy_tracker_connections && ps.type != settings_pack::none)
	{
		m_request.set_proxy(ps, settings.get_bool(settings_pack::validate_https_trackers));

		// assume proxy can connect to both ipv4/ipv6
		m_request.set_ipresolve(CURL_IPRESOLVE_WHATEVER);
	}
	else if (!bind_address.is_unspecified())
	{
		if (bind_address.is_v4())
			m_request.set_ipresolve(CURL_IPRESOLVE_V4);
		else
			m_request.set_ipresolve(CURL_IPRESOLVE_V6);
	}

#ifndef TORRENT_DISABLE_LOGGING
	if (auto cb = requester())
	{
		cb->debug_log("==> TRACKER_REQUEST [ url: %s ]", url.c_str());
	}
#endif

	return result;
}

void curl_tracker_request::complete(CURLcode result)
{
	m_owner.sent_bytes(static_cast<int>(m_request.get_request_size()));

	auto total_size = m_request.get_compressed_body_size() + m_request.get_header_size();
	m_owner.received_bytes(static_cast<int>(total_size));

	if (result != CURLE_OK)
	{
		auto error_status = m_request.get_error(result);
		return fail(error_status);
	}
	on_response();
}

void curl_tracker_request::fail(const error_code& ec,
								operation_t op,
								string_view message,
								seconds32 retry_delay)
{
	std::shared_ptr<request_callback> cb = m_callback.lock();
	if (!cb)
		return;

	if (op == operation_t::unknown)
		op = operation_t::bittorrent;

	const std::string msg{message};
	cb->tracker_request_error(*m_params, ec, op, msg, retry_delay);
}

void curl_tracker_request::on_response()
{
	if (auto status = m_request.http_status(); status != errors::http_errors::ok)
		return fail(error_code(status, http_category()), operation_t::unknown, {});

	std::shared_ptr<request_callback> cb = m_callback.lock();
	if (!cb)
		return;

	const auto data = m_request.data();
	error_code ec;
	tracker_response resp = parse_tracker_response(data, ec,
													m_params->kind, m_params->info_hash);

	resp.interval = std::max(resp.interval,
							seconds32{m_owner.settings().get_int(settings_pack::min_announce_interval)});

	if (!resp.warning_message.empty())
		cb->tracker_warning(*m_params, resp.warning_message);

	if (ec)
	{
		const seconds32 retry_delay = std::max(resp.interval, resp.min_interval);
		return fail(ec, operation_t::unknown, resp.failure_reason, retry_delay);
	}

	// do slightly different things for scrape requests
	if (m_params->kind & tracker_request::scrape_request)
	{
		cb->tracker_scrape_response(*m_params, resp.complete
									, resp.incomplete, resp.downloaded, resp.downloaders);
	}
	else
	{
		std::list<address> ip_list;
		const address ip = m_request.get_ip(ec);
		if (!ec)
			ip_list.push_back(ip);

		cb->tracker_response(*m_params, ip, ip_list, resp);
	}
}
}
#endif //TORRENT_USE_CURL
