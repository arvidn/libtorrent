/*

Copyright (c) 2004-2010, 2012-2021, Arvid Norberg
Copyright (c) 2016-2017, 2020-2021, Alden Torres
Copyright (c) 2017, Steven Siloti
Copyright (c) 2020, Paul-Louis Ageneau
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <algorithm>
#include <cctype>

#include "libtorrent/aux_/io.hpp"
#include "libtorrent/aux_/session_interface.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/aux_/http_tracker_connection.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/aux_/socket_io.hpp"
#include "libtorrent/aux_/ssl.hpp"
#include "libtorrent/aux_/tracker_manager.hpp"
#include "libtorrent/aux_/udp_tracker_connection.hpp"
#include "libtorrent/aux_/parse_url.hpp"
#include "libtorrent/aux_/proxy_settings.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/functional/hash.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#if TORRENT_USE_RTC
#include "libtorrent/aux_/websocket_tracker_connection.hpp"
#endif

using namespace std::placeholders;

namespace libtorrent::aux {

	timeout_handler::timeout_handler(io_context& ios)
		: m_start_time(clock_type::now())
		, m_read_time(m_start_time)
		, m_timeout(ios)
	{}

	timeout_handler::~timeout_handler()
	{
		TORRENT_ASSERT(m_outstanding_timer_wait == 0);
	}

	void timeout_handler::set_timeout(int completion_timeout, int read_timeout)
	{
		m_completion_timeout = completion_timeout;
		m_read_timeout = read_timeout;
		m_start_time = m_read_time = clock_type::now();

		TORRENT_ASSERT(completion_timeout > 0 || read_timeout > 0);

		if (m_abort) return;

		int timeout = 0;
		if (m_read_timeout > 0) timeout = m_read_timeout;
		if (m_completion_timeout > 0)
		{
			timeout = timeout == 0
				? m_completion_timeout
				: std::min(m_completion_timeout, timeout);
		}

		ADD_OUTSTANDING_ASYNC("timeout_handler::timeout_callback");
		m_timeout.expires_at(m_read_time + seconds(timeout));
		m_timeout.async_wait(std::bind(
			&timeout_handler::timeout_callback, shared_from_this(), _1));
#if TORRENT_USE_ASSERTS
		++m_outstanding_timer_wait;
#endif
	}

	void timeout_handler::restart_read_timeout()
	{
		m_read_time = clock_type::now();
	}

	void timeout_handler::cancel()
	{
		m_abort = true;
		m_completion_timeout = 0;
		m_timeout.cancel();
	}

	void timeout_handler::timeout_callback(error_code const& error)
	{
		COMPLETE_ASYNC("timeout_handler::timeout_callback");
#if TORRENT_USE_ASSERTS
		TORRENT_ASSERT(m_outstanding_timer_wait > 0);
		--m_outstanding_timer_wait;
#endif
		if (m_abort) return;

		time_point now = clock_type::now();
		time_duration receive_timeout = now - m_read_time;
		time_duration completion_timeout = now - m_start_time;

		if ((m_read_timeout
				&& m_read_timeout <= total_seconds(receive_timeout))
			|| (m_completion_timeout
				&& m_completion_timeout <= total_seconds(completion_timeout))
			|| error)
		{
			on_timeout(error);
			return;
		}

		int timeout = 0;
		if (m_read_timeout > 0) timeout = m_read_timeout;
		if (m_completion_timeout > 0)
		{
			timeout = timeout == 0
				? int(m_completion_timeout - total_seconds(m_read_time - m_start_time))
				: std::min(int(m_completion_timeout - total_seconds(m_read_time - m_start_time)), timeout);
		}
		ADD_OUTSTANDING_ASYNC("timeout_handler::timeout_callback");
		m_timeout.expires_at(m_read_time + seconds(timeout));
		m_timeout.async_wait(
			std::bind(&timeout_handler::timeout_callback, shared_from_this(), _1));
#if TORRENT_USE_ASSERTS
		++m_outstanding_timer_wait;
#endif
	}

	tracker_connection::tracker_connection(
		tracker_manager& man
		, tracker_request req
		, io_context& ios
		, std::weak_ptr<request_callback> r)
		: timeout_handler(ios)
		, m_req(std::move(req))
		, m_requester(std::move(r))
		, m_man(man)
	{}

	std::shared_ptr<request_callback> tracker_connection::requester() const
	{
		return m_requester.lock();
	}

	void tracker_connection::fail(error_code const& ec, operation_t const op
		, char const* msg, seconds32 const interval, seconds32 const min_interval)
	{
		// we need to post the error to avoid deadlock
		post(get_executor(), std::bind(&tracker_connection::fail_impl
			, shared_from_this(), ec, op, std::string(msg), interval, min_interval));
	}

	void tracker_connection::fail_impl(error_code const& ec, operation_t const op
		, std::string const msg, seconds32 const interval, seconds32 const min_interval)
	{
		// fail() only posts this call, so m_req is not necessarily still
		// pending by the time it runs: the connection's own logic may have
		// already reported an outcome for it (e.g. a legitimate response
		// arriving right as tracker_manager::abort_all_requests() posted
		// this same call). Checking here, rather than in fail(), is what
		// makes the two mutually exclusive.
		if (m_req_pending)
		{
			m_req_pending = false;
			std::shared_ptr<request_callback> cb = requester();
			if (cb)
				cb->tracker_request_error(
					m_req, ec, op, msg, interval.count() == 0 ? min_interval : interval);
		}
		close();
	}

	void tracker_connection::sent_bytes(int bytes)
	{
		m_man.sent_bytes(bytes);
	}

	void tracker_connection::received_bytes(int bytes)
	{
		m_man.received_bytes(bytes);
	}

	tracker_manager::tracker_manager(send_fun_t send_fun
		, send_fun_hostname_t send_fun_hostname
		, counters& stats_counters
		, aux::resolver_interface& resolver
		, aux::session_settings const& sett
#if !defined TORRENT_DISABLE_LOGGING || TORRENT_USE_ASSERTS
		, aux::session_logger& ses
#endif
		)
		: m_send_fun(std::move(send_fun))
		, m_send_fun_hostname(std::move(send_fun_hostname))
		, m_host_resolver(resolver)
		, m_settings(sett)
		, m_stats_counters(stats_counters)
#if !defined TORRENT_DISABLE_LOGGING || TORRENT_USE_ASSERTS
		, m_ses(ses)
#endif
	{}

	tracker_manager::~tracker_manager()
	{
		abort_all_requests(true);
	}

	void tracker_manager::sent_bytes(int bytes)
	{
		TORRENT_ASSERT(m_ses.is_single_thread());
		m_stats_counters.inc_stats_counter(counters::sent_tracker_bytes, bytes);
	}

	void tracker_manager::received_bytes(int bytes)
	{
		TORRENT_ASSERT(m_ses.is_single_thread());
		m_stats_counters.inc_stats_counter(counters::recv_tracker_bytes, bytes);
	}

	void tracker_manager::inc_queued_requests()
	{
		m_stats_counters.inc_stats_counter(counters::num_queued_tracker_announces, 1);
	}

	void tracker_manager::dec_queued_requests()
	{
		m_stats_counters.inc_stats_counter(counters::num_queued_tracker_announces, -1);
	}

	std::size_t tracker_manager::http_pool_key_hash::operator()(http_pool_key const& k) const
	{
		std::size_t ret = 0;
		boost::hash_combine(ret, std::hash<std::string>{}(k.hostname));
		boost::hash_combine(ret, std::hash<std::uint16_t>{}(k.port));
		boost::hash_combine(ret, std::hash<bool>{}(k.ssl));
		boost::hash_combine(ret, std::hash<void const*>{}(k.ssl_ctx));
		boost::hash_combine(ret, std::hash<void const*>{}(k.i2p_conn));
		boost::hash_combine(ret, k.bind.hash_value());
		boost::hash_combine(ret, std::hash<int>{}(int(k.proxy_type)));
		boost::hash_combine(ret, std::hash<std::string>{}(k.proxy_hostname));
		boost::hash_combine(ret, std::hash<std::uint16_t>{}(k.proxy_port));
		boost::hash_combine(ret, std::hash<std::uint64_t>{}(k.no_coalesce));
		boost::hash_combine(ret, std::hash<bool>{}(k.is_shutdown));
		return ret;
	}

	tracker_manager::http_pool_key tracker_manager::make_pool_key(tracker_request const& req)
	{
		http_pool_key key;

		if (m_settings.get_bool(settings_pack::disable_tracker_connection_reuse))
		{
			// escape hatch: never coalesce, give every request its own
			// connection, the same way an unparseable URL does below.
			key.no_coalesce = ++m_http_unique;
			return key;
		}

		// during shutdown, stop announces use a separate write-only connection
		// so they don't queue behind (or get blocked by) a connection that was
		// opened before shutdown began. In steady state this is false and
		// stopped events share the same connection as regular announces.
		key.is_shutdown = m_abort;

		error_code ec;
		auto const components = parse_url_components(req.url, ec);
		if (ec)
		{
			// the URL didn't parse, so we can't reliably key it. Give it a
			// unique key so it gets its own connection and is never coalesced
			// with an unrelated request.
			key.no_coalesce = ++m_http_unique;
			return key;
		}
		key.hostname = std::get<2>(components);
		key.ssl = (std::get<0>(components) == "https");
		int port = std::get<3>(components);
		if (port == -1) port = key.ssl ? 443 : 80;
		key.port = std::uint16_t(port);
		key.bind = req.outgoing_socket;
#if TORRENT_USE_SSL
		// the ssl context is per-torrent, so it is part of the connection's
		// identity for https. For plain http it is irrelevant (left null).
		if (key.ssl) key.ssl_ctx = req.ssl_ctx;
#endif
#if TORRENT_USE_I2P
		key.i2p_conn = req.i2pconn;
#endif
		aux::proxy_settings const ps(m_settings);
		if (ps.proxy_tracker_connections)
		{
			key.proxy_type = ps.type;
			key.proxy_hostname = ps.hostname;
			key.proxy_port = ps.port;
		}
		return key;
	}

	void tracker_manager::start_next_queued()
	{
		if (m_num_started_http >= m_settings.get_int(settings_pack::max_concurrent_http_announces))
			return;

		auto& seq = m_http_conns.get<0>();
		auto const i = std::find_if(
			seq.begin(), seq.end(), [](http_pool_entry const& e) { return !e.started; });
		if (i == seq.end()) return;

		seq.modify(i, [](http_pool_entry& e) { e.started = true; });
		++m_num_started_http;
		// this entry's own (base) request is no longer waiting for a socket
		// slot; any followers already coalesced onto it stay counted until
		// next_request() promotes each of them in turn.
		dec_queued_requests();
		i->conn->start();
	}

	void tracker_manager::remove_request(aux::http_tracker_connection const* c)
	{
		TORRENT_ASSERT(is_single_thread());
		auto& by_conn = m_http_conns.get<2>();
		auto const i = by_conn.find(c);
		if (i == by_conn.end()) return;

		bool const was_started = i->started;
		by_conn.erase(i);
		if (was_started)
		{
			--m_num_started_http;
			// a socket slot freed up; promote the next queued connection
			start_next_queued();
		}
		else
		{
			// this entry never got its own socket slot; its base request is
			// being removed without ever having been dispatched. (Any
			// followers of its own were already accounted for by close(),
			// which moves them out of m_followers before calling this.)
			dec_queued_requests();
		}
	}

	void tracker_manager::remove_request(aux::udp_tracker_connection const* c)
	{
		TORRENT_ASSERT(is_single_thread());
		m_udp_conns.erase(c->transaction_id());
	}

#if TORRENT_USE_RTC
	void tracker_manager::remove_request(aux::websocket_tracker_connection const* c)
	{
		TORRENT_ASSERT(is_single_thread());
		tracker_request const& req = c->tracker_req();
		m_websocket_conns.erase(req.url);
	}
#endif

	void tracker_manager::update_transaction_id(
		std::shared_ptr<aux::udp_tracker_connection> c
		, std::uint32_t tid)
	{
		TORRENT_ASSERT(is_single_thread());
		m_udp_conns.erase(c->transaction_id());
		m_udp_conns[tid] = std::move(c);
	}

	void tracker_manager::queue_request(
		io_context& ios
		, tracker_request&& req
		, aux::session_settings const& sett
		, std::weak_ptr<request_callback> c)
	{
		TORRENT_ASSERT(is_single_thread());
		TORRENT_ASSERT(req.num_want >= 0);
		TORRENT_ASSERT(!m_abort || req.event == event_t::stopped);
		if (m_abort && req.event != event_t::stopped) return;
		bool const high_priority = bool(req.kind & tracker_request::high_priority);

#ifndef TORRENT_DISABLE_LOGGING
		if (auto cb = c.lock())
		{
			cb->debug_log("*** QUEUE_TRACKER_REQUEST [ listen_port: %d priority: %d ]"
				, req.listen_port, high_priority);
		}
#endif

		std::string const protocol = req.url.substr(0, req.url.find(':'));

#if TORRENT_USE_SSL
		if (protocol == "http" || protocol == "https")
#else
		if (protocol == "http")
#endif
		{
			http_pool_key key = make_pool_key(req);

			// if there is already a connection to this server (started or still
			// queued), coalesce this request onto it; it will be issued
			// sequentially over the same keep-alive socket.
			//
			// note: if 'existing' is itself still queued (not yet started,
			// waiting for a socket slot under max_concurrent_http_announces),
			// a high-priority req only jumps the front of that connection's
			// own follower FIFO (see http_tracker_connection::queue_request());
			// it does not bump 'existing' itself to the front of the outer
			// not-yet-started queue (the 'seq' sequenced index below), so it
			// still waits for start_next_queued() to reach it in whatever
			// position it was originally queued at.
			auto& by_key = m_http_conns.get<1>();
			auto const existing = by_key.find(key);
			if (existing != by_key.end())
			{
				existing->conn->queue_request(std::move(req), c);
				return;
			}

			auto con = std::make_shared<aux::http_tracker_connection>(ios, *this, std::move(req), c);
			bool const start_now =
				m_num_started_http < sett.get_int(settings_pack::max_concurrent_http_announces);

			auto& seq = m_http_conns.get<0>();
			if (start_now)
			{
				seq.push_back(http_pool_entry{con, std::move(key), true});
				++m_num_started_http;
				con->start();
			}
			else
			{
				// queued: high-priority requests jump to the front of the queue
				if (high_priority)
					seq.push_front(http_pool_entry{con, std::move(key), false});
				else
					seq.push_back(http_pool_entry{con, std::move(key), false});
				inc_queued_requests();
			}
			return;
		}
		else if (protocol == "udp")
		{
			auto con = std::make_shared<aux::udp_tracker_connection>(ios, *this, std::move(req), c);
			m_udp_conns[con->transaction_id()] = con;
			con->start();
			return;
        }
#if TORRENT_USE_RTC
        else if (protocol == "ws" || protocol == "wss")
        {
			std::shared_ptr<request_callback> cb = c.lock();
			if (!cb) return;

			// TODO: introduce a setting for max_offers
			const int max_offers = 10;
			req.num_want = std::min(req.num_want, max_offers);
			if (req.num_want == 0)
			{
				// when we're shutting down, we don't really want to
				// re-establish the persistent websocket connection just to
				// announce "stopped", and advertise 0 offers. It may hang
				// shutdown.
				post(ios, std::bind(&request_callback::tracker_request_error, cb, std::move(req)
					, errors::torrent_aborted, operation_t::connect
					, "", seconds32(0)));
				return;
			}
			cb->generate_rtc_offers(req.num_want
				, [this, &ios, request = std::move(req), c](error_code const& ec
					, std::vector<aux::rtc_offer> offers) mutable
			{
				if (!ec) request.offers = std::move(offers);

				auto it = m_websocket_conns.find(request.url);
				if (it != m_websocket_conns.end() && it->second->is_started()) {
					it->second->queue_request(std::move(request), c);
				} else {
					auto con = std::make_shared<aux::websocket_tracker_connection>(
							ios, *this, std::move(request), c);
					con->start();
					m_websocket_conns[request.url] = con;
				}
			});
			return;
        }
#endif
		// we need to post the error to avoid deadlock
		else if (auto r = c.lock())
			post(ios, std::bind(&request_callback::tracker_request_error, r, std::move(req)
				, errors::unsupported_url_protocol, operation_t::parse_address
				, "", seconds32(0)));
	}

	bool tracker_manager::incoming_packet(udp::endpoint const& ep
		, span<char const> const buf)
	{
		TORRENT_ASSERT(is_single_thread());
		// ignore packets smaller than 8 bytes
		if (buf.size() < 8)
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (m_ses.should_log())
			{
				m_ses.session_log("incoming packet from %s, not a UDP tracker message "
					"(%d Bytes)", print_endpoint(ep).c_str(), int(buf.size()));
			}
#endif
			return false;
		}

		// the first word is the action, if it's not [0, 3]
		// it's not a valid udp tracker response
		span<const char> ptr = buf;
		std::uint32_t const action = aux::read_uint32(ptr);
		if (action > 3) return false;

		std::uint32_t const transaction = aux::read_uint32(ptr);
		auto const i = m_udp_conns.find(transaction);

		if (i == m_udp_conns.end())
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (m_ses.should_log())
			{
				m_ses.session_log("incoming UDP tracker packet from %s has invalid "
					"transaction ID (%x)", print_endpoint(ep).c_str()
					, transaction);
			}
#endif
			return false;
		}

		auto const p = i->second;
		// on_receive() may remove the tracker connection from the list
		return p->on_receive(ep, buf);
	}

	void tracker_manager::incoming_error(error_code const&
		, udp::endpoint const&)
	{
		TORRENT_ASSERT(is_single_thread());
		// TODO: 2 implement
	}

	bool tracker_manager::incoming_packet(string_view const hostname
		, span<char const> const buf)
	{
		TORRENT_ASSERT(is_single_thread());
		// ignore packets smaller than 8 bytes
		if (buf.size() < 16) return false;

		// the first word is the action, if it's not [0, 3]
		// it's not a valid udp tracker response
		span<const char> ptr = buf;
		std::uint32_t const action = aux::read_uint32(ptr);
		if (action > 3) return false;

		std::uint32_t const transaction = aux::read_uint32(ptr);
		auto const i = m_udp_conns.find(transaction);

		if (i == m_udp_conns.end())
		{
#ifndef TORRENT_DISABLE_LOGGING
			// now, this may not have been meant to be a tracker response,
			// but chances are pretty good, so it's probably worth logging
			m_ses.session_log("incoming UDP tracker packet from %s has invalid "
				"transaction ID (%x)", std::string(hostname).c_str(), transaction);
#endif
			return false;
		}

		auto const p = i->second;
		// on_receive() may remove the tracker connection from the list
		return p->on_receive_hostname(hostname, buf);
	}

	void tracker_manager::send_hostname(aux::listen_socket_handle const& sock
		, char const* hostname, int const port
		, span<char const> p, error_code& ec, aux::udp_send_flags_t const flags)
	{
		TORRENT_ASSERT(is_single_thread());
		m_send_fun_hostname(sock, hostname, port, p, ec, flags);
	}

	void tracker_manager::send(aux::listen_socket_handle const& sock
		, udp::endpoint const& ep
		, span<char const> p
		, error_code& ec, aux::udp_send_flags_t const flags)
	{
		TORRENT_ASSERT(is_single_thread());
		m_send_fun(sock, ep, p, ec, flags);
	}

	void tracker_manager::begin_shutdown() { m_abort = true; }

	void tracker_manager::stop()
	{
		begin_shutdown();
		abort_all_requests();
	}

	void tracker_manager::abort_all_requests(bool all)
	{
		// this is called from the destructor too, which is not subject to the
		// single-thread requirement.
		TORRENT_ASSERT(all || is_single_thread());
		// removes all connections except 'event=stopped'-requests. Aborted
		// requests are reported as a skipped announce, which clears the
		// "updating" flag on the requester's end, so the tracker is
		// eligible to be announced to again, e.g. once a paused session is
		// resumed.

		// kept separate from close_http_started so we can close the queued
		// ones first (see below).
		std::vector<std::shared_ptr<aux::http_tracker_connection>> close_http_queued;
		std::vector<std::shared_ptr<aux::http_tracker_connection>> close_http_started;
		std::vector<std::shared_ptr<aux::udp_tracker_connection>> close_udp_connections;

		// m_http_conns holds both started and queued connections
		for (auto const& e : m_http_conns)
		{
			// cancel followers that won't survive this abort, on every connection
			// -- including ones we keep open below. A full abort drops all
			// followers; a partial abort keeps only stop-requests. Without this, a
			// connection kept for its stop in-flight request would still issue its
			// non-stop followers during shutdown.
			e.conn->prune_followers(!all);

			tracker_request const& req = e.conn->tracker_req();
			if (req.event == event_t::stopped && !all)
				continue;

			(e.started ? close_http_started : close_http_queued).push_back(e.conn);

#ifndef TORRENT_DISABLE_LOGGING
			std::shared_ptr<request_callback> rc = e.conn->requester();
			if (rc) rc->debug_log("aborting: %s", req.url.c_str());
#endif
		}
		for (auto const& p : m_udp_conns)
		{
			auto const& c = p.second;
			tracker_request const& req = c->tracker_req();
			if (req.event == event_t::stopped && !all)
				continue;

			close_udp_connections.push_back(c);

#ifndef TORRENT_DISABLE_LOGGING
			std::shared_ptr<request_callback> rc = c->requester();
			if (rc) rc->debug_log("aborting: %s", req.url.c_str());
#endif
		}

#if TORRENT_USE_RTC
		std::vector<std::shared_ptr<aux::websocket_tracker_connection>> close_websocket_connections;
		for (auto const& p : m_websocket_conns)
		{
			auto const& c = p.second;
			close_websocket_connections.push_back(c);

#ifndef TORRENT_DISABLE_LOGGING
			std::shared_ptr<request_callback> rc = c->requester();
			if (rc) rc->debug_log("aborting: %s", c->tracker_req().url.c_str());
#endif
		}
#endif

		// followers were already pruned above; close() re-dispatches any kept
		// stop-requests onto a fresh connection.
		//
		// close the queued (not-yet-started) ones first: removing a
		// not-yet-started entry never frees a socket slot, so it can't
		// trigger start_next_queued() (see remove_request()). Closing a
		// started connection does free a slot and does call
		// start_next_queued() -- doing that only once every queued
		// connection destined for closure is already gone means it can only
		// ever promote a connection we deliberately kept (one whose own
		// in-flight request is itself a kept stop announce), never one about
		// to be closed a moment later, which would otherwise waste a
		// connect/TLS handshake (and send an announce/scrape) shutdown was
		// meant to cancel.
		for (auto const& c : close_http_queued)
			c->close();
		for (auto const& c : close_http_started)
			c->close();

		// close() on a udp_tracker_connection does not report the request's
		// outcome so a partial abort must go through fail(), which does
		for (auto const& c : close_udp_connections)
		{
			if (all)
				c->close();
			else
				c->fail(errors::announce_skipped, operation_t::bittorrent);
		}

#if TORRENT_USE_RTC
		for (auto const& c : close_websocket_connections)
		{
			// websocket_tracker_connection::fail() takes its arguments in the
			// opposite order (and hides tracker_connection::fail() by name).
			if (all)
				c->close();
			else
				c->fail(operation_t::bittorrent, errors::announce_skipped);
		}
#endif
	}

	bool tracker_manager::empty() const
	{
		TORRENT_ASSERT(is_single_thread());
		return m_http_conns.empty() && m_udp_conns.empty()
#if TORRENT_USE_RTC
			&& m_websocket_conns.empty()
#endif
			;
	}

	int tracker_manager::num_requests() const
	{
		TORRENT_ASSERT(is_single_thread());
		return int(m_http_conns.size() + m_udp_conns.size()
#if TORRENT_USE_RTC
			+ m_websocket_conns.size()
#endif
			);
	}
}
