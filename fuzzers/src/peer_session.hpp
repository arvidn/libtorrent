/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

// Shared helpers for peer-connection fuzzers (peer_conn, rtc_peer_conn,
// ut_metadata, ut_pex).

#pragma once

#include <chrono>
#include <deque>
#include <iostream>
#include <string>
#include <array>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <memory>

#include "libtorrent/session.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/aux_/io_bytes.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/aux_/deadline_timer.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/load_torrent.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/bencode.hpp"

// Bail out of a failed one-time setup in LLVMFuzzerInitialize. libFuzzer
// ignores the return value of LLVMFuzzerInitialize, so returning an error code
// would NOT stop fuzzing -- it would proceed to call LLVMFuzzerTestOneInput
// against half-initialized global state. Terminating the process is the only
// way to abort. Prints a diagnostic and aborts.
[[noreturn]] inline void fuzz_init_failed(char const* what)
{
	std::cerr << "fuzzer initialization failed: " << what << '\n';
	std::abort();
}

// Sets alert mask, disables all outbound services and ancillary protocols,
// and applies short peer timeouts. Does NOT set listen_interfaces; each
// fuzzer sets that itself (rtc_peer_conn uses "" for no TCP listener).
inline void configure_fuzz_session(lt::settings_pack& pack)
{
	pack.set_int(lt::settings_pack::alert_mask,
		lt::alert_category::connect | lt::alert_category::peer | lt::alert_category::error
			| lt::alert_category::status
#if DEBUG_LOGGING
			| lt::alert_category::peer_log
#endif
	);
	pack.set_int(lt::settings_pack::out_enc_policy, lt::settings_pack::pe_disabled);
	pack.set_int(lt::settings_pack::in_enc_policy, lt::settings_pack::pe_disabled);
	pack.set_bool(lt::settings_pack::enable_outgoing_tcp, false);
	pack.set_bool(lt::settings_pack::enable_outgoing_utp, false);
	pack.set_bool(lt::settings_pack::enable_upnp, false);
	pack.set_bool(lt::settings_pack::enable_natpmp, false);
	pack.set_bool(lt::settings_pack::enable_dht, false);
	pack.set_bool(lt::settings_pack::enable_lsd, false);
	pack.set_bool(lt::settings_pack::enable_ip_notifier, false);
	pack.set_int(lt::settings_pack::peer_timeout, 1);
	pack.set_int(lt::settings_pack::peer_connect_timeout, 1);
	pack.set_int(lt::settings_pack::handshake_timeout, 1);
	pack.set_int(lt::settings_pack::piece_timeout, 1);
	pack.set_int(lt::settings_pack::request_timeout, 1);
	pack.set_int(lt::settings_pack::inactivity_timeout, 1);
}

// Builds a hybrid (v1 + v2) torrent that exercises as many interesting
// metadata properties as possible:
//   - empty file (0 bytes)
//   - file smaller than the 16 KiB block size (100 bytes)
//   - file smaller than a piece but spanning multiple blocks (50 KiB)
//   - large file spanning many pieces (97 MiB + 200 KiB)
// canonicalize() will sort files alphabetically and insert pad files to
// align each non-last file to a piece boundary, yielding several pad
// files in the resulting layout. The sizes are chosen so the total
// (data + pad) is exactly 100 pieces of 1 MiB, matching NUM_PIECES
// in tools/gen_corpus.py.
// Returns add_torrent_params with save_path set to ".".
inline lt::add_torrent_params make_fuzz_torrent_params()
{
	int const piece_size = 1024 * 1024;
	std::vector<lt::create_file_entry> fs;
	fs.emplace_back("fuzzer_torrent/empty.txt", 0);
	fs.emplace_back("fuzzer_torrent/large.dat", std::int64_t(piece_size) * 97 + 200 * 1024);
	fs.emplace_back("fuzzer_torrent/medium.dat", 50 * 1024);
	fs.emplace_back("fuzzer_torrent/small.txt", 100);
	lt::create_torrent t(std::move(fs), piece_size);

	for (lt::piece_index_t i : t.piece_range())
		t.set_hash(i, lt::sha1_hash("abababababababababab"));

	for (lt::file_index_t const f : t.file_range())
	{
		auto const& fe = t.file_at(f);
		if (fe.flags & lt::file_storage::flag_pad_file) continue;
		if (fe.size == 0) continue;
		for (lt::piece_index_t::diff_type i : t.file_piece_range(f))
			t.set_hash2(f,
				i,
				lt::sha256_hash(
					"abababababababababababababababababababababababababababababababab"));
	}

	std::vector<char> buf;
	lt::bencode(std::back_inserter(buf), t.generate());
	lt::add_torrent_params atp = lt::load_torrent_buffer(buf);
	atp.save_path = ".";
	return atp;
}

// Length-prefix and send a BitTorrent protocol message over a TCP socket.
inline void send_bt_message(
	lt::tcp::socket& s, std::uint8_t const msg_type, lt::span<std::uint8_t const> const payload)
{
	std::array<char, 5> hdr;
	auto it = hdr.begin();
	lt::aux::write_uint32(1 + static_cast<int>(payload.size()), it);
	lt::aux::write_uint8(msg_type, it);
	lt::error_code ec;
	std::array<boost::asio::const_buffer, 2> const bufs{
		{boost::asio::buffer(hdr), boost::asio::buffer(payload.data(), payload.size())}};
	boost::asio::write(s, bufs, ec);
}

// Length-prefix and send a BEP 10 extended protocol message over a TCP socket.
inline void send_extended_message(
	lt::tcp::socket& s, std::uint8_t const ext_id, lt::span<std::uint8_t const> const payload)
{
	std::array<char, 6> hdr;
	auto it = hdr.begin();
	lt::aux::write_uint32(2 + static_cast<int>(payload.size()), it);
	lt::aux::write_uint8(20, it);
	lt::aux::write_uint8(ext_id, it);
	lt::error_code ec;
	std::array<boost::asio::const_buffer, 2> const bufs{
		{boost::asio::buffer(hdr), boost::asio::buffer(payload.data(), payload.size())}};
	boost::asio::write(s, bufs, ec);
}

// Opens a TCP connection to 127.0.0.1:port, retrying on EINTR. The final
// connect result is reported through `ec`; callers that care (ut_metadata,
// ut_pex, pe_conn) bail when it is set, while peer_conn ignores it.
inline lt::tcp::socket connect_to_session(lt::io_context& ios, int const port, lt::error_code& ec)
{
	lt::tcp::socket s(ios);
	do
	{
		ec.clear();
		lt::error_code ignore;
		s.connect(lt::tcp::endpoint(lt::make_address("127.0.0.1", ignore), port), ec);
	}
	while (ec == boost::system::errc::interrupted);
	return s;
}

// Reads exactly buf.size() bytes from `s`, but gives up after `timeout` so a
// peer that never sends the expected bytes (and never closes the socket) cannot
// hang the fuzzer until libFuzzer's process-wide watchdog fires. The read runs
// asynchronously on `ios` against a deadline timer that closes the socket on
// expiry; closing aborts the pending read. Returns the read's error_code (set
// to a non-zero value -- typically operation_aborted -- when the timer fired).
// `ios` must be the io_context `s` was created on, and nothing else may be
// running it concurrently.
inline lt::error_code read_with_timeout(lt::io_context& ios,
	lt::tcp::socket& s,
	boost::asio::mutable_buffer const buf,
	std::chrono::milliseconds const timeout = std::chrono::milliseconds(500))
{
	bool done = false;
	lt::error_code read_ec;
	boost::asio::async_read(s, buf, [&](lt::error_code const e, std::size_t) {
		read_ec = e;
		done = true;
	});

	lt::aux::deadline_timer timer(ios);
	timer.expires_after(timeout);
	timer.async_wait([&](lt::error_code const e) {
		// deadline reached: abort the pending read
		if (!e)
		{
			lt::error_code ignore;
			s.close(ignore);
		}
	});

	ios.restart();
	while (!done)
		ios.run_one();

	timer.cancel();
	ios.poll(); // drain the now-cancelled timer handler so `ios` is clean
	return read_ec;
}

// Writes a 68-byte BitTorrent handshake. `reserved` supplies the 8
// reserved/extension bytes (pass {} for none; only the first 8 are used).
// The BEP 10 (extended) and BEP 6 (FAST) bits are always forced on so those
// message handlers are reachable regardless of what the fuzzer provided.
// `info_hash` is the 20-byte info-hash to advertise.
inline void send_bt_handshake(
	lt::tcp::socket& s, lt::sha1_hash const& info_hash, lt::span<char const> const reserved = {})
{
	std::array<char, 68> hs{};
	std::memcpy(hs.data(),
		"\x13"
		"BitTorrent protocol",
		20);
	if (!reserved.empty())
		std::memcpy(hs.data() + 20,
			reserved.data(),
			std::min<std::size_t>(8, static_cast<std::size_t>(reserved.size())));
	hs[25] |= 0x10; // BEP 10: extended protocol (byte 5, bit 4)
	hs[27] |= 0x04; // BEP 6: FAST extension (byte 7, bit 2)
	std::memcpy(hs.data() + 28, info_hash.data(), 20);
	lt::error_code ignore;
	boost::asio::write(s, boost::asio::buffer(hs), ignore);
}

// Waits up to 5 s for a torrent_resumed_alert. Used by fuzzers that have no
// TCP listen port (e.g. rtc_peer_conn). Returns 0 on success, -1 on timeout.
inline int wait_for_torrent_resume(lt::session& ses)
{
	lt::time_point const end_time = lt::clock_type::now() + lt::seconds(5);
	for (;;)
	{
		std::vector<lt::alert*> alerts;
		auto const now = lt::clock_type::now();
		if (now > end_time) return -1;
		ses.wait_for_alert(end_time - now);
		ses.pop_alerts(&alerts);
		for (auto const* a : alerts)
		{
#if DEBUG_LOGGING
			std::cout << a->message() << '\n';
#endif
			if (lt::alert_cast<lt::torrent_resumed_alert>(a)) return 0;
		}
	}
}

// Waits up to 5 s for both a TCP listen_succeeded_alert (stored in
// listen_port) and a torrent_resumed_alert. Returns 0 on success, -1 on
// timeout.
inline int wait_for_session_ready(lt::session& ses, int& listen_port)
{
	lt::time_point const end_time = lt::clock_type::now() + lt::seconds(5);
	bool resumed = false;
	while (listen_port == 0 || !resumed)
	{
		std::vector<lt::alert*> alerts;
		auto const now = lt::clock_type::now();
		if (now > end_time) return -1;
		ses.wait_for_alert(end_time - now);
		ses.pop_alerts(&alerts);
		for (auto const* a : alerts)
		{
#if DEBUG_LOGGING
			std::cout << a->message() << '\n';
#endif
			if (auto const* la = lt::alert_cast<lt::listen_succeeded_alert>(a))
			{
				if (la->socket_type == lt::socket_type_t::tcp)
				{
					listen_port = la->port;
#if DEBUG_LOGGING
					std::cout << "listening on " << listen_port << '\n';
#endif
				}
			}
			if (lt::alert_cast<lt::torrent_resumed_alert>(a)) resumed = true;
		}
	}
	return 0;
}

// Drains alerts until a peer_error_alert or peer_disconnected_alert arrives.
// Returns 0 on disconnect, -1 on 3 s timeout.
// When DEBUG_LOGGING is defined as non-zero, prints the last 10 interesting
// alerts (excluding peer_connect_alert and routine peer_log noise) before each
// disconnect so that the message path leading to it is visible.
inline int wait_for_disconnect(lt::session& ses)
{
	lt::time_point const end_time = lt::clock_type::now() + lt::seconds(3);
#if DEBUG_LOGGING
	lt::time_point start_time = lt::clock_type::now(); // min of call time and first alert
	std::deque<std::pair<lt::time_point, std::string>> recent;
	auto const print_recent = [&](lt::time_point const print_time) {
		if (recent.empty()) return;
		auto const elapsed_ms =
			std::chrono::duration_cast<std::chrono::milliseconds>(print_time - start_time).count();
		auto const begin_it = (elapsed_ms > 20)
			? recent.begin()
			: (recent.size() > 10 ? recent.end() - 10 : recent.begin());
		for (auto it = begin_it; it != recent.end(); ++it)
		{
			auto const ms =
				std::chrono::duration_cast<std::chrono::milliseconds>(it->first - start_time)
					.count();
			std::cout << "  +" << ms << "ms " << it->second << '\n';
		}
	};
#endif
	for (;;)
	{
		std::vector<lt::alert*> alerts;
		auto const now = lt::clock_type::now();
		if (now > end_time)
		{
#if DEBUG_LOGGING
			print_recent(now);
			auto const ms =
				std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
			std::cout << "  +" << ms << "ms  -- timeout --\n";
#endif
			return -1;
		}
		ses.wait_for_alert(end_time - now);
		ses.pop_alerts(&alerts);
		for (auto const* a : alerts)
		{
			if (lt::alert_cast<lt::peer_error_alert>(a)
				|| lt::alert_cast<lt::peer_disconnected_alert>(a))
			{
#if DEBUG_LOGGING
				print_recent(a->timestamp());
				std::cout << "----\n";
#endif
				return 0;
			}
#if DEBUG_LOGGING
			if (a->timestamp() < start_time) start_time = a->timestamp();
			if (lt::alert_cast<lt::peer_connect_alert>(a)) continue;
			if (auto const* pl = lt::alert_cast<lt::peer_log_alert>(a))
			{
				if (pl->direction == lt::peer_log_alert::info
					&& (pl->event_type == lt::peer_log_alert::short_lived_disconnect
						|| pl->event_type == lt::peer_log_alert::connection_closed))
					continue;
			}
			recent.push_back({a->timestamp(), a->message()});
#endif
		}
	}
}

// Bundles the state every TCP peer-connection fuzzer needs: the session, an
// io_context for the fuzzer's own outgoing socket, the listen port discovered
// at startup, and the info-hash of the single torrent that was added. Declare
// one of these at namespace scope and call init() from LLVMFuzzerInitialize.
struct peer_fuzz_session
{
	std::unique_ptr<lt::session> ses;
	lt::io_context ios;
	lt::info_hash_t info_hash;
	int listen_port = 0;

	// Creates a session listening on 127.0.0.1:0, adds the torrent that
	// `make_atp` returns, and waits for it to be ready. `make_atp` receives the
	// settings_pack (already passed through configure_fuzz_session with
	// listen_interfaces set) so a fuzzer can apply extra settings before the
	// session is created, and returns the add_torrent_params to add. The
	// info-hash is taken from the parsed torrent_info when present, otherwise
	// from the add_torrent_params info_hashes (e.g. magnet-style adds).
	// Returns 0 on success, -1 if the session did not become ready within the
	// timeout. The caller is responsible for resetting `ses` at process exit
	// (typically via std::atexit) so the session is torn down before global
	// destructors run.
	template <typename MakeAtp>
	int init(MakeAtp make_atp)
	{
		lt::settings_pack pack;
		configure_fuzz_session(pack);
		pack.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:0");

		lt::add_torrent_params atp = make_atp(pack);
		info_hash = atp.ti ? atp.ti->info_hashes() : atp.info_hashes;

		ses = std::make_unique<lt::session>(pack);
		ses->add_torrent(std::move(atp));

		return wait_for_session_ready(*ses, listen_port);
	}
};
