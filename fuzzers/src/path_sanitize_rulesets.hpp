/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_FUZZERS_PATH_SANITIZE_RULESETS_HPP_INCLUDED
#define TORRENT_FUZZERS_PATH_SANITIZE_RULESETS_HPP_INCLUDED

#include "libtorrent/path_sanitize_flags.hpp"

#include <array>

namespace fuzzers {

// the rulesets a real caller can actually end up with: unpinned
// (default_flags, same as libtorrent_2_2), one of the historical
// per-version rulesets a resume-data upgrade might pin, or the two
// extremes. Not every bit combination, since the path itself, not the
// flag combination, is the interesting search space.
constexpr std::array<lt::path_sanitize_flags_t, 5> path_sanitize_rulesets{{
	lt::path_sanitize_flags_t{},
	lt::path_sanitize_flags::all,
	lt::path_sanitize_flags::libtorrent_2_0,
	lt::path_sanitize_flags::libtorrent_2_1,
	lt::path_sanitize_flags::libtorrent_2_2,
}};

}

#endif
