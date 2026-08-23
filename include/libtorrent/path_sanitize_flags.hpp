/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_PATH_SANITIZE_FLAGS_HPP_INCLUDED
#define TORRENT_PATH_SANITIZE_FLAGS_HPP_INCLUDED

#include <cstdint>

#include "libtorrent/config.hpp"
#include "libtorrent/flags.hpp"

namespace libtorrent {

// flags controlling the rules used to turn the (untrusted) path elements
// found in a torrent's info-dict into the file layout used on disk. The
// ruleset used to create a torrent's files on disk must be preserved for
// the lifetime of that torrent, since changing it could make libtorrent
// look for files under different names than the ones actually on disk.
// See ``add_torrent_params::sanitize_flags``.
using path_sanitize_flags_t = flags::bitfield_flag<std::uint32_t, struct path_sanitize_flags_tag>;

namespace path_sanitize_flags {

// when limiting a path element to 240 characters, count unicode
// characters (code points) rather than encoded (UTF-8) bytes. A
// single character can be several bytes, so this changes how much of
// a non-ASCII path element gets truncated.
constexpr path_sanitize_flags_t limit_unicode_characters = 0_bit;

// strip trailing spaces and dots from a path element. Windows
// doesn't allow them at the end of a filename or directory name.
constexpr path_sanitize_flags_t trim_trailing_spaces_and_dots = 1_bit;

// matches a path element whose base name (ignoring any extension)
// is a DOS/Windows reserved device name (e.g. "con", "com1", "lpt1"),
// and inserts "_" after the base name, e.g. "con.txt" becomes
// ``con_.txt``. Part of ``default_flags`` and ``libtorrent_2_2`` on
// Windows (see ``default_flags``).
constexpr path_sanitize_flags_t filter_dos_reserved_names = 2_bit;

// reject the windows-reserved characters (``?<>"|\b*:``), replacing
// each with "_".
constexpr path_sanitize_flags_t sanitize_invalid_chars_win = 3_bit;

// reject the android-reserved characters (``"*:<>?|``), replacing each
// with "_".
constexpr path_sanitize_flags_t sanitize_invalid_chars_android = 4_bit;

// filter additional zero-width/invisible unicode formatting characters
// (Arabic letter mark, ZWSP/ZWNJ/ZWJ, word joiner, invisible math
// operators, bidi isolates, byte order mark) that can be used to spoof
// filenames, on top of the bidi override/embedding controls and
// LRM/RLM marks that are always filtered. Part of ``default_flags`` and
// ``libtorrent_2_1``/``libtorrent_2_2`` on every platform (see
// ``default_flags``); clear it for torrents where these extra
// characters need to survive sanitization.
constexpr path_sanitize_flags_t filter_unicode_formatting_chars = 5_bit;

// all bits combined
constexpr path_sanitize_flags_t all = path_sanitize_flags_t::all();

// the ruleset that was unconditionally in effect in libtorrent 2.0,
// before ``sanitize_flags`` existed as a concept: it did not strip the
// zero-width/invisible unicode formatting characters that
// ``filter_unicode_formatting_chars`` covers.
constexpr path_sanitize_flags_t libtorrent_2_0 = path_sanitize_flags_t{}
#ifdef TORRENT_WINDOWS
	| path_sanitize_flags::limit_unicode_characters
	| path_sanitize_flags::trim_trailing_spaces_and_dots
	| path_sanitize_flags::sanitize_invalid_chars_win
#endif
#ifdef TORRENT_ANDROID
	| path_sanitize_flags::sanitize_invalid_chars_android
#endif
	;

// the ruleset that was in effect in libtorrent 2.1: ``libtorrent_2_0``
// plus ``filter_unicode_formatting_chars``, unconditionally on every
// platform.
constexpr path_sanitize_flags_t libtorrent_2_1 =
	path_sanitize_flags::libtorrent_2_0 | path_sanitize_flags::filter_unicode_formatting_chars;

// the ruleset introduced in libtorrent 2.2: ``libtorrent_2_1`` plus
// ``filter_dos_reserved_names`` on Windows.
constexpr path_sanitize_flags_t libtorrent_2_2 = path_sanitize_flags::libtorrent_2_1
#ifdef TORRENT_WINDOWS
	| path_sanitize_flags::filter_dos_reserved_names
#endif
	;

// the ruleset applied by the deprecated (``TORRENT_ABI_VERSION`` < 4)
// ``torrent_info`` constructors and ``parse_info_section()`` overloads
// that predate ``sanitize_flags`` as an explicit parameter. A caller
// stuck on one of those APIs cannot pin a different ruleset, so this is
// the one they're always given.
constexpr path_sanitize_flags_t deprecated_default = path_sanitize_flags::libtorrent_2_1;

// the ruleset applied to newly parsed torrents that don't already have
// an explicit or pinned value. A bit, once added here for some
// condition, can never be swapped for a different one, even to
// improve it: this also governs an unpinned (no resume data) re-parse
// of an already-downloaded torrent, which must keep resolving files
// under the same names. New, independent rules are added here freely;
// improvements to what's already here ship as an opt-in bit instead.
// This always matches the newest ``libtorrent_M_N`` constant.
constexpr path_sanitize_flags_t default_flags = path_sanitize_flags::libtorrent_2_2;
}
}

#endif
