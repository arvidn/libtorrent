/*

Copyright (c) 2020, Paul-Louis Ageneau
Copyright (c) 2020, Arvid Norberg
Copyright (c) 2020-2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/config.hpp"

#if TORRENT_USE_RTC

#include "libtorrent/alert.hpp"
#include "libtorrent/aux_/alert_manager.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/aux_/random.hpp"
#include "libtorrent/aux_/torrent.hpp"
#include "libtorrent/aux_/rtc_signaling.hpp"
#include "libtorrent/aux_/rtc_stream.hpp"
#include "libtorrent/aux_/session_interface.hpp"
#include "libtorrent/aux_/generate_peer_id.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <rtc/rtc.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include <cstdarg>
#include <utility>
#include <sstream>
#include <mutex>

// Enable this to pass libdatachannel log to the last created session
#define DEBUG_RTC 0

#if DEBUG_RTC
#include <plog/Formatters/FuncMessageFormatter.h>
#endif

namespace lt::aux {

namespace errc = boost::system::errc;

namespace {

template <class T> std::weak_ptr<T> make_weak_ptr(std::shared_ptr<T> ptr) { return ptr; }

#if DEBUG_RTC
class plog_appender : public plog::IAppender
{
public:
	void set_session(aux::session_interface* ses)
	{
		m_ses = ses;
	}

	void unset_session(aux::session_interface* ses)
	{
		if (m_ses == ses)
			m_ses = nullptr;
	}

	void write(const plog::Record& record) override
	{
		if (!m_ses) return;

		auto &alerts = m_ses->alerts();
		if (!alerts.should_post<log_alert>()) return;

		std::ostringstream ss;
		ss << "libdatachannel: "
			<< plog::severityToString(record.getSeverity()) << " "
			<< plog::FuncMessageFormatter::format(record);
		std::string line = ss.str();
		line.pop_back(); // remove newline
		alerts.emplace_alert<log_alert>(line.c_str());
	}

private:
	aux::session_interface* m_ses = nullptr;
};

static plog_appender appender;
#endif

}

rtc_signaling::rtc_signaling(io_context& ioc, torrent* t, rtc_stream_handler handler)
	: m_io_context(ioc)
	, m_torrent(t)
	, m_rtc_stream_handler(std::move(handler))
{
	debug_log("*** RTC signaling created");

	static std::once_flag flag;
#if DEBUG_RTC
	std::call_once(flag, [this]() {
		appender.set_session(&m_torrent->session());
		rtc::InitLogger(plog::Severity::debug, &appender);
	});
#else
	std::call_once(flag, []() {
		rtc::InitLogger(plog::Severity::none, nullptr);
	});
#endif
}

rtc_signaling::~rtc_signaling()
{
	close();

#if DEBUG_RTC
	appender.unset_session(&m_torrent->session());
#endif
}

alert_manager& rtc_signaling::alerts() const
{
	return m_torrent->alerts();
}

void rtc_signaling::close()
{
	m_connections.clear();
}

rtc_offer_id rtc_signaling::generate_offer_id() const
{
	rtc_offer_id id;
	aux::random_bytes({id.data(), int(id.size())});
	return id;
}

void rtc_signaling::generate_offers(int count, offers_handler handler)
{
#ifndef TORRENT_DISABLE_LOGGING
	debug_log("*** RTC signaling generating %d offers", count);
#endif
	m_offer_batches.push({count, std::move(handler)});
	while (count--)
	{
		rtc_offer_id offer_id = generate_offer_id();
		peer_id pid = aux::generate_peer_id(m_torrent->settings());

		auto& conn = create_connection(offer_id, [weak_this = weak_from_this(), offer_id, pid]
			(error_code const& ec, std::string sdp)
		{
			auto self = weak_this.lock();
			if (!self) return;

			auto& io_context = self->m_io_context;
			rtc_offer offer{std::move(offer_id), std::move(pid), std::move(sdp), {}};
			post(io_context, std::bind(&rtc_signaling::on_generated_offer
				, std::move(self)
				, ec
				, std::move(offer)
			));
		});

		auto dc = conn.peer_connection->createDataChannel("webtorrent");
		dc->onOpen([weak_this = weak_from_this(), offer_id, weak_dc = make_weak_ptr(dc)]()
		{
			// Warning: this is called from another thread
			auto self = weak_this.lock();
			auto dc_ = weak_dc.lock();
			if (!self || !dc_) return;

			auto& io_context = self->m_io_context;
			post(io_context, std::bind(&rtc_signaling::on_data_channel
				, std::move(self)
				, error_code{}
				, std::move(offer_id)
				, std::move(dc_)
			));
		});

		// We need to maintain the DataChannel alive
		conn.data_channel = std::move(dc);
	}
}

void rtc_signaling::process_offer(rtc_offer const& offer)
{
	if (m_connections.find(offer.id) != m_connections.end()) {
		// It seems the offer is from ourselves, ignore...
		return;
	}

#ifndef TORRENT_DISABLE_LOGGING
	debug_log("*** RTC signaling processing remote offer");
#endif
	auto& conn = create_connection(offer.id, [weak_this = weak_from_this(), offer]
		(error_code const& ec, std::string sdp)
	{
		auto self = weak_this.lock();
		if (!self) return;

		rtc_answer answer{offer.id, offer.pid, std::move(sdp)};
		auto& io_context = self->m_io_context;
		post(io_context, std::bind(&rtc_signaling::on_generated_answer
			, std::move(self)
			, ec
			, std::move(answer)
			, std::move(offer)
		));
	});

	conn.pid = offer.pid;

	try {
		conn.peer_connection->setRemoteDescription({offer.sdp, "offer"});
	}
	catch(std::exception const& e) {
#ifndef TORRENT_DISABLE_LOGGING
		debug_log("*** Failed to set remote RTC offer: %s", e.what());
#endif
	}
}

void rtc_signaling::process_answer(rtc_answer const& answer)
{
	auto it = m_connections.find(answer.offer_id);
	if (it == m_connections.end()) return;

#ifndef TORRENT_DISABLE_LOGGING
	debug_log("*** RTC signaling processing remote answer");
#endif

	connection& conn = it->second;
	if (conn.pid)
	{
#ifndef TORRENT_DISABLE_LOGGING
		debug_log("*** Local RTC offer already got an answer");
#endif
		return;
	}

	conn.pid = answer.pid;

	try {
		conn.peer_connection->setRemoteDescription({answer.sdp, "answer"});
	}
	catch(std::exception const& e) {
#ifndef TORRENT_DISABLE_LOGGING
		debug_log("*** Failed to set remote RTC answer: %s", e.what());
#endif
	}
}

rtc_signaling::connection& rtc_signaling::create_connection(rtc_offer_id const& offer_id, description_handler handler)
{
	if (auto it = m_connections.find(offer_id); it != m_connections.end())
		return it->second;

#ifndef TORRENT_DISABLE_LOGGING
	debug_log("*** RTC signaling creating connection");
#endif

	rtc::Configuration config;
	std::string stun_server = m_torrent->settings().get_str(settings_pack::webtorrent_stun_server);
	if (!stun_server.empty())
		config.iceServers.emplace_back(std::move(stun_server));

	auto pc = std::make_shared<rtc::PeerConnection>(config);
	pc->onStateChange([weak_this = weak_from_this(), weak_pc = make_weak_ptr(pc), offer_id, handler]
		(rtc::PeerConnection::State state)
	{
		// Warning: this is called from another thread
		auto self = weak_this.lock();
		auto pc_ = weak_pc.lock();
		if (!self || !pc_) return;

		if (state == rtc::PeerConnection::State::Failed)
		{
			error_code const ec = boost::asio::error::connection_refused;
			auto& io_context = self->m_io_context;

			if(pc_->gatheringState() != rtc::PeerConnection::GatheringState::Complete)
				post(io_context, std::bind(std::move(handler), ec, ""));

			post(io_context, std::bind(&rtc_signaling::on_data_channel
				, std::move(self)
				, ec
				, std::move(offer_id)
				, nullptr
			));
		}
	});

	pc->onGatheringStateChange([weak_this = weak_from_this(), weak_pc = make_weak_ptr(pc), offer_id
			, handler = std::move(handler)]
		(rtc::PeerConnection::GatheringState state)
	{
		// Warning: this is called from another thread
		auto self = weak_this.lock();
		auto pc_ = weak_pc.lock();
		if (!self || !pc_) return;

		if (state == rtc::PeerConnection::GatheringState::Complete)
		{
			auto& io_context = self->m_io_context;
			auto description = *pc_->localDescription();
			post(io_context, std::bind(std::move(handler), error_code{}, description));
		}
	});

	pc->onDataChannel([weak_this = weak_from_this(), offer_id]
		(std::shared_ptr<rtc::DataChannel> dc)
	{
		// Warning: this is called from another thread
		auto self = weak_this.lock();
		if (!self) return;

		auto& io_context = self->m_io_context;
		post(io_context, std::bind(&rtc_signaling::on_data_channel
			, std::move(self)
			, error_code{}
			, offer_id
			, dc
		));
	});

	int const connection_timeout = m_torrent->settings().get_int(settings_pack::webtorrent_connection_timeout);
	time_duration const timeout = seconds(std::max(connection_timeout, 1));
	connection conn(m_io_context);
	conn.peer_connection = std::move(pc);
	conn.timer.expires_after(timeout);
	conn.timer.async_wait(std::bind(&rtc_signaling::on_data_channel
		, shared_from_this()
		, boost::asio::error::timed_out
		, offer_id
		, nullptr
	));

	auto it = m_connections.emplace(offer_id, std::move(conn)).first;
	return it->second;
}

void rtc_signaling::on_generated_offer(error_code const& ec, rtc_offer offer)
{
#ifndef TORRENT_DISABLE_LOGGING
	debug_log("*** RTC signaling generated offer");
#endif
	while (!m_offer_batches.empty() && m_offer_batches.front().is_complete())
		m_offer_batches.pop();

	if (!m_offer_batches.empty())
		m_offer_batches.front().add(ec, std::forward<rtc_offer>(offer));
}

void rtc_signaling::on_generated_answer(error_code const& ec, rtc_answer answer, rtc_offer offer)
{
	if (ec)
	{
		// Ignore
		return;
	}
#ifndef TORRENT_DISABLE_LOGGING
	debug_log("*** RTC signaling generated answer");
#endif
	TORRENT_ASSERT(offer.answer_callback);
	peer_id pid = aux::generate_peer_id(m_torrent->settings());
	offer.answer_callback(pid, answer);
}

void rtc_signaling::on_data_channel(error_code const& ec
		, rtc_offer_id offer_id
		, std::shared_ptr<rtc::DataChannel> dc)
{
	auto it = m_connections.find(offer_id);
	if (it == m_connections.end()) return;

	connection conn = std::move(it->second);
	m_connections.erase(it);

	if (ec)
	{
#ifndef TORRENT_DISABLE_LOGGING
		debug_log("*** RTC negotiation failed");
#endif
		return;
	}

#ifndef TORRENT_DISABLE_LOGGING
	debug_log("*** RTC data channel open");
#endif

	TORRENT_ASSERT(dc);
	m_rtc_stream_handler(rtc_stream_init{conn.peer_connection, dc});
}

rtc_signaling::offer_batch::offer_batch(int count, rtc_signaling::offers_handler handler)
	: m_count(count)
	, m_handler(std::move(handler))
{
	if (m_count == 0) m_handler(error_code{}, {});
}

void rtc_signaling::offer_batch::add(error_code const& ec, rtc_offer offer)
{
	if (!ec) m_offers.push_back(std::move(offer));
	else --m_count;

	if (is_complete()) m_handler(ec, m_offers);
}

bool rtc_signaling::offer_batch::is_complete() const
{
	return int(m_offers.size()) == m_count;
}

#ifndef TORRENT_DISABLE_LOGGING
bool rtc_signaling::should_log() const
{
	return alerts().should_post<torrent_log_alert>();
}

TORRENT_FORMAT(2,3)
void rtc_signaling::debug_log(char const* fmt, ...) const noexcept try
{
	if (!alerts().should_post<torrent_log_alert>()) return;

	va_list v;
	va_start(v, fmt);
	alerts().emplace_alert<torrent_log_alert>(m_torrent->get_handle(), fmt, v);
	va_end(v);
}
catch (std::exception const&) {}
#endif

}

#endif
