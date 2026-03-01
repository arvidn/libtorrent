/*

Copyright (c) 2026, Arvid Norberg
Copyright (c) 2026, The Baron Vladimir Harkonnen
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/curl_pool.hpp"

#if TORRENT_USE_CURL
#include "libtorrent/aux_/curl_boost_socket.hpp"
#include "libtorrent/aux_/curl_request.hpp"
#include "libtorrent/aux_/debug.hpp"
#include "libtorrent/error.hpp"
#include "libtorrent/error_code.hpp"

namespace libtorrent::aux {
namespace {
void check_multi_returncode(CURLMcode result, string_view context)
{
	TORRENT_ASSERT(result == CURLM_OK);
	if (result != CURLM_OK)
	{
		auto message = std::string(context) + ": " + curl_multi_strerror(result);

		// All CURLM errors are programming errors because errors related to network/file-descriptors
		// are returned on the easy handles. They are entirely avoidable, only CURLM_INTERNAL_ERROR is outside
		// our control. At best the pool can be recreated once an error occurs, which seems superfluous for hardening
		// against programming errors.

		// This will most likely lead to program termination, which is fine as long as it is logged.
		throw_ex<std::runtime_error>(message);
	}
}
} // anonymous namespace

int curl_pool::update_socket_shim([[maybe_unused]] CURL* easy_handle,
							curl_socket_t native_socket,
							int what,
							void* clientp,
							void* socketp)
{
	try
	{
		return update_socket(
			native_socket,
			bitmask<curl_poll_t>(what),
			static_cast<curl_pool*>(clientp),
			static_cast<curl_boost_socket*>(socketp));
	}
#if TORRENT_DEBUG_LIBCURL
	catch (const std::exception& e) {
		std::fprintf(stderr, "curl_pool::update_socket exception: %s\n", e.what());
	}
#endif
	catch (...)
	{
#if TORRENT_DEBUG_LIBCURL
		std::fprintf(stderr, "curl_pool::update_socket unknown exception\n");
#endif
	}
	return 0;
}

int curl_pool::update_socket(curl_socket_t native_socket,
							bitmask<curl_poll_t> poll_mode,
							curl_pool* pool,
							curl_boost_socket* socket)
{
	// Note: it is not allowed to call curl processing functions from inside a curl callback
	TORRENT_ASSERT(pool);

	if (poll_mode == curl_poll_t::remove)
	{
		if (socket)
		{
			// release(): curl will keep the socket in cache or close it
			socket->release_handle();
			pool->remove_socket(*socket);
		}
		return 0;
	}

	if (!socket)
	{
		error_code ec;
		auto socket_ownership = curl_boost_socket::wrap(*pool, native_socket, ec);
		TORRENT_ASSERT(socket_ownership);

		// track the socket even if it has an error
		socket = &pool->add_socket(std::move(socket_ownership));
		auto result = curl_multi_assign(pool->handle(), native_socket, socket);
		check_multi_returncode(result, "curl_multi_assign failed: ");

#if TORRENT_DEBUG_LIBCURL
		// The async operations on broken sockets will fail and notify curl. There is no need to create a new code
		// path to explicitly notify curl here.
		if (ec)
			std::fprintf(stderr, "failed to wrap curl socket %s\n", ec.to_string().c_str());
#endif
	}

	socket->set_poll_mode(poll_mode);
	return 0;
}

template<typename T, typename>
void curl_pool::setopt(CURLMoption option, T value)
{
	check_multi_returncode(
		curl_multi_setopt(handle(), option, value),
		"curl_multi_setopt(option=" + std::to_string(option) + ")");
}

curl_pool::curl_pool(const executor_type& executor)
	: m_curl_handle{curl_multi_init()}
	, m_timer(executor)
	, m_executor(executor)
{
	if (!m_curl_handle)
		throw_ex<std::runtime_error>("curl_multi_init() returned nullptr");

	setopt(CURLMOPT_SOCKETDATA, this);
	setopt<curl_socket_callback>(CURLMOPT_SOCKETFUNCTION, update_socket_shim);

	setopt(CURLMOPT_TIMERDATA, this);
	setopt<curl_multi_timer_callback>(CURLMOPT_TIMERFUNCTION, [](CURLM*, long timeout_ms, void* pool) {
		if (!pool) return 0;
		return static_cast<curl_pool*>(pool)->set_timeout(timeout_ms);
	});

	// CURLMOPT_MAX_CONCURRENT_STREAMS is 100 by default, this is a global upper limit. A server negotiates their own limit
	// using OPTION frames, until that happens the connection specific limit is 1.

	// Libtorrent uses multiple independent connections to the same host. With CURLMOPT_MAX_HOST_CONNECTIONS set to 1,
	// all independent connections need to wait for the first connection to close.  Which might never happen for
	// persistent connections. Therefore, it can't be used.
	// TODO: add new option to curl to properly control queueing and remove this line.
	// setopt(CURLMOPT_MAX_HOST_CONNECTIONS, 1l);

	// Note on asio behavior: when m_requests is empty this class will cease all asio activity. Allowing the asio
	// thread to cleanly shutdown if necessary.
	//
	// This behavior is implicit based on curl's implementation.
	// - Curl removes a socket object from m_sockets when the connection has no active transfers* (through POLL_REMOVE).
	//   This implies that curl cannot perform any read/write operations on the curl_socket_t it keeps in its
	//   private connection cache.
	// - Curl cancels the timer if there are no active transfers
	//
	// *transfers: easy_handle that is associated with a socket (a.k.a. xfer), represent by curl_request in this codebase.
}

curl_pool::~curl_pool()
{
	// The curl_multi_cleanup destructor is allowed to:
	// - update m_sockets with update_socket(event, socket)
	// It's probably a good to destroy it manually in order for this callbacks to be run while the class object is
	// still valid.
	//
	// Note that it can't be a unique_ptr:
	// - m_curl_handle should stay a valid pointer during the destructor in case it is used by the callback code.
	//   unique_ptr's destructor may reset to nullptr before the destructor is called.
	if (m_curl_handle)
	{
		if (auto error = curl_multi_cleanup(m_curl_handle))
		{
			std::fprintf(stderr, "curl_multi_cleanup failed with '%s'\n", curl_multi_strerror(error));
		}
		m_curl_handle = nullptr;
	}
}

void curl_pool::set_max_connections(int max_connections)
{
	max_connections = std::max(0, max_connections);
	setopt(CURLMOPT_MAX_TOTAL_CONNECTIONS, static_cast<long>(max_connections));
}

void curl_pool::set_max_host_connections(long value)
{
	setopt(CURLMOPT_MAX_HOST_CONNECTIONS, value);
}

// sockets notify the pool on file descriptor event
void curl_pool::socket_event(curl_boost_socket& socket, curl_cselect_t event)
{
	process_socket_action(socket.native_handle(), event);
}

void curl_pool::process_socket_action(curl_socket_t native_socket, curl_cselect_t event)
{
	int running_handles = 0; // includes curl internal handles

	// note: curl completely ignores the "event" parameter internally
	auto result = curl_multi_socket_action(handle(), native_socket, static_cast<int>(event), &running_handles);
	check_multi_returncode(result, "curl_multi_socket_action");

	process_completed_requests();
}

void curl_pool::process_completed_requests()
{
	int msgs_in_queue = 0;
	while (CURLMsg* msg = curl_multi_info_read(handle(), &msgs_in_queue))
	{
		TORRENT_ASSERT(msg->easy_handle);
		// The easy_handle belongs to one of our requests, curl does not put internal easy_handles on the message queue
		if (msg->msg == CURLMSG_DONE && m_completion_handler)
			m_completion_handler(msg->easy_handle, msg->data.result);
	}
}

void curl_pool::add_request(CURL* request)
{
	auto result = curl_multi_add_handle(handle(), request);
	check_multi_returncode(result, "curl_multi_add_handle");
	// curl creates a timeout of 0ms inside `curl_multi_add_handle` to process this new handle
}

void curl_pool::remove_request(CURL* request)
{
	auto result = curl_multi_remove_handle(handle(), request);
	check_multi_returncode(result, "curl_multi_remove_handle");
	// curl creates a timeout of 0ms inside `curl_multi_remove_handle` to process any queued items (if needed).

	// workaround for https://github.com/curl/curl/pull/20502 fixed in 8.19.0
	static bool missing_timer_bug = curl_version_lower_than(0x081300);
	if (missing_timer_bug)
	{
		set_timeout(0);
	}
}

curl_boost_socket& curl_pool::add_socket(std::unique_ptr<curl_boost_socket> socket) noexcept
{
	return m_sockets.add(std::move(socket));
}

void curl_pool::remove_socket(curl_boost_socket& socket) noexcept
{
	(void)m_sockets.remove(socket);
}

int curl_pool::set_timeout(const long timeout_ms) noexcept
{
	constexpr long cancel_timer_value = -1;
	try
	{
		if (timeout_ms == cancel_timer_value)
		{
			m_timer.cancel();
			return 0;
		}
		// depending on expires_after to cancel older timers
		m_timer.expires_after(std::chrono::milliseconds(timeout_ms));
		ADD_OUTSTANDING_ASYNC("curl_pool::set_timeout");
		m_timer.async_wait([this](const error_code& ec) {
			COMPLETE_ASYNC("curl_pool::set_timeout");
			if (ec == error::operation_aborted)
				return;
			process_socket_action();
		});
	}
#if TORRENT_DEBUG_LIBCURL
	catch (const std::exception& e) {
		std::fprintf(stderr,"curl_pool::set_timeout exception: %s\n", e.what());
	}
#endif
	catch (...)
	{
#if TORRENT_DEBUG_LIBCURL
		std::fprintf(stderr, "curl_pool::set_timeout unknown exception\n");
#endif
	}
	return 0;
}
}
#endif //TORRENT_USE_CURL
