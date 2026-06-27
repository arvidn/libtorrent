/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

// Fuzz the combined path from a wss:// tracker WebSocket message to
// libdatachannel's SDP parser. parse_websocket_tracker_response() runs the
// boost::json parser and then unescapes the "sdp" field through JSON string
// decoding (including \uXXXX sequences), so the SDP bytes that reach
// rtc::Description can differ from raw bytes fed to sdp_offer.cpp.
//
// Exercises: boost::json parsing -> JSON string unescaping -> rtc::Description
// construction (which parses the SDP) for both offer and answer message types.

#include "libtorrent/config.hpp"

#if TORRENT_USE_RTC

#include <cstdint>
#include <cstddef>
#include <variant>

#include "libtorrent/aux_/websocket_tracker_connection.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/span.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <rtc/description.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, std::size_t size)
{
	lt::error_code ec;
	auto ret = lt::aux::parse_websocket_tracker_response(
		lt::span<char const>(reinterpret_cast<char const*>(data), static_cast<long>(size)), ec);
	if (ec) return 0;
	if (!std::holds_alternative<lt::aux::websocket_tracker_response>(ret)) return 0;

	auto const& response = std::get<lt::aux::websocket_tracker_response>(ret);

	if (response.offer)
	{
		try
		{
			rtc::Description const desc(response.offer->sdp, "offer");
		}
		catch (...)
		{}
	}

	if (response.answer)
	{
		try
		{
			rtc::Description const desc(response.answer->sdp, "answer");
		}
		catch (...)
		{}
	}

	return 0;
}

#else

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const*, std::size_t) { return 0; }

#endif
