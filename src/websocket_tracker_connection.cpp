/*

Copyright (c) 2019 Paul-Louis Ageneau
Copyright (c) 2020-2021, Alden Torres
Copyright (c) 2020-2021, Arvid Norberg
Copyright (c) 2020-2021, Paul-Louis Ageneau
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/config.hpp" // for TORRENT_USE_RTC

#if TORRENT_USE_RTC

#include "libtorrent/aux_/escape_string.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/aux_/websocket_tracker_connection.hpp"
#include "libtorrent/aux_/utf8.hpp"
#include "libtorrent/aux_/io_bytes.hpp"
#include "libtorrent/ip_filter.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/aux_/socket_io.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/aux_/tracker_manager.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/system/system_error.hpp>
#include <boost/json.hpp>
#ifdef BOOST_JSON_HEADER_ONLY
#include <boost/json/src.hpp>
#endif
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio> // for snprintf
#include <exception>
#include <functional>
#include <list>
#include <locale>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace libtorrent::aux {

using namespace std::placeholders;
namespace errc = boost::system::errc;
namespace error = boost::asio::error;
namespace json = boost::json;

namespace {

// Overload for JSON strings
std::string utf8_latin1(json::string const& sv)
{
	return aux::utf8_latin1(std::string_view{sv.data(), sv.size()});
}

void report_error(std::weak_ptr<request_callback> const& cb,
	tracker_request const& req,
	error_code const& ec,
	operation_t const op)
{
	if (auto c = cb.lock())
		c->tracker_request_error(req, ec, op, ec.message(), seconds32(120));
}
}

websocket_tracker_connection::websocket_tracker_connection(
	io_context& ios,
	tracker_manager& man,
	tracker_request const& req,
	std::weak_ptr<request_callback> cb
)
	: tracker_connection(man, req, ios, cb)
	, m_io_context(ios)
	, m_ssl_context(req.ssl_ctx)
	, m_announce_timer(ios)
{
	// not queue_request(): arming the announce timer requires
	// shared_from_this(), which isn't available yet. start() arms it
	m_pending.emplace_back(tracker_message{req}, std::move(cb), request_deadline(req));
}

void websocket_tracker_connection::start()
{
	if (is_started()) return;

	auto const& settings = m_man.settings();
	auto const& req = tracker_req();
	m_websocket =
		std::make_shared<aux::websocket_stream>(m_io_context, m_man.host_resolver(), m_ssl_context);

	// in anonymous mode we omit the user agent to mitigate fingerprinting of
	// the client. Private torrents is an exception because some private
	// trackers may require the user agent
	std::string const user_agent = settings.get_bool(settings_pack::anonymous_mode)
			&& !req.private_torrent ? "" : settings.get_str(settings_pack::user_agent);
	m_websocket->set_user_agent(user_agent);

#ifndef TORRENT_DISABLE_LOGGING
	if (auto cb = requester())
		cb->debug_log("*** WEBSOCKET_TRACKER_CONNECT [ url: %s ]", req.url.c_str());
#endif

	ADD_OUTSTANDING_ASYNC("websocket_tracker_connection::on_connect");
	m_websocket->async_connect(req.url, std::bind(&websocket_tracker_connection::on_connect
			, shared_from_this(), _1));

	// the requests' deadlines also cover establishing the connection
	update_announce_timer();
}

void websocket_tracker_connection::close()
{
	// no specific failure triggered this (e.g. session shutdown)
	close(error::operation_aborted, operation_t::unknown);
}

void websocket_tracker_connection::close(error_code const& ec, operation_t const op)
{
	m_announce_timer.cancel();

	if (m_websocket)
	{
		m_websocket->close();
		m_websocket.reset();
	}

	while (!m_pending.empty())
	{
		auto [msg, callback, deadline] = std::move(m_pending.front());
		TORRENT_UNUSED(deadline);
		m_pending.pop_front();
		if (callback.lock())
		{
			// only tracker_request messages ever carry a non-empty
			// callback (queue_answer() always passes an empty one). These
			// were never sent, so they were aborted rather than failed,
			// regardless of what ec/op close() was called with.
			auto const* req = std::get_if<tracker_request>(&msg);
			TORRENT_ASSERT(req);
			report_error(callback, *req, error::operation_aborted, operation_t::unknown);
		}
	}

	// any request still marked pending here never got a response. This is
	// the only place that reports it, whether it's the connection's
	// current request or another torrent multiplexed onto this same
	// connection. Unlike m_pending above, these were actually sent, so
	// they genuinely experienced ec/op (the reason this connection is
	// closing) rather than merely being aborted.
	for (auto& e : m_callbacks)
	{
		if (!e.second.pending)
			continue;
		e.second.pending = false;
		report_error(e.second.cb, e.second.req, ec, op);
	}

	m_callbacks.clear();
	m_offer_quota.clear();
	m_man.remove_request(this);
}

bool websocket_tracker_connection::is_started() const
{
	return m_websocket && (m_websocket->is_open() || m_websocket->is_connecting());
}

bool websocket_tracker_connection::is_open() const
{
	return m_websocket && m_websocket->is_open();
}

bool websocket_tracker_connection::prune_non_stopped_requests()
{
	error_code const ec = errors::announce_skipped;
	bool has_stopped = false;

	// erasing is the common case, so do it in one pass rather than paying
	// for an O(n) shift on every deque::erase() of a single element.
	auto const pending_end =
		std::remove_if(m_pending.begin(), m_pending.end(), [&](auto const& item) {
			auto const* req = std::get_if<tracker_request>(&std::get<0>(item));
			if (!req)
				return false;
			if (req->event == event_t::stopped)
			{
				has_stopped = true;
				return false;
			}

			report_error(std::get<1>(item), *req, ec, operation_t::bittorrent);
			return true;
		});
	m_pending.erase(pending_end, m_pending.end());

	for (auto it = m_callbacks.begin(); it != m_callbacks.end();)
	{
		if (!it->second.pending)
		{
			++it;
			continue;
		}
		if (it->second.req.event == event_t::stopped)
		{
			has_stopped = true;
			++it;
			continue;
		}

		report_error(it->second.cb, it->second.req, ec, operation_t::bittorrent);
		it = m_callbacks.erase(it);
	}

	update_announce_timer();

	return has_stopped;
}

void websocket_tracker_connection::queue_request(tracker_request req, std::weak_ptr<request_callback> cb)
{
	time_point const deadline = request_deadline(req);
	m_pending.emplace_back(tracker_message{std::move(req)}, cb, deadline);

	// the deadline applies while the request is still queued too, e.g. if
	// the connection is never established
	update_announce_timer();

	if (is_open()) send_pending();
}

void websocket_tracker_connection::queue_answer(tracker_answer ans)
{
	m_pending.emplace_back(tracker_message{std::move(ans)}, std::weak_ptr<request_callback>{}
		, max_time());
	if (is_open()) send_pending();
}

time_point websocket_tracker_connection::request_deadline(tracker_request const& req) const
{
	auto const& settings = m_man.settings();
	int const timeout = req.event == event_t::stopped
		? settings.get_int(settings_pack::stop_tracker_timeout)
		: settings.get_int(settings_pack::tracker_completion_timeout);

	// a timeout of 0 (or less) disables it
	if (timeout <= 0) return max_time();
	return clock_type::now() + seconds(timeout);
}

void websocket_tracker_connection::update_announce_timer()
{
	m_announce_timer.cancel();

	time_point earliest = max_time();

	for (auto const& [info_hash, entry] : m_callbacks)
	{
		if (entry.pending)
			earliest = std::min(earliest, entry.deadline);
	}

	for (auto const& item : m_pending)
	{
		if (std::holds_alternative<tracker_request>(std::get<0>(item)))
			earliest = std::min(earliest, std::get<2>(item));
	}

	if (earliest == max_time())
		return;

	ADD_OUTSTANDING_ASYNC("websocket_tracker_connection::on_announce_timeout");
	m_announce_timer.expires_at(earliest);
	m_announce_timer.async_wait(
		std::bind(&websocket_tracker_connection::on_announce_timeout
			, shared_from_this(), _1));
}

void websocket_tracker_connection::send_pending()
{
	if (m_sending || m_pending.empty()) return;

	m_sending = true;

	auto [msg, callback, deadline] = std::move(m_pending.front());
	m_pending.pop_front();

	std::visit(
		[this, cb = callback, deadline = deadline](auto const& m) {
			// record every sent request, even ones whose callback has
			// already expired, so m_callbacks always reflects whether the
			// connection's current request is still pending (used by
			// prune_non_stopped_requests()); a dead callback just means
			// nothing is invoked when its outcome is reported.
			if constexpr (std::is_same_v<std::decay_t<decltype(m)>, tracker_request>)
			{
			// torrent invariants ensure at most one request per info_hash
			// is ever in flight at a time, so this must never overwrite a
			// still-pending entry
#if TORRENT_USE_ASSERTS
				auto const existing = m_callbacks.find(m.info_hash);
				TORRENT_ASSERT(existing == m_callbacks.end() || !existing->second.pending);
#endif
				// the deadline was set when the request was queued
				m_callbacks[m.info_hash] = callback_entry{
					cb,
					m,
					true,
					deadline,
					clock_type::now()};
			}

			if (cb.lock())
				m_requester = cb;

			if constexpr (std::is_same_v<std::decay_t<decltype(m)>, tracker_request>)
				m_sending_request = m.info_hash;
			else
				m_sending_request.reset();

			do_send(m);

			if constexpr (std::is_same_v<std::decay_t<decltype(m)>, tracker_request>)
				update_announce_timer();
		},
		msg);
}


void websocket_tracker_connection::on_announce_timeout(error_code const& ec)
{
	COMPLETE_ASYNC("websocket_tracker_connection::on_announce_timeout");

	if (ec == boost::asio::error::operation_aborted)
		return;

	auto const now = clock_type::now();
	std::vector<std::pair<std::weak_ptr<request_callback>, tracker_request>> timed_out;

	// set if the timeouts suggest the connection itself is dead, rather
	// than the tracker not responding to some request (e.g. aquatic
	// silently drops announces in some cases)
	bool connection_dead = false;

	// set if a dead connection had a paused announce outstanding
	bool paused_event_failed = false;

	for (auto it = m_callbacks.begin(); it != m_callbacks.end();)
	{
		if (!it->second.pending || it->second.deadline > now)
		{
			++it;
			continue;
		}

		// The connection is dead if this request's WebSocket write is still
		// outstanding, or if nothing at all was received since the request was sent.
		// In the former case, incoming traffic from other torrents must not make a
		// stalled write look healthy.
		if ((m_sending_request && *m_sending_request == it->first)
			|| m_last_receive <= it->second.sent)
		{
			connection_dead = true;

			if (it->second.req.event == event_t::paused)
       			paused_event_failed = true;
		}

		timed_out.emplace_back(it->second.cb, it->second.req);
		m_offer_quota.erase(it->first);
		it = m_callbacks.erase(it);
	}

	// requests that couldn't even be sent before their deadline. The
	// connection is stuck connecting, or writing
	auto const pending_end = std::remove_if(m_pending.begin(), m_pending.end()
		, [&](auto const& item)
	{
		auto const* req = std::get_if<tracker_request>(&std::get<0>(item));
		if (!req || std::get<2>(item) > now) return false;

		connection_dead = true;
		timed_out.emplace_back(std::get<1>(item), *req);
		return true;
	});
	m_pending.erase(pending_end, m_pending.end());

#ifndef TORRENT_DISABLE_LOGGING
	if (auto cb = requester())
	{
		cb->debug_log("*** WEBSOCKET_TRACKER_TIMEOUT [ url: %s timed-out: %d connection-dead: %d ]"
			, tracker_req().url.c_str(), int(timed_out.size()), int(connection_dead));
	}
#endif

	// if the connection itself is dead, all outstanding announces on it
	// have failed. Remove them before closing so close() does not report
	// them using the underlying socket error. They are reported below as
	// tracker timeouts with a consistent error and message.
	if (connection_dead)
	{
		for (auto const& [hash, entry] : m_callbacks)
		{
			if (entry.pending && entry.req.event == event_t::paused)
			{
				paused_event_failed = true;
				break;
			}
		}

		for (auto it = m_callbacks.begin(); it != m_callbacks.end();)
		{
			if (!it->second.pending)
			{
				++it;
				continue;
			}

			timed_out.emplace_back(it->second.cb, it->second.req);
			m_offer_quota.erase(it->first);
			it = m_callbacks.erase(it);
		}

		if (paused_event_failed)
		{
			m_man.set_websocket_paused_support(
				tracker_req().url,
				tracker_manager::paused_event_support::unsupported);
		}

		// close() now has no pending announce callbacks to report. Its
		// purpose here is to tear down the dead connection and remove it
		// from tracker_manager so subsequent announces establish a new one.
		close(error::operation_aborted, operation_t::unknown);
	}

	for (auto const& [cb, req] : timed_out)
	{
		if (auto c = cb.lock())
		{
			c->tracker_request_error(
				req,
				errors::timed_out,
				operation_t::timer,
				"tracker announce timed out",
				seconds32{120});
		}
	}

	if (!connection_dead)
		update_announce_timer();
}

void websocket_tracker_connection::do_send(tracker_request const& req)
{
	m_req = req;
	m_offer_quota[req.info_hash] = req.num_want;

	json::object payload;
	payload["action"] = "announce";
	payload["info_hash"] = latin1_utf8(req.info_hash);
	payload["uploaded"] = req.uploaded;
	payload["downloaded"] = req.downloaded;
	payload["left"] = req.left;
	payload["corrupt"] = req.corrupt;
	payload["numwant"] = req.num_want;

	char str_key[9];
	std::snprintf(str_key, sizeof(str_key), "%08X", req.key);
	payload["key"] = str_key;

	if (req.event != event_t::none
		&& !(req.event == event_t::paused
			&& m_man.get_websocket_paused_support(req.url)
				== tracker_manager::paused_event_support::unsupported))
	{
		static const char* event_string[] = { "completed", "started", "stopped", "paused" };
		int event_index = static_cast<int>(req.event) - 1;
		TORRENT_ASSERT(event_index >= 0 && event_index < 4);
		payload["event"] = event_string[event_index];
	}

	payload["peer_id"] = latin1_utf8(req.pid);

	json::array &offers_array = payload["offers"].emplace_array();
	for (auto const& offer : req.offers)
	{
		json::object payload_offer;
		payload_offer["offer_id"] = latin1_utf8(offer.id);
		json::object &obj = payload_offer["offer"].emplace_object();
		obj["type"] = "offer";
		obj["sdp"] = offer.sdp;
		offers_array.emplace_back(std::move(payload_offer));
	}

	std::string const data = json::serialize(payload);
	m_write_data.assign(data.begin(), data.end());

#ifndef TORRENT_DISABLE_LOGGING
	if (auto cb = requester())
		cb->debug_log("*** WEBSOCKET_TRACKER_WRITE [ url: %s size: %ld, data: %s ]"
			, tracker_req().url.c_str()
			, long(m_write_data.size())
			, m_write_data.c_str());
#endif

	ADD_OUTSTANDING_ASYNC("websocket_tracker_connection::on_write");
	m_websocket->async_write(boost::asio::const_buffer(m_write_data.data(), m_write_data.size())
			, std::bind(&websocket_tracker_connection::on_write, shared_from_this(), _1, _2));
}

void websocket_tracker_connection::do_send(tracker_answer const& ans)
{
	if (!is_open()) return;

	json::object payload;
	payload["action"] = "announce";
	payload["info_hash"] = latin1_utf8(ans.info_hash);
	payload["offer_id"] = latin1_utf8(ans.answer.offer_id);
	payload["to_peer_id"] = latin1_utf8(ans.answer.pid);
	payload["peer_id"] =  latin1_utf8(ans.pid);
	json::object &obj = payload["answer"].emplace_object();
	obj["type"] = "answer";
	obj["sdp"] = ans.answer.sdp;

	std::string const data = json::serialize(payload);
	m_write_data.assign(data.begin(), data.end());

#ifndef TORRENT_DISABLE_LOGGING
	if (auto cb = requester())
		cb->debug_log("*** WEBSOCKET_TRACKER_WRITE [ url: %s size: %ld, data: %s ]"
			, tracker_req().url.c_str()
			, long(m_write_data.size())
			, m_write_data.c_str());
#endif

	ADD_OUTSTANDING_ASYNC("websocket_tracker_connection::on_write");
	m_websocket->async_write(boost::asio::const_buffer(m_write_data.data(), m_write_data.size())
			, std::bind(&websocket_tracker_connection::on_write, shared_from_this(), _1, _2));
}

void websocket_tracker_connection::do_read()
{
	if (!is_open()) return;

	// Can be replaced by m_read_buffer.clear() with boost 1.70+
	if (m_read_buffer.size() > 0) m_read_buffer.consume(m_read_buffer.size());

	ADD_OUTSTANDING_ASYNC("websocket_tracker_connection::on_read");
	m_websocket->async_read(m_read_buffer
			, std::bind(&websocket_tracker_connection::on_read, shared_from_this(), _1, _2));
}

void websocket_tracker_connection::on_timeout(error_code const& ec)
{
	// No async
	if (ec)
	{
		fail(ec, operation_t::sock_read);
		return;
	}

#ifndef TORRENT_DISABLE_LOGGING
	if (auto cb = requester())
		cb->debug_log("*** WEBSOCKET_TRACKER_TIMEOUT [ url: %s ]", tracker_req().url.c_str());
#endif

	fail(error_code(error::timed_out), operation_t::sock_read);
	close(error_code(error::timed_out), operation_t::sock_read);
}

void websocket_tracker_connection::on_connect(error_code const& ec)
{
	COMPLETE_ASYNC("websocket_tracker_connection::on_connect");
	if (ec)
	{
		fail(ec, operation_t::connect);
		close(ec, operation_t::connect);
		return;
	}

	m_last_receive = clock_type::now();
	send_pending();
	do_read();
}

void websocket_tracker_connection::on_read(error_code ec, std::size_t /* bytes_read */)
{
	COMPLETE_ASYNC("websocket_tracker_connection::on_read");
	if (ec)
	{
		if (ec != websocket::error::closed)
		{
			fail(ec, operation_t::sock_read);
			close(ec, operation_t::sock_read);
		}
		else
		{
			for (auto const& [hash, entry] : m_callbacks)
			{
				if (entry.pending && entry.req.event == event_t::paused)
				{
					m_man.set_websocket_paused_support(
						entry.req.url,
						tracker_manager::paused_event_support::unsupported);
					break;
				}
			}

			close();
		}
		return;
	}

	auto const& buf = m_read_buffer.data();

#ifndef TORRENT_DISABLE_LOGGING
	std::string str(static_cast<char const*>(buf.data()), buf.size());
	if (auto cb = requester())
		cb->debug_log("*** WEBSOCKET_TRACKER_READ [ url: %s size: %ld, data: %s ]"
			, tracker_req().url.c_str()
			, long(str.size())
			, str.c_str());
#endif

	auto ret = parse_websocket_tracker_response({static_cast<char const*>(buf.data()), long(buf.size())}, ec);
	if (ec)
	{
#ifndef TORRENT_DISABLE_LOGGING
		if (auto cb = requester())
		{
			TORRENT_ASSERT(std::holds_alternative<std::string>(ret));
			cb->debug_log("*** WEBSOCKET_TRACKER_READ [ ERROR: %s ]", std::get<std::string>(ret).c_str());
		}
#endif
		// this connection is shared by every torrent announcing to this
		// tracker, so a single message we can't make sense of (e.g. a
		// failure reason without an info_hash, which both reference
		// trackers send for requests they can't parse) must not tear it
		// down for all of them. Ignore it; a request it may have been the
		// response to will time out
		do_read();
		return;
	}

	// only consider well formed responses as received
	m_last_receive = clock_type::now();

	TORRENT_ASSERT(std::holds_alternative<websocket_tracker_response>(ret));
	auto response = std::move(std::get<websocket_tracker_response>(ret));

	std::shared_ptr<request_callback> cb;
	auto const cit = m_callbacks.find(response.info_hash);
	if (cit != m_callbacks.end())
		cb = cit->second.cb.lock();

	if (cb)
	{
		if (!response.failure_reason.empty())
		{
			// a failure reason is only the outcome of our announce if one
			// is outstanding. Otherwise it refers to something else, e.g.
			// aquatic reports an answer of ours for an offer it no longer
			// knows about this way, and must not be reported as a failed
			// announce
			if (cit->second.pending)
			{
				// learn if paused is supported
				if (cit->second.req.event == event_t::paused
					&& m_man.get_websocket_paused_support(cit->second.req.url)
						!= tracker_manager::paused_event_support::unsupported)
				{
					m_man.set_websocket_paused_support(
						cit->second.req.url,
						tracker_manager::paused_event_support::unsupported);
				}

				// mark it so close() won't also report an error for it
				cit->second.pending = false;

				cb->tracker_request_error(
					cit->second.req,
					errors::tracker_failure,
					operation_t::bittorrent,
					response.failure_reason,
					seconds32{120});

				update_announce_timer();
			}
#ifndef TORRENT_DISABLE_LOGGING
			else if (auto cb_ = requester())
			{
				cb_->debug_log("*** WEBSOCKET_TRACKER_READ [ ignoring failure reason, no announce outstanding: %s ]"
					, response.failure_reason.c_str());
			}
#endif
		}
		else
		{
			if (response.offer)
			{
				auto const quota = m_offer_quota.find(response.info_hash);
				if (quota != m_offer_quota.end() && quota->second > 0)
				{
					--quota->second;

					response.offer->answer_callback = [info_hash = response.info_hash,
														self = shared_from_this(),
														id = response.offer->id,
														pid = response.offer->pid](
														peer_id const& local_pid,
														aux::rtc_answer const& answer) {
						self->queue_answer(
							{std::move(info_hash), std::move(local_pid), std::move(answer)});
						self->start();
					};

					cb->on_rtc_offer(*response.offer);
				}
			}

			if (response.answer)
			{
				cb->on_rtc_answer(*response.answer);
			}

			// a response is only the outcome of our announce if one is
			// outstanding. Otherwise it's a duplicate, or a response to a
			// message that isn't an announce of ours (e.g. aquatic also
			// responds to the announces carrying our answers), which must
			// not be taken as a new announce outcome (it would postpone our
			// next announce)
			if (response.resp && !cit->second.pending)
			{
#ifndef TORRENT_DISABLE_LOGGING
				if (auto cb_ = requester())
					cb_->debug_log("*** WEBSOCKET_TRACKER_READ [ ignoring response, no announce outstanding ]");
#endif
			}
			else if (response.resp)
			{
				response.resp->interval = std::max(response.resp->interval
					, seconds32{m_man.settings().get_int(settings_pack::min_websocket_announce_interval)});

				if (cit->second.req.event == event_t::paused
					&& m_man.get_websocket_paused_support(cit->second.req.url)
						!= tracker_manager::paused_event_support::unsupported)
				{
					m_man.set_websocket_paused_support(
						cit->second.req.url,
						tracker_manager::paused_event_support::supported);
				}

				// this request's outcome has just been reported to its
				// requester; mark it so close() won't also report an error
				// for it.
				cit->second.pending = false;

				cb->tracker_response(cit->second.req, {}, {}, *response.resp);

				update_announce_timer();
			}
		}
	}
	else
	{
#ifndef TORRENT_DISABLE_LOGGING
		if (auto cb_ = requester())
			cb_->debug_log("*** WEBSOCKET_TRACKER_READ [ warning: no callback for info_hash ]");
#endif
		m_callbacks.erase(response.info_hash);
		m_offer_quota.erase(response.info_hash);
	}

	do_read();
}

void websocket_tracker_connection::on_write(error_code const& ec, std::size_t /* bytes_written */)
{
	COMPLETE_ASYNC("websocket_tracker_connection::on_write");
	m_write_data.clear();
	m_sending_request.reset();
	m_sending = false;
	if (ec)
	{
		fail(ec, operation_t::sock_write);
		close(ec, operation_t::sock_write);
		return;
	}

	// Continue sending
	send_pending();
}

void websocket_tracker_connection::fail(error_code const& ec, operation_t const op)
{
	// suppress tracker_connection::fail_impl()'s own report: it would
	// report m_req to requester(), but this connection multiplexes several
	// torrents' requests, so m_req may not even belong to the torrent whose
	// outcome this is. Callers follow fail() with close(ec, op), which
	// reports every still-pending m_callbacks entry using each request's
	// own stored data and the real ec/op, superseding fail_impl()'s report
	// (and its generic, no-args close()) entirely.
	m_req_pending = false;

	tracker_connection::fail(ec, op, ec.message().c_str(), seconds32{120}, seconds32{120});
}

TORRENT_EXTRA_EXPORT std::variant<websocket_tracker_response, std::string>
parse_websocket_tracker_response(span<char const> message, error_code& ec) try
{
	json::object payload = json::parse(
		{message.data(), size_t(message.size())}).as_object();

	auto it_info_hash = payload.find("info_hash");
	if (it_info_hash == payload.end() || !it_info_hash->value().is_string())
	{
		ec = error_code(errors::invalid_tracker_response);
		return "no or invalid info hash in message";
	}

	auto const raw_info_hash = utf8_latin1(it_info_hash->value().as_string());
	if (raw_info_hash.size() != 20)
	{
		ec = error_code(errors::invalid_tracker_response);
		return "invalid info hash size " + std::to_string(raw_info_hash.size());
	}

	websocket_tracker_response response;
	response.info_hash = sha1_hash(span<char const>{raw_info_hash.data(), 20});

	auto get_string = [&payload](char const* key) -> std::optional<std::string>
	{
		if (auto it = payload.find(key); it != payload.end()
			&& it->value().is_string())
		{
			return utf8_latin1(it->value().as_string());
		}
		return std::nullopt;
	};

	if (auto it = payload.find("offer"); it != payload.end())
	{
		if (!it->value().is_object())
		{
			ec = error_code(errors::invalid_tracker_response);
			return "offer is not an object";
		}

		json::object& payload_offer = it->value().as_object();
		auto const it_sdp = payload_offer.find("sdp");
		auto const id = get_string("offer_id");
		auto const pid = get_string("peer_id");

		if (it_sdp == payload_offer.end() || !it_sdp->value().is_string()
			|| !id || !pid)
		{
			ec = error_code(errors::invalid_tracker_response);
			return "missing required offer fields";
		}

		auto const& sdp = it_sdp->value().as_string();
		if (sdp.size() > aux::RTC_MAX_SDP_SIZE)
		{
			ec = error_code(errors::invalid_tracker_response);
			return "offer SDP too large";
		}

		if (id->size() != aux::RTC_OFFER_ID_LEN)
		{
			ec = error_code(errors::invalid_tracker_response);
			return "invalid offer_id size " + std::to_string(id->size());
		}

		if (pid->size() != 20)
		{
			ec = error_code(errors::invalid_tracker_response);
			return "invalid peer_id size " + std::to_string(pid->size());
		}

		aux::rtc_offer_id oid;
		std::copy(id->begin(), id->end(), oid.begin());
		response.offer.emplace(aux::rtc_offer{
			std::move(oid),
			peer_id(*pid),
			{sdp.data(), sdp.size()},
			nullptr});
	}

	if (auto it = payload.find("answer"); it != payload.end())
	{
		if (!it->value().is_object())
		{
			ec = error_code(errors::invalid_tracker_response);
			return "answer is not an object";
		}

		json::object& payload_answer = it->value().as_object();
		auto const it_sdp = payload_answer.find("sdp");
		auto const id = get_string("offer_id");
		auto const pid = get_string("peer_id");

		if (it_sdp == payload_answer.end() || !it_sdp->value().is_string()
			|| !id || !pid)
		{
			ec = error_code(errors::invalid_tracker_response);
			return "missing required answer fields";
		}

		auto const& sdp = it_sdp->value().as_string();
		if (sdp.size() > aux::RTC_MAX_SDP_SIZE)
		{
			ec = error_code(errors::invalid_tracker_response);
			return "answer SDP too large";
		}

		if (id->size() != aux::RTC_OFFER_ID_LEN)
		{
			ec = error_code(errors::invalid_tracker_response);
			return "invalid offer_id size " + std::to_string(id->size());
		}

		if (pid->size() != 20)
		{
			ec = error_code(errors::invalid_tracker_response);
			return "invalid peer_id size " + std::to_string(pid->size());
		}

		aux::rtc_offer_id oid;
		std::copy(id->begin(), id->end(), oid.begin());
		response.answer.emplace(aux::rtc_answer{
			std::move(oid),
			peer_id(*pid),
			{sdp.data(), sdp.size()}});
	}

	if (auto it = payload.find("failure reason"); it != payload.end())
	{
		if (!it->value().is_string())
		{
			ec = error_code(errors::invalid_tracker_response);
			return "failure reason is not a string";
		}

		response.failure_reason = utf8_latin1(it->value().as_string());
	}

	// A successful tracker announce response must contain an interval.
	// WebRTC offer/answer messages and tracker failure messages are handled
	// separately above.
	if (auto interval_it = payload.find("interval"); interval_it != payload.end())
	{
		if (!interval_it->value().is_int64())
		{
			ec = error_code(errors::invalid_tracker_response);
			return "invalid interval";
		}

		tracker_response& resp = response.resp.emplace();

		auto get_int64 = [&payload](char const* key, std::int64_t default_val)
			-> std::int64_t
		{
			if (auto it = payload.find(key); it != payload.end()
				&& it->value().is_int64())
			{
				return it->value().as_int64();
			}
			return default_val;
		};

		resp.interval = seconds32{interval_it->value().as_int64()};
		resp.min_interval = seconds32{get_int64("min_interval", 60)};
		resp.complete = int(get_int64("complete", -1));
		resp.incomplete = int(get_int64("incomplete", -1));
		resp.downloaded = int(get_int64("downloaded", -1));
	}
	else if (response.failure_reason.empty()
		&& !response.offer
		&& !response.answer)
	{
		// This is neither a tracker failure nor a WebRTC signalling
		// message, and it lacks the interval required for a successful
		// tracker announce response.
		ec = error_code(errors::invalid_tracker_response);
		return "missing interval in tracker response";
	}

	return response;
}
catch (boost::system::system_error const& e)
{
	ec = error_code(errors::invalid_tracker_response);
	return e.code().message();
}
catch (std::system_error const& e)
{
	ec = error_code(errors::invalid_tracker_response);
	return e.code().message();
}
catch (std::exception const& e)
{
	ec = error_code(errors::invalid_tracker_response);
	return std::string(e.what());
}

}

#endif // TORRENT_USE_RTC
