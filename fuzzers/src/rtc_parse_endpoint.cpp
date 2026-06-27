/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

// Fuzz the rtc_parse_endpoint() string parser, which splits the address string
// returned by PeerConnection::remoteAddress() into a Boost.Asio endpoint. The
// remote address is ultimately derived from the peer's ICE candidates, so a
// malicious peer can influence the input to this function.

#include "libtorrent/config.hpp"

#if TORRENT_USE_RTC

#include <cstdint>
#include <cstddef>
#include <string>

#include "libtorrent/aux_/rtc_stream.hpp"
#include "libtorrent/error_code.hpp"

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, std::size_t size)
{
	std::string const addr(reinterpret_cast<char const*>(data), size);
	lt::error_code ec;
	lt::aux::rtc_parse_endpoint(addr, ec);
	return 0;
}

#else

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const*, std::size_t) { return 0; }

#endif
