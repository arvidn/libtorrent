/*

Copyright (c) 2026, Arvid Norberg
Copyright (c) 2026, The Baron Vladimir Harkonnen
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/curl_tracker_manager.hpp"

#if TORRENT_USE_CURL
#include "libtorrent/aux_/curl.hpp"
#include "libtorrent/aux_/curl_pool.hpp"
#include "libtorrent/aux_/curl_request.hpp"
#include "libtorrent/aux_/session_interface.hpp"
#include "libtorrent/aux_/tracker_manager.hpp"

namespace libtorrent::aux {
curl_global_initializer::curl_global_initializer()
{
	auto result = curl_global_init(CURL_GLOBAL_DEFAULT);
	if (result != CURLE_OK)
	{
		TORRENT_ASSERT_FAIL();
		throw std::runtime_error("curl_global_init() failed: " + std::to_string(result));
	}
}

curl_global_initializer::~curl_global_initializer()
{
	curl_global_cleanup();
}

curl_tracker_manager::curl_tracker_manager(tracker_manager& general_manager)
	: m_general_manager(general_manager)
{
	const curl_version_info_data* ver = curl_version_info(CURLVERSION_NOW);
	if (ver && !ver->ares_num)
	{
		constexpr auto message = "WARNING: curl DNS lookups are using standard thread blocking OS functions "
						   "(e.g. getaddrinfo) because this version of curl is not compiled against c-ares.";
		static std::once_flag error_logged;
		std::call_once(error_logged, [] {
			std::fprintf(stderr, "%s\n", message);
		});

#ifndef TORRENT_DISABLE_LOGGING
		if (auto& ses = m_general_manager.get_session(); ses.should_log())
			ses.session_log("*** CURL_TRACKER_MANAGER %s", message);
#endif
	}
}

void curl_tracker_manager::initialize_pool(io_context& ios)
{
	if (m_pool) return;
	m_pool = std::make_unique<curl_pool>(ios.get_executor());
	m_pool->set_completion_callback([this](CURL* request, CURLcode result) {
		on_completed(request, result);
	});
}

curl_tracker_manager::~curl_tracker_manager()
{
	// note that removing the requests before destroying m_pool is the recommended cleanup order
	abort_all(true);
	TORRENT_ASSERT(m_requests.empty());
}

void curl_tracker_manager::received_bytes(int bytes)
{
	return m_general_manager.received_bytes(bytes);
}

void curl_tracker_manager::sent_bytes(int bytes)
{
	return m_general_manager.sent_bytes(bytes);
}

session_settings const& curl_tracker_manager::settings() const
{
	return m_general_manager.settings();
}

void curl_tracker_manager::add(io_context& ios,
								tracker_request&& req,
								std::weak_ptr<request_callback> cb)
{
	initialize_pool(ios);
	TORRENT_ASSERT(m_pool);

	// in case the setting was updated
	m_pool->set_max_connections(settings().get_int(settings_pack::max_concurrent_http_announces));

	auto request_ownership = std::make_unique<curl_tracker_request>(*this, std::move(req), std::move(cb));
	curl_tracker_request::error_type result;

	try
	{
		// two-step initialization to avoid throwing inside constructor
		// - protects the lifetimes of req and cb
		// - reusable fail() function
		result = request_ownership->initialize_request();
	}
	catch (const curl_easy_error& error)
	{
		if (error.code() == CURLE_BAD_FUNCTION_ARGUMENT)
		{
			// string too long, binding on a bad ip address
			result.ec = boost::asio::error::invalid_argument;
		}
		else if (error.code() == CURLE_NOT_BUILT_IN || error.code() == CURLE_UNKNOWN_OPTION)
		{
			// CURLE_UNKNOWN_OPTION: curl not build without feature support (IPv6, HTTP, Proxies, TLS, c-aris)
			// CURLE_NOT_BUILT_IN: curl dependency is missing support for feature
			result.ec = boost::asio::error::operation_not_supported;
		}
		result.message = error.what();
	}

	if (result.ec)
	{
		// async to avoid recursive calls towards our caller (through the completion handler)
		post(ios, [request_ownership = std::move(request_ownership), result = std::move(result)]() {
			request_ownership->fail(result);
		});
		return;
	}

	auto& request = m_requests.add(std::move(request_ownership));
	m_pool->add_request(request.get_curl_request().handle());
}

std::unique_ptr<curl_tracker_request> curl_tracker_manager::remove(curl_tracker_request& request)
{
	TORRENT_ASSERT(m_pool);
	m_pool->remove_request(request.get_curl_request().handle());
	return m_requests.remove(request);
}

// this is not triggered from within add() to prevent executing a parent callback inside their function call to add()
void curl_tracker_manager::on_completed(CURL* handle, CURLcode result)
{
	auto request = curl_tracker_request::from_handle(handle);
	TORRENT_ASSERT(request);

	auto request_ownership = remove(*request);
	request_ownership->complete(result);
}

void curl_tracker_manager::abort_all(bool abort_stopped_events)
{
	for (auto it = std::begin(m_requests); it != std::end(m_requests);)
	{
		if (abort_stopped_events || !it->is_stopped_event())
		{
#ifndef TORRENT_DISABLE_LOGGING
			std::shared_ptr<request_callback> rc = it->requester();
			if (rc) rc->debug_log("aborting: %s", it->get_params().url.c_str());
#endif

			// m_pool->remove NOT does invoke callbacks (like on_completed) which means the other iterators remain valid
			// Note that this does not trigger the completion handlers (same as reference implementation)
			(void) remove(*(it++));
		}
		else
		{
			++it;
		}
	}
}
}
#endif //TORRENT_USE_CURL
