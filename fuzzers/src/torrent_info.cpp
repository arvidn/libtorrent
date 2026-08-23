/*

Copyright (c) 2019-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <array>

#include "libtorrent/load_torrent.hpp"

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
	lt::span<char const> const buf{reinterpret_cast<char const*>(data), int(size)};

	// exercise every meaningfully distinct path-sanitizer ruleset
	// against the same input in one pass, rather than relying on
	// separate corpora to ever reach the non-default ones
	std::array<lt::path_sanitize_flags_t, 3> const configs = {{
		lt::path_sanitize_flags_t{},
		lt::path_sanitize_flags::default_flags,
		lt::path_sanitize_flags::all,
	}};

	for (auto const flags : configs)
	{
		lt::load_torrent_limits cfg;
		cfg.sanitize_flags = flags;
		lt::error_code ec;
		lt::add_torrent_params atp = lt::load_torrent_buffer(buf, ec, cfg);
	}
	return 0;
}

