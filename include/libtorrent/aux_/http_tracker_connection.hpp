/*

Copyright (c) 2004, 2006-2009, 2012, 2014-2017, 2019-2021, Arvid Norberg
Copyright (c) 2016, 2020-2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_HTTP_TRACKER_CONNECTION_HPP_INCLUDED
#define TORRENT_HTTP_TRACKER_CONNECTION_HPP_INCLUDED

#include <vector>
#include <memory>
#include <deque>
#include <utility>
#include <cstdint>

#include "libtorrent/config.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/tracker_manager.hpp" // for tracker_connection

namespace libtorrent {

	struct bdecode_node;
}

namespace libtorrent::aux {

	struct http_connection;
	class http_parser;
	struct peer_entry;

	class TORRENT_EXTRA_EXPORT http_tracker_connection
		: public tracker_connection
	{
	friend class tracker_manager;
	public:

		http_tracker_connection(
			io_context& ios
			, tracker_manager& man
			, tracker_request req
			, std::weak_ptr<request_callback> c);

		void start() override;
		void close() override;

		// coalesce another announce/scrape onto this connection, to be issued
		// sequentially (keep-alive) after the in-flight request completes. Only
		// called for requests to the same server (see tracker_manager). Accessed
		// by tracker_manager (a friend).
		void queue_request(tracker_request req, std::weak_ptr<request_callback> c);

		// drop queued followers when aborting. With keep_stopped, stop-requests
		// are kept (close() then re-dispatches them, since they must still be
		// announced while shutting down) and the rest dropped; otherwise all are
		// dropped. Used by tracker_manager::abort_all_requests.
		void prune_followers(bool keep_stopped);

	private:

		std::shared_ptr<http_tracker_connection> shared_from_this()
		{
			return std::static_pointer_cast<http_tracker_connection>(
				tracker_connection::shared_from_this());
		}

		// build the announce/scrape URL from the in-flight request (tracker_req())
		// and issue it on m_tracker_connection (creating it on the first call).
		// Reused for each follower, relying on http_connection's keep-alive reuse.
		void send_request();

		// after a request completes, promote the next queued follower and reissue
		// it on this connection, or close the connection if the queue is empty.
		void next_request();

		void on_filter(aux::http_connection& c, std::vector<tcp::endpoint>& endpoints);
		bool on_filter_hostname(aux::http_connection& c, string_view hostname);
		void on_connect(aux::http_connection& c);
		void on_response(error_code const& ec, aux::http_parser const& parser
			, span<char const> data);

		// write-completion handler for write_only (stop-announce) connections.
		// Advances to the next queued request or closes, just like on_response
		// does after a normal response.
		void on_write_complete(aux::http_connection& c);

		void on_timeout(error_code const&) override {}

		std::shared_ptr<aux::http_connection> m_tracker_connection;
		address m_tracker_ip;
		io_context& m_ioc;

		// announces/scrapes to the same server queued behind the in-flight
		// request (which is the base class m_req / m_requester). They are issued
		// one at a time as each response completes, reusing the keep-alive socket.
		std::deque<std::pair<tracker_request, std::weak_ptr<request_callback>>> m_followers;

		// this connection's operating mode, (re-)computed once per dispatched
		// request in send_request() and consulted by next_request(). Only
		// three of the four combinations of "write_only"/"flushing" are
		// reachable (flushing implies write_only), so they're one enum
		// rather than two bools.
		enum class connection_mode_t : std::uint8_t
		{
			// ordinary keep-alive connection; the in-flight request expects
			// a real response.
			normal,
			// fire-and-forget (a stop announce during shutdown): write and
			// don't wait for a response.
			write_only,
			// write_only, and the follower queue has drained: waiting for
			// the deadline timer to fire a second time before closing
			// gracefully, so the drain loop gets a chance to read
			// outstanding responses first.
			flushing
		};

		// using m_man.is_stopping() directly in next_request() would be
		// wrong: the session may start shutting down after a normal
		// connection is created, which would leave it stuck without a
		// close path -- so the mode is fixed at send_request() time instead.
		connection_mode_t m_mode = connection_mode_t::normal;

		// counts requests actually dispatched via send_request() over this
		// connection's lifetime, checked against
		// settings_pack::max_tracker_connection_requests by next_request() to
		// decide when to rotate onto a fresh connection.
		int m_requests_sent = 0;

		// set once send_request() has deferred a write-only connection's
		// first dispatch by a tick (see send_request()), so it only ever
		// defers once and does not loop.
		bool m_first_dispatch_deferred = false;
	};

	TORRENT_EXTRA_EXPORT tracker_response parse_tracker_response(
		span<char const> data, error_code& ec
		, tracker_request_flags_t flags, sha1_hash const& scrape_ih);

	TORRENT_EXTRA_EXPORT bool extract_peer_info(bdecode_node const& info
		, peer_entry& ret, error_code& ec);
}

#endif // TORRENT_HTTP_TRACKER_CONNECTION_HPP_INCLUDED
