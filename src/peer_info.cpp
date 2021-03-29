/*

Copyright (c) 2017, 2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/peer_info.hpp"

namespace lt {

	peer_info::peer_info() = default;
	peer_info::~peer_info() = default;
	peer_info::peer_info(peer_info const&) = default;
	peer_info::peer_info(peer_info&&) = default;
	peer_info& peer_info::operator=(peer_info const&) = default;
}
