/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

// This fuzzer is analogous to peer_conn.cpp but connects to a libtorrent
// session via WebRTC (WebTorrent). It exercises the same bt_peer_connection
// message handling as peer_conn, but exercises the rtc_stream code path as
// well.
//
// The local session generates a WebRTC offer, the fuzzer answers it, the
// resulting rtc_stream is accepted by the session torrent (via on_rtc_stream),
// and fuzz-controlled BitTorrent wire-protocol messages are sent over the
// WebRTC data channel.

#include "libtorrent/config.hpp"

#if TORRENT_USE_RTC

#include <memory>
#include <mutex>
#include <condition_variable>
#include <iostream>
#include <string_view>
#include <array>
#include <vector>
#include <cstring>
#include <cstdint>

#include "libtorrent/session.hpp"
#include "libtorrent/session_params.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/aux_/io_bytes.hpp"
#include "libtorrent/aux_/torrent.hpp"
#include "libtorrent/aux_/rtc_signaling.hpp"
#include "libtorrent/aux_/rtc_stream.hpp"
#include "libtorrent/aux_/tracker_manager.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/span.hpp"

#include "peer_session.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <rtc/rtc.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

using namespace lt;

std::unique_ptr<session> g_ses;
std::shared_ptr<aux::torrent> g_torrent;
// request_callback* gives access to the RTC virtual methods without
// requiring the caller to be a friend of torrent.
aux::request_callback* g_rtc_cb = nullptr;
info_hash_t g_info_hash;

// Pre-encoded BEP 10 extended handshake (same IDs as peer_conn.cpp so the
// same corpus seeds work for both fuzzers).
static std::string_view const k_extended_handshake =
	"d1:md11:ut_metadatai2e6:ut_pexi1e11:upload_onlyi3ee4:reqqi500e1:v6:fuzzere";

extern "C" int LLVMFuzzerInitialize(int* /*argc*/, char*** /*argv*/)
{
	settings_pack pack;
	configure_fuzz_session(pack);
	// no TCP listen port -- connections arrive via WebRTC only
	pack.set_str(settings_pack::listen_interfaces, "");
	// no STUN server; host-only ICE candidates suffice for local connections
	pack.set_str(settings_pack::webtorrent_stun_server, "");
	// short WebRTC connection timeout so failures clean up quickly
	pack.set_int(settings_pack::webtorrent_connection_timeout, 1);

	g_ses = std::make_unique<lt::session>(pack);

	add_torrent_params atp = make_fuzz_torrent_params();

	g_info_hash = atp.ti->info_hashes();

	torrent_handle h = g_ses->add_torrent(std::move(atp));

	if (wait_for_torrent_resume(*g_ses) < 0) fuzz_init_failed("torrent did not resume");

	g_torrent = h.native_handle();
	// Cast to the public base to call the private-in-torrent virtual methods.
	g_rtc_cb = g_torrent.get();

	// Destroy g_torrent first so the session holds the last shared_ptr
	// reference: the torrent destructor (and its Boost.Asio timers) must run
	// while the io_context is still alive inside the session.  Then destroy
	// the session before global destructors (error_code categories etc.) run.
	std::atexit([] {
		g_rtc_cb = nullptr;
		g_torrent.reset();
		g_ses.reset();
	});

	return 0;
}

namespace {

	// State shared between the fuzzer thread and the session thread while
	// establishing the WebRTC data channel for one test input.
	struct result_state
	{
		std::mutex mtx;
		std::condition_variable cv;
		bool completed = false;
		std::shared_ptr<rtc::DataChannel> data_channel;
	};

	// Send an arbitrary buffer as a single binary WebRTC message.
	// Returns false if the channel is closed or the send fails.
	bool dc_send_raw(rtc::DataChannel& dc, void const* data, std::size_t size)
	{
		auto const* p = static_cast<std::byte const*>(data);
		try
		{
			dc.send(rtc::binary(p, p + size));
		}
		catch (std::exception const&)
		{
			return false;
		}
		return true;
	}

	// Send a length-prefixed BitTorrent protocol message.
	// Returns false if the channel is closed.
	bool send_bt_message(
		rtc::DataChannel& dc, std::uint8_t const msg_type, span<std::uint8_t const> const payload)
	{
		std::vector<char> msg(5 + payload.size());
		auto it = msg.begin();
		aux::write_uint32(std::uint32_t(1 + payload.size()), it);
		aux::write_uint8(msg_type, it);
		if (!payload.empty()) std::memcpy(msg.data() + 5, payload.data(), payload.size());
		return dc_send_raw(dc, msg.data(), msg.size());
	}

	// Send a BEP 10 extended protocol message.
	// Returns false if the channel is closed.
	bool send_extended_message(
		rtc::DataChannel& dc, std::uint8_t const ext_id, span<std::uint8_t const> const payload)
	{
		std::vector<char> msg(6 + payload.size());
		auto it = msg.begin();
		aux::write_uint32(std::uint32_t(2 + payload.size()), it);
		aux::write_uint8(20, it);
		aux::write_uint8(ext_id, it);
		if (!payload.empty()) std::memcpy(msg.data() + 6, payload.data(), payload.size());
		return dc_send_raw(dc, msg.data(), msg.size());
	}

} // anonymous namespace

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, std::size_t size)
{
	// 8 bytes extension flags + at least one 3-byte message header
	if (size < 11) return 0;

	io_context& ioc = g_torrent->session().get_context();

	// Shared between the fuzzer thread and the session thread.
	// Does NOT hold fuzzer_sig to avoid a cycle (see below).
	result_state result{};

	// Stream handler called on the session thread once the data channel opens.
	// Captures result (not fuzzer_sig) -- no circular reference.
	auto stream_handler = [&result](aux::rtc_stream_init init) {
		std::lock_guard<std::mutex> lock(result.mtx);
		result.data_channel = std::move(init.data_channel);
		result.completed = true;
		result.cv.notify_one();
	};

	// All torrent/signaling calls must run on the session thread.
	// We pass fuzzer_sig by value through lambdas to control its lifetime.
	post(ioc, [&result, &ioc, sh = std::move(stream_handler)]() mutable {
		// fuzzer_sig uses the session's io_context and torrent (for settings/pid)
		auto fuzzer_sig = std::make_shared<aux::rtc_signaling>(ioc, g_torrent.get(), std::move(sh));

		// Ask the session torrent to generate one WebRTC offer.
		// generate_rtc_offers creates m_rtc_signaling lazily if needed.
		// Call via the public request_callback base since the override is private.
		g_rtc_cb->generate_rtc_offers(
			1, [fuzzer_sig, &result](error_code const& ec, std::vector<aux::rtc_offer> offers) {
				if (ec || offers.empty())
				{
					std::lock_guard<std::mutex> lock(result.mtx);
					result.completed = true;
					result.cv.notify_one();
					return;
				}

				auto offer = std::move(offers[0]);

				// Route the answer from the session torrent's signaling back via
				// on_rtc_answer. Capturing fuzzer_sig here keeps it alive across
				// the async gap between offer generation and connection completion.
				offer.answer_callback = [fuzzer_sig](
											peer_id const&, aux::rtc_answer const& answer) {
					TORRENT_UNUSED(fuzzer_sig);
					g_rtc_cb->on_rtc_answer(answer);
				};

				// The fuzzer's signaling processes the session's offer and will
				// generate an answer; once both sides open a data channel the
				// stream_handler above is called.
				fuzzer_sig->process_offer(offer);
			});
	});

	// Block the fuzzer thread until the WebRTC connection is up (or times out).
	{
		std::unique_lock<std::mutex> lock(result.mtx);
		result.cv.wait_for(lock, std::chrono::seconds(10), [&result] { return result.completed; });
	}

	if (!result.data_channel) return -1;

	rtc::DataChannel& dc = *result.data_channel;

	// --- Send the BitTorrent handshake ---
	//
	// data[0..7]: extension-flag bytes (reserved).  Force BEP 10 (extended
	// protocol, byte 5 bit 0x10) and BEP 6 FAST (byte 7 bit 0x04) on so
	// those message handlers are always reachable.
	std::array<char, 68> handshake{};
	std::memcpy(handshake.data(),
		"\x13"
		"BitTorrent protocol",
		20);
	std::memcpy(handshake.data() + 20, data, 8);
	handshake[25] |= 0x10; // BEP 10 extended protocol
	handshake[27] |= 0x04; // BEP 6 FAST extension
	std::memcpy(handshake.data() + 28, g_info_hash.get_best().data(), 20);

	// alive tracks whether the data channel is still open. On failure we stop
	// sending but still fall through to the disconnect wait below so the next
	// iteration starts with a clean session state.
	bool alive = dc_send_raw(dc, handshake.data(), handshake.size());

	data += 8;
	size -= 8;

	// Fixed BEP 10 extended handshake (ext_id 0) registering the same
	// extension IDs as peer_conn.cpp so the two fuzzers share corpus seeds.
	if (alive)
		alive = send_extended_message(dc,
			0,
			{reinterpret_cast<std::uint8_t const*>(k_extended_handshake.data()),
				static_cast<std::ptrdiff_t>(k_extended_handshake.size())});

	// Parse the rest of the fuzz data as a sequence of BT messages and send
	// each with a correct 4-byte length prefix (same wire format as peer_conn):
	//
	//   [1 byte  : msg_type]
	//   [2 bytes : payload_len big-endian, capped at remaining bytes]
	//   [N bytes : payload]
	//
	// msg_type 20 is the BEP 10 extended protocol; its first payload byte is
	// the extended message ID.
	while (alive && size >= 3)
	{
		std::uint8_t const msg_type = data[0];
		std::size_t payload_len = (std::size_t(data[1]) << 8) | std::size_t(data[2]);
		data += 3;
		size -= 3;

		payload_len = std::min(payload_len, size);

		if (msg_type == 20)
		{
			if (payload_len >= 1)
				alive = send_extended_message(
					dc, data[0], {data + 1, static_cast<std::ptrdiff_t>(payload_len - 1)});
			else
				alive = send_extended_message(dc, 0, {});
		}
		else
		{
			alive = send_bt_message(dc, msg_type, {data, static_cast<std::ptrdiff_t>(payload_len)});
		}

		data += payload_len;
		size -= payload_len;
	}

	// Closing our side of the data channel signals EOF to the session's
	// peer_connection, which will disconnect.
	dc.close();
	result.data_channel.reset();

	return wait_for_disconnect(*g_ses);
}

#else // !TORRENT_USE_RTC

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* /*data*/, std::size_t /*size*/)
{
	return 0;
}

#endif // TORRENT_USE_RTC
