/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/identify_client.hpp"
#include "libtorrent/peer_id.hpp"

#include <cstring>
#include <cstdint>
#include <cstddef>
#include <algorithm>

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
	char buf[20] = {};
	if (size > 0) std::memcpy(buf, data, std::min(size, sizeof(buf)));
	lt::peer_id const pid(lt::span<char const>(buf, sizeof(buf)));
	lt::aux::identify_client_impl(pid);
	return 0;
}
