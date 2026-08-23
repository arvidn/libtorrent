/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/load_torrent.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/path_sanitize_flags.hpp"
#include "libtorrent/aux_/path.hpp" // for combine_path, current_path, parent_path
#include "libtorrent/aux_/escape_string.hpp" // for convert_path_to_posix

#include "test.hpp"

#include <initializer_list>
#include <string>
#include <vector>

using namespace lt;

namespace {

// one entry in a test case's delta against the baseline file list below: the
// index into that list, and what the entry becomes once the rule is applied.
struct override_t
{
	int index;
	char const* name;
};

struct sanitize_test_case
{
	char const* name;
	path_sanitize_flags_t flags;
	std::vector<override_t> overrides;
};

std::vector<override_t> concat(std::initializer_list<std::vector<override_t>> lists)
{
	std::vector<override_t> ret;
	for (auto const& l : lists)
		ret.insert(ret.end(), l.begin(), l.end());
	return ret;
}

std::vector<std::string> apply_overrides(
	std::vector<std::string> files, std::vector<override_t> const& overrides)
{
	for (auto const& o : overrides)
	{
		TEST_CHECK(o.index >= 0 && o.index < int(files.size()));
		if (o.index >= 0 && o.index < int(files.size()))
			files[std::size_t(o.index)] = o.name;
	}
	return files;
}

} // anonymous namespace

// sanitize_limits.torrent (see test_torrents/) packs one file or directory per
// rule in path_sanitize_flags.hpp: a name over the 240-character limit, a name
// that only becomes a DOS reserved name once another rule runs (trailing space,
// embedded ZWSP), every DOS/Windows reserved device name, exact- and
// case-duplicate filenames, directories differing only by case (fold together
// harmlessly, unless the files inside share a name) plus a file colliding with
// one of them, a DOS reserved name whose sanitized form collides with an
// existing file, literal ".." path elements attempting to escape the download
// directory, elements that only turn into ".." once another rule strips a
// character, invalid UTF-8, the Windows/Android-invalid character sets,
// embedded path separators, unicode formatting characters, and trailing
// dots/spaces.
//
// Each test case below is expressed as a delta against the baseline (no
// sanitize_flags set at all) rather than repeating the full file list: only the
// handful of entries a given flag combination actually changes are listed, by
// index into the baseline below. Loading the torrent under every flag in
// isolation, and with all of them together, locks in exactly which rule fires
// for which flag combination.
TORRENT_TEST(sanitize_limits_combinations)
{
	std::vector<std::string> const baseline = {
		"sanitizer_test/long_names/"
		"long_filename_"
		"abcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghij"
		"abcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghij"
		"abcdefghijabcdefghijabcdefghijabcdefghijabcdef.txt", // 0
		"sanitizer_test/unicode_length/"
		"\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9"
		"\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9"
		"\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9"
		"\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9"
		"\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9"
		"\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9"
		"\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9"
		"\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9"
		"\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9"
		"\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9"
		"\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9.dat", // 1
		"sanitizer_test/hidden_reserved/CON ", // 2
		"sanitizer_test/hidden_reserved/co\xe2\x80\x8bn.txt", // 3
		"sanitizer_test/reserved_names/con", // 4
		"sanitizer_test/reserved_names/prn", // 5
		"sanitizer_test/reserved_names/aux", // 6
		"sanitizer_test/reserved_names/clock$", // 7
		"sanitizer_test/reserved_names/nul", // 8
		"sanitizer_test/reserved_names/com0", // 9
		"sanitizer_test/reserved_names/com1", // 10
		"sanitizer_test/reserved_names/com2", // 11
		"sanitizer_test/reserved_names/com3", // 12
		"sanitizer_test/reserved_names/com4", // 13
		"sanitizer_test/reserved_names/com5", // 14
		"sanitizer_test/reserved_names/com6", // 15
		"sanitizer_test/reserved_names/com7", // 16
		"sanitizer_test/reserved_names/com8", // 17
		"sanitizer_test/reserved_names/com9", // 18
		"sanitizer_test/reserved_names/com\xc2\xb9", // 19
		"sanitizer_test/reserved_names/com\xc2\xb2", // 20
		"sanitizer_test/reserved_names/com\xc2\xb3", // 21
		"sanitizer_test/reserved_names/lpt0", // 22
		"sanitizer_test/reserved_names/lpt1", // 23
		"sanitizer_test/reserved_names/lpt2", // 24
		"sanitizer_test/reserved_names/lpt3", // 25
		"sanitizer_test/reserved_names/lpt4", // 26
		"sanitizer_test/reserved_names/lpt5", // 27
		"sanitizer_test/reserved_names/lpt6", // 28
		"sanitizer_test/reserved_names/lpt7", // 29
		"sanitizer_test/reserved_names/lpt8", // 30
		"sanitizer_test/reserved_names/lpt9", // 31
		"sanitizer_test/reserved_names/lpt\xc2\xb9", // 32
		"sanitizer_test/reserved_names/lpt\xc2\xb2", // 33
		"sanitizer_test/reserved_names/lpt\xc2\xb3", // 34
		"sanitizer_test/reserved_names_ext/con.txt", // 35
		"sanitizer_test/reserved_names_ext/nul.tar.gz", // 36
		"sanitizer_test/reserved_names_ext/lpt1.log", // 37
		"sanitizer_test/duplicates/readme.txt", // 38
		"sanitizer_test/duplicates/readme.1.txt", // 39
		"sanitizer_test/duplicates/Notes.TXT", // 40
		"sanitizer_test/duplicates/notes.1.txt", // 41
		"sanitizer_test/Docs/readme.txt", // 42
		"sanitizer_test/docs/notes.txt", // 43
		"sanitizer_test/Docs/same.txt", // 44
		"sanitizer_test/docs/same.1.txt", // 45
		"sanitizer_test/docs.1", // 46
		"sanitizer_test/reserved_collision/con", // 47
		"sanitizer_test/reserved_collision/con_", // 48
		"sanitizer_test/path_traversal_dir/_/escape.txt", // 49
		"sanitizer_test/path_traversal_leaf/_", // 50
		"sanitizer_test/hidden_dotdot_trim/.. /escape.txt", // 51
		"sanitizer_test/hidden_dotdot_fmt/.\xe2\x80\x8b./escape.txt", // 52
		"sanitizer_test/invalid_utf8/broken____bytes.bin", // 53
		"sanitizer_test/invalid_chars/win_android_?<>\"|*:.txt", // 54
		"sanitizer_test/invalid_chars/ctrl_____name.txt", // 55
		"sanitizer_test/path_separators/sepsandtogether.txt", // 56
		"sanitizer_test/formatting_chars/zwsp_\xe2\x80\x8bname.txt", // 57
		"sanitizer_test/formatting_chars/bom_\xef\xbb\xbfname.txt", // 58
		"sanitizer_test/formatting_chars/bidi_override_name.txt", // 59
		"sanitizer_test/trailing/report...", // 60
		"sanitizer_test/trailing/notes   ", // 61
	};

	std::vector<override_t> const unicode_length_override = {
		{1,
			"sanitizer_test/unicode_length/"
			"\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2"
			"\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9"
			"\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2"
			"\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9"
			"\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2"
			"\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9"
			"\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2"
			"\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9"
			"\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2"
			"\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9"
			"\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2"
			"\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9"
			"\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2"
			"\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9"
			"\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2"
			"\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9"
			"\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2"
			"\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9"
			"\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2"
			"\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9"
			"\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2"
			"\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9"
			"\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9\xc2\xa9.dat"},
	};

	std::vector<override_t> const trim_hidden_override = {
		{2, "sanitizer_test/hidden_reserved/CON"},
	};

	// a trailing space in "hidden_dotdot_trim/.. " is only stripped once
	// trim_trailing_spaces_and_dots runs, revealing the traversal element "..",
	// which the (always-on) all-dots check then neutralizes into "_"
	std::vector<override_t> const dotdot_trim_override = {
		{51, "sanitizer_test/hidden_dotdot_trim/_/escape.txt"},
	};

	// "hidden_dotdot_fmt/.<ZWSP>." reacts to trim_trailing_spaces_and_dots and
	// filter_unicode_formatting_chars independently, and differently: trimming
	// alone only strips the trailing literal dot (byte-level, oblivious to the
	// ZWSP still in the middle), leaving ".<ZWSP>" rather than "..", so this does
	// NOT trigger the all-dots check by itself -- see dotdot_fmt_override below
	// for what happens once filter_unicode_formatting_chars also runs
	std::vector<override_t> const dotdot_fmt_trim_partial_override = {
		{52, "sanitizer_test/hidden_dotdot_fmt/.\xe2\x80\x8b/escape.txt"},
	};

	std::vector<override_t> const trailing_overrides = {
		{60, "sanitizer_test/trailing/report"},
		{61, "sanitizer_test/trailing/notes"},
	};

	// every DOS/Windows reserved device name gets "_" appended right after the
	// base name (see indices 4-37 in the baseline above); reserved_collision/con
	// additionally shows that appending "_" is fed back into the general
	// duplicate-filename resolution: it collides with the already-existing
	// reserved_collision/con_, so that file gets renamed in turn
	std::vector<override_t> const dos_reserved_overrides = {
		{4, "sanitizer_test/reserved_names/con_"}, // reserved_names/con_
		{5, "sanitizer_test/reserved_names/prn_"}, // reserved_names/prn_
		{6, "sanitizer_test/reserved_names/aux_"}, // reserved_names/aux_
		{7, "sanitizer_test/reserved_names/clock$_"}, // reserved_names/clock$_
		{8, "sanitizer_test/reserved_names/nul_"}, // reserved_names/nul_
		{9, "sanitizer_test/reserved_names/com0_"}, // reserved_names/com0_
		{10, "sanitizer_test/reserved_names/com1_"}, // reserved_names/com1_
		{11, "sanitizer_test/reserved_names/com2_"}, // reserved_names/com2_
		{12, "sanitizer_test/reserved_names/com3_"}, // reserved_names/com3_
		{13, "sanitizer_test/reserved_names/com4_"}, // reserved_names/com4_
		{14, "sanitizer_test/reserved_names/com5_"}, // reserved_names/com5_
		{15, "sanitizer_test/reserved_names/com6_"}, // reserved_names/com6_
		{16, "sanitizer_test/reserved_names/com7_"}, // reserved_names/com7_
		{17, "sanitizer_test/reserved_names/com8_"}, // reserved_names/com8_
		{18, "sanitizer_test/reserved_names/com9_"}, // reserved_names/com9_
		{19, "sanitizer_test/reserved_names/com\xc2\xb9_"}, // reserved_names/com\xc2\xb9_
		{20, "sanitizer_test/reserved_names/com\xc2\xb2_"}, // reserved_names/com\xc2\xb2_
		{21, "sanitizer_test/reserved_names/com\xc2\xb3_"}, // reserved_names/com\xc2\xb3_
		{22, "sanitizer_test/reserved_names/lpt0_"}, // reserved_names/lpt0_
		{23, "sanitizer_test/reserved_names/lpt1_"}, // reserved_names/lpt1_
		{24, "sanitizer_test/reserved_names/lpt2_"}, // reserved_names/lpt2_
		{25, "sanitizer_test/reserved_names/lpt3_"}, // reserved_names/lpt3_
		{26, "sanitizer_test/reserved_names/lpt4_"}, // reserved_names/lpt4_
		{27, "sanitizer_test/reserved_names/lpt5_"}, // reserved_names/lpt5_
		{28, "sanitizer_test/reserved_names/lpt6_"}, // reserved_names/lpt6_
		{29, "sanitizer_test/reserved_names/lpt7_"}, // reserved_names/lpt7_
		{30, "sanitizer_test/reserved_names/lpt8_"}, // reserved_names/lpt8_
		{31, "sanitizer_test/reserved_names/lpt9_"}, // reserved_names/lpt9_
		{32, "sanitizer_test/reserved_names/lpt\xc2\xb9_"}, // reserved_names/lpt\xc2\xb9_
		{33, "sanitizer_test/reserved_names/lpt\xc2\xb2_"}, // reserved_names/lpt\xc2\xb2_
		{34, "sanitizer_test/reserved_names/lpt\xc2\xb3_"}, // reserved_names/lpt\xc2\xb3_
		{35, "sanitizer_test/reserved_names_ext/con_.txt"}, // reserved_names_ext/con_.txt
		{36, "sanitizer_test/reserved_names_ext/nul_.tar.gz"}, // reserved_names_ext/nul_.tar.gz
		{37, "sanitizer_test/reserved_names_ext/lpt1_.log"}, // reserved_names_ext/lpt1_.log
		{47, "sanitizer_test/reserved_collision/con_"}, // reserved_collision/con_
		{48, "sanitizer_test/reserved_collision/con_.1"}, // reserved_collision/con_.1
	};

	std::vector<override_t> const win_android_override = {
		{54, "sanitizer_test/invalid_chars/win_android________.txt"},
	};

	std::vector<override_t> const formatting_hidden_override = {
		{3, "sanitizer_test/hidden_reserved/con.txt"},
	};

	// once filter_unicode_formatting_chars removes the ZWSP, "." + ZWSP + "."
	// collapses to the traversal element "..", which the all-dots check
	// neutralizes into "_" -- the same end result as dotdot_trim_override above,
	// reached via a different rule
	std::vector<override_t> const dotdot_fmt_override = {
		{52, "sanitizer_test/hidden_dotdot_fmt/_/escape.txt"},
	};

	std::vector<override_t> const formatting_overrides = {
		{57, "sanitizer_test/formatting_chars/zwsp_name.txt"},
		{58, "sanitizer_test/formatting_chars/bom_name.txt"},
	};

	// under "all", the reserved names hidden behind a trailing space / ZWSP
	// resolve differently than under trim/formatting alone: the DOS-reserved-name
	// check now also fires, so the result is suffixed, not just trimmed/cleaned.
	std::vector<override_t> const all_hidden_reserved_overrides = {
		{2, "sanitizer_test/hidden_reserved/CON_"},
		{3, "sanitizer_test/hidden_reserved/con_.txt"},
	};

	sanitize_test_case const cases[] = {
		{"none", path_sanitize_flags_t{}, {}},
		{"limit_unicode_characters",
			path_sanitize_flags::limit_unicode_characters,
			unicode_length_override},
		{"trim_trailing_spaces_and_dots",
			path_sanitize_flags::trim_trailing_spaces_and_dots,
			concat({trim_hidden_override,
				dotdot_trim_override,
				dotdot_fmt_trim_partial_override,
				trailing_overrides})},
		{"filter_dos_reserved_names",
			path_sanitize_flags::filter_dos_reserved_names,
			dos_reserved_overrides},
		{"sanitize_invalid_chars_win",
			path_sanitize_flags::sanitize_invalid_chars_win,
			win_android_override},
		{"sanitize_invalid_chars_android",
			path_sanitize_flags::sanitize_invalid_chars_android,
			win_android_override},
		{"filter_unicode_formatting_chars",
			path_sanitize_flags::filter_unicode_formatting_chars,
			concat({formatting_hidden_override, dotdot_fmt_override, formatting_overrides})},
		{"all",
			path_sanitize_flags::all,
			concat({unicode_length_override,
				all_hidden_reserved_overrides,
				dos_reserved_overrides,
				win_android_override,
				formatting_overrides,
				trailing_overrides,
				dotdot_trim_override,
				dotdot_fmt_override})},
	};

	std::string const filename = combine_path(
		combine_path(parent_path(current_path()), "test_torrents"), "sanitize_limits.torrent");

	for (auto const& c : cases)
	{
		std::vector<std::string> const expected = apply_overrides(baseline, c.overrides);

		load_torrent_limits cfg;
		cfg.sanitize_flags = c.flags;
		error_code ec;
		auto const atp = load_torrent_file(filename, ec, cfg);
		TEST_CHECK(!ec);
		if (ec)
			continue;
		TEST_CHECK(atp.ti);
		if (!atp.ti)
			continue;

		auto const& fs = atp.ti->layout();
		TEST_EQUAL(fs.num_files(), int(expected.size()));

		int idx = 0;
		for (auto const i : fs.file_range())
		{
			if (idx < int(expected.size()))
			{
				auto const it = atp.renamed_files.find(i);
				std::string path = (it != atp.renamed_files.end()) ? it->second : fs.file_path(i);
				convert_path_to_posix(path);
				TEST_EQUAL(path, expected[std::size_t(idx)]);
			}
			++idx;
		}
	}
}
