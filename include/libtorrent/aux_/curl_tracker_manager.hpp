/*

Copyright (c) 2026, Arvid Norberg
Copyright (c) 2026, The Baron Vladimir Harkonnen
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LIBTORRENT_CURL_REQUEST_MANAGER_HPP
#define LIBTORRENT_CURL_REQUEST_MANAGER_HPP
#include "libtorrent/config.hpp"

#if TORRENT_USE_CURL
#include "libtorrent/aux_/curl_tracker_request.hpp"
#include "libtorrent/aux_/intrusive_list.hpp"
#include "libtorrent/aux_/memory.hpp"
#include "libtorrent/io_context.hpp"

namespace libtorrent::aux {
class curl_pool;
class curl_tracker_request;
struct session_settings;
struct request_callback;
class tracker_manager;
struct tracker_request;

struct TORRENT_EXTRA_EXPORT curl_global_initializer {
	curl_global_initializer();
	~curl_global_initializer();
};

class curl_tracker_manager {
public:
	explicit curl_tracker_manager(tracker_manager& general_manager);

	~curl_tracker_manager();
	curl_tracker_manager(curl_tracker_manager const&) = delete;

	curl_tracker_manager& operator=(curl_tracker_manager const&) = delete;

	// the request callback shall not be called before this function returns
	void add(io_context& ios,
			tracker_request&& req,
			std::weak_ptr<request_callback> cb);

	// note: stopped events notify the tracker that this client is no longer an active peer
	void abort_all(bool abort_stopped_events = false);

	[[nodiscard]] int size()   const noexcept { return static_cast<int>(m_requests.size()); }
	[[nodiscard]] bool empty() const noexcept { return m_requests.empty(); }

	void received_bytes(int bytes);
	void sent_bytes(int bytes);

	[[nodiscard]] session_settings const& settings() const;

private:
	// two-step initialization because it needs an executor
	void initialize_pool(io_context& ios);
	std::unique_ptr<curl_tracker_request> remove(curl_tracker_request& request);
	void on_completed(CURL* handle, CURLcode result);

	// constructed first, destructed last
	curl_global_initializer m_curl_initializer;

	using request_list_t = unique_ptr_intrusive_list<curl_tracker_request>;
	request_list_t m_requests;

	std::unique_ptr<curl_pool> m_pool;

	tracker_manager& m_general_manager;
};
}
#endif //TORRENT_USE_CURL
#endif //LIBTORRENT_CURL_REQUEST_MANAGER_HPP
