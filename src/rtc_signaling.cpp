/*

Copyright (c) 2019, Paul-Louis Ageneau
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

#include "libtorrent/alert.hpp"
#include "libtorrent/aux_/alert_manager.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/aux_/rtc_signaling.hpp"
#include "libtorrent/aux_/rtc_stream.hpp"
#include "libtorrent/aux_/session_interface.hpp"
#include "libtorrent/aux_/generate_peer_id.hpp"

#include "rtc/rtc.hpp"
#ifndef TORRENT_DISABLE_LOGGING
#include "plog/Formatters/FuncMessageFormatter.h"
#endif

#include <cstdarg>
#include <utility>
#include <sstream>

namespace libtorrent {
namespace aux {

namespace {

template <class T> std::weak_ptr<T> make_weak_ptr(std::shared_ptr<T> ptr) { return ptr; }

#ifndef TORRENT_DISABLE_LOGGING
class plog_appender : public plog::IAppender
{
public:
	void set_session(aux::session_interface* ses)
	{
		m_ses = ses;
	}

	void unset_session(aux::session_interface* ses)
	{
		if(m_ses == ses)
			m_ses = nullptr;
	}

	void write(const plog::Record& record) override
	{
		if(!m_ses) return;

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
	, m_rtc_stream_handler(handler)
{
	debug_log("*** RTC signaling created");

	static bool once = true;
	if(std::exchange(once, false)) {
#ifndef TORRENT_DISABLE_LOGGING
	appender.set_session(&m_torrent->session());
	rtc::InitLogger(plog::Severity::debug, &appender);
#else
	rtc::InitLogger(plog::Severity::none, nullptr);
#endif
	}
}

rtc_signaling::~rtc_signaling()
{
	close();

#ifndef TORRENT_DISABLE_LOGGING
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
	do {
		aux::random_bytes({id.data(), int(id.size())});
	}
	while(m_connections.find(id) != m_connections.end());

	return id;
}

void rtc_signaling::generate_offers(int count, offers_handler handler)
{
#ifndef TORRENT_DISABLE_LOGGING
	debug_log("*** RTC signaling generating %d offers", count);
#endif
	m_offer_batches.push({count, handler});
	while(count--)
	{
		rtc_offer_id offer_id = generate_offer_id();
		peer_id pid = aux::generate_peer_id(m_torrent->settings());

		auto& conn = create_connection(offer_id, [this, offer_id, pid](error_code const& ec
					, std::string const& sdp)
		{
			rtc_offer offer{std::move(offer_id), std::move(pid), sdp, {}};
			post(m_io_context, std::bind(&rtc_signaling::on_generated_offer
				, shared_from_this()
                , ec
                , offer
            ));
		});

		auto dc = conn.peer_connection->createDataChannel("webtorrent");
		auto weak_dc = make_weak_ptr(dc);
		dc->onOpen([this, weak_this = weak_from_this(), offer_id, weak_dc]()
		{
			// Warning: this is called from another thread
			auto self = weak_this.lock();
			auto dc = weak_dc.lock();
			if(!self || !dc) return;

			post(m_io_context, std::bind(&rtc_signaling::on_data_channel
				, self
				, error_code{}
				, offer_id
				, dc
			));
		});

		// We need to maintain the DataChannel alive
		conn.data_channel = dc;
	}
}

void rtc_signaling::process_offer(rtc_offer const& offer)
{
	if(auto it = m_connections.find(offer.id); it != m_connections.end()) {
		// It seems the offer is from ourselves, ignore...
		return;
	}

#ifndef TORRENT_DISABLE_LOGGING
	debug_log("*** RTC signaling processing remote offer");
#endif
	auto& conn = create_connection(offer.id, [this, offer](error_code const& ec, std::string const& sdp) {
		rtc_answer answer{offer.id, offer.pid, sdp};
        post(m_io_context, std::bind(&rtc_signaling::on_generated_answer
			, this
            , ec
            , answer
            , offer
        ));
	});

	conn.pid = offer.pid;

	try {
		conn.peer_connection->setRemoteDescription({offer.sdp, "offer"});
	}
	catch(const std::exception &e) {
#ifndef TORRENT_DISABLE_LOGGING
		debug_log("*** OOPS: Failed to set remote offer: %s", e.what());
#endif
	}
}

void rtc_signaling::process_answer(rtc_answer const& answer)
{
	auto it = m_connections.find(answer.offer_id);
	if(it == m_connections.end()) return;

#ifndef TORRENT_DISABLE_LOGGING
	debug_log("*** RTC signaling processing remote answer");
#endif

	connection& conn = it->second;
	if(conn.pid)
	{
#ifndef TORRENT_DISABLE_LOGGING
		debug_log("*** OOPS: Local RTC offer already got an answer");
#endif
		return;
	}

	conn.pid = answer.pid;

	try {
		conn.peer_connection->setRemoteDescription({answer.sdp, "answer"});
	}
	catch(const std::exception &e) {
#ifndef TORRENT_DISABLE_LOGGING
		debug_log("*** OOPS: Failed to set remote answer: %s", e.what());
#endif
	}
}

rtc_signaling::connection& rtc_signaling::create_connection(rtc_offer_id const& offer_id, description_handler handler)
{
	if(auto it = m_connections.find(offer_id); it != m_connections.end())
		return it->second;

#ifndef TORRENT_DISABLE_LOGGING
	debug_log("*** RTC signaling creating connection");
#endif

	rtc::Configuration config;
	std::string stun_server = m_torrent->settings().get_str(settings_pack::webtorrent_stun_server);
	if(!stun_server.empty())
		config.iceServers.emplace_back(std::move(stun_server));

	auto pc = std::make_shared<rtc::PeerConnection>(config);
	auto weak_pc = make_weak_ptr(pc);
	pc->onStateChange([this, weak_this = weak_from_this(), offer_id](rtc::PeerConnection::State state)
	{
		// Warning: this is called from another thread
		auto self = weak_this.lock();
        if (!self) return;

		if(state == rtc::PeerConnection::State::Failed)
		{
			post(m_io_context, std::bind(&rtc_signaling::on_data_channel
				, self
				, boost::asio::error::connection_refused
				, offer_id
				, nullptr
			));
		}
    });

	pc->onGatheringStateChange([this, weak_this = weak_from_this(), offer_id, handler, weak_pc](
			rtc::PeerConnection::GatheringState state)
	{
		// Warning: this is called from another thread
		auto self = weak_this.lock();
		auto pc = weak_pc.lock();
		if(!self || !pc) return;

		if(state == rtc::PeerConnection::GatheringState::Complete)
		{
			auto description = *pc->localDescription();
			post(m_io_context, std::bind(handler, error_code{}, description));
		}
	});

	pc->onDataChannel([this, weak_this = weak_from_this(), offer_id](
				std::shared_ptr<rtc::DataChannel> dc)
	{
		// Warning: this is called from another thread
        auto self = weak_this.lock();
        if (!self) return;

		post(m_io_context, std::bind(&rtc_signaling::on_data_channel
        	, self
        	, error_code{}
        	, offer_id
        	, dc
        ));
    });

	time_duration const timeout = seconds(m_torrent->settings().get_int(settings_pack::webtorrent_connection_timeout));
	connection conn(m_io_context);
	conn.peer_connection = pc;
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
	while(!m_offer_batches.empty() && m_offer_batches.front().is_complete())
	{
		m_offer_batches.pop();
	}

	if(!m_offer_batches.empty()) m_offer_batches.front().add(ec, std::forward<rtc_offer>(offer));
}

void rtc_signaling::on_generated_answer(error_code const& ec, rtc_answer answer, rtc_offer offer)
{
    if(ec)
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
    if(it == m_connections.end()) return;

	if(ec)
	{
#ifndef TORRENT_DISABLE_LOGGING
		debug_log("*** RTC negociation failed");
#endif
		m_connections.erase(it);
		return;
	}
#ifndef TORRENT_DISABLE_LOGGING
	debug_log("*** RTC data channel open");
#endif
	TORRENT_ASSERT(dc);
	connection const& conn = it->second;
	rtc_stream_init init{conn.peer_connection, dc};
	m_rtc_stream_handler(conn.pid.value_or(peer_id{}), init);
    m_connections.erase(it);
}

rtc_signaling::offer_batch::offer_batch(int count, rtc_signaling::offers_handler handler)
	: m_count(count)
	, m_handler(handler)
{
	if(m_count == 0) m_handler(error_code{}, {});
}

void rtc_signaling::offer_batch::add(error_code const& ec, rtc_offer &&offer)
{
	if(!ec) m_offers.push_back(std::forward<rtc_offer>(offer));
	else --m_count;

	if(is_complete()) m_handler(error_code{}, m_offers);
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
	alerts().emplace_alert<torrent_log_alert>(const_cast<torrent*>(m_torrent)->get_handle(), fmt, v);
	va_end(v);
}
catch (std::exception const&) {}
#endif

}
}

