/*

Copyright (c) 2015, Mikhail Titov
Copyright (c) 2004-2020, Arvid Norberg
Copyright (c) 2016-2017, Steven Siloti
Copyright (c) 2016, 2020, Alden Torres
Copyright (c) 2020, Paul-Louis Ageneau
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

#ifndef TORRENT_TRACKER_MANAGER_HPP_INCLUDED
#define TORRENT_TRACKER_MANAGER_HPP_INCLUDED

#include "libtorrent/config.hpp"

#include <vector>
#include <string>
#include <list>
#include <utility>
#include <cstdint>
#include <tuple>
#include <functional>
#include <memory>
#include <unordered_map>
#include <deque>

#include "libtorrent/flags.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/fwd.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/peer.hpp" // peer_entry
#include "libtorrent/deadline_timer.hpp"
#include "libtorrent/union_endpoint.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/debug.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/listen_socket_handle.hpp"
#include "libtorrent/udp_socket.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/ssl.hpp"

namespace libtorrent {

	class tracker_manager;
	struct timeout_handler;
	class udp_tracker_connection;
	class http_tracker_connection;
	struct counters;
#if TORRENT_USE_I2P
	class i2p_connection;
#endif
namespace aux {
	struct session_logger;
	struct session_settings;
	struct resolver_interface;
}

using tracker_request_flags_t = flags::bitfield_flag<std::uint8_t, struct tracker_request_flags_tag>;

// Kinds of tracker announces. This is typically indicated as the ``&event=``
// HTTP query string parameter to HTTP trackers.
enum class event_t : std::uint8_t
{
	none,
	completed,
	started,
	stopped,
	paused
};

	struct TORRENT_EXTRA_EXPORT tracker_request
	{
		tracker_request() = default;

		static constexpr tracker_request_flags_t scrape_request = 0_bit;

		// affects interpretation of peers string in HTTP response
		// see parse_tracker_response()
		static constexpr tracker_request_flags_t i2p = 1_bit;

		std::string url;
		std::string trackerid;
#if TORRENT_ABI_VERSION == 1
		std::string auth;
#endif

		std::shared_ptr<const ip_filter> filter;

		std::int64_t downloaded = -1;
		std::int64_t uploaded = -1;
		std::int64_t left = -1;
		std::int64_t corrupt = 0;
		std::int64_t redundant = 0;
		std::uint16_t listen_port = 0;
		event_t event = event_t::none;
		tracker_request_flags_t kind = {};

		std::uint32_t key = 0;
		int num_want = 0;
		std::vector<address_v6> ipv6;
		std::vector<address_v4> ipv4;
		sha1_hash info_hash;
		peer_id pid;

		aux::listen_socket_handle outgoing_socket;

		// set to true if the .torrent file this tracker announce is for is marked
		// as private (i.e. has the "priv": 1 key)
		bool private_torrent = false;

		// this is set to true if this request was triggered by a "manual" call to
		// scrape_tracker() or force_reannounce()
		bool triggered_manually = false;

#if TORRENT_USE_SSL
		ssl::context* ssl_ctx = nullptr;
#endif
#if TORRENT_USE_I2P
		i2p_connection* i2pconn = nullptr;
#endif
	};

	struct tracker_response
	{
		tracker_response()
			: interval(1800)
			, min_interval(1)
			, complete(-1)
			, incomplete(-1)
			, downloaders(-1)
			, downloaded(-1)
		{}

		// peers from the tracker, in various forms
		std::vector<peer_entry> peers;
		std::vector<ipv4_peer_entry> peers4;
		std::vector<ipv6_peer_entry> peers6;
		// our external IP address (if the tracker responded with ti, otherwise
		// INADDR_ANY)
		address external_ip;

		// the tracker id, if it was included in the response, otherwise
		// an empty string
		std::string trackerid;

		// if the tracker returned an error, this is set to that error
		std::string failure_reason;

		// contains a warning message from the tracker, if included in
		// the response
		std::string warning_message;

		// re-announce interval, in seconds
		seconds32 interval;

		// the lowest force-announce interval
		seconds32 min_interval;

		// the number of seeds in the swarm
		int complete;

		// the number of downloaders in the swarm
		int incomplete;

		// if supported by the tracker, the number of actively downloading peers.
		// i.e. partial seeds. If not supported, -1
		int downloaders;

		// the number of times the torrent has been downloaded
		int downloaded;
	};

	struct TORRENT_EXTRA_EXPORT request_callback
	{
		friend class tracker_manager;
		request_callback() {}
		virtual ~request_callback() {}
		virtual void tracker_warning(tracker_request const& req
			, std::string const& msg) = 0;
		virtual void tracker_scrape_response(tracker_request const& /*req*/
			, int /*complete*/, int /*incomplete*/, int /*downloads*/
			, int /*downloaders*/) {}
		virtual void tracker_response(
			tracker_request const& req
			, address const& tracker_ip
			, std::list<address> const& ip_list
			, struct tracker_response const& response) = 0;
		virtual void tracker_request_error(
			tracker_request const& req
			, error_code const& ec
			, operation_t op
			, const std::string& msg
			, seconds32 retry_interval) = 0;

#ifndef TORRENT_DISABLE_LOGGING
		virtual bool should_log() const = 0;
		virtual void debug_log(const char* fmt, ...) const noexcept TORRENT_FORMAT(2,3) = 0;
#endif
	};

	struct TORRENT_EXTRA_EXPORT timeout_handler
		: std::enable_shared_from_this<timeout_handler>
	{
		explicit timeout_handler(io_context&);

		timeout_handler(timeout_handler const&) = delete;
		timeout_handler& operator=(timeout_handler const&) = delete;

		void set_timeout(int completion_timeout, int read_timeout);
		void restart_read_timeout();
		void cancel();
		bool cancelled() const { return m_abort; }

		virtual void on_timeout(error_code const& ec) = 0;
		virtual ~timeout_handler();

		auto get_executor() { return m_timeout.get_executor(); }

	private:

		void timeout_callback(error_code const&);

		int m_completion_timeout = 0;

		// used for timeouts
		// this is set when the request has been sent
		time_point m_start_time;

		// this is set every time something is received
		time_point m_read_time;

		// the asio async operation
		deadline_timer m_timeout;

		int m_read_timeout = 0;

		bool m_abort = false;
#if TORRENT_USE_ASSERTS
		int m_outstanding_timer_wait = 0;
#endif
	};

	struct TORRENT_EXTRA_EXPORT tracker_connection
		: timeout_handler
	{
		tracker_connection(tracker_manager& man
			, tracker_request req
			, io_context& ios
			, std::weak_ptr<request_callback> r);

		std::shared_ptr<request_callback> requester() const;
		~tracker_connection() override {}

		tracker_request const& tracker_req() const { return m_req; }

		void fail(error_code const& ec, operation_t op, char const* msg = ""
			, seconds32 interval = seconds32(0), seconds32 min_interval = seconds32(0));
		virtual void start() = 0;
		virtual void close() = 0;
		address bind_interface() const;
		aux::listen_socket_handle const& bind_socket() const { return m_req.outgoing_socket; }
		void sent_bytes(int bytes);
		void received_bytes(int bytes);

		std::shared_ptr<tracker_connection> shared_from_this()
		{
			return std::static_pointer_cast<tracker_connection>(
				timeout_handler::shared_from_this());
		}

	private:

		const tracker_request m_req;

	protected:

		void fail_impl(error_code const& ec, operation_t op, std::string msg = std::string()
			, seconds32 interval = seconds32(0), seconds32 min_interval = seconds32(0));

		std::weak_ptr<request_callback> m_requester;

		tracker_manager& m_man;
	};

	class TORRENT_EXTRA_EXPORT tracker_manager final
		: single_threaded
	{
	public:

		using send_fun_t = std::function<void(aux::listen_socket_handle const&
			, udp::endpoint const&
			, span<char const>
			, error_code&, udp_send_flags_t)>;
		using send_fun_hostname_t = std::function<void(aux::listen_socket_handle const&
			, char const*, int
			, span<char const>
			, error_code&, udp_send_flags_t)>;

		tracker_manager(send_fun_t send_fun
			, send_fun_hostname_t send_fun_hostname
			, counters& stats_counters
			, aux::resolver_interface& resolver
			, aux::session_settings const& sett
#if !defined TORRENT_DISABLE_LOGGING || TORRENT_USE_ASSERTS
			, aux::session_logger& ses
#endif
			);

		~tracker_manager();

		tracker_manager(tracker_manager const&) = delete;
		tracker_manager& operator=(tracker_manager const&) = delete;

		void queue_request(
			io_context& ios
			, tracker_request&& r
			, aux::session_settings const& sett
			, std::weak_ptr<request_callback> c
				= std::weak_ptr<request_callback>());
		void queue_request(
			io_context& ios
			, tracker_request const& r
			, aux::session_settings const& sett
			, std::weak_ptr<request_callback> c
				= std::weak_ptr<request_callback>()) = delete;
		void abort_all_requests(bool all = false);
		void stop();

		void remove_request(http_tracker_connection const* c);
		void remove_request(udp_tracker_connection const* c);
		bool empty() const;
		int num_requests() const;

		void sent_bytes(int bytes);
		void received_bytes(int bytes);

		void incoming_error(error_code const& ec, udp::endpoint const& ep);
		bool incoming_packet(udp::endpoint const& ep, span<char const> buf);

		// this is only used for SOCKS packets, since
		// they may be addressed to hostname
		// TODO: 3 make sure the udp_socket supports passing on string-hostnames
		// too, and that this function is used
		bool incoming_packet(char const* hostname, span<char const> buf);

		void update_transaction_id(
			std::shared_ptr<udp_tracker_connection> c
			, std::uint32_t tid);

		aux::session_settings const& settings() const { return m_settings; }
		aux::resolver_interface& host_resolver() { return m_host_resolver; }

		void send_hostname(aux::listen_socket_handle const& sock
			, char const* hostname, int port, span<char const> p
			, error_code& ec, udp_send_flags_t flags = {});

		void send(aux::listen_socket_handle const& sock
			, udp::endpoint const& ep, span<char const> p
			, error_code& ec, udp_send_flags_t flags = {});

	private:

		// maps transactionid to the udp_tracker_connection
		// These must use shared_ptr to avoid a dangling reference
		// if a connection is erased while a timeout event is in the queue
		std::unordered_map<std::uint32_t, std::shared_ptr<udp_tracker_connection>> m_udp_conns;

		std::vector<std::shared_ptr<http_tracker_connection>> m_http_conns;
		std::deque<std::shared_ptr<http_tracker_connection>> m_queued;

		send_fun_t m_send_fun;
		send_fun_hostname_t m_send_fun_hostname;
		aux::resolver_interface& m_host_resolver;
		aux::session_settings const& m_settings;
		counters& m_stats_counters;
		bool m_abort = false;
#if !defined TORRENT_DISABLE_LOGGING || TORRENT_USE_ASSERTS
		aux::session_logger& m_ses;
#endif
	};
}

#endif // TORRENT_TRACKER_MANAGER_HPP_INCLUDED
