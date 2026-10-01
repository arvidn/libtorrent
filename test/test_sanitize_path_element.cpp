/*

Copyright (c) 2013-2022, Arvid Norberg
Copyright (c) 2016, 2018, 2021, Alden Torres
Copyright (c) 2017, Pavel Pimenov
Copyright (c) 2017-2019, Steven Siloti
Copyright (c) 2019, Andrei Kurushin
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"

#include "libtorrent/aux_/sanitize_path_element.hpp"
#include "libtorrent/path_sanitize_flags.hpp"
#include "libtorrent/string_view.hpp"

#include <string>

using namespace lt;

namespace {
// sanitize_path_element() only writes to "path" when the element
// actually needed sanitizing; otherwise it returns true and leaves "path"
// untouched so the caller can borrow "element" directly. This collapses
// that into a plain returned string for the tests below, and checks the
// borrow invariant along the way.
std::string sanitize(string_view const element,
	bool const force_element = false,
	lt::path_sanitize_flags_t const sanitize_flags = lt::path_sanitize_flags::default_flags)
{
	std::string path;
	bool const unchanged =
		lt::aux::sanitize_path_element(path, element, sanitize_flags, force_element);
	if (unchanged)
	{
		TEST_CHECK(path.empty());
		return std::string(element);
	}
	return path;
}
} // anonymous namespace

TORRENT_TEST(sanitize_path_truncate)
{
	TEST_EQUAL(sanitize("abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
						"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
						"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
						"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
						"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"),
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_");

	TEST_EQUAL(sanitize("abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
						"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
						"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
						"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
						"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcde.test"),
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_.test");
}

TORRENT_TEST(sanitize_path_truncate_utf)
{
	// msvc doesn't like unicode string literals, so we encode it as UTF-8 explicitly
	TEST_EQUAL(sanitize("abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
						"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
						"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
						"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
						"abcdefghi_abcdefghi_abcdefghi_abcdefghi"
						"\xE2"
						"\x80"
						"\x94"
						"abcde.jpg"),
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi"
		"\xE2"
		"\x80"
		"\x94"
		".jpg");
}

TORRENT_TEST(sanitize_path_truncate_unicode_chars)
{
	// path_sanitize_flags::limit_unicode_characters selects whether the
	// 240-limit counts unicode characters (code points) or encoded UTF-8
	// bytes. Build a multi-byte character run long enough that the two
	// rules truncate at different points, and exercise both rulesets
	// explicitly so the test doesn't depend on the platform's default.

	// U+00A9 (copyright sign), 2 bytes per character
	std::string const two_byte_char = "\xC2\xA9";

	std::string long_element;
	for (int i = 0; i < 300; ++i)
		long_element += two_byte_char;

	// counting characters: truncated after 240 characters (480 bytes)
	std::string expected_by_chars;
	for (int i = 0; i < 240; ++i)
		expected_by_chars += two_byte_char;
	TEST_EQUAL(sanitize(long_element, false, lt::path_sanitize_flags::limit_unicode_characters),
		expected_by_chars);

	// counting bytes: truncated after 240 bytes (120 characters)
	std::string expected_by_bytes;
	for (int i = 0; i < 120; ++i)
		expected_by_bytes += two_byte_char;
	TEST_EQUAL(sanitize(long_element, false, lt::path_sanitize_flags_t{}), expected_by_bytes);
}

TORRENT_TEST(sanitize_path_dos_reserved_names)
{
	// path_sanitize_flags::filter_dos_reserved_names inserts "_" right
	// after the base name of a reserved device name, keeping the rest
	// (including any extension) intact, rather than dropping the element.
	lt::path_sanitize_flags_t const filter = lt::path_sanitize_flags::filter_dos_reserved_names;

	// a reserved name gets "_" appended
	TEST_EQUAL(sanitize("con", false, filter), "con_");
	// force_element doesn't matter here, the result is already non-empty
	TEST_EQUAL(sanitize("con", true, filter), "con_");

	// the extension, and the original casing, are both preserved
	TEST_EQUAL(sanitize("con.txt", false, filter), "con_.txt");
	TEST_EQUAL(sanitize("CoM1", false, filter), "CoM1_");

	// windows treats the superscript digits U+00B9/00B2/00B3 as valid
	// COM#/LPT# digits too (encoded here as UTF-8)
	TEST_EQUAL(sanitize("com\xc2\xb9.txt", false, filter), "com\xc2\xb9_.txt");
	TEST_EQUAL(sanitize("lpt\xc2\xb2", false, filter), "lpt\xc2\xb2_");

	// the *first* dot delimits the base name: "nul.tar.gz" is equivalent
	// to "nul", not "nul.tar", so "_" goes right after "nul"
	TEST_EQUAL(sanitize("nul.tar.gz", false, filter), "nul_.tar.gz");

	// a name that isn't reserved is untouched
	TEST_EQUAL(sanitize("console", false, filter), "console");

	// the substituted element is fed back through the normal
	// sanitization, so e.g. a filtered character in the extension is
	// still dropped
	TEST_EQUAL(sanitize("con.a/b", false, filter), "con_.ab");

	// ... and the 240-character limit still applies too. The reserved-name
	// check runs after truncation (see sanitize_path_dos_reserved_names_
	// hidden_by_other_rules below for why), so the inserted "_" adds one
	// byte on top of the 240-byte truncation point.
	std::string const long_element = "nul." + std::string(240, 'x');
	std::string const long_expected = "nul_." + std::string(236, 'x');
	TEST_EQUAL(sanitize(long_element, false, filter), long_expected);

	// with the flag clear, reserved names are left alone
	TEST_EQUAL(sanitize("con", false, lt::path_sanitize_flags_t{}), "con");
}

TORRENT_TEST(sanitize_path_dos_reserved_names_hidden_by_other_rules)
{
	// path_sanitize_flags::filter_dos_reserved_names must catch a reserved
	// name that only becomes one *after* other sanitization rules run,
	// otherwise a name hidden behind a character dropped or trimmed later
	// would slip past the check unnoticed, only to reappear once the rest
	// of sanitization strips those characters away.
	lt::path_sanitize_flags_t const filter = lt::path_sanitize_flags::filter_dos_reserved_names
		| lt::path_sanitize_flags::filter_unicode_formatting_chars
		| lt::path_sanitize_flags::trim_trailing_spaces_and_dots;

	// U+200B (zero-width space, encoded here as UTF-8) sits between "co"
	// and "n"; once filter_unicode_formatting_chars strips it out, the
	// base name becomes the reserved "con"
	TEST_EQUAL(sanitize("co\xe2\x80\x8bn.txt", false, filter), "con_.txt");

	// a trailing space, once trim_trailing_spaces_and_dots removes it,
	// turns "CON " into the reserved "CON"
	TEST_EQUAL(sanitize("CON ", false, filter), "CON_");

	// sanity check: with filter_dos_reserved_names clear, the other rules
	// still filter/trim these the same way, but the now-reserved name is
	// left alone
	lt::path_sanitize_flags_t const no_dos_filter =
		lt::path_sanitize_flags::filter_unicode_formatting_chars
		| lt::path_sanitize_flags::trim_trailing_spaces_and_dots;
	TEST_EQUAL(sanitize("co\xe2\x80\x8bn.txt", false, no_dos_filter), "con.txt");
	TEST_EQUAL(sanitize("CON ", false, no_dos_filter), "CON");
}

TORRENT_TEST(sanitize_path_trailing_dots)
{
	TEST_EQUAL(sanitize("a"), "a");
	TEST_EQUAL(sanitize("c"), "c");

	// path_sanitize_flags::trim_trailing_spaces_and_dots selects whether
	// trailing dots are stripped. Exercise both rulesets explicitly so the
	// test doesn't depend on the platform's default.
	lt::path_sanitize_flags_t const trim = lt::path_sanitize_flags::trim_trailing_spaces_and_dots;
	TEST_EQUAL(sanitize("abc...", false, trim), "abc");
	TEST_EQUAL(sanitize("abc.", false, trim), "abc");
	TEST_EQUAL(sanitize("a. . .", false, trim), "a");

	TEST_EQUAL(sanitize("abc...", false, lt::path_sanitize_flags_t{}), "abc...");
	TEST_EQUAL(sanitize("abc.", false, lt::path_sanitize_flags_t{}), "abc.");
	TEST_EQUAL(sanitize("a. . .", false, lt::path_sanitize_flags_t{}), "a. . .");
}

TORRENT_TEST(sanitize_path_trailing_spaces)
{
	TEST_EQUAL(sanitize("a"), "a");
	TEST_EQUAL(sanitize("c"), "c");

	// see path_sanitize_flags::trim_trailing_spaces_and_dots, tested
	// explicitly here so this runs the same on every platform.
	lt::path_sanitize_flags_t const trim = lt::path_sanitize_flags::trim_trailing_spaces_and_dots;
	TEST_EQUAL(sanitize("abc   ", false, trim), "abc");
	TEST_EQUAL(sanitize("abc ", false, trim), "abc");

	TEST_EQUAL(sanitize("abc   ", false, lt::path_sanitize_flags_t{}), "abc   ");
	TEST_EQUAL(sanitize("abc ", false, lt::path_sanitize_flags_t{}), "abc ");
}

TORRENT_TEST(sanitize_path)
{
	TEST_EQUAL(sanitize("\0\0\xed\0\x80"), "_");

	TEST_EQUAL(sanitize("/a/"), "a");
	TEST_EQUAL(sanitize("b"), "b");
	TEST_EQUAL(sanitize("c"), "c");

	TEST_EQUAL(sanitize("a...b"), "a...b");

	TEST_EQUAL(sanitize("a"), "a");
	// ".." sanitizes to nothing (unless force_element is set)
	TEST_EQUAL(sanitize(".."), "");
	TEST_EQUAL(sanitize("c"), "c");

	// "/.." sanitizes to nothing: the "/" is filtered out, leaving the same
	// all-dots case as above. "." (without force_element) is skipped too
	TEST_EQUAL(sanitize("/.."), "");
	TEST_EQUAL(sanitize("."), "");

	// see path_sanitize_flags::sanitize_invalid_chars_win /
	// sanitize_invalid_chars_android for the flag-driven coverage; ":" is
	// invalid under either, tested explicitly here so this runs the same
	// on every platform.
	TEST_EQUAL(
		sanitize("dev:", false, lt::path_sanitize_flags::sanitize_invalid_chars_win), "dev_");
	TEST_EQUAL(sanitize("c:", false, lt::path_sanitize_flags::sanitize_invalid_chars_win), "c_");
	TEST_EQUAL(sanitize("dev:", false, lt::path_sanitize_flags_t{}), "dev:");
	TEST_EQUAL(sanitize("c:", false, lt::path_sanitize_flags_t{}), "c:");

	// leading backslash is filtered out regardless of platform
	TEST_EQUAL(sanitize("\\c"), "c");

	TEST_EQUAL(sanitize("\b"), "_");

	TEST_EQUAL(sanitize("filename"), "filename");

	TEST_EQUAL(sanitize("abc"), "abc");
	// an empty element sanitizes to "_"
	TEST_EQUAL(sanitize(""), "_");

	// path_sanitize_flags::trim_trailing_spaces_and_dots: trailing spaces
	// are trimmed; when nothing is left, the result is "_". Tested
	// explicitly here so this runs the same on every platform.
	TEST_EQUAL(sanitize("   ", false, lt::path_sanitize_flags::trim_trailing_spaces_and_dots), "_");
	TEST_EQUAL(sanitize("   ", false, lt::path_sanitize_flags_t{}), "   ");

	// "\b" is always invalid (a C0 control); "?" only under
	// sanitize_invalid_chars_win / _android
	TEST_EQUAL(
		sanitize("\b?filename=4", false, lt::path_sanitize_flags::sanitize_invalid_chars_win),
		"__filename=4");
	TEST_EQUAL(sanitize("\b?filename=4", false, lt::path_sanitize_flags_t{}), "_?filename=4");

	TEST_EQUAL(sanitize("filename=4"), "filename=4");

	// valid 2-byte sequence
	TEST_EQUAL(sanitize("filename\xc2\xa1"), "filename\xc2\xa1");

	// truncated 2-byte sequence
	TEST_EQUAL(sanitize("filename\xc2"), "filename_");

	// valid 3-byte sequence
	TEST_EQUAL(sanitize("filename\xe2\x9f\xb9"), "filename\xe2\x9f\xb9");

	// truncated 3-byte sequence
	TEST_EQUAL(sanitize("filename\xe2\x9f"), "filename_");

	// truncated 3-byte sequence
	TEST_EQUAL(sanitize("filename\xe2"), "filename_");

	// valid 4-byte sequence
	TEST_EQUAL(sanitize("filename\xf0\x9f\x92\x88"), "filename\xf0\x9f\x92\x88");

	// truncated 4-byte sequence
	TEST_EQUAL(sanitize("filename\xf0\x9f\x92"), "filename_");

	// 5-byte utf-8 sequence (not allowed)
	TEST_EQUAL(sanitize("filename\xf8\x9f\x9f\x9f\x9f"
						"foobar"),
		"filename_foobar");

	// redundant (overlong) 2-byte sequence
	// ascii code 0x2e encoded with a leading 0
	TEST_EQUAL(sanitize("filename\xc0\xae"), "filename_");

	// redundant (overlong) 3-byte sequence
	// ascii code 0x2e encoded with two leading 0s
	TEST_EQUAL(sanitize("filename\xe0\x80\xae"), "filename_");

	// redundant (overlong) 4-byte sequence
	// ascii code 0x2e encoded with three leading 0s
	TEST_EQUAL(sanitize("filename\xf0\x80\x80\xae"), "filename_");

	// a filename where every character is filtered is not replaced by an underscore
	TEST_EQUAL(sanitize("//\\"), "");

	// make sure suspicious unicode characters are filtered out
	// that's utf-8 for U+200e LEFT-TO-RIGHT MARK
	TEST_EQUAL(sanitize("foo\xe2\x80\x8e"
						"bar"),
		"foobar");

	// make sure suspicious unicode characters are filtered out
	// that's utf-8 for U+202b RIGHT-TO-LEFT EMBEDDING
	TEST_EQUAL(sanitize("foo\xe2\x80\xab"
						"bar"),
		"foobar");
}

TORRENT_TEST(sanitize_path_control_chars)
{
	// DEL (U+007F) is replaced with '_'
	TEST_EQUAL(sanitize("foo\x7f"
						"bar"),
		"foo_bar");

	// C1 control U+0080 (utf-8: c2 80) is replaced with '_'
	TEST_EQUAL(sanitize("foo\xc2\x80"
						"bar"),
		"foo_bar");

	// C1 control U+009F (utf-8: c2 9f) is replaced with '_'
	TEST_EQUAL(sanitize("foo\xc2\x9f"
						"bar"),
		"foo_bar");
}

TORRENT_TEST(sanitize_path_format_chars)
{
	// zero-width space U+200B (utf-8: e2 80 8b) is silently dropped
	TEST_EQUAL(sanitize("foo\xe2\x80\x8b"
						"bar"),
		"foobar");

	// zero-width non-joiner U+200C (utf-8: e2 80 8c) is dropped
	TEST_EQUAL(sanitize("foo\xe2\x80\x8c"
						"bar"),
		"foobar");

	// zero-width joiner U+200D (utf-8: e2 80 8d) is dropped
	TEST_EQUAL(sanitize("foo\xe2\x80\x8d"
						"bar"),
		"foobar");

	// word joiner U+2060 (utf-8: e2 81 a0) is dropped
	TEST_EQUAL(sanitize("foo\xe2\x81\xa0"
						"bar"),
		"foobar");

	// invisible times U+2062 (utf-8: e2 81 a2) is dropped
	TEST_EQUAL(sanitize("foo\xe2\x81\xa2"
						"bar"),
		"foobar");

	// LRI U+2066 (utf-8: e2 81 a6) is dropped
	TEST_EQUAL(sanitize("foo\xe2\x81\xa6"
						"bar"),
		"foobar");

	// PDI U+2069 (utf-8: e2 81 a9) is dropped
	TEST_EQUAL(sanitize("foo\xe2\x81\xa9"
						"bar"),
		"foobar");

	// Arabic letter mark U+061C (utf-8: d8 9c) is dropped
	TEST_EQUAL(sanitize("foo\xd8\x9c"
						"bar"),
		"foobar");

	// BOM / zero-width no-break space U+FEFF (utf-8: ef bb bf) is dropped
	TEST_EQUAL(sanitize("foo\xef\xbb\xbf"
						"bar"),
		"foobar");
}

TORRENT_TEST(sanitize_path_force)
{
	TEST_EQUAL(sanitize("\0\0\xed\0\x80", true), "_");

	TEST_EQUAL(sanitize("/a/", true), "a");
	TEST_EQUAL(sanitize("b", true), "b");
	TEST_EQUAL(sanitize("c", true), "c");

	TEST_EQUAL(sanitize("a...b", true), "a...b");

	TEST_EQUAL(sanitize("a", true), "a");
	// with force_element, ".." is replaced with "_" instead of being skipped
	TEST_EQUAL(sanitize("..", true), "_");
	TEST_EQUAL(sanitize("c", true), "c");

	// "/.." : the "/" is filtered out, leaving the same all-dots case as
	// above. "." also becomes "_" with force_element set
	TEST_EQUAL(sanitize("/..", true), "_");
	TEST_EQUAL(sanitize(".", true), "_");

	// see path_sanitize_flags::sanitize_invalid_chars_win /
	// sanitize_invalid_chars_android for the flag-driven coverage; ":" is
	// invalid under either, tested explicitly here so this runs the same
	// on every platform.
	TEST_EQUAL(sanitize("dev:", true, lt::path_sanitize_flags::sanitize_invalid_chars_win), "dev_");
	TEST_EQUAL(sanitize("c:", true, lt::path_sanitize_flags::sanitize_invalid_chars_win), "c_");
	TEST_EQUAL(sanitize("dev:", true, lt::path_sanitize_flags_t{}), "dev:");
	TEST_EQUAL(sanitize("c:", true, lt::path_sanitize_flags_t{}), "c:");

	// leading backslash is filtered out regardless of platform
	TEST_EQUAL(sanitize("\\c", true), "c");

	TEST_EQUAL(sanitize("\b", true), "_");

	TEST_EQUAL(sanitize("filename", true), "filename");

	TEST_EQUAL(sanitize("abc", true), "abc");
	TEST_EQUAL(sanitize("", true), "_");

	// see path_sanitize_flags::trim_trailing_spaces_and_dots, tested
	// explicitly here so this runs the same on every platform.
	TEST_EQUAL(sanitize("   ", true, lt::path_sanitize_flags::trim_trailing_spaces_and_dots), "_");
	TEST_EQUAL(sanitize("   ", true, lt::path_sanitize_flags_t{}), "   ");

	// "\b" is always invalid (a C0 control); "?" only under
	// sanitize_invalid_chars_win / _android
	TEST_EQUAL(sanitize("\b?filename=4", true, lt::path_sanitize_flags::sanitize_invalid_chars_win),
		"__filename=4");
	TEST_EQUAL(sanitize("\b?filename=4", true, lt::path_sanitize_flags_t{}), "_?filename=4");

	TEST_EQUAL(sanitize("filename=4", true), "filename=4");

	// valid 2-byte sequence
	TEST_EQUAL(sanitize("filename\xc2\xa1", true), "filename\xc2\xa1");

	// truncated 2-byte sequence
	TEST_EQUAL(sanitize("filename\xc2", true), "filename_");

	// valid 3-byte sequence
	TEST_EQUAL(sanitize("filename\xe2\x9f\xb9", true), "filename\xe2\x9f\xb9");

	// truncated 3-byte sequence
	TEST_EQUAL(sanitize("filename\xe2\x9f", true), "filename_");

	// truncated 3-byte sequence
	TEST_EQUAL(sanitize("filename\xe2", true), "filename_");

	// valid 4-byte sequence
	TEST_EQUAL(sanitize("filename\xf0\x9f\x92\x88", true), "filename\xf0\x9f\x92\x88");

	// truncated 4-byte sequence
	TEST_EQUAL(sanitize("filename\xf0\x9f\x92", true), "filename_");

	// 5-byte utf-8 sequence (not allowed)
	TEST_EQUAL(sanitize("filename\xf8\x9f\x9f\x9f\x9f"
						"foobar",
				   true),
		"filename_foobar");

	// redundant (overlong) 2-byte sequence
	// ascii code 0x2e encoded with a leading 0
	TEST_EQUAL(sanitize("filename\xc0\xae", true), "filename_");

	// redundant (overlong) 3-byte sequence
	// ascii code 0x2e encoded with two leading 0s
	TEST_EQUAL(sanitize("filename\xe0\x80\xae", true), "filename_");

	// redundant (overlong) 4-byte sequence
	// ascii code 0x2e encoded with three leading 0s
	TEST_EQUAL(sanitize("filename\xf0\x80\x80\xae", true), "filename_");

	// a filename where every character is filtered is replaced by an underscore
	// when force_element is set
	TEST_EQUAL(sanitize("//\\", true), "_");

	// make sure suspicious unicode characters are filtered out
	// that's utf-8 for U+200e LEFT-TO-RIGHT MARK
	TEST_EQUAL(sanitize("foo\xe2\x80\x8e"
						"bar",
				   true),
		"foobar");

	// make sure suspicious unicode characters are filtered out
	// that's utf-8 for U+202b RIGHT-TO-LEFT EMBEDDING
	TEST_EQUAL(sanitize("foo\xe2\x80\xab"
						"bar",
				   true),
		"foobar");
}

TORRENT_TEST(sanitize_path_zeroes)
{
	TEST_EQUAL(sanitize("\0foo"), "_");
	TEST_EQUAL(sanitize("\0\0\0\0"), "_");
}

TORRENT_TEST(sanitize_path_colon)
{
	// sanitize_invalid_chars_win and sanitize_invalid_chars_android reject
	// the same set of printable characters (win additionally lists "\b",
	// but that's already a C0 control, rejected unconditionally)
	TEST_EQUAL(
		sanitize("foo:bar", false, lt::path_sanitize_flags::sanitize_invalid_chars_win), "foo_bar");
	TEST_EQUAL(sanitize("foo:bar", false, lt::path_sanitize_flags::sanitize_invalid_chars_android),
		"foo_bar");
	TEST_EQUAL(sanitize("foo:bar", false, lt::path_sanitize_flags_t{}), "foo:bar");
}

TORRENT_TEST(sanitize_path_unicode_formatting_chars)
{
	// the bidi marks/overrides (U+200E/F, U+202A-E) are always filtered;
	// path_sanitize_flags::filter_unicode_formatting_chars additionally
	// filters other zero-width/invisible characters, e.g. U+200B (ZWSP)
	// here, which some users need to disable
	lt::path_sanitize_flags_t const filter =
		lt::path_sanitize_flags::filter_unicode_formatting_chars;

	TEST_EQUAL(sanitize("foo\xe2\x80\x8e"
						"bar",
				   false,
				   lt::path_sanitize_flags_t{}),
		"foobar");

	TEST_EQUAL(sanitize("foo\xe2\x80\x8b"
						"bar",
				   false,
				   filter),
		"foobar");
	TEST_EQUAL(sanitize("foo\xe2\x80\x8b"
						"bar",
				   false,
				   lt::path_sanitize_flags_t{}),
		"foo\xe2\x80\x8b"
		"bar");
}

TORRENT_TEST(sanitize_path_borrow)
{
	// an element that needs no sanitization is borrowed: the function
	// returns true and leaves "path" untouched
	std::string path;
	TEST_CHECK(
		lt::aux::sanitize_path_element(path, "readme.txt", lt::path_sanitize_flags::default_flags));
	TEST_CHECK(path.empty());

	// an element that needs sanitizing is materialized into "path", and the
	// function returns false
	path.clear();
	TEST_CHECK(
		!lt::aux::sanitize_path_element(path, "foo\\bar", lt::path_sanitize_flags::default_flags));
	TEST_EQUAL(path, "foobar");
}
