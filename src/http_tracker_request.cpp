/*

Copyright (c) 2004-2026, Arvid Norberg
Copyright (c) 2004, Magnus Jonsson
Copyright (c) 2015, Mikhail Titov
Copyright (c) 2016-2018, 2020-2021, Alden Torres
Copyright (c) 2016, Andrei Kurushin
Copyright (c) 2016-2018, Steven Siloti
Copyright (c) 2017, Pavel Pimenov
Copyright (c) 2020, Paul-Louis Ageneau
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/http_tracker_request.hpp"

#include <string>
#include <list>
#include <cstdio> // for snprintf
#include <cinttypes> // for PRId64 et.al.

#include "libtorrent/aux_/array.hpp"
#include "libtorrent/aux_/escape_string.hpp"
#include "libtorrent/aux_/http_tracker_connection.hpp"
#include "libtorrent/aux_/parse_url.hpp"
#include "libtorrent/aux_/string_util.hpp"
#include "libtorrent/aux_/tracker_manager.hpp"
#include "libtorrent/i2p_stream.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/string_view.hpp"

namespace libtorrent::aux {

using error_type = http_tracker_request::error_type;

error_type http_tracker_request::validate_socket(bool i2p) const
{
	// i2p trackers don't use our outgoing sockets, they use the SAM connection
	if (!i2p && !m_params.outgoing_socket)
	{
		return {errors::invalid_listen_socket, operation_t::get_interface
			, "outgoing socket was closed"};
	}
	return {};
}

std::string http_tracker_request::get_user_agent() const
{
	// in anonymous mode we omit the user agent to mitigate fingerprinting of
	// the client. Private torrents is an exception because some private
	// trackers may require the user agent
	bool const anon_user = m_settings.get_bool(settings_pack::anonymous_mode)
		&& !m_params.private_torrent;
	return anon_user
		? "curl/7.81.0"
		: m_settings.get_str(settings_pack::user_agent);
}

seconds32 http_tracker_request::get_timeout() const
{
	const auto timeout = m_params.event == event_t::stopped
		? m_settings.get_int(settings_pack::stop_tracker_timeout)
		: m_settings.get_int(settings_pack::tracker_completion_timeout);
	return seconds32{timeout};
}

error_type http_tracker_request::process_response(
	request_callback& cb,
	const address& tracker_ip,
	const std::list<address>& ip_list,
	const span<char const> data) const
{
	error_code ecode;
	tracker_response resp = parse_tracker_response(data, ecode, m_params.kind, m_params.info_hash);

	resp.interval = std::max(resp.interval,
		seconds32{m_settings.get_int(settings_pack::min_announce_interval)});

	if (!resp.warning_message.empty())
		cb.tracker_warning(m_params, resp.warning_message);

	if (ecode)
	{
		return {ecode,
			operation_t::bittorrent,
			resp.failure_reason,
			resp.interval,
			resp.min_interval};
	}

	// do slightly different things for scrape requests
	if (m_params.kind & tracker_request::scrape_request)
	{
		cb.tracker_scrape_response(m_params, resp.complete
			, resp.incomplete, resp.downloaded, resp.downloaders);
	}
	else
	{
		cb.tracker_response(m_params, tracker_ip, ip_list, resp);
	}

	return {};
}

#ifndef TORRENT_DISABLE_LOGGING
void http_tracker_request::log_request(request_callback& cb, const std::string& url) const
{
	cb.debug_log("==> TRACKER_REQUEST [ url: %s ]", url.c_str());
}
#endif

std::string http_tracker_request::build_tracker_url(bool i2p, error_type& error) const
{
	std::string url = m_params.url;

	if (m_params.kind & tracker_request::scrape_request)
	{
		// find and replace "announce" with "scrape"
		// in request
		std::size_t pos = url.find("announce");
		if (pos == std::string::npos) {
			error.code = errors::scrape_not_available;
			return {};
		}
		url.replace(pos, 8, "scrape");
	}

	// if request-string already contains
	// some parameters, append an ampersand instead
	// of a question mark
	auto const arguments_start = url.find('?');
	if (arguments_start != std::string::npos)
	{
		// tracker URLs that come pre-baked with query string arguments will be
		// rejected when SSRF-mitigation is enabled
		bool const ssrf_mitigation = m_settings.get_bool(settings_pack::ssrf_mitigation);
		if (ssrf_mitigation && has_tracker_query_string(string_view(url).substr(arguments_start + 1)))
		{
			error.code = errors::ssrf_mitigation;
			return {};
		}
		url += "&";
	}
	else
	{
		url += "?";
	}

	url += "info_hash=";
	url += lt::escape_string({m_params.info_hash.data(), 20});

	if (!(m_params.kind & tracker_request::scrape_request))
	{
		static array<const char*, 4> const event_string{{{"completed", "started", "stopped", "paused"}}};

		char str[1024];
		std::snprintf(str, sizeof(str)
			, "&peer_id=%s"
			"&port=%d"
			"&uploaded=%" PRId64
			"&downloaded=%" PRId64
			"&left=%" PRId64
			"&corrupt=%" PRId64
			"&key=%08X"
			"%s%s" // event
			"&numwant=%d"
			"&compact=1"
			"&no_peer_id=1"
			, lt::escape_string({m_params.pid.data(), 20}).c_str()
			// the i2p tracker seems to verify that the port is not 0,
			// even though it ignores it otherwise
			, m_params.listen_port
			, m_params.uploaded
			, m_params.downloaded
			, m_params.left
			, m_params.corrupt
			, m_params.key
			, (m_params.event != event_t::none) ? "&event=" : ""
			, (m_params.event != event_t::none) ? event_string[static_cast<int>(m_params.event) - 1] : ""
			, m_params.num_want);
		url += str;
#if !defined TORRENT_DISABLE_ENCRYPTION
		if (m_settings.get_int(settings_pack::in_enc_policy) != settings_pack::pe_disabled
			&& m_settings.get_bool(settings_pack::announce_crypto_support))
			url += "&supportcrypto=1";
#endif
		if (m_settings.get_bool(settings_pack::report_redundant_bytes))
		{
			url += "&redundant=";
			url += to_string(m_params.redundant).data();
		}
		if (!m_params.trackerid.empty())
		{
			url += "&trackerid=";
			url += lt::escape_string(m_params.trackerid);
		}

#if TORRENT_USE_I2P
		if (i2p && m_params.i2pconn)
		{
			if (m_params.i2pconn->local_endpoint().empty())
			{
				error.code = errors::no_i2p_endpoint;
				error.failure_reason = "Waiting for i2p acceptor from SAM bridge";
				error.interval = seconds32(5);
				return {};
			}
			else
			{
				url += "&ip=" + m_params.i2pconn->local_endpoint () + ".i2p";
			}
		}
		else
#endif
			if (!m_settings.get_bool(settings_pack::anonymous_mode))
			{
				std::string const& announce_ip = m_settings.get_str(settings_pack::announce_ip);
				if (!announce_ip.empty())
				{
					url += "&ip=" + lt::escape_string(announce_ip);
				}
			}
	}

	if (!m_params.ipv4.empty() && !i2p)
	{
		for (auto const& v4 : m_params.ipv4)
		{
			std::string const ip = v4.to_string();
			url += "&ipv4=";
			url += lt::escape_string(ip);
		}
	}
	if (!m_params.ipv6.empty() && !i2p)
	{
		for (auto const& v6 : m_params.ipv6)
		{
			std::string const ip = v6.to_string();
			url += "&ipv6=";
			url += lt::escape_string(ip);
		}
	}

	return url;
}

bool http_tracker_request::allowed_by_idna(string_view hostname) const
{
	if (m_settings.get_bool(settings_pack::allow_idna))
		return true;
	return !is_idna(hostname);
}

namespace {
template<typename T>
address get_address(const T& obj) {
	if constexpr (std::is_same_v<std::decay_t<T>, address>)
	{
		return obj;
	}
	else if constexpr (std::is_same_v<std::decay_t<T>, tcp::endpoint>)
	{
		return obj.address();
	}
	else
	{
		static_assert(false);
	}
}
} // anonymous namespace

template<typename T>
error_type http_tracker_request::filter_impl(std::weak_ptr<request_callback>& logger, std::vector<T>& endpoints, string_view url) const
{
	bool const ssrf_mitigation = m_settings.get_bool(settings_pack::ssrf_mitigation);
	const bool has_loopback = std::find_if(endpoints.begin(), endpoints.end()
		, [](auto const& ep) { return get_address(ep).is_loopback(); }) != endpoints.end();

	if (ssrf_mitigation && has_loopback)
	{
		// there is at least one loopback address in here. If the request
		// path for this tracker is not /announce. filter all loopback
		// addresses.
		std::string path;

		error_code ec;
		std::tie(std::ignore, std::ignore, std::ignore, std::ignore, path)
			= parse_url_components(url, ec);
		if (ec)
		{
			return {ec, operation_t::parse_address};
		}

		// mitigation for Server Side request forgery. Any tracker
		// announce to localhost need to look like a standard BitTorrent
		// announce
		if (!is_announce_path(path))
		{
			for (auto i = endpoints.begin(); i != endpoints.end();)
			{
				if (get_address(*i).is_loopback())
					i = endpoints.erase(i);
				else
					++i;
			}
		}

		if (endpoints.empty())
		{
			return {errors::ssrf_mitigation, operation_t::bittorrent};
		}
	}

	if (!m_params.filter) return {};

	// remove endpoints that are filtered by the IP filter
	for (auto i = endpoints.begin(); i != endpoints.end();)
	{
		if (m_params.filter->access(get_address(*i)) == ip_filter::blocked)
			i = endpoints.erase(i);
		else
			++i;
	}

#ifndef TORRENT_DISABLE_LOGGING
	if (auto cb = logger.lock())
	{
		cb->debug_log("*** TRACKER_FILTER");
	}
#endif

	if (endpoints.empty())
		return {errors::banned_by_ip_filter, operation_t::bittorrent};

	return {};
}

error_type http_tracker_request::filter(std::weak_ptr<request_callback>& logger, std::vector<tcp::endpoint>& endpoints, string_view url) const
{
	return filter_impl(logger, endpoints, url);
}

error_type http_tracker_request::filter(std::weak_ptr<request_callback>& logger, const address& ip, string_view url) const
{
	std::vector endpoints = {ip};
	return filter_impl(logger, endpoints, url);
}
}
