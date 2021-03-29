/*

Copyright (c) 2020, Arvid Norberg
Copyright (c) 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/http_tracker_connection.hpp"

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
	lt::error_code ec;
	lt::sha1_hash const ih("abababababababababab");
	lt::span<char const> const input(reinterpret_cast<char const*>(data), size);

	lt::aux::parse_tracker_response(input, ec, lt::aux::tracker_request_flags_t{}, ih);
	lt::aux::parse_tracker_response(input, ec, lt::aux::tracker_request::scrape_request, ih);
#if TORRENT_USE_I2P
	lt::aux::parse_tracker_response(input, ec, lt::aux::tracker_request::i2p, ih);
#endif

	return 0;
}
