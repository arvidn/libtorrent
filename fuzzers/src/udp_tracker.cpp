/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <cstdint>
#include <vector>
#include <list>
#include <memory>
#include <iterator>
#include <string>
#include <functional>
#include <algorithm>

#include "libtorrent/io_context.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/aux_/tracker_manager.hpp"
#include "libtorrent/aux_/udp_tracker_connection.hpp"
#include "libtorrent/aux_/resolver_interface.hpp"
#include "libtorrent/aux_/session_interface.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/aux_/listen_socket_handle.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/io_bytes.hpp"

namespace {

	struct fuzz_resolver final : lt::aux::resolver_interface
	{
		void async_resolve(std::string const&, lt::aux::resolver_flags, callback_t h) override
		{
			h(lt::error_code{}, std::vector<lt::address>{lt::make_address_v4("127.0.0.1")});
		}

		void abort() override {}
		void set_cache_timeout(lt::seconds) override {}
	};

	struct fuzz_logger final : lt::aux::session_logger
	{
#ifndef TORRENT_DISABLE_LOGGING
		bool should_log() const override { return false; }
		void session_log(char const*, ...) const override {}
#endif

#if TORRENT_USE_ASSERTS
		bool is_single_thread() const override { return true; }
		bool has_peer(lt::aux::peer_connection const*) const override { return false; }
		bool any_torrent_has_peer(lt::aux::peer_connection const*) const override { return false; }
		bool is_posting_torrent_updates() const override { return false; }
#endif
	};

	struct fuzz_request_callback final : lt::aux::request_callback
	{
		void tracker_warning(lt::aux::tracker_request const&, std::string const&) override {}
		void tracker_response(lt::aux::tracker_request const&,
			lt::address const&,
			std::list<lt::address> const&,
			lt::aux::tracker_response const&) override
		{}
		void tracker_request_error(lt::aux::tracker_request const&,
			lt::error_code const&,
			lt::operation_t,
			std::string const&,
			lt::seconds32) override
		{}

#if TORRENT_USE_RTC
		void generate_rtc_offers(int,
			std::function<void(lt::error_code const&, std::vector<lt::aux::rtc_offer>)>) override
		{}
		void on_rtc_offer(lt::aux::rtc_offer const&) override {}
		void on_rtc_answer(lt::aux::rtc_answer const&) override {}
#endif

#ifndef TORRENT_DISABLE_LOGGING
		bool should_log() const override { return false; }
		void debug_log(const char*, ...) const noexcept override {}
#endif
	};

	void fuzz_send(lt::aux::listen_socket_handle const&,
		lt::udp::endpoint const&,
		lt::span<char const>,
		lt::error_code&,
		lt::aux::udp_send_flags_t)
	{}

	void fuzz_send_hostname(lt::aux::listen_socket_handle const&,
		char const*,
		int,
		lt::span<char const>,
		lt::error_code&,
		lt::aux::udp_send_flags_t)
	{}

	lt::io_context g_ios;
	lt::aux::session_settings g_settings;
	lt::counters g_counters;
	fuzz_resolver g_resolver;
	fuzz_logger g_logger;
	lt::aux::tracker_manager g_manager(
		fuzz_send, fuzz_send_hostname, g_counters, g_resolver, g_settings, g_logger);
	std::shared_ptr<fuzz_request_callback> g_cb = std::make_shared<fuzz_request_callback>();
	std::shared_ptr<lt::aux::listen_socket_t> g_listen_sock =
		std::make_shared<lt::aux::listen_socket_t>();

	// bit assignments in the per-packet control byte read from the fuzz input
	constexpr std::uint8_t ctrl_action_mask = 0x03;
	constexpr std::uint8_t ctrl_unexpected_sender = 0x04;
	constexpr std::uint8_t ctrl_override_tid = 0x08;

} // anonymous namespace

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, std::size_t size)
{
	g_manager.abort_all_requests(true);
	g_ios.restart();
	g_ios.poll();

	lt::aux::tracker_request req;
	req.url = "udp://tracker.example.com:6969/announce";
	req.info_hash = lt::sha1_hash("abababababababababab");
	req.pid = lt::peer_id("-LT0001-123456789012");
	req.listen_port = 6881;
	req.num_want = 50;
	req.outgoing_socket = lt::aux::listen_socket_handle(g_listen_sock);

	auto conn = std::make_shared<lt::aux::udp_tracker_connection>(g_ios, g_manager, req, g_cb);
	g_manager.update_transaction_id(conn, conn->transaction_id());
	conn->start();
	g_ios.restart();
	g_ios.poll();

	auto cur = reinterpret_cast<char const*>(data);
	auto const end = cur + size;
	std::vector<char> pkt;
	while (end - cur >= 3)
	{
		std::uint8_t const ctrl = *cur++;
		std::size_t const payload_len = lt::aux::read_uint16(cur);

		std::uint32_t tid = conn->transaction_id();
		if ((ctrl & ctrl_override_tid) != 0 && end - cur >= 4) tid = lt::aux::read_uint32(cur);

		std::size_t const n = std::min(payload_len, std::size_t(end - cur));
		lt::span<char const> payload(
			reinterpret_cast<char const*>(cur), static_cast<std::ptrdiff_t>(n));
		cur += n;
		std::uint32_t const action = std::uint32_t(ctrl & ctrl_action_mask);

		pkt.clear();
		pkt.reserve(8 + n);
		auto out = std::back_inserter(pkt);
		lt::aux::write_uint32(action, out);
		lt::aux::write_uint32(tid, out);
		pkt.insert(pkt.end(), payload.begin(), payload.end());

		lt::udp::endpoint ep = (ctrl & ctrl_unexpected_sender) != 0
			? lt::udp::endpoint(lt::make_address_v4("8.8.8.8"), 6969)
			: lt::udp::endpoint(lt::make_address_v4("127.0.0.1"), 6969);

		g_manager.incoming_packet(ep, {pkt.data(), static_cast<int>(pkt.size())});
		g_ios.restart();
		g_ios.poll();
	}

	conn->close();
	g_ios.restart();
	g_ios.poll();
	return 0;
}
