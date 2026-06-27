/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

// Fuzz the SDP parser in libdatachannel, which is the code path exercised by
// rtc_signaling::process_offer() and process_answer() when a remote peer sends
// an SDP string via the wss:// tracker.  rtc::Description's constructor parses
// the SDP synchronously; this is what the initializer-list argument in
// setRemoteDescription({sdp, "offer"}) constructs.

#include "libtorrent/config.hpp"

#if TORRENT_USE_RTC

#include <cstdint>
#include <cstddef>
#include <string>

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <rtc/description.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, std::size_t size)
{
	std::string const sdp(reinterpret_cast<char const*>(data), size);
	try
	{
		rtc::Description const desc(sdp, "offer");
	}
	catch (...)
	{}
	return 0;
}

#else

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const*, std::size_t) { return 0; }

#endif
