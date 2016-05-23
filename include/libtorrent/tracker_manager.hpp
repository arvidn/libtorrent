/*

Copyright (c) 2003-2016, Arvid Norberg
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

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/cstdint.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/unordered_map.hpp>
#include <boost/function.hpp>

#ifdef TORRENT_USE_OPENSSL
#include <boost/asio/ssl/context.hpp>
#endif

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/socket.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/peer.hpp" // peer_entry
#include "libtorrent/deadline_timer.hpp"
#include "libtorrent/union_endpoint.hpp"
#include "libtorrent/io_service.hpp"
#include "libtorrent/aux_/array_view.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/debug.hpp"
#include "libtorrent/error_code.hpp"

namespace libtorrent
{
	struct request_callback;
	class tracker_manager;
	struct timeout_handler;
	struct tracker_connection;
	class udp_tracker_connection;
	class http_tracker_connection;
	class  udp_socket;
	struct resolver_interface;
	struct counters;
	struct ip_filter;
#if TORRENT_USE_I2P
	class i2p_connection;
#endif
	namespace aux { struct session_logger; struct session_settings; }

	// returns -1 if gzip header is invalid or the header size in bytes
	TORRENT_EXTRA_EXPORT int gzip_header(const char* buf, int size);

	struct TORRENT_EXTRA_EXPORT tracker_request
	{
		tracker_request()
			: downloaded(-1)
			, uploaded(-1)
			, left(-1)
			, corrupt(0)
			, redundant(0)
			, listen_port(0)
			, event(none)
			, kind(announce_request)
			, key(0)
			, num_want(0)
			, send_stats(true)
			, triggered_manually(false)
#ifdef TORRENT_USE_OPENSSL
			, ssl_ctx(0)
#endif
#if TORRENT_USE_I2P
			, i2pconn(0)
#endif
		{}

		enum event_t
		{
			none,
			completed,
			started,
			stopped,
			paused
		};

		enum kind_t
		{
			// do not compare against announce_request ! check if not scrape instead
			announce_request = 0,
			scrape_request = 1,
			// affects interpretation of peers string in HTTP response
			// see parse_tracker_response()
			i2p = 2
		};

		std::string url;
		std::string trackerid;
#ifndef TORRENT_NO_DEPRECATE
		std::string auth;
#endif

		boost::shared_ptr<const ip_filter> filter;

		boost::int64_t downloaded;
		boost::int64_t uploaded;
		boost::int64_t left;
		boost::int64_t corrupt;
		boost::int64_t redundant;
		boost::uint16_t listen_port;

		// values from event_t
		boost::uint8_t event;

		// values from kind_t
		boost::uint8_t kind;

		boost::uint32_t key;
		int num_want;
#if TORRENT_USE_IPV6
		address_v6 ipv6;
#endif
		sha1_hash info_hash;
		peer_id pid;
		address bind_ip;

		bool send_stats;

		// this is set to true if this request was triggered by a "manual" call to
		// scrape_tracker() or force_reannounce()
		bool triggered_manually;

#ifdef TORRENT_USE_OPENSSL
		boost::asio::ssl::context* ssl_ctx;
#endif
#if TORRENT_USE_I2P
		i2p_connection* i2pconn;
#endif
	};

	struct tracker_response
	{
		tracker_response()
			: interval(1800)
			, min_interval(120)
			, complete(-1)
			, incomplete(-1)
			, downloaders(-1)
			, downloaded(-1)
		{}

		// peers from the tracker, in various forms
		std::vector<peer_entry> peers;
		std::vector<ipv4_peer_entry> peers4;
#if TORRENT_USE_IPV6
		std::vector<ipv6_peer_entry> peers6;
#endif
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
		int interval;

		// the lowest force-announce interval
		int min_interval;

		// the number of seeds in the swarm
		int complete;

		// the number of downloaders in the swarm
		int incomplete;

		// if supported by the tracker, the number of actively downloading peers.
		// i.e. partial seeds. If not suppored, -1
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
			, int response_code
			, error_code const& ec
			, const std::string& msg
			, int retry_interval) = 0;

#ifndef TORRENT_DISABLE_LOGGING
		virtual void debug_log(const char* fmt, ...) const TORRENT_FORMAT(2,3) = 0;
#endif
	};

	struct TORRENT_EXTRA_EXPORT timeout_handler
		: boost::enable_shared_from_this<timeout_handler>
		, boost::noncopyable
	{
		timeout_handler(io_service& str);

		void set_timeout(int completion_timeout, int read_timeout);
		void restart_read_timeout();
		void cancel();
		bool cancelled() const { return m_abort; }

		virtual void on_timeout(error_code const& ec) = 0;
		virtual ~timeout_handler() {}

		io_service& get_io_service() { return m_timeout.get_io_service(); }

	private:

		void timeout_callback(error_code const&);

		int m_completion_timeout;

		// used for timeouts
		// this is set when the request has been sent
		time_point m_start_time;

		// this is set every time something is received
		time_point m_read_time;

		// the asio async operation
		deadline_timer m_timeout;

		int m_read_timeout;

		bool m_abort;
	};

	// TODO: 2 this class probably doesn't need to have virtual functions.
	struct TORRENT_EXTRA_EXPORT tracker_connection
		: timeout_handler
	{
		tracker_connection(tracker_manager& man
			, tracker_request const& req
			, io_service& ios
			, boost::weak_ptr<request_callback> r);

		void update_transaction_id(boost::shared_ptr<udp_tracker_connection> c
			, boost::uint64_t tid);

		boost::shared_ptr<request_callback> requester() const;
		virtual ~tracker_connection() {}

		tracker_request const& tracker_req() const { return m_req; }

		void fail(error_code const& ec, int code = -1, char const* msg = ""
			, int interval = 0, int min_interval = 0);
		virtual void start() = 0;
		virtual void close();
		address const& bind_interface() const { return m_req.bind_ip; }
		void sent_bytes(int bytes);
		void received_bytes(int bytes);

		boost::shared_ptr<tracker_connection> shared_from_this()
		{
			return boost::static_pointer_cast<tracker_connection>(
				timeout_handler::shared_from_this());
		}

	private:

		const tracker_request m_req;

	protected:

		void fail_impl(error_code const& ec, int code = -1, std::string msg = std::string()
			, int interval = 0, int min_interval = 0);

		boost::weak_ptr<request_callback> m_requester;

		tracker_manager& m_man;
	};

	class TORRENT_EXTRA_EXPORT tracker_manager final
		: boost::noncopyable
		, single_threaded
	{
	public:

		typedef boost::function<void(udp::endpoint const&
			, aux::array_view<char const>
			, error_code&, int)> send_fun_t;
		typedef boost::function<void(char const*, int
			, aux::array_view<char const>
			, error_code&, int)> send_fun_hostname_t;

		tracker_manager(send_fun_t const& send_fun
			, send_fun_hostname_t const& send_fun_hostname
			, counters& stats_counters
			, resolver_interface& resolver
			, aux::session_settings const& sett
#if !defined TORRENT_DISABLE_LOGGING || TORRENT_USE_ASSERTS
			, aux::session_logger& ses
#endif
			);
		virtual ~tracker_manager();

		void queue_request(
			io_service& ios
			, tracker_request r
			, boost::weak_ptr<request_callback> c
				= boost::weak_ptr<request_callback>());
		void abort_all_requests(bool all = false);

		void remove_request(tracker_connection const*);
		bool empty() const;
		int num_requests() const;

		void sent_bytes(int bytes);
		void received_bytes(int bytes);

		void incoming_error(error_code const& ec, udp::endpoint const& ep);
		bool incoming_packet(udp::endpoint const& ep, aux::array_view<char const> buf);

		// this is only used for SOCKS packets, since
		// they may be addressed to hostname
		// TODO: 3 make sure the udp_socket supports passing on string-hostnames
		// too, and that this function is used
		bool incoming_packet(char const* hostname, aux::array_view<char const> buf);

		void update_transaction_id(
			boost::shared_ptr<udp_tracker_connection> c
			, boost::uint64_t tid);

		aux::session_settings const& settings() const { return m_settings; }
		resolver_interface& host_resolver() { return m_host_resolver; }

		void send_hostname(char const* hostname, int port, aux::array_view<char const> p
			, error_code& ec, int flags = 0);

		void send(udp::endpoint const& ep, aux::array_view<char const> p
			, error_code& ec, int flags = 0);

	private:

		// maps transactionid to the udp_tracker_connection
		// These must use shared_ptr to avoid a dangling reference
		// if a connection is erased while a timeout event is in the queue
		typedef boost::unordered_map<boost::uint32_t
			, boost::shared_ptr<udp_tracker_connection> > udp_conns_t;
		udp_conns_t m_udp_conns;

		typedef std::vector<boost::shared_ptr<http_tracker_connection> > http_conns_t;
		http_conns_t m_http_conns;

		send_fun_t m_send_fun;
		send_fun_hostname_t m_send_fun_hostname;
		resolver_interface& m_host_resolver;
		aux::session_settings const& m_settings;
		counters& m_stats_counters;
#if !defined TORRENT_DISABLE_LOGGING || TORRENT_USE_ASSERTS
		aux::session_logger& m_ses;
#endif

		bool m_abort;
	};
}

#endif // TORRENT_TRACKER_MANAGER_HPP_INCLUDED

