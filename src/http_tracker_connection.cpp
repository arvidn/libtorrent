/*

Copyright (c) 2004-2022, Arvid Norberg
Copyright (c) 2004, Magnus Jonsson
Copyright (c) 2015, Mikhail Titov
Copyright (c) 2016-2018, 2020-2021, Alden Torres
Copyright (c) 2016, Andrei Kurushin
Copyright (c) 2016-2018, Steven Siloti
Copyright (c) 2017, Pavel Pimenov
Copyright (c) 2020, Paul-Louis Ageneau
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/config.hpp"
#include "libtorrent/aux_/socket_io.hpp"

#include <string>
#include <functional>
#include <vector>
#include <list>
#include <cctype>
#include <algorithm>
#include <cstdio> // for snprintf
#include <cinttypes> // for PRId64 et.al.
#include <limits>

#include "libtorrent/aux_/http_tracker_connection.hpp"
#include "libtorrent/aux_/http_connection.hpp"
#include "libtorrent/aux_/escape_string.hpp"
#include "libtorrent/aux_/io_bytes.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/aux_/string_util.hpp" // for is_i2p_url
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/aux_/resolver_interface.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/aux_/parse_url.hpp"
#include "libtorrent/aux_/array.hpp"

namespace libtorrent::aux {

	namespace {

		bool valid_compact_peer_list(int const len, int const entry_size)
		{
			TORRENT_ASSERT(entry_size > 0);
			return len >= 0 && len % entry_size == 0;
		}

		// SSRF mitigation: an announce/scrape to a loopback address must look
		// like a standard BitTorrent announce (the /announce path prefix) --
		// otherwise a tracker URL (or a follower coalescing onto an
		// already-connected loopback address with a different path, e.g. a
		// scrape) could be used to make arbitrary requests against services
		// running on localhost. Used both by on_filter() (the initial,
		// per-endpoint-list check at resolve time) and send_request() (a
		// per-dispatch check, since followers reusing a connection never
		// go through on_filter() again).
		bool ssrf_blocks(bool const ssrf_mitigation, address const& addr, string_view const path)
		{
			return ssrf_mitigation && addr.is_loopback() && path.substr(0, 9) != "/announce";
		}
	}

	http_tracker_connection::http_tracker_connection(
		io_context& ios
		, tracker_manager& man
		, tracker_request req
		, std::weak_ptr<request_callback> c)
		: tracker_connection(man, std::move(req), ios, std::move(c))
		, m_ioc(ios)
	{}

	void http_tracker_connection::start() { send_request(); }

	void http_tracker_connection::send_request()
	{
		// m_req is being (re-)dispatched now, so its outcome is unreported
		// again from this point on -- including for any of the early fail()
		// calls below, which run before the write-only-specific refinement
		// further down. A promoted follower reaches here with m_req_pending
		// already false (cleared when the previous request completed), so
		// without this, an early failure on a promoted dispatch would be
		// silently dropped instead of reported.
		m_req_pending = true;

		std::string url = tracker_req().url;

		if (tracker_req().kind & tracker_request::scrape_request)
		{
			// find and replace "announce" with "scrape"
			// in request

			std::size_t pos = url.find("announce");
			if (pos == std::string::npos)
			{
				fail(errors::scrape_not_available, operation_t::bittorrent);
				return;
			}
			url.replace(pos, 8, "scrape");
		}

#if TORRENT_USE_I2P
		bool const i2p = is_i2p_url(url);
#else
		static const bool i2p = false;
#endif

		aux::session_settings const& settings = m_man.settings();
		bool const ssrf_mitigation = settings.get_bool(settings_pack::ssrf_mitigation);

		// re-check SSRF mitigation on every dispatch, not just the first: a
		// follower coalesced onto this connection's already-connected socket
		// (see m_tracker_ip) never goes through on_filter()'s per-endpoint
		// check, since that only runs at DNS-resolution time for the first
		// request. On the very first dispatch m_tracker_ip is still
		// default-constructed (not loopback), so this is a no-op then.
		if (ssrf_mitigation)
		{
			std::string path;
			error_code ec;
			std::tie(std::ignore, std::ignore, std::ignore, std::ignore, path) =
				parse_url_components(url, ec);
			if (!ec && ssrf_blocks(ssrf_mitigation, m_tracker_ip, path))
			{
				fail(errors::ssrf_mitigation, operation_t::bittorrent);
				return;
			}
		}

		// if request-string already contains
		// some parameters, append an ampersand instead
		// of a question mark
		auto const arguments_start = url.find('?');
		if (arguments_start != std::string::npos)
		{
			// tracker URLs that come pre-baked with query string arguments will be
			// rejected when SSRF-mitigation is enabled
			if (ssrf_mitigation && has_tracker_query_string(string_view(url).substr(arguments_start + 1)))
			{
				fail(errors::ssrf_mitigation, operation_t::bittorrent);
				return;
			}
			url += "&";
		}
		else
		{
			url += "?";
		}

		url += "info_hash=";
		url += lt::escape_string({tracker_req().info_hash.data(), 20});

		if (!(tracker_req().kind & tracker_request::scrape_request))
		{
			static aux::array<const char*, 4> const event_string{{{"completed", "started", "stopped", "paused"}}};

			char str[1024];
			std::snprintf(str, sizeof(str)
				, "&peer_id=%s"
				"&port=%d"
				"&uploaded=%" PRId64
				"&downloaded=%" PRId64
				"&left=%" PRId64
				"&corrupt=%" PRId64
				"&key=%08X"
				"%s%s" // event
				"&numwant=%d"
				"&compact=1"
				"&no_peer_id=1"
				, lt::escape_string({tracker_req().pid.data(), 20}).c_str()
				// the i2p tracker seems to verify that the port is not 0,
				// even though it ignores it otherwise
				, tracker_req().listen_port
				, tracker_req().uploaded
				, tracker_req().downloaded
				, tracker_req().left
				, tracker_req().corrupt
				, tracker_req().key
				, (tracker_req().event != event_t::none) ? "&event=" : ""
				, (tracker_req().event != event_t::none) ? event_string[static_cast<int>(tracker_req().event) - 1] : ""
				, tracker_req().num_want);
			url += str;
#if !defined TORRENT_DISABLE_ENCRYPTION
			if (settings.get_int(settings_pack::in_enc_policy) != settings_pack::pe_disabled
				&& settings.get_bool(settings_pack::announce_crypto_support))
				url += "&supportcrypto=1";
#endif
			if (settings.get_bool(settings_pack::report_redundant_bytes))
			{
				url += "&redundant=";
				url += to_string(tracker_req().redundant).data();
			}
			if (!tracker_req().trackerid.empty())
			{
				url += "&trackerid=";
				url += lt::escape_string(tracker_req().trackerid);
			}

#if TORRENT_USE_I2P
			if (i2p && tracker_req().i2pconn)
			{
				if (tracker_req().i2pconn->local_endpoint().empty())
				{
					fail(errors::no_i2p_endpoint, operation_t::bittorrent
						, "Waiting for i2p acceptor from SAM bridge", seconds32(5));
					return;
				}
				else
				{
					url += "&ip=" + tracker_req().i2pconn->local_endpoint () + ".i2p";
				}
			}
			else
#endif
			if (!settings.get_bool(settings_pack::anonymous_mode))
			{
				std::string const& announce_ip = settings.get_str(settings_pack::announce_ip);
				if (!announce_ip.empty())
				{
					url += "&ip=" + lt::escape_string(announce_ip);
				}
			}
		}

		if (!tracker_req().ipv4.empty() && !i2p)
		{
			for (auto const& v4 : tracker_req().ipv4)
			{
				std::string const ip = v4.to_string();
				url += "&ipv4=";
				url += lt::escape_string(ip);
			}
		}
		if (!tracker_req().ipv6.empty() && !i2p)
		{
			for (auto const& v6 : tracker_req().ipv6)
			{
				std::string const ip = v6.to_string();
				url += "&ipv6=";
				url += lt::escape_string(ip);
			}
		}

		// i2p trackers don't use our outgoing sockets, they use the SAM
		// connection
		if (!i2p && !tracker_req().outgoing_socket)
		{
			fail(errors::invalid_listen_socket, operation_t::get_interface
				, "outgoing socket was closed");
			return;
		}

		// the escape-hatch setting disables the write-only fire-and-forget
		// shutdown path along with connection reuse in general (see
		// tracker_manager::make_pool_key()): every stop announce is then a
		// normal, response-reading request, same as pre-reuse behavior.
		bool const reuse_disabled =
			settings.get_bool(settings_pack::disable_tracker_connection_reuse);

		// fix the mode once at request-dispatch time (see the member comment
		// for why this can't be re-derived later in next_request()).
		bool const write_only =
			!reuse_disabled && m_man.is_stopping() && tracker_req().event == event_t::stopped;

		// a connection's very first dispatch can't trust "m_followers is
		// currently empty" as "no more are coming" -- during session-wide
		// shutdown, torrents are aborted synchronously in one loop, and a
		// sibling torrent sharing this tracker host may not have coalesced its
		// own stop announce onto this connection yet (that happens on a later
		// iteration of the same loop, still before this connection's first
		// write ever completes). Defer the first write-only dispatch by one
		// tick, so it only ever runs once that whole synchronous wave -- and
		// with it, every possible follower -- has already happened: then
		// m_followers.empty() truthfully means "no more are coming", the same
		// as it does for a promoted dispatch (from next_request(), which only
		// ever runs from an async completion handler and so is never subject
		// to this in the first place).
		bool const is_first_dispatch = !m_tracker_connection;
		if (write_only && is_first_dispatch && !m_first_dispatch_deferred)
		{
			m_first_dispatch_deferred = true;
			post(m_ioc, std::bind(&http_tracker_connection::send_request, shared_from_this()));
			return;
		}

		// create the http_connection on the first request. Followers reuse it,
		// and its keep-alive socket, via send_request() -> http_connection::get().
		if (!m_tracker_connection)
		{
			using namespace std::placeholders;
			m_tracker_connection = std::make_shared<aux::http_connection>(m_ioc,
				m_man.host_resolver(),
				std::bind(&http_tracker_connection::on_response, shared_from_this(), _1, _2, _3),
				settings.get_int(settings_pack::max_http_recv_buffer_size),
				std::bind(&http_tracker_connection::on_connect, shared_from_this(), _1),
				std::bind(&http_tracker_connection::on_filter, shared_from_this(), _1, _2),
				std::bind(&http_tracker_connection::on_filter_hostname, shared_from_this(), _1, _2)
#if TORRENT_USE_SSL
					,
				tracker_req().ssl_ctx
#endif
			);
			// drives the write_only (stop-announce) path: each completed write
			// advances to the next queued request or closes the connection.
			m_tracker_connection->set_write_handler(
				std::bind(&http_tracker_connection::on_write_complete, shared_from_this(), _1));
		}

		int const timeout = tracker_req().event == event_t::stopped
			? settings.get_int(settings_pack::stop_tracker_timeout)
			: settings.get_int(settings_pack::tracker_completion_timeout);

		// in anonymous mode we omit the user agent to mitigate fingerprinting of
		// the client. Private torrents is an exception because some private
		// trackers may require the user agent
		bool const anon_user = settings.get_bool(settings_pack::anonymous_mode)
			&& !tracker_req().private_torrent;
		std::string const user_agent = anon_user
			? "curl/7.81.0"
			: settings.get_str(settings_pack::user_agent);

		auto const ls = bind_socket();
		bind_info_t bi = [&ls](){
			if (ls.get() == nullptr)
				return bind_info_t{};
			else
				return bind_info_t{ls.device(), ls.get_local_endpoint().address()};
		}();

		// when sending stopped requests, prefer the cached DNS entry
		// to avoid being blocked for slow or failing responses. Chances
		// are that we're shutting down, and this should be a best-effort
		// attempt. It's not worth stalling shutdown.

		m_mode = write_only ? connection_mode_t::write_only : connection_mode_t::normal;

		// a write-only dispatch is deliberately fire-and-forget: no outcome
		// is ever reported for it, so it is never "pending" one. A normal
		// dispatch expects a response, reported by on_response(); until then
		// it is pending, so close() knows to report it if torn down first.
		m_req_pending = !write_only;

		// in write-only mode, only keep-alive while a follower is already
		// queued (trustworthy even on the first dispatch, since that was
		// deferred above until the whole synchronous shutdown wave was done).
		// Once we stop asking for it, on_write() marks the connection
		// non-reusable, so draining picks up the peer's own close promptly
		// and closes this connection (see on_drain()).
		bool const keep_alive = !reuse_disabled && (!write_only || !m_followers.empty());

		aux::proxy_settings ps(settings);
		m_tracker_connection->get(url,
			seconds(timeout),
			ps.proxy_tracker_connections ? &ps : nullptr,
			5,
			user_agent,
			bi,
			(tracker_req().event == event_t::stopped ? aux::resolver_interface::cache_only
													 : aux::resolver_flags{})
				| aux::resolver_interface::abort_on_shutdown,
#if TORRENT_ABI_VERSION == 1
			tracker_req().auth,
#else
			"",
#endif
#if TORRENT_USE_I2P
			tracker_req().i2pconn,
#endif
			keep_alive,
			// during shutdown, stop announces are best-effort: write and forget,
			// don't wait for a response. In steady state a stopped event is a
			// normal request and the response is read so the connection can be
			// reused for subsequent announces.
			write_only);

		++m_requests_sent;

		// the url + 100 estimated header size
		sent_bytes(int(url.size()) + 100);

#ifndef TORRENT_DISABLE_LOGGING

		std::shared_ptr<request_callback> cb = requester();
		if (cb)
		{
			cb->debug_log("==> TRACKER_REQUEST [ url: %s ]", url.c_str());
		}
#endif
	}

	void http_tracker_connection::queue_request(
		tracker_request req, std::weak_ptr<request_callback> c)
	{
		// high-priority requests jump ahead of the other queued followers, but
		// cannot preempt the in-flight request (that is m_req, which is not part
		// of m_followers, so the front of m_followers is the next one served).
		if (req.kind & tracker_request::high_priority)
			m_followers.emplace_front(std::move(req), std::move(c));
		else
			m_followers.emplace_back(std::move(req), std::move(c));
		m_man.inc_queued_requests();
	}

	void http_tracker_connection::on_write_complete(aux::http_connection&)
	{
		// a stop announce was written; we don't read a response. Move on to the
		// next queued stop, or close the connection once the queue drains.
		next_request();
	}

	void http_tracker_connection::next_request()
	{
		if (m_followers.empty())
		{
			// during shutdown, a write_only stop connection doesn't close the instant
			// its queue drains: it keeps the socket open while the drain loop reads
			// the outstanding responses and the writes flush, then closes gracefully
			// when the deadline timer fires (which calls on_write_complete again).
			if (m_mode == connection_mode_t::write_only)
			{
				m_mode = connection_mode_t::flushing;
				return;
			}
			close();
			return;
		}

		// rotate onto a fresh connection once this one has dispatched the
		// configured maximum, rather than keep piling followers onto a
		// socket a tracker (or an intermediary reverse proxy) may be about
		// to close on its own initiative. close() moves the remaining
		// followers (including the one that would have been promoted here)
		// out and re-dispatches them, letting them coalesce onto a new
		// connection, itself subject to the same limit.
		int const max_requests =
			m_man.settings().get_int(settings_pack::max_tracker_connection_requests);
		if (max_requests > 0 && m_requests_sent >= max_requests)
		{
			close();
			return;
		}

		// promote the next queued request to be the in-flight request and issue
		// it on this connection. http_connection reuses the keep-alive socket if
		// the previous response allowed it, otherwise it reconnects transparently.
		// m_tracker_ip is left untouched: on reuse on_connect does not fire and
		// the address (same server) stays valid; on a reconnect on_connect
		// refreshes it.
		auto next = std::move(m_followers.front());
		m_followers.pop_front();
		m_man.dec_queued_requests();

		// a non-stop request must never follow a write_only/flushing dispatch
		// on the same connection: http_connection's drain state
		// (m_write_only_state) is never reset when switching back to a
		// normal, response-reading dispatch, which would race a fresh read
		// against the still-running drain loop. prune_followers() is relied
		// on to keep this from happening, by stripping non-stop followers
		// from every pooled connection during shutdown before any of this
		// could race.
		TORRENT_ASSERT(m_mode == connection_mode_t::normal || next.first.event == event_t::stopped);

		m_req = std::move(next.first);
		m_requester = std::move(next.second);
		m_req_pending = true;
		send_request();
	}

	void http_tracker_connection::prune_followers(bool const keep_stopped)
	{
		// each dropped follower is reported to its requester as a skipped
		// announce, which resets announce_infohash::updating so the torrent
		// can announce to this tracker again later.
		auto const drop = [this](std::pair<tracker_request, std::weak_ptr<request_callback>>& f) {
			m_man.dec_queued_requests();
			if (std::shared_ptr<request_callback> cb = f.second.lock())
			{
				post(m_ioc,
					std::bind(&request_callback::tracker_request_error,
						cb,
						std::move(f.first),
						lt::errors::announce_skipped,
						operation_t::get_interface,
						"",
						seconds32(0)));
			}
		};

		if (!keep_stopped)
		{
			for (auto& f : m_followers)
				drop(f);
			m_followers.clear();
			return;
		}
		// keep only stop-requests; close() re-dispatches them so they are still
		// announced while shutting down. Everything else is dropped.
		for (auto& f : m_followers)
		{
			if (f.first.event != event_t::stopped) drop(f);
		}
		m_followers.erase(
			std::remove_if(m_followers.begin(),
				m_followers.end(),
				[](std::pair<tracker_request, std::weak_ptr<request_callback>> const& f) {
					return f.first.event != event_t::stopped;
				}),
			m_followers.end());
	}

	void http_tracker_connection::close()
	{
		// m_req itself (as opposed to m_followers, handled below) is only
		// ever torn down here without its outcome having been reported when
		// this connection is closed directly by the manager (e.g. aborting a
		// still-queued or still-in-flight connection on shutdown/pause),
		// rather than via a path that already reported it (fail_impl(), or
		// on_response() promoting a follower).
		if (m_req_pending)
		{
			m_req_pending = false;
			if (std::shared_ptr<request_callback> cb = requester())
			{
				post(m_ioc,
					std::bind(&request_callback::tracker_request_error,
						cb,
						tracker_req(),
						lt::errors::announce_skipped,
						operation_t::get_interface,
						"",
						seconds32(0)));
			}
		}

		if (m_tracker_connection)
		{
			m_tracker_connection->close();
			m_tracker_connection.reset();
		}
		cancel();

		// re-dispatch any followers that have not been served yet (e.g. the
		// in-flight request failed). They go back through the manager and are
		// retried on a fresh connection. Capture the manager/io_context (which
		// outlive this connection) before remove_request, which may drop the last
		// reference to 'this'. Remove this connection from the pool first, so the
		// followers don't coalesce back onto the connection that is going away.
		// (During abort the manager calls prune_followers() first, so only
		// stop-requests, if any, remain here to be re-dispatched.)
		std::deque<std::pair<tracker_request, std::weak_ptr<request_callback>>> followers =
			std::move(m_followers);
		m_followers.clear();
		tracker_manager& man = m_man;
		io_context& ioc = m_ioc;
		aux::session_settings const& sett = man.settings();
		man.remove_request(this);
		for (auto& f : followers)
		{
			// leaving this connection's queue; queue_request() will re-count it
			// if it lands somewhere that waits (a fresh coalesce or the pool
			// queue), or not at all if it's dispatched immediately.
			man.dec_queued_requests();
			man.queue_request(ioc, std::move(f.first), sett, std::move(f.second));
		}
	}

	// endpoints is an in-out parameter
	void http_tracker_connection::on_filter(aux::http_connection& c
		, std::vector<tcp::endpoint>& endpoints)
	{
		// filter all endpoints we cannot reach from this listen socket, which may
		// be all of them, in which case we should not announce this listen socket
		// to this tracker
		auto const ls = bind_socket();
		if (ls.get() != nullptr)
		{
			endpoints.erase(std::remove_if(endpoints.begin(), endpoints.end()
				, [&](tcp::endpoint const& ep) { return !ls.can_route(ep.address()); })
				, endpoints.end());
		}

		if (endpoints.empty())
		{
			fail(lt::errors::announce_skipped, operation_t::get_interface);
			return;
		}

		aux::session_settings const& settings = m_man.settings();
		bool const ssrf_mitigation = settings.get_bool(settings_pack::ssrf_mitigation);
		if (ssrf_mitigation && std::find_if(endpoints.begin(), endpoints.end()
			, [](tcp::endpoint const& ep) { return ep.address().is_loopback(); }) != endpoints.end())
		{
			// there is at least one loopback address in here. Parse the path
			// once and remove any endpoint ssrf_blocks() rejects for it (see
			// that function for the actual mitigation logic).
			std::string path;

			error_code ec;
			std::tie(std::ignore, std::ignore, std::ignore, std::ignore, path)
				= parse_url_components(c.url(), ec);
			if (ec)
			{
				fail(ec, operation_t::parse_address);
				return;
			}

			endpoints.erase(std::remove_if(endpoints.begin(),
								endpoints.end(),
								[&](tcp::endpoint const& ep) {
									return ssrf_blocks(ssrf_mitigation, ep.address(), path);
								}),
				endpoints.end());

			if (endpoints.empty())
			{
				fail(errors::ssrf_mitigation, operation_t::bittorrent);
				return;
			}
		}

		TORRENT_UNUSED(c);
		if (!tracker_req().filter) return;

		// remove endpoints that are filtered by the IP filter
		for (auto i = endpoints.begin(); i != endpoints.end();)
		{
			if (tracker_req().filter->access(i->address()) == ip_filter::blocked)
				i = endpoints.erase(i);
			else
				++i;
		}

#ifndef TORRENT_DISABLE_LOGGING
		std::shared_ptr<request_callback> cb = requester();
		if (cb)
		{
			cb->debug_log("*** TRACKER_FILTER");
		}
#endif
		if (endpoints.empty())
			fail(errors::banned_by_ip_filter, operation_t::bittorrent);
	}

	// returns true if the hostname is allowed
	bool http_tracker_connection::on_filter_hostname(aux::http_connection&
		, string_view hostname)
	{
		aux::session_settings const& settings = m_man.settings();
		if (settings.get_bool(settings_pack::allow_idna)) return true;
		return !is_idna(hostname);
	}

	void http_tracker_connection::on_connect(aux::http_connection& c)
	{
		error_code ec;
		tcp::endpoint ep = c.socket().remote_endpoint(ec);
		m_tracker_ip = ep.address();
	}

	void http_tracker_connection::on_response(error_code const& ec
		, aux::http_parser const& parser, span<char const> data)
	{
		// keep this alive
		std::shared_ptr<http_tracker_connection> me(shared_from_this());

		if (ec && ec != boost::asio::error::eof)
		{
			fail(ec, operation_t::sock_read);
			return;
		}

		if (!parser.header_finished())
		{
			fail(boost::asio::error::eof, operation_t::sock_read);
			return;
		}

		// every path below this point reports m_req's outcome (or has no
		// requester left to report it to), so its outcome is accounted for
		// from here on.
		m_req_pending = false;

		if (parser.status_code() != 200)
		{
			// a complete HTTP response with an error status: the socket is still
			// at a clean message boundary, so fail just this request and continue
			// with the next queued follower (reusing the keep-alive socket if the
			// server allowed it) rather than tearing the connection down. With no
			// followers queued, next_request() closes the connection.
			if (auto const cb = requester())
				cb->tracker_request_error(tracker_req(),
					error_code(parser.status_code(), http_category()),
					operation_t::bittorrent,
					parser.message(),
					seconds32(0));
			next_request();
			return;
		}

		received_bytes(static_cast<int>(data.size()) + parser.body_start());

		// handle tracker response
		error_code ecode;

		std::shared_ptr<request_callback> cb = requester();
		if (!cb)
		{
			m_req_pending = false;
			next_request();
			return;
		}

		tracker_response resp = parse_tracker_response(data, ecode
			, tracker_req().kind, tracker_req().info_hash);

		resp.interval = std::max(resp.interval
				, seconds32{m_man.settings().get_int(settings_pack::min_announce_interval)});

		if (!resp.warning_message.empty())
			cb->tracker_warning(tracker_req(), resp.warning_message);

		if (ecode)
		{
			// the HTTP response framed correctly but the tracker payload didn't
			// parse (or the tracker reported a failure). The connection itself is
			// still healthy, so report it to this requester and move on to the
			// next follower rather than closing.
			cb->tracker_request_error(tracker_req(),
				ecode,
				operation_t::bittorrent,
				resp.failure_reason,
				resp.interval.count() == 0 ? resp.min_interval : resp.interval);
			next_request();
			return;
		}

		// the response is about to be reported below; mark it as no longer
		// pending first, so a fail() already posted (e.g. by
		// tracker_manager::abort_all_requests() racing this response) finds
		// nothing left to report when it runs.
		m_req_pending = false;

		// do slightly different things for scrape requests
		if (tracker_req().kind & tracker_request::scrape_request)
		{
			cb->tracker_scrape_response(tracker_req(), resp.complete
				, resp.incomplete, resp.downloaded, resp.downloaders);
		}
		else
		{
			std::list<address> ip_list;
			if (m_tracker_connection)
			{
				for (auto const& endp : m_tracker_connection->endpoints())
				{
					ip_list.push_back(endp.address());
				}
			}

			cb->tracker_response(tracker_req(), m_tracker_ip, ip_list, resp);
		}
		next_request();
	}

	// TODO: 2 returning a bool here is redundant. Instead this function should
	// return the peer_entry
	bool extract_peer_info(bdecode_node const& info, peer_entry& ret, error_code& ec)
	{
		// extract peer id (if any)
		if (info.type() != bdecode_node::dict_t)
		{
			ec = errors::invalid_peer_dict;
			return false;
		}
		bdecode_node i = info.dict_find_string("peer id");
		if (i && i.string_length() == 20)
		{
			std::copy(i.string_ptr(), i.string_ptr() + 20, ret.pid.begin());
		}
		else
		{
			// if there's no peer_id, just initialize it to a bunch of zeroes
			ret.pid.clear();
		}

		// extract ip
		i = info.dict_find_string("ip");
		if (!i)
		{
			ec = errors::invalid_tracker_response;
			return false;
		}
		ret.hostname = i.string_value();

		// extract port
		i = info.dict_find_int("port");
		if (!i)
		{
			ec = errors::invalid_tracker_response;
			return false;
		}

		auto const port = i.int_value();
		if (port < 0 || port > std::numeric_limits<std::uint16_t>::max())
		{
			ec = errors::invalid_tracker_response;
			return false;
		}
		ret.port = std::uint16_t(port);

		return true;
	}

	tracker_response parse_tracker_response(span<char const> const data, error_code& ec
		, tracker_request_flags_t const flags, sha1_hash const& scrape_ih)
	{
		tracker_response resp;

		bdecode_node e;
		int const res = bdecode(data.begin(), data.end(), e, ec);

		if (ec) return resp;

		if (res != 0 || e.type() != bdecode_node::dict_t)
		{
			ec = errors::invalid_tracker_response;
			return resp;
		}

		// if no interval is specified, default to 30 minutes
		resp.interval = seconds32{e.dict_find_int_value("interval", 1800)};
		resp.min_interval = seconds32{e.dict_find_int_value("min interval", 30)};

		bdecode_node const tracker_id = e.dict_find_string("tracker id");
		if (tracker_id)
			resp.trackerid = tracker_id.string_value();

		// parse the response
		bdecode_node const failure = e.dict_find_string("failure reason");
		if (failure)
		{
			resp.failure_reason = failure.string_value();
			ec = errors::tracker_failure;
			return resp;
		}

		bdecode_node const warning = e.dict_find_string("warning message");
		if (warning)
			resp.warning_message = warning.string_value();

		if (flags & tracker_request::scrape_request)
		{
			bdecode_node const files = e.dict_find_dict("files");
			if (!files)
			{
				ec = errors::invalid_files_entry;
				return resp;
			}

			bdecode_node const scrape_data = files.dict_find_dict(
				scrape_ih.to_string());

			if (!scrape_data)
			{
				ec = errors::invalid_hash_entry;
				return resp;
			}

			resp.complete = int(scrape_data.dict_find_int_value("complete", -1));
			resp.incomplete = int(scrape_data.dict_find_int_value("incomplete", -1));
			resp.downloaded = int(scrape_data.dict_find_int_value("downloaded", -1));
			resp.downloaders = int(scrape_data.dict_find_int_value("downloaders", -1));

			return resp;
		}

		// look for optional scrape info
		resp.complete = int(e.dict_find_int_value("complete", -1));
		resp.incomplete = int(e.dict_find_int_value("incomplete", -1));
		resp.downloaded = int(e.dict_find_int_value("downloaded", -1));

		bdecode_node peers_ent = e.dict_find("peers");
		if (peers_ent && peers_ent.type() == bdecode_node::string_t)
		{
			char const* peers = peers_ent.string_ptr();
			int const len = peers_ent.string_length();
#if TORRENT_USE_I2P
			if (flags & tracker_request::i2p)
			{
				if (!valid_compact_peer_list(len, 32))
				{
					ec = errors::invalid_peers_entry;
					return resp;
				}
				for (int i = 0; i < len; i += 32)
				{
					i2p_peer_entry p;
					std::memcpy(p.destination.data(), peers + i, 32);
					resp.i2p_peers.push_back(p);
				}
			}
			else
#endif
			{
				if (!valid_compact_peer_list(len, 6))
				{
					ec = errors::invalid_peers_entry;
					return resp;
				}
				resp.peers4.reserve(std::size_t(len / 6));
				for (int i = 0; i < len; i += 6)
				{
					ipv4_peer_entry p;
					p.ip = aux::read_v4_address(peers).to_bytes();
					p.port = aux::read_uint16(peers);
					resp.peers4.push_back(p);
				}
			}
		}
		else if (peers_ent && peers_ent.type() == bdecode_node::list_t)
		{
			int const len = peers_ent.list_size();
			resp.peers.reserve(std::size_t(len));
			error_code parse_error;
			for (int i = 0; i < len; ++i)
			{
				peer_entry p;
				if (!extract_peer_info(peers_ent.list_at(i), p, parse_error))
					continue;
				resp.peers.push_back(p);
			}

			// only report an error if all peer entries are invalid
			if (resp.peers.empty() && parse_error)
			{
				ec = parse_error;
				return resp;
			}
		}
		else
		{
			peers_ent.clear();
		}

		bdecode_node ipv6_peers = e.dict_find_string("peers6");
		if (ipv6_peers)
		{
			char const* peers = ipv6_peers.string_ptr();
			int const len = ipv6_peers.string_length();
			if (!valid_compact_peer_list(len, 18))
			{
				ec = errors::invalid_peers_entry;
				return resp;
			}
			resp.peers6.reserve(std::size_t(len / 18));
			for (int i = 0; i < len; i += 18)
			{
				ipv6_peer_entry p;
				p.ip = aux::read_v6_address(peers).to_bytes();
				p.port = aux::read_uint16(peers);
				resp.peers6.push_back(p);
			}
		}
		else
		{
			ipv6_peers.clear();
		}
/*
		// if we didn't receive any peers. We don't care if we're stopping anyway
		if (peers_ent == 0 && ipv6_peers == 0
			&& tracker_req().event != event_t::stopped)
		{
			ec = errors::invalid_peers_entry;
			return resp;
		}
*/
		bdecode_node const ip_ent = e.dict_find_string("external ip");
		if (ip_ent)
		{
			char const* p = ip_ent.string_ptr();
			if (ip_ent.string_length() == std::tuple_size<address_v4::bytes_type>::value)
				resp.external_ip = aux::read_v4_address(p);
			else if (ip_ent.string_length() == std::tuple_size<address_v6::bytes_type>::value)
				resp.external_ip = aux::read_v6_address(p);
		}

		return resp;
	}
}
