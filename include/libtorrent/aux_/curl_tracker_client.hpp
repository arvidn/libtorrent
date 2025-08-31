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

#ifndef TORRENT_CURL_TRACKER_CLIENT_HPP
#define TORRENT_CURL_TRACKER_CLIENT_HPP

#include "libtorrent/config.hpp"

#ifdef TORRENT_USE_LIBCURL

#include "libtorrent/aux_/curl_thread_manager.hpp"
#include "libtorrent/aux_/tracker_manager.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/settings_pack.hpp"
#include <memory>
#include <string>

namespace libtorrent { namespace aux {

// Tracker response parsing functions (following http_tracker_connection pattern)
TORRENT_EXTRA_EXPORT tracker_response parse_announce_response(
	span<char const> data,
	error_code& ec);

TORRENT_EXTRA_EXPORT tracker_response parse_scrape_response(
	span<char const> data,
	error_code& ec);

// Unified libcurl-based tracker client for HTTP/1.1 and HTTP/2
// REPLACES: http_tracker_connection entirely
// FIXES: Connection reuse, timeout handling, proxy support, HTTP parsing
// ADDS: HTTP/2 support, multiplexing, proper keep-alive
class TORRENT_EXPORT curl_tracker_client {
public:
	curl_tracker_client(
		io_context& ios,
		std::string const& url,
		settings_pack const& settings,
		std::shared_ptr<curl_thread_manager> curl_mgr);
	
	~curl_tracker_client();
	
	void announce(
		tracker_request const& req,
		std::function<void(error_code const&, tracker_response const&)> handler);
	
	void scrape(
		tracker_request const& req,
		std::function<void(error_code const&, tracker_response const&)> handler);
	
	// libcurl handles connection pooling, always reusable
	bool can_reuse() const { return true; }
	
	void close();
	
private:
	std::string build_announce_url(tracker_request const& req) const;
	std::string build_scrape_url(tracker_request const& req) const;
	std::string build_tracker_query(tracker_request const& req, bool scrape = false) const;
	
	std::string scrape_url_from_announce(std::string const& announce) const;
	
private:
	io_context& m_ios;
	std::string m_tracker_url;
	settings_pack m_settings;
	std::shared_ptr<curl_thread_manager> m_curl_thread_manager;
};

}} // namespace libtorrent::aux

#endif // TORRENT_USE_LIBCURL

#endif // TORRENT_CURL_TRACKER_CLIENT_HPP