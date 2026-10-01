/*

Copyright (c) 2003-2022, Arvid Norberg
Copyright (c) 2016-2018, 2020-2021, Alden Torres
Copyright (c) 2016, 2019, Andrei Kurushin
Copyright (c) 2016-2017, Pavel Pimenov
Copyright (c) 2016-2019, Steven Siloti
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/sanitize_path_element.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/aux_/string_util.hpp" // for string_equal_no_case
#include "libtorrent/aux_/utf8.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"
#include "libtorrent/path_sanitize_flags.hpp"
#include "libtorrent/string_view.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <tuple>

namespace libtorrent {

namespace {

// Which characters are valid is primarily determined by the
// filesystem, so this logic is an approximation. Note that forward- and
// backslash are filtered unconditionally and separately from this function.
// "invalid_chars" is the (possibly empty) platform-specific reserved-character
// set, precomputed once by the caller from "sanitize_flags" rather than
// re-derived on every call, since this runs once per code point of every
// path element parsed.
bool valid_path_character(std::int32_t const c, string_view const invalid_chars)
{
	// C0 controls and DEL
	if (c < 32 || c == 0x7f)
		return false;
	// C1 controls
	if (c >= 0x80 && c <= 0x9f)
		return false;
	if (c > 127)
		return true;

	// most builds run with an empty invalid_chars (no platform-specific
	// restrictions apply), so skip the search entirely in that case
	// rather than relying on the optimizer to prove it out at a call
	// site where invalid_chars is a runtime value, not a constant.
	if (invalid_chars.empty())
		return true;

	return invalid_chars.find_first_of(static_cast<char>(c)) == string_view::npos;
}

// "filter_extra_formatting_chars" mirrors
// path_sanitize_flags::filter_unicode_formatting_chars, precomputed once
// by the caller (see valid_path_character() above).
bool filter_path_character(std::int32_t const c, bool const filter_extra_formatting_chars)
{
	// Unicode directional marks and bidi embedding/override controls,
	// used to spoof filenames so they appear identical to legitimate
	// names but resolve to a different path:
	// https://security.stackexchange.com/questions/158802/how-can-this-executable-have-an-avi-extension
	// - U+200E-200F: LRM, RLM
	// - U+202A-202E: LRE, RLE, PDF, LRO, RLO (bidi embedding/override)
	if ((c >= 0x200e && c <= 0x200f) || (c >= 0x202a && c <= 0x202e))
		return true;

	if (filter_extra_formatting_chars)
	{
		// further zero-width or invisible formatting characters, usable
		// for the same kind of spoofing as above
		// - U+061C: Arabic letter mark
		// - U+200B-200D: ZWSP, ZWNJ, ZWJ
		// - U+2060-2064: word joiner and invisible math operators
		// - U+2066-2069: LRI, RLI, FSI, PDI (bidi isolates, Unicode 6.3+)
		// - U+FEFF: byte order mark / zero-width no-break space
		if (c == 0x061c || (c >= 0x200b && c <= 0x200d) || (c >= 0x2060 && c <= 0x2064)
			|| (c >= 0x2066 && c <= 0x2069) || c == 0xfeff)
			return true;
	}

	if (c > 127)
		return false;
	return c == '/' || c == '\\' || c == '\0';
}

// matches a DOS/Windows reserved device name. Windows treats the
// ISO/IEC 8859-1 superscript digits as valid COM#/LPT# digits too, e.g.
// "com\xc2\xb9" is "com" + superscript 1 (U+00B9) in UTF-8
bool is_reserved_dos_name(string_view const name)
{
	// longest entry is "clock$"; to_lower() cannot change the byte
	// length, so anything longer can never match
	if (name.size() > 6)
		return false;

	static constexpr std::array<char const*, 31> reserved_names = {{"con",
		"prn",
		"aux",
		"clock$",
		"nul",
		"com0",
		"com1",
		"com2",
		"com3",
		"com4",
		"com5",
		"com6",
		"com7",
		"com8",
		"com9",
		"com\xc2\xb9", // superscript 1 (U+00B9)
		"com\xc2\xb2", // superscript 2 (U+00B2)
		"com\xc2\xb3", // superscript 3 (U+00B3)
		"lpt0",
		"lpt1",
		"lpt2",
		"lpt3",
		"lpt4",
		"lpt5",
		"lpt6",
		"lpt7",
		"lpt8",
		"lpt9",
		"lpt\xc2\xb9", // superscript 1 (U+00B9)
		"lpt\xc2\xb2", // superscript 2 (U+00B2)
		"lpt\xc2\xb3"}}; // superscript 3 (U+00B3)

	return std::any_of(reserved_names.begin(),
		reserved_names.end(),
		[name](char const* const reserved) { return aux::string_equal_no_case(name, reserved); });
}

} // anonymous namespace

namespace aux {

// sanitizes a single path element. The result can never be empty. Empty
// files have special meaning in v2 torrents (it means the previous path
// element was the filename). Also, if we're adding the torrent name as
// the first path element, in a multi-file torrent, we must have a
// directory name.
//
// returns true if "element" needed no sanitization at all, in which case
// "path" is left untouched and the caller can use "element" itself
// (borrowed, no copy). Returns false if "path" holds the sanitized
// result instead (this is the only case where "path" is written to).
// This lets the common case, an already-valid element, skip allocating
// and copying into "path" entirely.
bool sanitize_path_element(std::string& path,
	string_view element,
	path_sanitize_flags_t const sanitize_flags,
	bool const force_element)
{
	bool const limit_unicode_chars =
		bool(sanitize_flags & path_sanitize_flags::limit_unicode_characters);
	bool const trim_trailing =
		bool(sanitize_flags & path_sanitize_flags::trim_trailing_spaces_and_dots);
	bool const filter_extra_formatting_chars =
		bool(sanitize_flags & path_sanitize_flags::filter_unicode_formatting_chars);

	// on windows, both the filesystem and the operating system impose
	// restrictions. The Android kernel probably has similar restrictions
	// as Linux (i.e. very few) but it appears some user-space system
	// libraries impose additional restrictions, and it's probably more
	// common to use FAT32 style filesystems, which also further
	// restricts valid characters:
	// https://cs.android.com/android/platform/superproject/+/master:frameworks/base/core/java/android/os/FileUtils.java;l=997?q=isValidFatFilenameChar
	string_view const invalid_chars =
		(sanitize_flags & path_sanitize_flags::sanitize_invalid_chars_win)		 ? "?<>\"|\b*:"_sv
		: (sanitize_flags & path_sanitize_flags::sanitize_invalid_chars_android) ? "\"*:<>?|"_sv
																				 : string_view{};

	if (element.size() == 1 && element[0] == '.' && !force_element)
		return false;

	if (element.empty())
	{
		path += "_";
		return false;
	}

	// until "changed" becomes true, nothing has been written to "path"
	// (it stays empty), because every byte processed so far is a
	// byte-for-byte match of "element" itself. The moment something
	// needs to diverge (a character dropped, substituted, or the
	// element truncated) materialize() copies the bytes processed up to
	// that point into "path", and every subsequent byte is written
	// there too.
	bool changed = false;
	auto const materialize = [&](std::size_t const prefix_len) {
		if (changed)
			return;
		// the sanitized output can never exceed the input length (dropped
		// or substituted characters only shrink it), plus a byte or two
		// for a trailing "_" appended after the loop. reserving up front
		// avoids repeated reallocation as bytes are appended below.
		path.reserve(element.size() + 2);
		path.assign(element.data(), prefix_len);
		changed = true;
	};

	// counts characters, not bytes like "added" below
	int unicode_chars = 0;

	int added = 0;
	// the number of dots we've added
	char num_dots = 0;
	bool found_extension = false;

	int seq_len = 0;
	for (std::size_t i = 0; i < element.size(); i += std::size_t(seq_len))
	{
		std::int32_t code_point;
		std::tie(code_point, seq_len) = parse_utf8_codepoint(element.substr(i));

		if (code_point >= 0 && filter_path_character(code_point, filter_extra_formatting_chars))
		{
			materialize(i);
			continue;
		}

		if (code_point < 0 || !valid_path_character(code_point, invalid_chars))
		{
			materialize(i);
			// invalid utf8 sequence, replace with "_"
			path += '_';
			++added;
			++unicode_chars;
			continue;
		}

		// validation passed, add it to the output string (if it's
		// already diverged from "element"; otherwise this byte is
		// already implicitly "copied" by leaving "path" untouched)
		if (changed)
		{
			path.append(element.data() + i, std::size_t(seq_len));
		}

		if (code_point == '.')
			++num_dots;

		added += seq_len;
		++unicode_chars;

		// any given path element should not
		// be more than 255 characters
		// if we exceed 240, pick up any potential
		// file extension and add that too
		if ((limit_unicode_chars ? unicode_chars : added) >= 240 && !found_extension)
		{
			int dot = -1;
			for (int j = int(element.size()) - 1; j > std::max(int(element.size()) - 10, int(i));
				 --j)
			{
				if (element[aux::numeric_cast<std::size_t>(j)] != '.')
					continue;
				dot = j;
				break;
			}
			// there is no extension
			if (dot == -1)
			{
				materialize(i + std::size_t(seq_len));
				break;
			}
			found_extension = true;
			TORRENT_ASSERT(dot > 0);
			// if the extension isn't immediately adjacent, the bytes
			// in between are dropped, which requires materializing
			if (std::size_t(dot) != i + std::size_t(seq_len))
				materialize(i + std::size_t(seq_len));
			i = std::size_t(dot - seq_len);
		}
	}

	if (added == num_dots && added <= 2)
	{
		if (changed)
		{
			// revert the invalid filename
			path.clear();
		}
		if (force_element)
		{
			// replace it with an underscore
			path += "_";
		}
		// the result ("_" or nothing) never equals the all-dots
		// "element" that got us here
		return false;
	}

	if (trim_trailing)
	{
		// remove trailing spaces and dots. These aren't allowed in
		// filenames on windows. Count how many need trimming from
		// whichever of "path" or "element" currently holds the content,
		// so an element with no trailing space/dot doesn't lose the
		// borrow optimization.
		{
			string_view current = changed ? string_view(path) : element;
			std::size_t const orig_size = current.size();
			while (!current.empty() && (current.back() == ' ' || current.back() == '.'))
				current.remove_suffix(1);
			std::size_t const trim = orig_size - current.size();
			if (trim > 0)
			{
				if (changed)
					path.resize(path.size() - trim);
				else
					materialize(element.size() - trim);
				added -= int(trim);
				TORRENT_ASSERT(added >= 0);
			}
		}

		if (force_element && added == 0)
		{
			path += "_";
		}
	}

	if (sanitize_flags & path_sanitize_flags::filter_dos_reserved_names)
	{
		// this must run last, against the fully sanitized result rather
		// than the raw "element". Otherwise a reserved name hidden behind
		// a character dropped or trimmed by one of the rules above (a
		// filtered unicode formatting character, or a trailing space/dot)
		// would slip past this check unnoticed, only for the reserved
		// name to reappear once the rest of sanitization strips those
		// characters away.
		string_view const current = changed ? string_view(path) : element;
		std::string::size_type const dot = current.find_first_of('.');
		if (is_reserved_dos_name(dot == string_view::npos ? current : current.substr(0, dot)))
		{
			// pull in the whole current content, not just the part up to
			// "dot": materialize() is meant to be called with a growing
			// prefix while walking "element" left to right, not as a
			// one-shot substitute after the fact
			if (!changed)
				materialize(element.size());
			path.insert(dot == string_view::npos ? path.size() : dot, "_");
		}
	}

	if (changed && path.empty())
		path = "_";

	return !changed;
}
}
}
