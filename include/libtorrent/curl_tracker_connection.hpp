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

#ifndef TORRENT_CURL_TRACKER_CONNECTION_HPP
#define TORRENT_CURL_TRACKER_CONNECTION_HPP

#include "libtorrent/config.hpp"

#ifdef TORRENT_USE_LIBCURL

#include "libtorrent/aux_/http_tracker_connection.hpp"
#include "libtorrent/aux_/curl_tracker_client.hpp"
#include <memory>

namespace libtorrent {

namespace aux {
	class curl_thread_manager;
}

// Wrapper class that adapts curl_tracker_client to the tracker_connection interface
// This allows seamless integration with tracker_manager without major refactoring
class TORRENT_EXTRA_EXPORT curl_tracker_connection
	: public aux::http_tracker_connection
{
	friend class aux::tracker_manager;
public:
	curl_tracker_connection(
		io_context& ios
		, aux::tracker_manager& man
		, aux::tracker_request req
		, std::weak_ptr<aux::request_callback> c
		, std::shared_ptr<aux::curl_thread_manager> curl_mgr);

	void start() override;
	void close() override;

private:
	[[nodiscard]] std::shared_ptr<curl_tracker_connection> shared_from_this()
	{
		return std::static_pointer_cast<curl_tracker_connection>(
			aux::tracker_connection::shared_from_this());
	}

	void on_timeout(error_code const& ec) override;

	void on_response(error_code const& ec, aux::tracker_response const& resp);

	std::unique_ptr<aux::curl_tracker_client> m_client;
	std::shared_ptr<aux::curl_thread_manager> m_curl_thread_manager;
	bool m_started = false;
};

} // namespace libtorrent

#endif // TORRENT_USE_LIBCURL

#endif // TORRENT_CURL_TRACKER_CONNECTION_HPP