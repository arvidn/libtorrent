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
#include "libtorrent/aux_/string_util.hpp" // for aux::to_lower

#include "test.hpp"

#include <initializer_list>
#include <string>
#include <unordered_map>
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

std::string lower_case(std::string s)
{
	for (char& c : s)
		c = aux::to_lower(c);
	return s;
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
// embedded path separators, unicode formatting characters, trailing
// dots/spaces, two distinct groups of duplicate filenames colliding
// with each other's disambiguated names in the same directory, and two
// such groups whose disambiguated names only differ from each other by
// case.
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
		"sanitizer_test/docs-1", // 62
		"sanitizer_test/dup_ctrl/alpha", // 63
		"sanitizer_test/dup_ctrl/alpha.1", // 64
		"sanitizer_test/dup_ctrl/alpha-1", // 65
		"sanitizer_test/cross_group_dedup/alpha-1", // 66
		"sanitizer_test/cross_group_dedup/alpha-1.1", // 67
		"sanitizer_test/cross_group_dedup/alpha-2", // 68
		"sanitizer_test/cross_group_dedup/alpha-2.1", // 69
		"sanitizer_test/whole_tree_dedup/beta.txt", // 70
		"sanitizer_test/whole_tree_dedup/beta.1.txt", // 71
		"sanitizer_test/whole_tree_dedup/beta.1.1.txt", // 72
		"sanitizer_test/case_cross_group_dedup/widget", // 73
		"sanitizer_test/case_cross_group_dedup/widget.1", // 74
		"sanitizer_test/case_cross_group_dedup/WIDGET-3", // 75
		"sanitizer_test/case_cross_group_dedup/Widget-3.1", // 76
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

	// unlike the baseline's whole-tree pass, deduplicate_per_directory
	// doesn't exempt directories from collision-renaming: "Docs"/"docs"/
	// the bare top-level file "docs" (entry 46) are all one case-
	// insensitively colliding group under the root, resolved together.
	// "Docs" (created first) keeps its name. The directory "docs" tries
	// "docs-1" first, but entry 62's raw name is the literal "docs-1",
	// already taken, so it's rejected up front and "docs" lands on
	// "docs-2" instead; "docs" (entry 46) then continues that same
	// group's numbering and lands on "docs-3". Entry 62 was never part
	// of this collision group to begin with (its own name never
	// literally matched "docs"/"Docs"), and nothing here ever renames it
	// after the fact, so it's untouched, matching the baseline.
	std::vector<override_t> const docs_dedup_override = {
		{43, "sanitizer_test/docs-2/notes.txt"},
		{45, "sanitizer_test/docs-2/same.txt"},
		{46, "sanitizer_test/docs-3"},
	};

	// contrast with entry 62 above: entries 63 and 64 both have the raw
	// name "alpha" (no '-' or '.'), and entry 65 already occupies the
	// "alpha-1" slot entry 64 tries first. Entry 64's candidates are the
	// plain "alpha-1", "alpha-2", ... sequence, correctly skipping the
	// taken "alpha-1" for "alpha-2".
	std::vector<override_t> const dup_ctrl_override = {
		{64, "sanitizer_test/dup_ctrl/alpha-2"},
	};

	// two distinct groups colliding in the same directory: entries 66-67
	// are both literally "alpha-1", entries 68-69 both literally
	// "alpha-2". Each group keeps its first (earliest-created) member
	// unrenamed and only needs to rename its second. split_base_ext()
	// keeps a trailing "-<digits>" as part of the base, so entry 67's
	// candidates are built from the base "alpha-1" (its own full name),
	// landing on "alpha-1-1"; likewise entry 69 lands on "alpha-2-1".
	// The two groups' candidates never share a base, so, unlike a
	// stripped base would, they can't collide with each other here.
	std::vector<override_t> const cross_group_dedup_override = {
		{67, "sanitizer_test/cross_group_dedup/alpha-1-1"},
		{69, "sanitizer_test/cross_group_dedup/alpha-2-1"},
	};

	// entry 71 (a literal duplicate of entry 70's "beta.txt") and entry 72
	// (a distinct, literal "beta.1.txt") probe the same numbering:
	// whichever pass renames entry 71 to "beta.1.txt" first must still be
	// visible when entry 72 is checked, or entry 72 goes untouched and
	// collides with it. Under the whole-tree pass (every case below
	// except the two using this override), resolve_duplicate_filenames_
	// slow() renames entry 71 to "beta.1.txt", and entry 72 then collides
	// with that rename and is bumped to "beta.1.1.txt" in turn (see
	// baseline above).
	//
	// deduplicate_per_directory's file/symlink pass groups by literal
	// name rather than by probing a shared counter, so entry 72 (a
	// distinct group of its own) never collides with entry 71's rename:
	// entry 71 lands on "beta-1.txt" and entry 72 is left as "beta.1.txt".
	std::vector<override_t> const whole_tree_dedup_override = {
		{71, "sanitizer_test/whole_tree_dedup/beta-1.txt"},
		{72, "sanitizer_test/whole_tree_dedup/beta.1.txt"},
	};

	// two colliding groups under one directory, "widget"/"widget" (entries
	// 73-74) and "WIDGET-3"/"Widget-3" (entries 75-76). Entry 74's own
	// name "widget" has no extension, so its first candidate "widget-1"
	// is accepted outright. Entry 76's own name "Widget-3" keeps its
	// "-3" as part of the base, so its candidate is built from
	// "Widget-3", landing on "Widget-3-1" rather than anything that
	// could be mistaken, case-insensitively, for the first group's
	// "widget-1".
	std::vector<override_t> const case_cross_group_dedup_override = {
		{74, "sanitizer_test/case_cross_group_dedup/widget-1"},
		{76, "sanitizer_test/case_cross_group_dedup/Widget-3-1"},
	};

	// entries 39 and 41 (see the baseline above) collide under every
	// case, but only deduplicate_per_directory routes their resolution
	// through the per-directory pass instead of the whole-tree one, so
	// only there do they pick up its "-N" suffix instead of the
	// whole-tree pass's ".N"
	std::vector<override_t> const per_directory_dup_override = {
		{39, "sanitizer_test/duplicates/readme-1.txt"},
		{41, "sanitizer_test/duplicates/notes-1.txt"},
	};

	// same reasoning as above, but this collision only exists once
	// filter_dos_reserved_names has renamed entry 47's "con" to "con_",
	// so it only surfaces under "all", where that flag and
	// deduplicate_per_directory are both set
	std::vector<override_t> const per_directory_reserved_collision_override = {
		{48, "sanitizer_test/reserved_collision/con_-1"},
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
		{"deduplicate_per_directory",
			path_sanitize_flags::deduplicate_per_directory,
			concat({docs_dedup_override,
				dup_ctrl_override,
				per_directory_dup_override,
				cross_group_dedup_override,
				whole_tree_dedup_override,
				case_cross_group_dedup_override})},
		{"all",
			path_sanitize_flags::all,
			concat({unicode_length_override,
				all_hidden_reserved_overrides,
				dos_reserved_overrides,
				win_android_override,
				formatting_overrides,
				trailing_overrides,
				dotdot_trim_override,
				dotdot_fmt_override,
				docs_dedup_override,
				dup_ctrl_override,
				per_directory_dup_override,
				per_directory_reserved_collision_override,
				cross_group_dedup_override,
				whole_tree_dedup_override,
				case_cross_group_dedup_override})},
	};

	std::string const filename = combine_path(
		combine_path(parent_path(current_path()), "test_torrents"), "sanitize_limits.torrent");

	for (auto const& c : cases)
	{
		std::printf("case: %s\n", c.name);

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

		// deduplicate_per_directory's own resolution runs eagerly, once,
		// at load_torrent() time (see aux::resolve_directory_duplicates()),
		// landing in atp.renamed_path_elements rather than mutating
		// file_storage or the unrelated atp.renamed_files, so check
		// against that instead.
		bool const per_directory = bool(c.flags & path_sanitize_flags::deduplicate_per_directory);
		renamed_files per_directory_renamed;
		if (per_directory)
			per_directory_renamed.import_path_elements(fs, atp.renamed_path_elements);
		filenames const per_directory_names(fs, per_directory_renamed);

		// every rule above resolves collisions among files, so no two
		// non-pad files should ever end up sharing a resolved path
		// (case-insensitively); this is the invariant deduplicate_per_
		// directory and the whole-tree pass both exist to guarantee, and
		// catches a colliding pair even when no test case happens to
		// assert its exact (wrong) resulting names
		std::unordered_map<std::string, file_index_t> seen;

		int idx = 0;
		for (auto const i : fs.file_range())
		{
			std::string path;
			if (per_directory)
			{
				path = per_directory_names.file_path(i);
			}
			else
			{
				auto const it = atp.renamed_files.find(i);
				path = (it != atp.renamed_files.end()) ? it->second : fs.file_path(i);
			}
			convert_path_to_posix(path);

			if (idx < int(expected.size()))
				TEST_EQUAL(path, expected[std::size_t(idx)]);
			++idx;

			if (fs.pad_file_at(i))
				continue;

			auto const ins = seen.emplace(lower_case(path), i);
			if (!ins.second)
			{
				std::printf("duplicate resolved path (case-insensitive): \"%s\" "
							"for file %d and file %d\n",
					path.c_str(),
					int(ins.first->second),
					int(i));
				TEST_ERROR("duplicate resolved path");
			}
		}

#if TORRENT_ABI_VERSION < 4
		// torrent_info::parse_torrent_file() (the deprecated ABI<4
		// constructor path) takes a completely different route to the
		// same result: it folds every rename directly into file_storage
		// via rename_file(), one file at a time, for both algorithms.
		// This is the only coverage of that path against a torrent whose
		// directories actually collide, so a regression specific to its
		// deduplicate_per_directory branch wouldn't otherwise be caught.
		try
		{
			torrent_info const ti2(filename, cfg);

			TEST_EQUAL(ti2.files().num_files(), int(expected.size()));

			std::unordered_map<std::string, file_index_t> seen2;
			int idx2 = 0;
			for (auto const i : ti2.files().file_range())
			{
				std::string path = ti2.files().file_path(i);
				convert_path_to_posix(path);

				if (idx2 < int(expected.size()))
					TEST_EQUAL(path, expected[std::size_t(idx2)]);
				++idx2;

				if (ti2.files().pad_file_at(i))
					continue;

				auto const ins = seen2.emplace(lower_case(path), i);
				if (!ins.second)
				{
					std::printf("duplicate resolved path (case-insensitive) via "
								"the deprecated torrent_info ctor: \"%s\" for "
								"file %d and file %d\n",
						path.c_str(),
						int(ins.first->second),
						int(i));
					TEST_ERROR("duplicate resolved path");
				}
			}
		}
		catch (system_error const& e)
		{
			TEST_ERROR(e.what());
		}
#endif
	}
}
