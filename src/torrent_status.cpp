/*

Copyright (c) 2016-2017, 2019-2020, Arvid Norberg
Copyright (c) 2016-2017, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/torrent_status.hpp"

namespace lt {

	torrent_status::torrent_status() noexcept {}
	torrent_status::~torrent_status() = default;
	torrent_status::torrent_status(torrent_status const&) = default;
	torrent_status& torrent_status::operator=(torrent_status const&) = default;
	torrent_status::torrent_status(torrent_status&&) noexcept = default;
	torrent_status& torrent_status::operator=(torrent_status&&) = default;

	static_assert(std::is_nothrow_move_constructible<torrent_status>::value
		, "should be nothrow move constructible");
	static_assert(std::is_nothrow_default_constructible<torrent_status>::value
		, "should be nothrow default constructible");
}
