/*

Copyright (c) 2026, Arvid Norberg
Copyright (c) 2026, The Baron Vladimir Harkonnen
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/curl_tracker_manager.hpp"

#if TORRENT_USE_CURL
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

	auto request_ownership = std::make_unique<curl_tracker_request>(*this,
		std::move(req),
		std::move(cb),
		ios.get_executor());

	curl_tracker_request::error_type error;
	try
	{
		// two-step initialization to avoid throwing inside constructor
		// - protects the lifetimes of req and cb
		// - able to call fail() function
		error = request_ownership->initialize_request();
	}
	catch (const curl_easy_error& ex)
	{
		if (ex.code() == CURLE_BAD_FUNCTION_ARGUMENT)
		{
			// string too long, binding on a bad ip address
			error.code = boost::asio::error::invalid_argument;
		}
		else if (ex.code() == CURLE_NOT_BUILT_IN || ex.code() == CURLE_UNKNOWN_OPTION)
		{
			// CURLE_UNKNOWN_OPTION: curl not build without feature support (IPv6, HTTP, Proxies, TLS, c-aris)
			// CURLE_NOT_BUILT_IN: curl dependency is missing support for feature
			error.code = boost::asio::error::operation_not_supported;
		}
		else
		{
			error.code = boost::asio::error::no_recovery;
		}
		error.failure_reason = ex.what();
	}

	if (error)
	{
		// async to avoid recursive calls towards our caller (through the completion handler)
		post(ios, [request_ownership = std::move(request_ownership), result = std::move(error)]() {
			request_ownership->fail(result);
			// request gets deleted
		});
		return;
	}

	request_ownership->get_curl_request().set_timeout_callback(on_timeout);

	auto& request = m_requests.add(std::move(request_ownership));
	m_pool->add_request(request.get_curl_request().handle());
}

std::unique_ptr<curl_tracker_request> curl_tracker_manager::remove(curl_tracker_request& request)
{
	TORRENT_ASSERT(m_pool);
	m_pool->remove_request(request.get_curl_request().handle());
	return m_requests.remove(request);
}

void curl_tracker_manager::follow_redirect(curl_tracker_request& request)
{
	m_pool->remove_request(request.get_curl_request().handle());
	if (auto error = request.prepare_redirect())
	{
		auto request_ownership = m_requests.remove(request);
		request_ownership->fail(error);
		// ownership goes out of scope
		return;
	}
	m_pool->add_request(request.get_curl_request().handle());
}

void curl_tracker_manager::on_completed(CURL* handle, CURLcode result)
{
	auto request = curl_tracker_request::from_handle(handle);
	TORRENT_ASSERT(request);

	if (result == CURLE_OK && request->is_redirect_response())
	{
		follow_redirect(*request);
		return;
	}

	auto request_ownership = remove(*request);
	request_ownership->complete(result);
	// ownership goes out of scope
}

void curl_tracker_manager::on_timeout(curl_request& crequest)
{
	auto request = curl_tracker_request::from_handle(crequest.handle());
	TORRENT_ASSERT(request);

	auto& manager = request->get_owner();
	auto request_ownership = manager.remove(*request);

	curl_tracker_request::error_type error = {errors::timed_out};
	request_ownership->fail(error);
	// ownership goes out of scope
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

			// remove() leaves all other iterators intact
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
