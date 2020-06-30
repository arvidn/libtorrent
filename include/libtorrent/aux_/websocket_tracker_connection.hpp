/*

Copyright (c) 2019 Paul-Louis Ageneau
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

#ifndef TORRENT_WEBSOCKET_TRACKER_CONNECTION_HPP_INCLUDED
#define TORRENT_WEBSOCKET_TRACKER_CONNECTION_HPP_INCLUDED

#include "libtorrent/config.hpp"

#if TORRENT_USE_RTC

#include "libtorrent/aux_/rtc_signaling.hpp" // for rtc_offer and rtc_answer
#include "libtorrent/aux_/websocket_stream.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/aux_/resolver_interface.hpp"
#include "libtorrent/tracker_manager.hpp" // for tracker_connection
#include "libtorrent/ssl.hpp"

#include <boost/beast/core/flat_buffer.hpp>

#include <map>
#include <memory>
#include <queue>
#include <tuple>
#include <variant>
#include <optional>

namespace libtorrent {
namespace aux {

struct tracker_answer
{
	sha1_hash info_hash;
	peer_id pid;
	aux::rtc_answer answer;
};

struct TORRENT_EXTRA_EXPORT websocket_tracker_connection : tracker_connection
{
	friend class tracker_manager;

	websocket_tracker_connection(
		io_context& ios
		, tracker_manager& man
		, tracker_request const& req
		, std::weak_ptr<request_callback> cb);
	~websocket_tracker_connection() = default;

	void start() override;
	void close() override;

	bool is_started() const;
	bool is_open() const;

	void queue_request(tracker_request req, std::weak_ptr<request_callback> cb);
	void queue_answer(tracker_answer ans);

private:
	std::shared_ptr<websocket_tracker_connection> shared_from_this()
	{
		return std::static_pointer_cast<websocket_tracker_connection>(
			tracker_connection::shared_from_this());
	}

	std::weak_ptr<websocket_tracker_connection> weak_from_this()
	{
		return shared_from_this();
	}

	void send_pending();
	void do_send(tracker_request const& req);
	void do_send(tracker_answer const& ans);
	void do_read();
	void on_timeout(error_code const& ec) override;
	void on_connect(std::weak_ptr<websocket_tracker_connection> weak_this
			, error_code const& ec);
	void on_read(std::weak_ptr<websocket_tracker_connection> weak_this
			, error_code ec, std::size_t bytes_read);
	void on_write(std::weak_ptr<websocket_tracker_connection> weak_this
			, error_code const& ec, std::size_t bytes_written);
	void fail(operation_t op, error_code const& ec);

	io_context& m_io_context;
	ssl::context m_ssl_context;
	std::shared_ptr<aux::websocket_stream> m_websocket;
	boost::beast::flat_buffer m_read_buffer;

	using tracker_message = std::variant<tracker_request, tracker_answer>;
	std::queue<std::tuple<tracker_message, std::weak_ptr<request_callback>>> m_pending;
	std::map<sha1_hash, std::weak_ptr<request_callback>> m_callbacks;

	bool m_sending = false;
};

struct websocket_tracker_response {
	sha1_hash info_hash;
	std::optional<tracker_response> resp;
	std::optional<aux::rtc_offer> offer;
	std::optional<aux::rtc_answer> answer;
};

TORRENT_EXTRA_EXPORT std::variant<websocket_tracker_response, std::string>
	parse_websocket_tracker_response(span<char const> message, error_code &ec);

}
}

#endif // TORRENT_USE_RTC

#endif // TORRENT_WEBSOCKET_TRACKER_CONNECTION_HPP_INCLUDED

