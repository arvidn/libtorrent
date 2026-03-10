/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <cstdint>
#include "libtorrent/config.hpp"

#if TORRENT_USE_RTC
#include "libtorrent/aux_/websocket_tracker_connection.hpp"

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
	lt::error_code ec;
	lt::span<char const> const input(reinterpret_cast<char const*>(data), size);
	lt::aux::parse_websocket_tracker_response(input, ec);
	return 0;
}
#endif
