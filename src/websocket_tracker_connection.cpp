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
#include <vector>

namespace lt::aux {

using namespace std::placeholders;
namespace errc = boost::system::errc;
namespace error = boost::asio::error;
namespace json = boost::json;

namespace {

// Overload for JSON strings
std::string utf8_latin1(json::string_view sv)
{
	return aux::utf8_latin1(std::string_view{sv.data(), sv.size()});
}

}

websocket_tracker_connection::websocket_tracker_connection(io_context& ios
		, tracker_manager& man
		, tracker_request const& req
		, std::weak_ptr<request_callback> cb)
	: tracker_connection(man, req, ios, cb)
	  , m_io_context(ios)
	  , m_ssl_context(ssl::context::tlsv12_client)
{
	queue_request(req, std::move(cb));
}

void websocket_tracker_connection::start()
{
	if (is_started()) return;

	auto const& settings = m_man.settings();
	auto const& req = tracker_req();
	m_websocket = std::make_shared<aux::websocket_stream>(m_io_context, m_man.host_resolver(), &m_ssl_context);

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
}

void websocket_tracker_connection::close()
{
	if (m_websocket)
	{
		m_websocket->close();
		m_websocket.reset();
	}

	error_code const ec = error::operation_aborted;
	while (!m_pending.empty())
	{
		auto [msg, callback] = std::move(m_pending.front());
		TORRENT_UNUSED(msg);
		m_pending.pop();
		if (auto cb = callback.lock())
			cb->tracker_request_error(
					tracker_req()
					, ec
					, operation_t::unknown
					, ec.message()
					, seconds32(120));
	}

	m_callbacks.clear();
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

void websocket_tracker_connection::queue_request(tracker_request req, std::weak_ptr<request_callback> cb)
{
	m_pending.emplace(tracker_message{std::move(req)}, cb);
	if (is_open()) send_pending();
}

void websocket_tracker_connection::queue_answer(tracker_answer ans)
{
	m_pending.emplace(tracker_message{std::move(ans)}, std::weak_ptr<request_callback>{});
	if (is_open()) send_pending();
}

void websocket_tracker_connection::send_pending()
{
	if (m_sending || m_pending.empty()) return;

	m_sending = true;

	auto [msg, callback] = std::move(m_pending.front());
	m_pending.pop();

	std::visit([this, callback = callback](auto const& m)
		{
			// Update requester and store callback
			if (callback.lock())
			{
				m_requester = callback;
				m_callbacks[m.info_hash] = std::move(callback);
			}

			do_send(m);
		}
		, msg);
}

void websocket_tracker_connection::do_send(tracker_request const& req)
{
	// Update request
	m_req = req;

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

	if (req.event != event_t::none)
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
		cb->debug_log("*** WEBSOCKET_TRACKER_WRITE [ size: %ld, data: %s ]", long(m_write_data.size()), m_write_data.c_str());
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
		cb->debug_log("*** WEBSOCKET_TRACKER_WRITE [ size: %ld, data: %s ]"
				, long(m_write_data.size()), m_write_data.c_str());
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
		fail(operation_t::sock_read, ec);
		return;
	}

#ifndef TORRENT_DISABLE_LOGGING
	if (auto cb = requester())
		cb->debug_log("*** WEBSOCKET_TRACKER_TIMEOUT [ url: %s ]", tracker_req().url.c_str());
#endif

	fail(operation_t::sock_read, error_code(error::timed_out));
	close();
}

void websocket_tracker_connection::on_connect(error_code const& ec)
{
	COMPLETE_ASYNC("websocket_tracker_connection::on_connect");
	if (ec)
	{
		fail(operation_t::connect, ec);
		close();
		return;
	}

	send_pending();
	do_read();
}

void websocket_tracker_connection::on_read(error_code ec, std::size_t /* bytes_read */)
{
	COMPLETE_ASYNC("websocket_tracker_connection::on_read");
	if (ec)
	{
		if(ec != websocket::error::closed)
			fail(operation_t::sock_read, ec);
		close();
		return;
	}

	auto const& buf = m_read_buffer.data();

#ifndef TORRENT_DISABLE_LOGGING
	std::string str(static_cast<char const*>(buf.data()), buf.size());
	if (auto cb = requester())
		cb->debug_log("*** WEBSOCKET_TRACKER_READ [ size: %ld, data: %s ]", long(str.size()), str.c_str());
#endif

	auto ret = parse_websocket_tracker_response({static_cast<char const*>(buf.data()), long(buf.size())}, ec);
	if(ec)
	{
#ifndef TORRENT_DISABLE_LOGGING
		if (auto cb = requester())
		{
			TORRENT_ASSERT(std::holds_alternative<std::string>(ret));
			cb->debug_log("*** WEBSOCKET_TRACKER_READ [ ERROR: %s ]", std::get<std::string>(ret).c_str());
		}
#endif
		fail(operation_t::handshake, ec);
		close();
		return;
	}

	TORRENT_ASSERT(std::holds_alternative<websocket_tracker_response>(ret));
	auto response = std::move(std::get<websocket_tracker_response>(ret));

	std::shared_ptr<request_callback> cb;
	if (auto cit = m_callbacks.find(response.info_hash); cit != m_callbacks.end())
		cb = cit->second.lock();

	if (cb)
	{
		if (response.offer)
		{
			response.offer->answer_callback =
				[info_hash = response.info_hash
					, self = shared_from_this()
					, id = response.offer->id
					, pid = response.offer->pid]
				(peer_id const& local_pid
					, aux::rtc_answer const& answer)
				{
					self->queue_answer({std::move(info_hash), std::move(local_pid), std::move(answer)});
					self->start();
				};

			cb->on_rtc_offer(*response.offer);
		}

		if(response.answer)
		{
			cb->on_rtc_answer(*response.answer);
		}

		if(response.resp)
		{
			response.resp->interval = std::max(response.resp->interval
				, seconds32{m_man.settings().get_int(settings_pack::min_websocket_announce_interval)});

			cb->tracker_response(tracker_req(), {}, {}, *response.resp);
		}
	}
	else
	{
#ifndef TORRENT_DISABLE_LOGGING
		if (auto cb_ = requester())
			cb_->debug_log("*** WEBSOCKET_TRACKER_READ [ warning: no callback for info_hash ]");
#endif
		m_callbacks.erase(response.info_hash);
	}

	do_read();
}

void websocket_tracker_connection::on_write(error_code const& ec, std::size_t /* bytes_written */)
{
	COMPLETE_ASYNC("websocket_tracker_connection::on_write");
	m_write_data.clear();
	m_sending = false;
	if (ec)
	{
		fail(operation_t::sock_write, ec);
		close();
		return;
	}

	// Continue sending
	send_pending();
}

void websocket_tracker_connection::fail(operation_t op, error_code const& ec)
{
	tracker_connection::fail(ec, op, ec.message().c_str(), seconds32{120}, seconds32{120});
}

TORRENT_EXTRA_EXPORT std::variant<websocket_tracker_response, std::string>
	parse_websocket_tracker_response(span<char const> message, error_code& ec)
try {
	json::object payload = json::parse({message.data(), size_t(message.size())}).as_object();

	auto it_info_hash = payload.find("info_hash");
	if (it_info_hash == payload.end())
		throw std::invalid_argument("no info hash in message");

	auto const raw_info_hash = utf8_latin1(it_info_hash->value().as_string());
	if (raw_info_hash.size() != 20)
		throw std::invalid_argument("invalid info hash size " + std::to_string(raw_info_hash.size()));

	websocket_tracker_response response;
	response.info_hash = sha1_hash(span<char const>{raw_info_hash.data(), 20});

	if (auto it = payload.find("offer"); it != payload.end())
	{
		json::object& payload_offer = it->value().as_object();
		auto sdp = payload_offer["sdp"].as_string();
		auto id = utf8_latin1(payload["offer_id"].as_string());
		auto pid = utf8_latin1(payload["peer_id"].as_string());
		if (pid.size() != 20)
			throw std::invalid_argument("invalid peer_id size " + std::to_string(pid.size()));

		aux::rtc_offer_id oid{span<char const>(id)};
		response.offer.emplace(aux::rtc_offer{std::move(oid), peer_id(pid), {sdp.data(), sdp.size()}, nullptr});
	}

	if (auto it = payload.find("answer"); it != payload.end())
	{
		json::object& payload_answer = it->value().as_object();
		auto sdp = payload_answer["sdp"].as_string();
		auto id = utf8_latin1(payload["offer_id"].as_string());
		auto pid = utf8_latin1(payload["peer_id"].as_string());
		if (pid.size() != 20)
			throw std::invalid_argument("invalid peer_id size " + std::to_string(pid.size()));

		aux::rtc_offer_id oid{span<char const>(id)};
		response.answer.emplace(aux::rtc_answer{std::move(oid), peer_id(pid), {sdp.data(), sdp.size()}});
	}

	if (payload.find("interval") != payload.end())
	{
		tracker_response& resp = response.resp.emplace();

		if (auto it = payload.find("interval"); it != payload.end())
			resp.interval = seconds32{it->value().as_int64()};
		else
			resp.interval = seconds32{120};

		if (auto it = payload.find("min_interval"); it != payload.end())
			resp.min_interval = seconds32{it->value().as_int64()};
		else
			resp.min_interval = seconds32{60};

		if (auto it = payload.find("complete"); it != payload.end())
			resp.complete = int(it->value().as_int64());
		else
			resp.complete = -1;

		if (auto it = payload.find("incomplete"); it != payload.end())
			resp.incomplete = int(it->value().as_int64());
		else
			resp.incomplete = -1;

		if (auto it = payload.find("downloaded"); it != payload.end())
			resp.downloaded = int(it->value().as_int64());
		else
			resp.downloaded = -1;
	}

	return response;
}
catch(std::invalid_argument const& e)
{
	ec = errc::make_error_code(errc::invalid_argument);
	return std::string(e.what());
}
catch(std::exception const& e)
{
	ec = errc::make_error_code(errc::bad_message);
	return std::string(e.what());
}

}

#endif // TORRENT_USE_RTC
