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

#include <functional>
#include <memory>
#include <optional>
#include <queue>
#include <vector>
#include <chrono>
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

struct rtc_offer_id : std::vector<char>
{
	rtc_offer_id() : std::vector<char>(RTC_OFFER_ID_LEN, '\0') {}
	explicit rtc_offer_id(span<char const> s) : std::vector<char>(s.begin(), s.end()) {}
};

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

	explicit rtc_signaling(io_context& ioc, torrent* t, rtc_stream_handler handler);
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

		deadline_timer timer;
	};

	rtc_offer_id generate_offer_id() const;

	connection& create_connection(rtc_offer_id const& offer_id, description_handler handler);
	void on_generated_offer(error_code const& ec, rtc_offer offer);
	void on_generated_answer(error_code const& ec, rtc_answer answer, rtc_offer offer);
	void on_data_channel(error_code const& ec, rtc_offer_id offer_id, std::shared_ptr<rtc::DataChannel> dc);

	io_context& m_io_context;
	torrent* m_torrent;
	rtc_stream_handler m_rtc_stream_handler;

	std::unordered_map<rtc_offer_id, connection, boost::hash<std::vector<char>>> m_connections;
	std::queue<rtc_offer_id> m_queue;

	struct offer_batch
	{
		offer_batch(int count, offers_handler handler);

		void add(error_code const& ec, rtc_offer offer);
		bool is_complete() const;

	private:
		int m_count;
		offers_handler m_handler;
		std::vector<rtc_offer> m_offers;
	};

	std::queue<offer_batch> m_offer_batches;
};

}
}

#endif
