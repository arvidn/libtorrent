/*

Copyright (c) 2020-2021, Alden Torres
Copyright (c) 2020-2021, Arvid Norberg
Copyright (c) 2020-2021, Paul-Louis Ageneau
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
#include "libtorrent/hex.hpp"
#include "libtorrent/aux_/torrent.hpp"
#include "libtorrent/aux_/rtc_signaling.hpp"
#include "libtorrent/aux_/rtc_stream.hpp"
#include "libtorrent/aux_/session_interface.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <rtc/rtc.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include <cstdarg>
#include <utility>
#include <sstream>
#include <mutex>

// Enable this to pass libdatachannel log to the last created session
#define DEBUG_RTC 0

namespace libtorrent {
namespace aux {

namespace errc = boost::system::errc;

namespace {

template <class T> std::weak_ptr<T> make_weak_ptr(std::shared_ptr<T> ptr) { return ptr; }

#ifndef TORRENT_DISABLE_LOGGING
std::string offer_id_hex(rtc_offer_id const& id)
{
	return aux::to_hex({id.data(), int(id.size())});
}
#endif

#if DEBUG_RTC
class rtc_log_appender
{
public:
	// write() is called from libdatachannel's background threads; the lock
	// is held for its whole body (not just the m_ses read) so unset_session()
	// can't clear the pointer out from under an in-flight write(), which
	// would otherwise race with the session being destroyed right after
	void set_session(aux::session_interface* ses)
	{
		std::lock_guard<std::mutex> l(m_mutex);
		m_ses = ses;
	}

	void unset_session(aux::session_interface* ses)
	{
		std::lock_guard<std::mutex> l(m_mutex);
		if (m_ses == ses)
			m_ses = nullptr;
	}

	void write(rtc::LogLevel level, rtc::string message)
	{
		std::lock_guard<std::mutex> l(m_mutex);
		if (!m_ses) return;

		auto &alerts = m_ses->alerts();
		if (!alerts.should_post<log_alert>()) return;

		using ::operator<<;
		std::ostringstream ss;
		ss << "libdatachannel: ";
		ss << level  << " " << message;
		alerts.emplace_alert<log_alert>(ss.str().c_str());
	}

private:
	std::mutex m_mutex;
	aux::session_interface* m_ses = nullptr;
};

static rtc_log_appender appender;
#endif

}

rtc_signaling::rtc_signaling(io_context& ioc, torrent* t, rtc_stream_handler handler
	, offer_creation_hook offer_creation_hook)
	: m_io_context(ioc)
	, m_torrent(t)
	, m_rtc_stream_handler(std::move(handler))
	, m_offer_creation_hook(std::move(offer_creation_hook))
{
#ifndef TORRENT_DISABLE_LOGGING
	debug_log("*** RTC signaling created");
#endif

	static std::once_flag flag;
#if DEBUG_RTC
	std::call_once(flag, [this]() {
		appender.set_session(&m_torrent->session());

		using namespace std::placeholders;
		rtc::InitLogger(rtc::LogLevel::Debug
				, std::bind(&rtc_log_appender::write, &appender, _1, _2));
	});
#else
	std::call_once(flag, []() {
		rtc::InitLogger(rtc::LogLevel::None, nullptr);
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
	m_num_incoming_connections = 0;

	// the offers of any outstanding batch were just destroyed along with
	// their connections, so those batches can never complete
	m_offer_batches.clear();
}

rtc_offer_id rtc_signaling::generate_offer_id() const
{
	rtc_offer_id id;
	aux::random_bytes({id.data(), int(id.size())});
	return id;
}

void rtc_signaling::generate_offers(int const count, offers_handler handler)
{
	if (count <= 0)
	{
		handler(error_code{}, {});
		return;
	}

	std::uint64_t const batch_id = m_next_batch_id++;

#ifndef TORRENT_DISABLE_LOGGING
	debug_log("*** RTC signaling generating %d offers [ batch: %llu connections: %d incoming: %d pending-batches: %d ]"
		, count, static_cast<unsigned long long>(batch_id), int(m_connections.size())
		, m_num_incoming_connections, int(m_offer_batches.size()));
#endif
	m_offer_batches.emplace(batch_id, offer_batch(count, std::move(handler)));

	peer_id const pid = m_torrent->pid();
	for (int i = 0; i < count; ++i)
	{
		rtc_offer_id const offer_id = generate_offer_id();

		try
		{
			auto& conn = create_connection(offer_id, [weak_this = weak_from_this(), offer_id, pid]
				(error_code const& ec, std::string sdp)
			{
				auto self = weak_this.lock();
				if (!self) return;

				auto& io_context = self->m_io_context;
				rtc_offer offer{offer_id, pid, std::move(sdp), {}};
				post(io_context, std::bind(&rtc_signaling::on_generated_offer
					, std::move(self)
					, ec
					, std::move(offer)
				));
			});

			conn.batch_id = batch_id;

			// test hook, called where libdatachannel may throw (e.g. when it
			// can't open a UDP socket), once the connection already exists
			if (m_offer_creation_hook)
				m_offer_creation_hook();

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
					, offer_id
					, std::move(dc_)
				));
			});

			// We need to maintain the DataChannel alive
			conn.data_channel = std::move(dc);
		}
		catch (std::exception const& e)
		{
			TORRENT_UNUSED(e);
#ifndef TORRENT_DISABLE_LOGGING
			debug_log("*** RTC signaling failed to create offer [ offer: %s ]: %s"
				, offer_id_hex(offer_id).c_str(), e.what());
#endif
			// libdatachannel throws synchronously when it can't set up the
			// ICE transport, typically because no more UDP sockets can be
			// opened. Release whatever was allocated for this offer right
			// away and still report the failure (asynchronously, like any
			// other outcome), otherwise the batch would never complete and
			// the announce waiting on it would never be sent
			if (auto const it = m_connections.find(offer_id); it != m_connections.end())
				remove_connection(it);

			post(m_io_context, std::bind(&rtc_signaling::report_offer
				, shared_from_this()
				, batch_id
				, errc::make_error_code(errc::resource_unavailable_try_again)
				, rtc_offer{offer_id, pid, {}, {}}
			));
		}
	}
}

void rtc_signaling::process_offer(rtc_offer const& offer)
{
	if (m_connections.find(offer.id) != m_connections.end())
	{
#ifndef TORRENT_DISABLE_LOGGING
		debug_log("*** RTC signaling ignored duplicate offer");
#endif
		// It seems the offer is from ourselves, ignore...
		return;
	}

	int const max_offers =
		std::max(m_torrent->settings().get_int(settings_pack::max_webtorrent_offers), 0);
	if (m_num_incoming_connections >= max_offers)
	{
#ifndef TORRENT_DISABLE_LOGGING
		debug_log("*** RTC signaling rejected offer: connection limit reached");
#endif
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
	conn.incoming = true;
	++m_num_incoming_connections;

	try
	{
		conn.peer_connection->setRemoteDescription({offer.sdp, "offer"});
	}
	catch (std::exception const& e)
	{
		TORRENT_UNUSED(e);
#ifndef TORRENT_DISABLE_LOGGING
		debug_log("*** Failed to set remote RTC offer: %s", e.what());
#endif
		auto const it = m_connections.find(offer.id);
		if (it != m_connections.end())
		{
			TORRENT_ASSERT(it->second.incoming);
			remove_connection(it);
		}
	}
}

void rtc_signaling::process_answer(rtc_answer const& answer)
{
	auto it = m_connections.find(answer.offer_id);
	if (it == m_connections.end())
	{
#ifndef TORRENT_DISABLE_LOGGING
		debug_log("*** RTC signaling ignored answer for unknown offer");
#endif
		return;
	}

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

	try
	{
		conn.peer_connection->setRemoteDescription({answer.sdp, "answer"});
	}
	catch (std::exception const& e)
	{
		TORRENT_UNUSED(e);
#ifndef TORRENT_DISABLE_LOGGING
		debug_log("*** Failed to set remote RTC answer: %s", e.what());
#endif
		remove_connection(it);
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
	pc->onStateChange([weak_this = weak_from_this(), weak_pc = make_weak_ptr(pc), offer_id]
		(rtc::PeerConnection::State state)
	{
		// Warning: this is called from another thread
		auto self = weak_this.lock();
		auto pc_ = weak_pc.lock();
		if (!self || !pc_) return;

		if (state == rtc::PeerConnection::State::Failed)
		{
			auto& io_context = self->m_io_context;
			// on_data_channel() also reports the failure to the offer's
			// batch, if its description hadn't been generated yet
			post(io_context, std::bind(&rtc_signaling::on_data_channel
				, std::move(self)
				, error_code(boost::asio::error::connection_refused)
				, offer_id
				, nullptr
			));
		}
	});

	pc->onGatheringStateChange([weak_this = weak_from_this(), weak_pc = make_weak_ptr(pc), offer_id]
		(rtc::PeerConnection::GatheringState state)
	{
		// Warning: this is called from another thread
		auto self = weak_this.lock();
		auto pc_ = weak_pc.lock();
		if (!self || !pc_) return;

		auto& io_context = self->m_io_context;


		if (state != rtc::PeerConnection::GatheringState::Complete) return;

		auto const description = pc_->localDescription();
		if (!description)
		{
			// gathering completed without a local description, which
			// means it can't be used. Report it rather than dereferencing
			// an empty optional
			post(io_context, std::bind(&rtc_signaling::on_description_generated
				, std::move(self)
				, error_code(boost::asio::error::connection_refused)
				, offer_id
				, std::string()));
			return;
		}

		post(io_context, std::bind(&rtc_signaling::on_description_generated
			, std::move(self)
			, error_code{}
			, offer_id
			, std::string(*description)));
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
	conn.handler = std::move(handler);
	conn.timer.expires_after(timeout);
	conn.timer.async_wait([self = shared_from_this(), offer_id](error_code const& ec)
	{
		// the timer is cancelled when the connection is removed
		if (ec == boost::asio::error::operation_aborted) return;
		self->on_data_channel(boost::asio::error::timed_out, offer_id, nullptr);
	});

	auto it = m_connections.emplace(offer_id, std::move(conn)).first;
	return it->second;
}

rtc_signaling::connection rtc_signaling::remove_connection(connection_map::iterator const it)
{
	if (it->second.incoming)
	{
		TORRENT_ASSERT(m_num_incoming_connections > 0);
		--m_num_incoming_connections;
	}

	connection conn = std::move(it->second);
	m_connections.erase(it);
	return conn;
}

void rtc_signaling::report_offer(std::uint64_t const batch_id, error_code const& ec, rtc_offer offer)
{
	auto const it = m_offer_batches.find(batch_id);
	if (it == m_offer_batches.end()) return;

	if (!it->second.add(ec, std::move(offer))) return;

	// detach the batch before invoking its handler, in case it calls back
	// into this object
	offer_batch batch = std::move(it->second);
	m_offer_batches.erase(it);

	batch.complete();
}

void rtc_signaling::on_description_generated(
	error_code const& ec,
	rtc_offer_id offer_id,
	std::string description)
{
	auto const it = m_connections.find(offer_id);

	// the connection is gone if it already failed or timed out, in which
	// case its outcome was reported then
	if (it == m_connections.end()) return;

	// the handler is only ever invoked once per connection. Clearing it
	// guarantees that even if gathering were to complete more than once
	auto const handler = std::exchange(it->second.handler, description_handler{});
	if (handler) handler(ec, description);
}

void rtc_signaling::on_generated_offer(error_code const& ec, rtc_offer offer)
{
	auto const it = m_connections.find(offer.id);
	// if the connection is gone, it already failed or timed out and its
	// outcome was reported to its batch at that point
	if (it == m_connections.end()) return;

	if (ec)
	{
#ifndef TORRENT_DISABLE_LOGGING
	debug_log("*** RTC signaling generated offer");
#endif
		// the offer can't be used, release its resources now rather than
		// holding on to them until the connection times out
		connection conn = remove_connection(it);
		report_offer(std::exchange(conn.batch_id, 0), ec, std::move(offer));
		return;
	}

	report_offer(std::exchange(it->second.batch_id, 0), ec, std::move(offer));
}

void rtc_signaling::on_generated_answer(error_code const& ec, rtc_answer answer, rtc_offer offer)
{
	if (ec)
	{
		// Ignore
		return;
	}
	if (m_connections.find(answer.offer_id) == m_connections.end())
		return;
#ifndef TORRENT_DISABLE_LOGGING
	debug_log("*** RTC signaling generated answer");
#endif
	TORRENT_ASSERT(offer.answer_callback);
	offer.answer_callback(m_torrent->pid(), answer);
}

void rtc_signaling::on_data_channel(error_code const& ec
		, rtc_offer_id offer_id
		, std::shared_ptr<rtc::DataChannel> dc)
{
	auto const it = m_connections.find(offer_id);
	if (it == m_connections.end())
		return;

	connection conn = remove_connection(it);

	if (ec)
	{
#ifndef TORRENT_DISABLE_LOGGING
		debug_log("*** RTC negotiation failed");
#endif
		// an outgoing offer that fails or times out before it was generated
		// (e.g. ICE gathering never completed) must still be reported to
		// its batch, otherwise the batch, and the announce waiting on it,
		// would stall forever
		if (conn.batch_id != 0)
		{
			report_offer(std::exchange(conn.batch_id, 0), ec
				, rtc_offer{offer_id, m_torrent->pid(), {}, {}});
		}
		return;
	}

#ifndef TORRENT_DISABLE_LOGGING
	debug_log("*** RTC data channel open");
#endif

	TORRENT_ASSERT(dc);
	m_rtc_stream_handler(rtc_stream_init{conn.peer_connection, dc});
}

rtc_signaling::offer_batch::offer_batch(int const count, rtc_signaling::offers_handler handler)
	: m_requested(count)
	, m_count(count)
	, m_handler(std::move(handler))
{
	TORRENT_ASSERT(count > 0);
}

bool rtc_signaling::offer_batch::add(error_code const& ec, rtc_offer offer)
{
	if (!ec)
	{
		m_offers.push_back(std::move(offer));
	}
	else
	{
		--m_count;
		m_error = ec;
	}

	return is_complete();
}

bool rtc_signaling::offer_batch::is_complete() const
{
	return int(m_offers.size()) == m_count;
}

void rtc_signaling::offer_batch::complete()
{
	m_handler(m_offers.empty() ? m_error : error_code{}, m_offers);
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
}

#endif
