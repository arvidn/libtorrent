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
	m_request.set_defaults();
	m_request.set_private_data(this);

	const auto& settings = m_owner.settings();
	constexpr bool i2p = false; // unsupported

	http_tracker_request_common common{*m_params, settings};
	auto error = common.validate_socket(i2p);
	if (error)
	{
		return error;
	}

	const std::string url = common.build_tracker_url(i2p, error);
	if (error)
	{
		return error;
	}
	m_request.set_url(url);

	const auto bind_device = m_params->outgoing_socket.device();
	const auto bind_address = m_params->outgoing_socket.get_local_endpoint().address();
	if (!m_request.bind(bind_device, bind_address))
	{
		error.code = errors::invalid_listen_socket;
		error.op = operation_t::get_interface;
		error.failure_reason = "could not bind to device '"+ bind_device +"' with ip '"+ bind_address.to_string() +"'";
		return error;
	}

	m_request.set_user_agent(common.get_user_agent());
	m_request.set_timeout(common.get_timeout());

	if (!settings.get_bool(settings_pack::validate_https_trackers))
	{
		m_request.set_ssl_verify_host(false);
		m_request.set_ssl_verify_peer(false);
	}

	m_request.set_ssrf_mitigation(settings.get_bool(settings_pack::ssrf_mitigation));
	m_request.set_ip_filter(m_params->filter);

#if TORRENT_ABI_VERSION == 1
	if (!m_params->auth.empty())
	{
		auto [protocol, auth, hostname, port, path]
				= parse_url_components(url, error.code);

		if (error)
		{
			error.op = operation_t::parse_address;
			return error;
		}

		if (auth.empty())
		{
			m_request.set_userpwd(m_params->auth);
		}
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
		common.log_request(*cb, url);
	}
#endif

	return error;
}

void curl_tracker_request::complete(CURLcode result)
{
	m_owner.sent_bytes(static_cast<int>(m_request.get_request_size()));

	auto total_size = m_request.get_compressed_body_size() + m_request.get_header_size();
	m_owner.received_bytes(static_cast<int>(total_size));

	if (result != CURLE_OK)
	{
		auto error = m_request.get_error(result);
		return fail(error.ec, error.op, error.message);
	}
	on_response();
}

void curl_tracker_request::fail(const error_code& ec,
								operation_t op,
								string_view message,
								seconds32 interval,
								seconds32 min_interval)
{
	std::shared_ptr<request_callback> cb = m_callback.lock();
	if (!cb)
		return;

	if (interval == seconds32{0})
		interval = min_interval;

	if (op == operation_t::unknown)
		op = operation_t::bittorrent;

	const std::string msg{message};
	cb->tracker_request_error(*m_params, ec, op, msg, interval);
}

void curl_tracker_request::on_response()
{
	if (auto status = m_request.http_status(); status != errors::http_errors::ok)
		return fail(error_code(status, http_category()), operation_t::unknown, {});

	std::shared_ptr<request_callback> cb = m_callback.lock();
	if (!cb)
		return;

	error_code ip_error;
	const address ip = m_request.get_ip(ip_error);

	// the reference implementation returns the list of resolved ip addresses but curl does not expose this information.
	// Not a problem because it is only used for a debug_log (it is probably cleaner to remove it entirely).
	std::list<address> ip_list;
	if (!ip_error)
		ip_list.push_back(ip);

	http_tracker_request_common common{*m_params, m_owner.settings()};
	common.process_response(*cb, ip, ip_list, m_request.data());
}
}
#endif //TORRENT_USE_CURL
