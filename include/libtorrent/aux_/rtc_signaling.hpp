/*

Copyright (c) 2020-2021, Alden Torres
Copyright (c) 2020-2021, Arvid Norberg
Copyright (c) 2020, Paul-Louis Ageneau
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_RTC_SIGNALING_HPP_INCLUDED
#define TORRENT_RTC_SIGNALING_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/io_bytes.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/aux_/deadline_timer.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/functional/hash.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <queue>
#include <vector>
#include <chrono>
#include <cstdint>
#include <map>
#include <unordered_map>

namespace rtc {
	class PeerConnection;
	class DataChannel;
}

namespace libtorrent {

struct alert_manager;
struct torrent;

namespace aux {

struct rtc_stream_init;

constexpr int RTC_OFFER_ID_LEN = 20;
constexpr std::size_t RTC_MAX_SDP_SIZE = 64 * 1024;
constexpr std::size_t RTC_MAX_MESSAGE_SIZE = 2 * RTC_MAX_SDP_SIZE;

using rtc_offer_id = std::array<char, RTC_OFFER_ID_LEN>;

struct rtc_answer
{
	rtc_offer_id offer_id;
	peer_id pid;
	std::string sdp; // session description in SDP format
};

struct rtc_offer
{
	rtc_offer_id id;
	peer_id pid;
	std::string sdp; // session description in SDP format
	std::function<void(peer_id const& pid, rtc_answer const& answer)> answer_callback;
};

// This class handles client signaling for WebRTC DataChannels
struct TORRENT_EXTRA_EXPORT rtc_signaling final : std::enable_shared_from_this<rtc_signaling>
{
	using offers_handler = std::function<void(error_code const&, std::vector<rtc_offer> const&)>;
	using rtc_stream_handler = std::function<void(rtc_stream_init)>;
	using offer_creation_hook = std::function<void()>;

	explicit rtc_signaling(io_context& ioc, torrent* t, rtc_stream_handler handler
		, offer_creation_hook offer_creation_hook = {});
	~rtc_signaling();
	rtc_signaling& operator=(rtc_signaling const&) = delete;
	rtc_signaling(rtc_signaling const&) = delete;
	rtc_signaling& operator=(rtc_signaling&&) noexcept = delete;
	rtc_signaling(rtc_signaling&&) noexcept = delete;

	alert_manager& alerts() const;

	void close();
	void generate_offers(int count, offers_handler handler);
	void process_offer(rtc_offer const& offer);
	void process_answer(rtc_answer const& answer);

#ifndef TORRENT_DISABLE_LOGGING
	bool should_log() const;
	void debug_log(const char* fmt, ...) const noexcept TORRENT_FORMAT(2,3);
#endif

private:
	using description_handler = std::function<void(error_code const&, std::string const& description)>;

	struct connection
	{
		explicit connection(io_context& ioc) : timer(ioc) {}

		std::shared_ptr<rtc::PeerConnection> peer_connection;
		std::shared_ptr<rtc::DataChannel> data_channel;
		std::optional<peer_id> pid;
		bool incoming = false;

		description_handler handler;

		// for outgoing offers, the offer_batch this offer has not yet been
		// reported to, or 0 once it has. Every outgoing offer must be
		// reported to its batch exactly once (success or failure), or
		// the batch never completes and the tracker announce waiting on
		// it is never sent
		std::uint64_t batch_id = 0;

		deadline_timer timer;
	};

	rtc_offer_id generate_offer_id() const;

	using connection_map = std::unordered_map<rtc_offer_id, connection, boost::hash<rtc_offer_id>>;

	connection& create_connection(rtc_offer_id const& offer_id, description_handler handler);
	connection remove_connection(connection_map::iterator it);
	void report_offer(std::uint64_t batch_id, error_code const& ec, rtc_offer offer);
	void on_generated_offer(error_code const& ec, rtc_offer offer);
	void on_description_generated(error_code const& ec, rtc_offer_id offer_id, std::string description);
	void on_generated_answer(error_code const& ec, rtc_answer answer, rtc_offer offer);
	void on_data_channel(error_code const& ec, rtc_offer_id offer_id, std::shared_ptr<rtc::DataChannel> dc);

	io_context& m_io_context;
	torrent* m_torrent;
	rtc_stream_handler m_rtc_stream_handler;
	offer_creation_hook m_offer_creation_hook;

	std::unordered_map<rtc_offer_id, connection, boost::hash<rtc_offer_id>> m_connections;
	int m_num_incoming_connections = 0;
	std::queue<rtc_offer_id> m_queue;

	struct offer_batch
	{
		offer_batch(int count, offers_handler handler);

		// returns true once every offer in the batch has been reported
		bool add(error_code const& ec, rtc_offer offer);
		bool is_complete() const;

		// invokes the handler. The error is only reported if no offer
		// could be generated at all; a partially failed batch still
		// delivers the offers that succeeded
		void complete();

		int requested() const { return m_requested; }
		int generated() const { return int(m_offers.size()); }

	private:
		int m_requested;
		int m_count;
		error_code m_error;
		offers_handler m_handler;
		std::vector<rtc_offer> m_offers;
	};

	// outstanding batches, keyed by batch id. Offers are routed to the
	// batch they were generated for, so that a failed or slow offer in one
	// batch can't steal offers from (and stall) another one
	std::map<std::uint64_t, offer_batch> m_offer_batches;
	std::uint64_t m_next_batch_id = 1;
};

}
}

#endif
