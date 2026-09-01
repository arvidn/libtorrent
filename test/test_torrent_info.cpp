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
#include "setup_transfer.hpp" // for load_file
#include "test_utils.hpp"
#include "settings.hpp" // for settings()

#include "libtorrent/file_storage.hpp"
#include "libtorrent/aux_/resolve_duplicate_filenames.hpp"
#include "libtorrent/load_torrent.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/disk_interface.hpp" // for default_block_size
#include "libtorrent/aux_/escape_string.hpp" // for convert_path_to_posix
#include "libtorrent/aux_/piece_picker.hpp"
#include "libtorrent/hex.hpp" // to_hex
#include "libtorrent/hasher.hpp"
#include "libtorrent/write_resume_data.hpp" // write_torrent_file
#include "libtorrent/session.hpp"
#include "libtorrent/alert_types.hpp"

#include <iostream>

using namespace lt;

#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS
TORRENT_TEST(mutable_torrents)
{
	std::vector<lt::create_file_entry> fs;

	fs.emplace_back("test/temporary.txt", 0x4000);

	lt::create_torrent t(std::move(fs), 0x4000);

	// calculate the hash for all pieces
	for (auto const i : t.piece_range())
		t.set_hash(i, sha1_hash::max());

	t.add_collection("collection1");
	t.add_collection("collection2");

	t.add_similar_torrent(sha1_hash("abababababababababab"));
	t.add_similar_torrent(sha1_hash("babababababababababa"));

	std::vector<char> const buf = t.generate_buf();
	lt::add_torrent_params atp = load_torrent_buffer(buf);

	std::vector<sha1_hash> similar;
	similar.push_back(sha1_hash("abababababababababab"));
	similar.push_back(sha1_hash("babababababababababa"));

	std::vector<std::string> collections;
	collections.push_back("collection1");
	collections.push_back("collection2");

	TEST_CHECK(similar == atp.ti->similar_torrents());
	TEST_CHECK(collections == atp.ti->collections());
}
#endif

namespace {

struct test_torrent_t
{
	test_torrent_t(char const* f, std::function<void(lt::add_torrent_params)> atp = {}) // NOLINT
		: file(f), test(std::move(atp)) {}

	char const* file;
	std::function<void(lt::add_torrent_params atp)> test;
};

using namespace lt;

#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
#define SEPARATOR "\\"
#else
#define SEPARATOR "/"
#endif

static test_torrent_t const test_torrents[] = {
	{"base.torrent"},
	{"empty_path.torrent"},
	{"parent_path.torrent"},
	{"hidden_parent_path.torrent"},
	{"single_multi_file.torrent"},
	{"slash_path.torrent",
		[](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.ti->num_files(), 1);
			TEST_EQUAL(atp.ti->layout().file_path(file_index_t{0}), "temp" SEPARATOR "_" SEPARATOR "_" SEPARATOR "bar");
		}},
	{"slash_path2.torrent",
		[](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.ti->num_files(), 1);
			TEST_EQUAL(atp.ti->layout().file_path(file_index_t{0}), "temp" SEPARATOR "abc....def" SEPARATOR "_" SEPARATOR "bar");
		}},
	{"slash_path3.torrent",
		[](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.ti->num_files(), 1);
			TEST_EQUAL(atp.ti->layout().file_path(file_index_t{0}), "temp....abc");
		}},
	{"backslash_path.torrent"},
	{"url_list.torrent"},
	{"url_list2.torrent"},
	{"url_list3.torrent"},
	{"httpseed.torrent"},
	{"empty_httpseed.torrent"},
	{"long_name.torrent"},
	{"whitespace_url.torrent",
		[](lt::add_torrent_params atp) {
			// make sure we trimmed the url
			TEST_CHECK(atp.trackers.size() > 0);
			if (atp.trackers.size() > 0)
				TEST_EQUAL(atp.trackers[0], "udp://test.com/announce");
		}},
	{"duplicate_files.torrent",
		[](lt::add_torrent_params atp) {
			// make sure we disambiguated the files
			TEST_EQUAL(atp.ti->num_files(), 2);
			TEST_CHECK(atp.ti->layout().file_path(file_index_t{0}) == combine_path(combine_path("temp", "foo"), "bar.txt"));
			TEST_EQUAL(atp.renamed_files.find(file_index_t{1})->second, combine_path(combine_path("temp", "foo"), "bar.1.txt"));
		}},
	{"pad_file.torrent",
		[](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.ti->num_files(), 2);
			TEST_EQUAL(bool(atp.ti->layout().file_flags(file_index_t{0}) & file_storage::flag_pad_file), false);
			TEST_EQUAL(bool(atp.ti->layout().file_flags(file_index_t{1}) & file_storage::flag_pad_file), true);
		}},
	{"creation_date.torrent",
		[](lt::add_torrent_params atp) {
#if TORRENT_ABI_VERSION < 4
			TEST_EQUAL(atp.ti->creation_date(), 1234567);
#endif
			TEST_EQUAL(atp.creation_date, 1234567);
		}},
	{"no_creation_date.torrent",
		[](lt::add_torrent_params atp) {
#if TORRENT_ABI_VERSION < 4
			TEST_CHECK(!atp.ti->creation_date());
#endif
			TEST_CHECK(!atp.creation_date);
		}},
	{"url_seed.torrent",
		[](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.url_seeds.size(), 1);
			TEST_EQUAL(atp.url_seeds[0], "http://test.com/file");
#if TORRENT_ABI_VERSION == 1
			// when using load_torrent, the web seeds are not stored in
			// the torrent_info object, just the add_torrent_params object
			TEST_EQUAL(atp.ti->http_seeds().size(), 0);
			TEST_EQUAL(atp.ti->url_seeds().size(), 0);
#endif
		}},
	{"url_seed_multi.torrent",
		[](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.url_seeds.size(), 1);
			TEST_EQUAL(atp.url_seeds[0], "http://test.com/file/");
#if TORRENT_ABI_VERSION == 1
			// when using load_torrent, the web seeds are not stored in
			// the torrent_info object, just the add_torrent_params object
			TEST_EQUAL(atp.ti->http_seeds().size(), 0);
			TEST_EQUAL(atp.ti->url_seeds().size(), 0);
#endif
		}},
	{"url_seed_multi_single_file.torrent",
		[](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.url_seeds.size(), 1);
			TEST_EQUAL(atp.url_seeds[0], "http://test.com/file/temp/foo/bar.txt");
		}},
	{"url_seed_multi_space.torrent",
		[](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.url_seeds.size(), 1);
			TEST_EQUAL(atp.url_seeds[0], "http://test.com/test%20file/foo%20bar/");
#if TORRENT_ABI_VERSION == 1
			// when using load_torrent, the web seeds are not stored in
			// the torrent_info object, just the add_torrent_params object
			TEST_EQUAL(atp.ti->http_seeds().size(), 0);
			TEST_EQUAL(atp.ti->url_seeds().size(), 0);
#endif
		}},
	{"url_seed_multi_space_nolist.torrent",
		[](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.url_seeds.size(), 1);
			TEST_EQUAL(atp.url_seeds[0], "http://test.com/test%20file/foo%20bar/");
#if TORRENT_ABI_VERSION == 1
			// when using load_torrent, the web seeds are not stored in
			// the torrent_info object, just the add_torrent_params object
			TEST_EQUAL(atp.ti->http_seeds().size(), 0);
			TEST_EQUAL(atp.ti->url_seeds().size(), 0);
#endif
		}},
	{"empty_path_multi.torrent"},
	{"duplicate_web_seeds.torrent",
		[](lt::add_torrent_params atp) { TEST_EQUAL(atp.url_seeds.size(), 3); }},
	{"invalid_name2.torrent",
		[](lt::add_torrent_params atp) {
			// if, after all invalid characters are removed from the name, it ends up
			// being empty, it's set to the info-hash. Some torrents also have an empty name
			// in which case it's also set to the info-hash
			TEST_EQUAL(atp.ti->name(), "b61560c2918f463768cd122b6d2fdd47b77bdb35");
		}},
	{"invalid_name3.torrent",
		[](lt::add_torrent_params atp) {
			// windows does not allow trailing spaces in filenames
#ifdef TORRENT_WINDOWS
			TEST_EQUAL(atp.ti->name(), "foobar");
#else
			TEST_EQUAL(atp.ti->name(), "foobar ");
#endif
		}},
	{"symlink1.torrent",
		[](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.ti->num_files(), 2);
			TEST_EQUAL(atp.ti->layout().symlink(file_index_t{1}), "temp" SEPARATOR "a" SEPARATOR "b" SEPARATOR "bar");
		}},
	{"symlink2.torrent",
		[](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.ti->num_files(), 5);
			TEST_EQUAL(atp.ti->layout().symlink(file_index_t{0}), "Some.framework" SEPARATOR "Versions" SEPARATOR "A" SEPARATOR "SDL2");
			TEST_EQUAL(atp.ti->layout().symlink(file_index_t{4}), "Some.framework" SEPARATOR "Versions" SEPARATOR "A");
		}},
	{"unordered.torrent"},
	{"symlink_zero_size.torrent",
		[](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.ti->num_files(), 2);
			TEST_EQUAL(atp.ti->layout().symlink(1_file), "temp" SEPARATOR "a" SEPARATOR "b" SEPARATOR "bar");
		}},
	{"symlink_filtered_path.torrent",
		[](lt::add_torrent_params atp) {
			// a path element of fully-filtered characters (here "/") must
			// sanitize to "_" in both file paths and symlink targets so the
			// symlink resolves to the file it points to.
			TEST_EQUAL(atp.ti->num_files(), 2);
			TEST_EQUAL(atp.ti->layout().file_path(file_index_t{0}),
				"temp" SEPARATOR "a" SEPARATOR "_" SEPARATOR "bar");
			TEST_EQUAL(atp.ti->layout().symlink(file_index_t{1}),
				"temp" SEPARATOR "a" SEPARATOR "_" SEPARATOR "bar");
		}},
	{"pad_file_no_path.torrent",
		[](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.ti->num_files(), 2);
			TEST_EQUAL(atp.ti->layout().file_path(file_index_t{1}),
				combine_path("temp", combine_path(".pad", "2124")));
		}},
	{"large.torrent"},
	{"absolute_filename.torrent",
		[](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.ti->num_files(), 2);
			TEST_EQUAL(atp.ti->layout().file_path(file_index_t{0}), combine_path("temp", "abcde"));
			TEST_EQUAL(atp.ti->layout().file_path(file_index_t{1}), combine_path("temp", "foobar"));
		}},
	{"invalid_filename.torrent",
		[](lt::add_torrent_params atp) { TEST_EQUAL(atp.ti->num_files(), 2); }},
	{"invalid_filename2.torrent",
		[](lt::add_torrent_params atp) { TEST_EQUAL(atp.ti->num_files(), 3); }},
	{"overlapping_symlinks.torrent",
		[](lt::add_torrent_params atp) {
			TEST_CHECK(atp.ti->num_files() > 3);
			// "Versions/Current" is itself a symlink, used here as a
			// directory hop to reach "Versions/A". The chain is followed
			// through, so these resolve to their real, final targets
			// rather than the literal "Versions/Current/..." path
			TEST_EQUAL(atp.ti->layout().symlink(file_index_t{0}),
				"SDL2.framework" SEPARATOR "Versions" SEPARATOR "A" SEPARATOR "Headers");
			TEST_EQUAL(atp.ti->layout().symlink(file_index_t{1}),
				"SDL2.framework" SEPARATOR "Versions" SEPARATOR "A" SEPARATOR "Resources");
			TEST_EQUAL(atp.ti->layout().symlink(file_index_t{2}),
				"SDL2.framework" SEPARATOR "Versions" SEPARATOR "A" SEPARATOR "SDL2");
		}},
	{"invalid_directory_name.torrent",
		[](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.ti->num_files(), 2);
#if TORRENT_ABI_VERSION < 4
			// for backwards compatibility, the files() is changed when
			// files are renamed
			TEST_EQUAL(atp.ti->files().file_path(file_index_t{1}), "test2" SEPARATOR "_" SEPARATOR "foo.1");
#endif
			TEST_EQUAL(atp.ti->layout().file_path(file_index_t{0}), "test2" SEPARATOR "_" SEPARATOR "foo");
			TEST_EQUAL(atp.ti->layout().file_path(file_index_t{1}), "test2" SEPARATOR "_" SEPARATOR "foo");
			TEST_EQUAL(atp.renamed_files[file_index_t{1}], "test2" SEPARATOR "_" SEPARATOR "foo.1");
		}},
	{"v2.torrent",
		[](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.ti->num_files(), 1);
			TEST_EQUAL(atp.ti->layout().file_path(file_index_t{ 0 }), "test64K"_sv);
			TEST_EQUAL(atp.ti->layout().file_size(file_index_t{ 0 }), 65536);
			TEST_EQUAL(aux::to_hex(atp.ti->layout().root(file_index_t{ 0 })), "60aae9c7b428f87e0713e88229e18f0adf12cd7b22a0dd8a92bb2485eb7af242"_sv);
			TEST_EQUAL(atp.ti->info_hashes().has_v1(), true);
			TEST_EQUAL(atp.ti->info_hashes().has_v2(), true);
			TEST_EQUAL(aux::to_hex(atp.ti->info_hashes().v2), "597b180c1a170a585dfc5e85d834d69013ceda174b8f357d5bb1a0ca509faf0a"_sv);
			TEST_CHECK(atp.ti->v2());
			TEST_CHECK(atp.ti->v1());
		}},
	{"v2_multipiece_file.torrent",
		[](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.ti->num_files(), 1);
			TEST_EQUAL(atp.ti->layout().file_path(file_index_t{ 0 }), "test1MB"_sv);
			TEST_EQUAL(atp.ti->layout().file_size(file_index_t{ 0 }), 1048576);
			TEST_EQUAL(aux::to_hex(atp.ti->layout().root(file_index_t{ 0 })), "515ea9181744b817744ded9d2e8e9dc6a8450c0b0c52e24b5077f302ffbd9008"_sv);
			TEST_EQUAL(atp.ti->info_hashes().has_v1(), true);
			TEST_EQUAL(atp.ti->info_hashes().has_v2(), true);
			TEST_EQUAL(aux::to_hex(atp.ti->info_hashes().v2), "108ac2c3718ce722e6896edc56c4afa98f1d711ecaace7aad74fca418ebd03de"_sv);
		}},
	{"v2_only.torrent",
		[](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.ti->num_files(), 1);
			TEST_EQUAL(atp.ti->layout().file_path(file_index_t{ 0 }), "test1MB"_sv);
			TEST_EQUAL(atp.ti->layout().file_size(file_index_t{ 0 }), 1048576);
			TEST_EQUAL(aux::to_hex(atp.ti->layout().root(file_index_t{ 0 })), "515ea9181744b817744ded9d2e8e9dc6a8450c0b0c52e24b5077f302ffbd9008"_sv);
			TEST_EQUAL(atp.ti->info_hashes().has_v1(), false);
			TEST_EQUAL(atp.ti->info_hashes().has_v2(), true);
			TEST_EQUAL(aux::to_hex(atp.ti->info_hashes().v2), "95e04d0c4bad94ab206efa884666fd89777dbe4f7bd9945af1829037a85c6192"_sv);
			TEST_CHECK(atp.ti->v2());
			TEST_CHECK(!atp.ti->v1());
		}},
	{"v2_invalid_filename.torrent",
		[](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.ti->num_files(), 1);
			TEST_EQUAL(atp.ti->layout().file_path(file_index_t{0}), "_estMB"_sv);
		}},
	{"v2_multiple_files.torrent",
		[](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.merkle_trees.empty(), false);
			TEST_EQUAL(atp.ti->num_files(), 5);
			TEST_CHECK(atp.ti->v2());
#if TORRENT_ABI_VERSION < 4
			atp.ti->free_piece_layers();
			TEST_CHECK(atp.ti->v2());
			TEST_EQUAL(atp.ti->v2_piece_hashes_verified(), false);
#endif
		}},
	{"v2_invalid_filename2.torrent",
		[](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.ti->num_files(), 3);
			TEST_EQUAL(atp.ti->layout().file_path(file_index_t{0}), "test" SEPARATOR "_"_sv);
#if TORRENT_ABI_VERSION < 4
			// for backwards compatibility, the files() is changed when
			// files are renamed
			TEST_EQUAL(atp.ti->files().file_path(file_index_t{1}), "test" SEPARATOR "_.1"_sv);
#endif
			TEST_EQUAL(atp.ti->layout().file_path(file_index_t{1}), "test" SEPARATOR "_"_sv);
			TEST_EQUAL(atp.ti->layout().file_path(file_index_t{2}), "test" SEPARATOR "stress_test2"_sv);
			TEST_EQUAL(atp.renamed_files[file_index_t{1}], "test" SEPARATOR "_.1"_sv);
		}},
	{"v2_symlinks.torrent",
		[](lt::add_torrent_params atp) {
			TEST_CHECK(atp.ti->num_files() > 3);
			TEST_EQUAL(atp.ti->layout().symlink(0_file), "SDL2.framework" SEPARATOR "Versions" SEPARATOR "Current" SEPARATOR "Headers");
			TEST_EQUAL(atp.ti->layout().symlink(1_file), "SDL2.framework" SEPARATOR "Versions" SEPARATOR "Current" SEPARATOR "Resources");
			TEST_EQUAL(atp.ti->layout().symlink(2_file), "SDL2.framework" SEPARATOR "Versions" SEPARATOR "Current" SEPARATOR "SDL2");
		}},
	{"v2_hybrid.torrent",
		[](lt::add_torrent_params atp) {
			TEST_CHECK(atp.ti->info_hashes().has_v1());
			TEST_CHECK(atp.ti->info_hashes().has_v2());
		}},
	{"empty-files-1.torrent",
		[](lt::add_torrent_params atp) {
			TEST_CHECK(atp.ti->info_hashes().has_v1());
			TEST_CHECK(atp.ti->info_hashes().has_v2());
		}},
	{"empty-files-2.torrent",
		[](lt::add_torrent_params atp) {
			TEST_CHECK(atp.ti->info_hashes().has_v1());
			TEST_CHECK(atp.ti->info_hashes().has_v2());
		}},
	{"empty-files-3.torrent",
		[](lt::add_torrent_params atp) {
			TEST_CHECK(atp.ti->info_hashes().has_v1());
			TEST_CHECK(atp.ti->info_hashes().has_v2());
		}},
	{"empty-files-4.torrent",
		[](lt::add_torrent_params atp) {
			TEST_CHECK(atp.ti->info_hashes().has_v1());
			TEST_CHECK(atp.ti->info_hashes().has_v2());
		}},
	{"empty-files-5.torrent",
		[](lt::add_torrent_params atp) {
			TEST_CHECK(atp.ti->info_hashes().has_v1());
			TEST_CHECK(atp.ti->info_hashes().has_v2());
		}},
	{"v2_no_piece_layers.torrent",
		[](lt::add_torrent_params atp) {
			// it's OK to not have a piece layers field.
			// It's just like adding a magnet link
			TEST_CHECK(!atp.ti->info_hashes().has_v1());
			TEST_CHECK(atp.ti->info_hashes().has_v2());
		}},
	{"v2_incomplete_piece_layer.torrent",
		[](lt::add_torrent_params atp) {
			// it's OK for some files to not have a piece layer.
			// It's just like adding a magnet link
			TEST_CHECK(!atp.ti->info_hashes().has_v1());
			TEST_CHECK(atp.ti->info_hashes().has_v2());
		}},
	{"similar.torrent",
		[](lt::add_torrent_params atp) {
			TEST_CHECK((atp.ti->similar_torrents() == std::vector<lt::sha1_hash>{sha1_hash("aaaaaaaaaaaaaaaaaaaa")}));
		}},
	{"similar2.torrent",
		[](lt::add_torrent_params atp) {
			TEST_CHECK((atp.ti->similar_torrents() == std::vector<lt::sha1_hash>{sha1_hash("aaaaaaaaaaaaaaaaaaaa")}));
		}},
	{"collection.torrent",
		[](lt::add_torrent_params atp) {
			TEST_CHECK((atp.ti->collections() == std::vector<std::string>{"bar", "foo"}));
		}},
	{"collection2.torrent",
		[](lt::add_torrent_params atp) {
			TEST_CHECK((atp.ti->collections() == std::vector<std::string>{"bar", "foo"}));
		}},
	{"dht_nodes.torrent",
		[](lt::add_torrent_params atp) {
			using np = std::pair<std::string, int>;
			TEST_CHECK((atp.dht_nodes == std::vector<np>{np("127.0.0.1", 6881), np("192.168.1.1", 6881)}));
		}},
	{"large_piece_size.torrent",
		[](lt::add_torrent_params atp) { TEST_EQUAL(atp.ti->piece_length(), (32767 * 0x4000)); }},
};

struct test_failing_torrent_t
{
	char const* file;
	error_code error; // the expected error
};

test_failing_torrent_t test_error_torrents[] = {
	{"missing_piece_len.torrent", errors::torrent_missing_piece_length},
	{"invalid_piece_len.torrent", errors::torrent_missing_piece_length},
	{"negative_piece_len.torrent", errors::torrent_missing_piece_length},
	{"no_name.torrent", errors::torrent_missing_name},
	{"bad_name.torrent", errors::torrent_missing_name},
	{"invalid_name.torrent", errors::torrent_missing_name},
	{"invalid_info.torrent", errors::torrent_missing_info},
	{"string.torrent", errors::torrent_is_no_dict},
	{"negative_size.torrent", errors::torrent_invalid_length},
	{"negative_file_size.torrent", errors::torrent_invalid_length},
	{"invalid_path_list.torrent", errors::torrent_invalid_name},
	{"missing_path_list.torrent", errors::torrent_missing_name},
	{"pad_file_symlink.torrent", errors::torrent_invalid_pad_file},
	{"invalid_pieces.torrent", errors::torrent_missing_pieces},
	{"unaligned_pieces.torrent", errors::torrent_invalid_hashes},
	{"invalid_file_size.torrent", errors::torrent_invalid_length},
	{"invalid_symlink.torrent", errors::torrent_invalid_name},
	{"many_pieces.torrent", errors::too_many_pieces_in_torrent},
	{"no_files.torrent", errors::no_files_in_torrent},
	{"zero.torrent", errors::torrent_invalid_length},
	{"zero2.torrent", errors::torrent_invalid_length},
	{"v2_mismatching_metadata.torrent", errors::torrent_inconsistent_files},
	{"v2_no_power2_piece.torrent", errors::torrent_missing_piece_length},
	{"v2_invalid_file.torrent", errors::torrent_file_parse_failed},
	{"v2_deep_recursion.torrent", bdecode_errors::depth_exceeded},
	{"v2_non_multiple_piece_layer.torrent", errors::torrent_invalid_piece_layer},
	{"v2_piece_layer_invalid_file_hash.torrent", errors::torrent_invalid_piece_layer},
	{"v2_invalid_piece_layer.torrent", errors::torrent_invalid_piece_layer},
	{"v2_invalid_piece_layer_root.torrent", errors::torrent_invalid_piece_layer},
	{"v2_unknown_piece_layer_entry.torrent", errors::torrent_invalid_piece_layer},
	{"v2_invalid_piece_layer_size.torrent", errors::torrent_invalid_piece_layer},
	{"v2_bad_file_alignment.torrent", errors::torrent_inconsistent_files},
	{"v2_unordered_files.torrent", errors::invalid_bencoding},
	{"v2_overlong_integer.torrent", errors::invalid_bencoding},
	{"v2_missing_file_root_invalid_symlink.torrent", errors::torrent_missing_pieces_root},
	{"v2_large_file.torrent", errors::torrent_invalid_length},
	{"v2_large_offset.torrent", errors::too_many_pieces_in_torrent},
	{"v2_piece_size.torrent", errors::torrent_missing_piece_length},
	{"v2_invalid_pad_file.torrent", errors::torrent_invalid_pad_file},
	{"v2_zero_root.torrent", errors::torrent_missing_pieces_root},
	{"v2_zero_root_small.torrent", errors::torrent_missing_pieces_root},
	{"v2_empty_filename.torrent", errors::torrent_file_parse_failed},
	{"duplicate_files2.torrent", errors::too_many_duplicate_filenames},
};

} // anonymous namespace

// TODO: test remap_files
// TODO: torrent with 'p' (padfile) attribute
// TODO: torrent with 'h' (hidden) attribute
// TODO: torrent with 'x' (executable) attribute
// TODO: torrent with 'l' (symlink) attribute
// TODO: torrent with multiple trackers in multiple tiers, making sure we
// shuffle them (how do you test shuffling?, load it multiple times and make
// sure it's in different order at least once)
// TODO: torrents with a zero-length name
// TODO: torrent with a non-dictionary info-section
// TODO: torrents with DHT nodes
// TODO: torrent with url-list as a single string
// TODO: torrent with http seed as a single string
// TODO: torrent with a comment
// TODO: torrent with an SSL cert
// TODO: torrent with attributes (executable and hidden)
// TODO: torrent_info constructor that takes an invalid bencoded buffer
// TODO: sanitize_encoding with a string that triggers character replacement

#if TORRENT_ABI_VERSION < 4
TORRENT_TEST(add_tracker)
{
	torrent_info ti(info_hash_t(sha1_hash("                   ")));
	TEST_EQUAL(ti.trackers().size(), 0);

	ti.add_tracker("http://test.com/announce");
	TEST_EQUAL(ti.trackers().size(), 1);

	announce_entry ae = ti.trackers()[0];
	TEST_EQUAL(ae.url, "http://test.com/announce");

	ti.clear_trackers();
	TEST_EQUAL(ti.trackers().size(), 0);
}

TORRENT_TEST(url_list_duplicate)
{
	entry info;
	info["pieces"] = "aaaaaaaaaaaaaaaaaaaa";
	info["name.utf-8"] = "test1";
	info["name"] = "test__";
	info["piece length"] = 16 * 1024;
	info["length"] = 3245;
	entry::list_type l;
	l.push_back(entry("http://foo.com/bar1"));
	l.push_back(entry("http://foo.com/bar1")); // <- duplicate
	l.push_back(entry("http://foo.com/bar2"));
	entry const e(l);
	entry torrent;
	torrent["url-list"] = e;
	torrent["info"] = info;
	std::vector<char> const buf = bencode(torrent);
	torrent_info ti(buf, from_span);
	TEST_EQUAL(ti.web_seeds().size(), 2);
}

TORRENT_TEST(add_url_seed)
{
	torrent_info ti(info_hash_t(sha1_hash("                   ")));
	TEST_EQUAL(ti.web_seeds().size(), 0);

	ti.add_url_seed("http://test.com");

	TEST_EQUAL(ti.web_seeds().size(), 1);
	web_seed_entry we = ti.web_seeds()[0];
	TEST_EQUAL(we.url, "http://test.com");
}

TORRENT_TEST(set_web_seeds)
{
	torrent_info ti(info_hash_t(sha1_hash("                   ")));
	TEST_EQUAL(ti.web_seeds().size(), 0);

	std::vector<web_seed_entry> seeds;
	web_seed_entry e1("http://test1.com");
	seeds.push_back(e1);
	web_seed_entry e2("http://test2com");
	seeds.push_back(e2);

	ti.set_web_seeds(seeds);

	TEST_EQUAL(ti.web_seeds().size(), 2);
	TEST_CHECK(ti.web_seeds() == seeds);
}

TORRENT_TEST(add_tracker_reject_invalid_url)
{
	torrent_info ti(info_hash_t(sha1_hash("                   ")));
	TEST_EQUAL(ti.trackers().size(), 0);

	ti.add_tracker("http://test.com/announce");
	TEST_EQUAL(ti.trackers().size(), 1);

	// random text is invalid
	ti.add_tracker("invalid url");
	TEST_EQUAL(ti.trackers().size(), 1);

	ti.add_tracker("https://test.com/announce");
	TEST_EQUAL(ti.trackers().size(), 2);

	// ftp scheme is invalid
	ti.add_tracker("ftp://test.com");
	TEST_EQUAL(ti.trackers().size(), 2);

	ti.add_tracker("udp://test.com/announce");
	TEST_EQUAL(ti.trackers().size(), 3);

	ti.add_tracker("httpfoo://test.com/announce");
	TEST_EQUAL(ti.trackers().size(), 3);

	ti.add_tracker("http://foo.com/announce");
	TEST_EQUAL(ti.trackers().size(), 4);

#if TORRENT_USE_RTC
	ti.add_tracker("wss://foo-wss.com/announce");
	TEST_EQUAL(ti.trackers().size(), 5);

	ti.add_tracker("ws://foo-ws.com/announce");
	TEST_EQUAL(ti.trackers().size(), 6);
#endif

	ti.clear_trackers();
	TEST_EQUAL(ti.trackers().size(), 0);
}

#endif

// a v2-only torrent whose "name" field sanitizes to an empty string (here,
// ".") must fall back to a name derived from the v1 (SHA-1) info-hash, even
// though a v2-only torrent has no meaningful v1 info-hash of its own.
TORRENT_TEST(v2_only_torrent_empty_name_fallback)
{
	entry file_entry_1;
	file_entry_1["length"] = 0x4000;
	file_entry_1["pieces root"] = std::string(32, '\x01');

	entry file_dict_1;
	file_dict_1[""] = file_entry_1;

	entry file_entry_2;
	file_entry_2["length"] = 0x4000;
	file_entry_2["pieces root"] = std::string(32, '\x02');

	entry file_dict_2;
	file_dict_2[""] = file_entry_2;

	// use two files so the file tree can't take the single-file shortcut
	// (which discards the root name entirely); this way the torrent's
	// synthesized name is observable via torrent_info::name()
	entry file_tree;
	file_tree["file-1"] = file_dict_1;
	file_tree["file-2"] = file_dict_2;

	entry info;
	info["file tree"] = file_tree;
	info["meta version"] = 2;
	info["name"] = ".";
	info["piece length"] = 0x4000;

	entry torrent;
	torrent["info"] = info;

	std::vector<char> const buf = bencode(torrent);
	std::shared_ptr<torrent_info const> const ti = load_torrent_buffer(buf).ti;

	TEST_CHECK(ti->info_hashes().has_v2());
	TEST_CHECK(!ti->info_hashes().has_v1());

	sha1_hash const info_section_v1_hash = hasher(ti->info_section()).final();
	TEST_EQUAL(ti->name(), aux::to_hex(info_section_v1_hash));
}

// a "meta version" 2 torrent that also carries a v1 "length" listing computes a
// v1 info-hash, so v1() reports true and the v1 piece-verification path calls
// hash_for_piece(). If it has no "pieces" string there are no v1 hashes, and
// that call would read past the info section. Loading it must fail instead.
TORRENT_TEST(hybrid_torrent_missing_pieces)
{
	int const piece_length = 16 * 1024;
	int const num_pieces = 2000;
	std::int64_t const total = std::int64_t(piece_length) * num_pieces;

	entry file_entry;
	file_entry["length"] = total;
	file_entry["pieces root"] = std::string(32, '\x01');

	entry file_dict;
	file_dict[""] = file_entry;

	entry file_tree;
	file_tree["test"] = file_dict;

	entry info;
	info["file tree"] = file_tree;
	info["length"] = total; // v1 listing -> a v1 info-hash is computed
	info["meta version"] = 2;
	info["name"] = "test";
	info["piece length"] = piece_length;
	// deliberately no "pieces" string

	entry torrent;
	torrent["info"] = info;

	std::vector<char> const buf = bencode(torrent);

	error_code ec;
	add_torrent_params const atp = load_torrent_buffer(buf, ec, load_torrent_limits{});
	TEST_CHECK(!atp.ti);
	TEST_EQUAL(ec, error_code(errors::torrent_missing_pieces));
}

namespace {
// sanitize_path_element() only writes to "path" when the element
// actually needed sanitizing; otherwise it returns true and leaves "path"
// untouched so the caller can borrow "element" directly. This collapses
// that into a plain returned string for the tests below, and checks the
// borrow invariant along the way.
std::string sanitize(string_view const element, bool const force_element = false)
{
	std::string path;
	bool const unchanged = lt::aux::sanitize_path_element(path, element, force_element);
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

TORRENT_TEST(sanitize_path_trailing_dots)
{
	TEST_EQUAL(sanitize("a"), "a");
	TEST_EQUAL(sanitize("c"), "c");
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(sanitize("abc..."), "abc");
	TEST_EQUAL(sanitize("abc."), "abc");
	TEST_EQUAL(sanitize("a. . ."), "a");
#else
	TEST_EQUAL(sanitize("abc..."), "abc...");
	TEST_EQUAL(sanitize("abc."), "abc.");
	TEST_EQUAL(sanitize("a. . ."), "a. . .");
#endif
}

TORRENT_TEST(sanitize_path_trailing_spaces)
{
	TEST_EQUAL(sanitize("a"), "a");
	TEST_EQUAL(sanitize("c"), "c");
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(sanitize("abc   "), "abc");
	TEST_EQUAL(sanitize("abc "), "abc");
#else
	TEST_EQUAL(sanitize("abc   "), "abc   ");
	TEST_EQUAL(sanitize("abc "), "abc ");
#endif
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

#ifdef TORRENT_WINDOWS
	TEST_EQUAL(sanitize("dev:"), "dev_");
	TEST_EQUAL(sanitize("c:"), "c_");
#else
	TEST_EQUAL(sanitize("dev:"), "dev:");
	TEST_EQUAL(sanitize("c:"), "c:");
#endif

	// leading backslash is filtered out regardless of platform
	TEST_EQUAL(sanitize("\\c"), "c");

	TEST_EQUAL(sanitize("\b"), "_");

	TEST_EQUAL(sanitize("filename"), "filename");

	TEST_EQUAL(sanitize("abc"), "abc");
	// an empty element sanitizes to "_"
	TEST_EQUAL(sanitize(""), "_");

	// on windows, trailing spaces are trimmed; when nothing is left, the
	// result is "_"
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(sanitize("   "), "_");
	TEST_EQUAL(sanitize("\b?filename=4"), "__filename=4");
#else
	TEST_EQUAL(sanitize("   "), "   ");
	TEST_EQUAL(sanitize("\b?filename=4"), "_?filename=4");
#endif

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

#ifdef TORRENT_WINDOWS
	TEST_EQUAL(sanitize("dev:", true), "dev_");
	TEST_EQUAL(sanitize("c:", true), "c_");
#else
	TEST_EQUAL(sanitize("dev:", true), "dev:");
	TEST_EQUAL(sanitize("c:", true), "c:");
#endif

	// leading backslash is filtered out regardless of platform
	TEST_EQUAL(sanitize("\\c", true), "c");

	TEST_EQUAL(sanitize("\b", true), "_");

	TEST_EQUAL(sanitize("filename", true), "filename");

	TEST_EQUAL(sanitize("abc", true), "abc");
	TEST_EQUAL(sanitize("", true), "_");

#ifdef TORRENT_WINDOWS
	TEST_EQUAL(sanitize("   ", true), "_");
	TEST_EQUAL(sanitize("\b?filename=4", true), "__filename=4");
#else
	TEST_EQUAL(sanitize("   ", true), "   ");
	TEST_EQUAL(sanitize("\b?filename=4", true), "_?filename=4");
#endif

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
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(sanitize("foo:bar"), "foo_bar");
#else
	TEST_EQUAL(sanitize("foo:bar"), "foo:bar");
#endif
}

TORRENT_TEST(sanitize_path_borrow)
{
	// an element that needs no sanitization is borrowed: the function
	// returns true and leaves "path" untouched
	std::string path;
	TEST_CHECK(lt::aux::sanitize_path_element(path, "readme.txt"));
	TEST_CHECK(path.empty());

	// an element that needs sanitizing is materialized into "path", and the
	// function returns false
	path.clear();
	TEST_CHECK(!lt::aux::sanitize_path_element(path, "foo\\bar"));
	TEST_EQUAL(path, "foobar");
}

TORRENT_TEST(sanitize_encoding)
{
	using aux::sanitize_encoding;

	// sanitize_encoding
	std::string test = "\b?filename=4";
	TEST_EQUAL(sanitize_encoding(test), test);

	test = "filename=4";
	TEST_EQUAL(sanitize_encoding(test), test);

	// valid 2-byte sequence
	test = "filename\xc2\xa1";
	TEST_EQUAL(sanitize_encoding(test), test);

	// truncated 2-byte sequence
	test = "filename\xc2";
	TEST_EQUAL(sanitize_encoding(test), "filename_");

	// valid 3-byte sequence
	test = "filename\xe2\x9f\xb9";
	TEST_EQUAL(sanitize_encoding(test), test);

	// truncated 3-byte sequence
	test = "filename\xe2\x9f";
	TEST_EQUAL(sanitize_encoding(test), "filename_");

	// truncated 3-byte sequence
	test = "filename\xe2";
	TEST_EQUAL(sanitize_encoding(test), "filename_");

	// valid 4-byte sequence
	test = "filename\xf0\x9f\x92\x88";
	TEST_EQUAL(sanitize_encoding(test), test);

	// truncated 4-byte sequence
	test = "filename\xf0\x9f\x92";
	TEST_EQUAL(sanitize_encoding(test), "filename_");

	// 5-byte utf-8 sequence (not allowed)
	test = "filename\xf8\x9f\x9f\x9f\x9f""foobar";
	TEST_EQUAL(sanitize_encoding(test), "filename_foobar");

	// redundant (overlong) 2-byte sequence
	// ascii code 0x2e encoded with a leading 0
	test = "filename\xc0\xae";
	TEST_EQUAL(sanitize_encoding(test), "filename_");

	// redundant (overlong) 3-byte sequence
	// ascii code 0x2e encoded with two leading 0s
	test = "filename\xe0\x80\xae";
	TEST_EQUAL(sanitize_encoding(test), "filename_");

	// redundant (overlong) 4-byte sequence
	// ascii code 0x2e encoded with three leading 0s
	test = "filename\xf0\x80\x80\xae";
	TEST_EQUAL(sanitize_encoding(test), "filename_");

	// missing byte header
	test = "filename\xed\0\x80";
	TEST_EQUAL(sanitize_encoding(test), "filename_");
}

namespace {
void sanity_check(std::shared_ptr<torrent_info const> const& ti)
{
	// construct a piece_picker to get some more test coverage. Perhaps
	// loading the torrent is fine, but if we can't construct a piece_picker
	// for it, it's still no good.
	aux::piece_picker pp(ti->total_size(), ti->piece_length());

	TEST_CHECK(ti->piece_length() <= file_storage::max_piece_size);
	TEST_EQUAL(ti->v1(), ti->info_hashes().has_v1());
	TEST_EQUAL(ti->v2(), ti->info_hashes().has_v2());
}
}

TORRENT_TEST(symlink_hop_limit)
{
	// a chain of 12 symlinks, each one's target using the next as a
	// directory hop, landing on a real 11-level-deep directory tower.
	// Hops needed to fully resolve symlink i (0-indexed, root = 0) is
	// (11 - i); with the default max_symlink_hops == 8, index 3 (needs
	// exactly 8 hops) resolves for real, index 2 (needs 9) doesn't and
	// is left self-pointing instead
	std::string const root_dir = parent_path(current_path());
	std::string const filename =
		combine_path(combine_path(root_dir, "test_torrents"), "symlink_hop_limit.torrent");

	auto atp = load_torrent_file(filename);
	TEST_EQUAL(atp.ti->num_files(), 14);
	TEST_EQUAL(
		atp.ti->layout().symlink(file_index_t{4}) != atp.ti->layout().file_path(file_index_t{4}),
		true);
	TEST_EQUAL(
		atp.ti->layout().symlink(file_index_t{3}), atp.ti->layout().file_path(file_index_t{3}));

	// the same chain resolves further (or less far) as max_symlink_hops
	// is raised (or lowered) at load time
	load_torrent_limits cfg;
	cfg.max_symlink_hops = 20;
	auto atp2 = load_torrent_file(filename, cfg);
	TEST_EQUAL(
		atp2.ti->layout().symlink(file_index_t{3}) != atp2.ti->layout().file_path(file_index_t{3}),
		true);

	cfg.max_symlink_hops = 0;
	auto atp3 = load_torrent_file(filename, cfg);
	// needs 0 hops (its own target's only component is the real tower
	// root): resolves even with no hop budget at all
	TEST_EQUAL(atp3.ti->layout().symlink(file_index_t{12})
			!= atp3.ti->layout().file_path(file_index_t{12}),
		true);
	// needs 1 hop: fails with a zero hop budget
	TEST_EQUAL(
		atp3.ti->layout().symlink(file_index_t{11}), atp3.ti->layout().file_path(file_index_t{11}));
}

TORRENT_TEST(symlink_count_limit)
{
	// symlink2.torrent has 2 symlinks; a max_symlinks limit lower than
	// that must reject the whole torrent, matching the
	// max_duplicate_filenames precedent (an operation that isn't cheap,
	// capped explicitly, rejected outright rather than silently
	// truncated)
	std::string const root_dir = parent_path(current_path());
	std::string const filename =
		combine_path(combine_path(root_dir, "test_torrents"), "symlink2.torrent");

	load_torrent_limits cfg;
	cfg.max_symlinks = 1;
	error_code ec;
	auto atp = load_torrent_file(filename, ec, cfg);
	TEST_EQUAL(ec, errors::too_many_symlinks);

	cfg.max_symlinks = 2;
	auto atp2 = load_torrent_file(filename, cfg);
	TEST_EQUAL(atp2.ti->num_files(), 5);
}

TORRENT_TEST(symlink_empty_target)
{
	// a "symlink path" that's present but resolves to no real components
	// (either literally empty, or made up entirely of "." / ".."
	// components) must downgrade the file to a regular, non-symlink file,
	// matching a missing "symlink path" -- rather than leaving it flagged
	// as a symlink with no target
	auto make_torrent = [](entry::list_type symlink_path) {
		entry info;
		info["name"] = "test";
		info["piece length"] = 16 * 1024;
		info["pieces"] = "aaaaaaaaaaaaaaaaaaaa";
		entry link;
		link["length"] = 0;
		link["attr"] = "l";
		entry::list_type link_path;
		link_path.push_back(entry("link"));
		link["path"] = link_path;
		link["symlink path"] = symlink_path;
		// a torrent with only a zero-length file is itself rejected as
		// invalid, so a real file with content is needed alongside it
		entry real;
		real["length"] = 10;
		entry::list_type real_path;
		real_path.push_back(entry("real.txt"));
		real["path"] = real_path;
		info["files"] = entry::list_type{link, real};
		entry torrent;
		torrent["info"] = info;
		return bencode(torrent);
	};

	// an empty "symlink path" list
	{
		std::vector<char> const buf = make_torrent(entry::list_type{});
		auto atp = load_torrent_buffer(buf);
		TEST_EQUAL(atp.ti->num_files(), 2);
		TEST_CHECK(
			!bool(atp.ti->layout().file_flags(file_index_t{0}) & file_storage::flag_symlink));
		TEST_EQUAL(atp.ti->layout().file_path(file_index_t{0}), combine_path("test", "link"));
	}

	// a "symlink path" made up entirely of "." / ".." components
	{
		entry::list_type path;
		path.push_back(entry("."));
		path.push_back(entry(".."));
		std::vector<char> const buf = make_torrent(path);
		auto atp = load_torrent_buffer(buf);
		TEST_EQUAL(atp.ti->num_files(), 2);
		TEST_CHECK(
			!bool(atp.ti->layout().file_flags(file_index_t{0}) & file_storage::flag_symlink));
	}
}

TORRENT_TEST(parse_torrents)
{
	// test torrent parsing

	entry info;
	info["pieces"] = "aaaaaaaaaaaaaaaaaaaa";
	info["name.utf-8"] = "test1";
	info["name"] = "test__";
	info["piece length"] = 16 * 1024;
	info["length"] = 3245;
	entry torrent;
	torrent["info"] = info;

	std::vector<char> const buf1 = bencode(torrent);
	torrent_info ti1 = *load_torrent_buffer(buf1).ti;
	std::cout << ti1.name() << std::endl;
	TEST_CHECK(ti1.name() == "test1");

#ifdef TORRENT_WINDOWS
	info["name.utf-8"] = "c:/test1/test2/test3";
#else
	info["name.utf-8"] = "/test1/test2/test3";
#endif
	torrent["info"] = info;
	std::vector<char> const buf2 = bencode(torrent);
	torrent_info ti2 = *load_torrent_buffer(buf2).ti;
	std::cout << ti2.name() << std::endl;
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(ti2.name(), "c_test1test2test3");
#else
	TEST_EQUAL(ti2.name(), "test1test2test3");
#endif

	info["name.utf-8"] = "test2/../test3/.././../../test4";
	torrent["info"] = info;
	std::vector<char> const buf3 = bencode(torrent);
	torrent_info ti3 = *load_torrent_buffer(buf3).ti;
	std::cout << ti3.name() << std::endl;
	TEST_EQUAL(ti3.name(), "test2..test3.......test4");

	std::string root_dir = parent_path(current_path());
	for (auto const& t : test_torrents)
	{
		std::printf("loading %s\n", t.file);
		std::string filename = combine_path(combine_path(root_dir, "test_torrents")
			, t.file);
#if TORRENT_ABI_VERSION < 4
		error_code ec;
		auto old_ti = std::make_shared<lt::torrent_info>(filename, ec);
		if (ec) std::printf(" -> failed %s\n", ec.message().c_str());
		TEST_CHECK(!ec);
		sanity_check(old_ti);
#endif

		lt::add_torrent_params atp = lt::load_torrent_file(filename);
#if TORRENT_ABI_VERSION < 4
		TEST_CHECK(atp.info_hashes == old_ti->info_hashes());
#endif
		sanity_check(atp.ti);

#if TORRENT_ABI_VERSION < 4
		// trackers are loaded into atp.trackers
		TEST_CHECK(atp.ti->trackers().empty());

		// web seeds are loaded into atp.url_seeds
		TEST_CHECK(atp.ti->web_seeds().empty());
#endif

#if TORRENT_ABI_VERSION < 4
		// piece layers are loaded into atp.merkle_trees and
		// atp.merkle_trees_mask
		TEST_CHECK(!atp.ti->v2_piece_hashes_verified());
#endif

		auto ti = atp.ti;
		if (t.test) t.test(std::move(atp));

		file_storage const& fs = ti->layout();
		for (file_index_t const idx : fs.file_range())
		{
			piece_index_t const first = ti->map_file(idx, 0, 0).piece;
			piece_index_t const last = ti->map_file(idx, std::max(fs.file_size(idx)-1, std::int64_t(0)), 0).piece;
			file_flags_t const flags = fs.file_flags(idx);
#if TORRENT_ABI_VERSION < 4
			sha1_hash const ih = fs.hash(idx);
#endif
			std::printf("  %11" PRId64 " %c%c%c%c [ %4d, %4d ] %7u "
#if TORRENT_ABI_VERSION < 4
				"%s "
#endif
				"%s %s%s\n"
				, fs.file_size(idx)
				, (flags & file_storage::flag_pad_file)?'p':'-'
				, (flags & file_storage::flag_executable)?'x':'-'
				, (flags & file_storage::flag_hidden)?'h':'-'
				, (flags & file_storage::flag_symlink)?'l':'-'
				, static_cast<int>(first), static_cast<int>(last)
				, std::uint32_t(fs.mtime(idx))
#if TORRENT_ABI_VERSION < 4
				, ih != sha1_hash(nullptr) ? aux::to_hex(ih).c_str() : ""
#endif
				, fs.file_path(idx).c_str()
				, flags & file_storage::flag_symlink ? "-> ": ""
				, flags & file_storage::flag_symlink ? fs.symlink(idx).c_str() : "");
		}
	}
}

TORRENT_TEST(parse_invalid_torrents)
{
	std::string const root_dir = parent_path(current_path());
	for (auto const& e : test_error_torrents)
	{
		error_code ec;
		std::printf("loading %s\n", e.file);
		std::string const filename = combine_path(combine_path(root_dir, "test_torrents")
			, e.file);

		try
		{
			auto add_torrent_params = load_torrent_file(filename);
		}
		catch (lt::system_error const& err)
		{
			ec = err.code();
		}
		std::printf("E:        \"%s\"\nexpected: \"%s\"\n", ec.message().c_str()
			, e.error.message().c_str());
		// Some checks only happen in the load_torrent_*() functions, not in the
		// torrent_info constructor. For these, it's OK for ec to not report an
		// error
		if (e.error != errors::torrent_invalid_piece_layer || ec)
		{
			TEST_EQUAL(ec.message(), e.error.message());
		}

		try
		{
			add_torrent_params atp = load_torrent_file(filename);
			TORRENT_ASSERT(!e.error);
		}
		catch (system_error const& err)
		{
			std::printf("E:        \"%s\"\nexpected: \"%s\"\n", err.code().message().c_str()
				, e.error.message().c_str());
			TEST_EQUAL(err.code().message(), e.error.message());
		}
	}
}

TORRENT_TEST(parse_invalid_torrents_no_throw)
{
	std::string const root_dir = parent_path(current_path());
	for (auto const& e : test_error_torrents)
	{
		error_code ec;
		std::printf("loading %s\n", e.file);
		std::string const filename = combine_path(combine_path(root_dir, "test_torrents")
			, e.file);

		auto const atp = load_torrent_file(filename, ec, load_torrent_limits{});
		// minimal validation that the return value is empty
		TEST_EQUAL(atp.name, "");
		TORRENT_ASSERT(!atp.ti);

		std::printf("E:        \"%s\"\nexpected: \"%s\"\n", ec.message().c_str()
			, e.error.message().c_str());
		TEST_EQUAL(ec.message(), e.error.message());
	}
}

namespace {

// builds a v1 .torrent buffer directly from raw path components, one
// list per file, bypassing create_torrent's own path handling. This
// gives tests exact byte-level control over path elements, including
// ones that aren't valid on any filesystem, the way an adversarial or
// simply old/non-conforming .torrent file might contain them. Every
// file is 1 byte, so a single placeholder piece hash always suffices.
std::vector<char> make_v1_torrent_raw(std::vector<std::vector<std::string>> const& file_paths)
{
	entry::list_type files;
	for (auto const& path_components : file_paths)
	{
		entry::list_type path;
		for (auto const& c : path_components)
			path.emplace_back(c);

		entry file;
		file["length"] = 1;
		file["path"] = path;
		files.emplace_back(std::move(file));
	}

	entry info;
	info["name"] = "root";
	info["piece length"] = 16 * 1024;
	info["pieces"] = "aaaaaaaaaaaaaaaaaaaa";
	info["files"] = files;

	entry torrent;
	torrent["info"] = info;
	return bencode(torrent);
}

std::vector<char> make_v1_torrent_with_path_depth(int const depth)
{
	std::vector<std::string> path;
	for (int i = 0; i < depth - 1; ++i)
		path.emplace_back("d");
	path.emplace_back("file.txt");
	return make_v1_torrent_raw({path});
}

// builds a v2-only .torrent whose "file tree" nests `depth` single-child
// directories before the (single-block, so no piece layer needed) leaf
// file. extract_files2() walks this with an explicit stack rather than
// recursion, and checks the stack depth rather than a flat list length
// (as the v1 "path" list check does), so it needs its own coverage.
std::vector<char> make_v2_torrent_with_tree_depth(int const depth)
{
	entry file_meta;
	file_meta["length"] = 1;
	file_meta["pieces root"] = std::string(32, '\x01');

	entry leaf;
	leaf[""] = file_meta;

	entry node;
	node["file.txt"] = leaf;

	for (int i = 0; i < depth - 1; ++i)
	{
		entry dir;
		dir["d"] = node;
		node = dir;
	}

	entry info;
	info["name"] = "root";
	info["piece length"] = 16 * 1024;
	info["meta version"] = 2;
	info["file tree"] = node;

	entry torrent;
	torrent["info"] = info;
	return bencode(torrent);
}

// builds a v1 .torrent whose sole symlink has a "symlink path" (the
// link's target, not the link's own location) of `depth` raw
// components, to exercise extract_single_file()'s separate depth check
// on the target list. This is checked purely at parse time, before
// sanitize_symlinks() ever tries to resolve the (here, dangling) target.
std::vector<char> make_v1_symlink_target_depth_torrent(int const depth)
{
	entry::list_type target;
	for (int i = 0; i < depth; ++i)
		target.emplace_back("d");

	entry::list_type link_path;
	link_path.emplace_back("link");

	entry link_file;
	link_file["path"] = link_path;
	link_file["attr"] = "l";
	link_file["symlink path"] = target;

	entry::list_type regular_path;
	regular_path.emplace_back("regular.txt");
	entry regular_file;
	regular_file["length"] = 1;
	regular_file["path"] = regular_path;

	entry::list_type files;
	files.emplace_back(std::move(link_file));
	files.emplace_back(std::move(regular_file));

	entry info;
	info["name"] = "root";
	info["piece length"] = 16 * 1024;
	info["pieces"] = "aaaaaaaaaaaaaaaaaaaa";
	info["files"] = files;

	entry torrent;
	torrent["info"] = info;
	return bencode(torrent);
}

load_torrent_limits make_limits_with_directory_depth(int const max_depth)
{
	load_torrent_limits cfg;
	cfg.max_directory_depth = max_depth;
	return cfg;
}

// every case builds a torrent whose path/tree/symlink-target depth is
// pushed to `depth`, loads it with `cfg`, and checks the resulting
// error (default-constructed error_code means "expect success"). The
// three builders share this exact test shape even though the depth
// check they each exercise lives in a different function
// (extract_single_file() for v1 paths and symlink targets,
// extract_files2() for the v2 file tree).
struct depth_case_t
{
	std::vector<char> (*builder)(int);
	int depth;
	load_torrent_limits cfg;
	error_code expected; // default: expect success
};

std::vector<depth_case_t> const depth_cases = {
	// v1: the "path" list length is checked directly, so bdecode's own
	// (much shallower) structural nesting is never a factor
	{make_v1_torrent_with_path_depth, 50, load_torrent_limits{}, error_code()},
	{make_v1_torrent_with_path_depth,
		200,
		load_torrent_limits{},
		errors::torrent_directory_too_deep},
	{make_v1_torrent_with_path_depth,
		20,
		make_limits_with_directory_depth(5),
		errors::torrent_directory_too_deep},

	// v2: each directory level is itself a nested bencode dict, so with
	// both limits at their default of 100, a sufficiently deep tree
	// hits bdecode's own structural depth limit
	// (load_torrent_limits::max_decode_depth) before
	// extract_files2()'s separate max_directory_depth check ever runs
	{make_v2_torrent_with_tree_depth, 50, load_torrent_limits{}, error_code()},
	{make_v2_torrent_with_tree_depth, 200, load_torrent_limits{}, bdecode_errors::depth_exceeded},
	// max_directory_depth=5 is small enough that extract_files2()'s own
	// check fires well before bdecode's default depth limit could
	{make_v2_torrent_with_tree_depth,
		20,
		make_limits_with_directory_depth(5),
		errors::torrent_directory_too_deep},

	// symlink target list depth is checked independently of the
	// symlink's own path depth
	{make_v1_symlink_target_depth_torrent, 50, load_torrent_limits{}, error_code()},
	{make_v1_symlink_target_depth_torrent,
		200,
		load_torrent_limits{},
		errors::torrent_directory_too_deep},
	{make_v1_symlink_target_depth_torrent,
		20,
		make_limits_with_directory_depth(5),
		errors::torrent_directory_too_deep},
};

} // anonymous namespace

TORRENT_TEST(load_torrent_directory_depth)
{
	for (auto const& t : depth_cases)
	{
		auto const buf = t.builder(t.depth);
		error_code ec;
		auto const atp = load_torrent_buffer(buf, ec, t.cfg);
		TEST_EQUAL(ec, t.expected);
		if (!t.expected)
			TEST_CHECK(atp.ti);
	}
}

namespace {

	struct file_t
	{
		std::string filename;
		int size;
		file_flags_t flags;
		string_view expected_filename;
	};

	std::vector<lt::aux::vector<file_t, lt::file_index_t>> const test_cases{
		{
			{"test/temporary.txt", 0x4000, {}, "test/temporary.txt"},
			{"test/Temporary.txt", 0x4000, {}, "test/Temporary.1.txt"},
			{"test/TeMPorArY.txT", 0x4000, {}, "test/TeMPorArY.2.txT"},
			// a file with the same name in a separate directory is fine
			{"test/test/TEMPORARY.TXT", 0x4000, {}, "test/test/TEMPORARY.TXT"},
		},
		{
			{"test/b.exe", 0x4000, {}, "test/b.exe"},
			// duplicate of b.exe
			{"test/B.ExE", 0x4000, {}, "test/B.1.ExE"},
			// duplicate of b.exe
			{"test/B.exe", 0x4000, {}, "test/B.2.exe"},
			{"test/filler", 0x4000, {}, "test/filler"},
		},
		{
			{"test/a/b/c/d/e/f/g/h/i/j/k/l/m", 0x4000, {}, "test/a/b/c/d/e/f/g/h/i/j/k/l/m"},
			{"test/a", 0x4000, {}, "test/a.1"},
			{"test/a/b", 0x4000, {}, "test/a/b.1"},
			{"test/a/b/c", 0x4000, {}, "test/a/b/c.1"},
			{"test/a/b/c/d", 0x4000, {}, "test/a/b/c/d.1"},
			{"test/a/b/c/d/e", 0x4000, {}, "test/a/b/c/d/e.1"},
			{"test/a/b/c/d/e/f", 0x4000, {}, "test/a/b/c/d/e/f.1"},
			{"test/a/b/c/d/e/f/g", 0x4000, {}, "test/a/b/c/d/e/f/g.1"},
			{"test/a/b/c/d/e/f/g/h", 0x4000, {}, "test/a/b/c/d/e/f/g/h.1"},
			{"test/a/b/c/d/e/f/g/h/i", 0x4000, {}, "test/a/b/c/d/e/f/g/h/i.1"},
			{"test/a/b/c/d/e/f/g/h/i/j", 0x4000, {}, "test/a/b/c/d/e/f/g/h/i/j.1"},
		},
		{
			// it doesn't matter whether the file comes before the directory,
			// directories take precedence
			{"test/a", 0x4000, {}, "test/a.1"},
			{"test/a/b", 0x4000, {}, "test/a/b"},
		},
		{
			{"test/A/tmp", 0x4000, {}, "test/A/tmp"},
			// a file may not have the same name as a directory
			{"test/a", 0x4000, {}, "test/a.1"},
			// duplicate of directory a
			{"test/A", 0x4000, {}, "test/A.2"},
			{"test/filler", 0x4000, {}, "test/filler"},
		},
		{
			// a subset of this path collides with the next filename
			{"test/long/path/name/that/collides", 0x4000, {}, "test/long/path/name/that/collides"},
			// so this file needs to be renamed, to not collide with the path name
			{"test/long/path", 0x4000, {}, "test/long/path.1"},
			{"test/filler-1", 0x4000, {}, "test/filler-1"},
			{"test/filler-2", 0x4000, {}, "test/filler-2"},
		},
		{
			// pad files are allowed to collide, as long as they have the same
			// size. their name is always synthesized from their size (rooted
			// at the torrent name, same as any other file), so the literal
			// name given here ("1234") is irrelevant
			{"test/.pad/1234", 0x4000, file_storage::flag_pad_file, "test/.pad/16384"},
			{"test/filler-1", 0x4000, {}, "test/filler-1"},
			{"test/.pad/1234", 0x4000, file_storage::flag_pad_file, "test/.pad/16384"},
			{"test/filler-2", 0x4000, {}, "test/filler-2"},
		},
		{
			// pad files of different sizes never collide in the first place,
			// since their synthesized size is part of the path
			{"test/.pad/1234", 0x8000, file_storage::flag_pad_file, "test/.pad/32768"},
			{"test/filler-1", 0x4000, {}, "test/filler-1"},
			{"test/.pad/1234", 0x4000, file_storage::flag_pad_file, "test/.pad/16384"},
			{"test/filler-2", 0x4000, {}, "test/filler-2"},
		},
		{
			// a pad file is rooted at the torrent name just like any other
			// file, so its synthesized path ("test/.pad/16384", its size) can
			// coincide with a normal file's path -- but pad files never touch
			// disk, so this must not cause the normal file to be renamed
			{"test/.pad/16384", 0x4000, {}, "test/.pad/16384"},
			{"test/filler-1", 0x4000, {}, "test/filler-1"},
			{"test/.pad/1234", 0x4000, file_storage::flag_pad_file, "test/.pad/16384"},
			{"test/filler-2", 0x4000, {}, "test/filler-2"},
		},
		{
			// same as above, the other way around
			{"test/.pad/1234", 0x4000, file_storage::flag_pad_file, "test/.pad/16384"},
			{"test/filler-1", 0x4000, {}, "test/filler-1"},
			{"test/.pad/16384", 0x4000, {}, "test/.pad/16384"},
			{"test/filler-2", 0x4000, {}, "test/filler-2"},
		},
		{
			// a pad file's synthesized path can also coincide with a
			// directory implied by a normal file; same reasoning, no rename
			{"test/.pad/1234", 1234, file_storage::flag_pad_file, "test/.pad/1234"},
			{"test/filler-1", 0x4000, {}, "test/filler-1"},
			{"test/.pad/1234/filler-2", 0x4000, {}, "test/.pad/1234/filler-2"},
		},
		{
			// two directories differing only by case are allowed to coexist
			// unrenamed, unlike two files. They may fold together into a
			// single directory on a case-insensitive filesystem, but that's
			// harmless since directories don't carry content of their own
			{"test/Dir/a", 0x4000, {}, "test/Dir/a"},
			{"test/dir/b", 0x4000, {}, "test/dir/b"},
		},
		{
			// a leading-dot ("hidden") filename has no extension in the
			// conventional sense: remove_extension()/extension() find the
			// leading '.' itself and treat everything after it (in its
			// original, non-lowercased case) as the "extension", so the
			// disambiguating counter is spliced in before that leading dot
			// rather than appended after the name
			{"test/.hidden", 0x4000, {}, "test/.hidden"},
			{"test/.HIDDEN", 0x4000, {}, "test/.1.HIDDEN"},
		},
		{
			// the first attempt at a disambiguated name ("a.1.txt") is
			// itself already taken by an unrelated, pre-existing file, so
			// the counter must skip past it and try again
			{"test/a.txt", 0x4000, {}, "test/a.txt"},
			{"test/a.1.txt", 0x4000, {}, "test/a.1.txt"},
			// duplicate of a.txt; ".1.txt" is taken, so this becomes ".2.txt"
			{"test/A.txt", 0x4000, {}, "test/A.2.txt"},
			{"test/filler", 0x4000, {}, "test/filler"},
		},
	};

	std::string resolved_path(lt::add_torrent_params const& atp, lt::file_index_t const i)
	{
		std::string p;
		auto const it = atp.renamed_files.find(i);
		if (it == atp.renamed_files.end())
			p = atp.ti->layout().file_path(i);
		else
			p = it->second;
		convert_path_to_posix(p);
		return p;
	}

	void test_resolve_duplicates(aux::vector<file_t, file_index_t> const& test)
	{
		std::vector<lt::create_file_entry> fs;
		for (auto const& f : test)
			fs.emplace_back(f.filename, f.size, f.flags);

		// This test creates torrents with duplicate (identical) filenames, which
		// isn't supported by v2 torrents, so we can only test this with v1 torrents
		lt::create_torrent t(std::move(fs), 0x4000, create_torrent::v1_only);

		for (auto const i : t.piece_range())
			t.set_hash(i, sha1_hash::max());

		std::vector<char> const tmp = t.generate_buf();
		auto const atp = load_torrent_buffer(tmp);
		for (auto const i : t.file_range())
		{
			std::string const p = resolved_path(atp, i);
			std::printf("%s == %s\n", p.c_str(), std::string(test[i].expected_filename).c_str());

			TEST_EQUAL(p, test[i].expected_filename);
		}
	}

} // anonymous namespace

TORRENT_TEST(resolve_duplicates)
{
	for (auto const& t : test_cases)
		test_resolve_duplicates(t);
}

namespace {

// N files that all sanitize to the identical name, forcing the
// duplicate-resolver's ".N" counter loop to run. Every failed attempt at
// finding a free "name.<n>.ext" counts against load_torrent_limits::
// max_duplicate_filenames, shared across the whole torrent, so an
// identical set of N inputs is used to probe both sides of a given
// limit: with 2 duplicates (1 file needs renaming) the counter never
// increments past 0, but with 3 duplicates (2 files need renaming) the
// second rename's first attempt collides with the first rename's
// result, incrementing the counter to 1.
std::vector<char> make_v1_torrent_with_n_duplicates(int const n)
{
	std::vector<std::vector<std::string>> paths;
	for (int i = 0; i < n; ++i)
		paths.push_back({"dir", "dup.txt"});
	return make_v1_torrent_raw(paths);
}

struct duplicate_limit_case_t
{
	int n;
	int max_duplicate_filenames;
	error_code expected; // default: expect success
};

std::vector<duplicate_limit_case_t> const duplicate_limit_cases = {
	// 2 duplicates need only 1 (always-successful) rename attempt, so the
	// collision counter never increments past 0
	{2, 0, error_code()},
	// 3 duplicates need a second rename whose first attempt collides with
	// the first rename's result, incrementing the counter to 1
	{3, 0, errors::too_many_duplicate_filenames},
};

} // anonymous namespace

TORRENT_TEST(load_torrent_duplicate_filenames_configurable)
{
	for (auto const& t : duplicate_limit_cases)
	{
		auto const buf = make_v1_torrent_with_n_duplicates(t.n);
		error_code ec;
		load_torrent_limits cfg;
		cfg.max_duplicate_filenames = t.max_duplicate_filenames;
		auto const atp = load_torrent_buffer(buf, ec, cfg);
		TEST_EQUAL(ec, t.expected);
		if (!t.expected)
			TEST_CHECK(atp.ti);
	}
}

TORRENT_TEST(resolve_duplicate_filenames_bucket_scan_cap)
{
	// unlike duplicate_limit_cases above, this isn't exercising a real
	// naming collision: every file here has a distinct name, so
	// resolve_duplicate_filenames_slow() never finds a match and never
	// renames anything, max_duplicate_filenames (the counter that bounds
	// failed rename attempts) never comes into play. Instead,
	// element_hashes::crc is doctored after the fact so every file's hash
	// collides, simulating what a crafted, non-keyed hash could otherwise
	// force for real: an ever-growing single bucket, scanned in full on
	// every subsequent lookup. This is what the size-scaled bucket-scan
	// budget, independent of max_duplicate_filenames, is meant to catch.
	auto build = [](int const n) {
		file_storage fs;
		fs.set_piece_length(0x4000);
		for (int i = 0; i < n; ++i)
			fs.add_file_borrow({}, combine_path("dir", "file" + std::to_string(i)), 1);

		file_storage::element_hashes eh = fs.compute_element_hashes();
		for (auto const idx : eh.is_dir.range())
			if (!eh.is_dir[idx])
				eh.crc[idx] = 0xdeadbeefu;
		return std::make_pair(std::move(fs), std::move(eh));
	};

	// cumulative scan cost for n all-colliding, distinct files is
	// 1 + 2 + ... + (n - 1), i.e. n * (n - 1) / 2
	{
		// 10 * 9 / 2 == 45, comfortably under the 10 * 16 == 160 budget
		auto [fs, eh] = build(10);
		error_code ec;
		aux::resolve_duplicate_filenames_slow(fs, eh, 10000, ec);
		TEST_CHECK(!ec);
	}
	{
		// 200 * 199 / 2 == 19900, well past the 200 * 16 == 3200 budget
		auto [fs, eh] = build(200);
		error_code ec;
		aux::resolve_duplicate_filenames_slow(fs, eh, 10000, ec);
		TEST_EQUAL(ec, errors::too_many_duplicate_filenames);
	}
}

namespace {

// each case builds a v1 torrent straight from raw path components (via
// make_v1_torrent_raw(), bypassing create_torrent's own path handling),
// loads it through load_torrent_buffer() end-to-end (not just
// sanitize_path_element() in isolation), and checks the resulting
// file paths and rename count. This locks in a baseline for the current
// path-sanitization and duplicate-filename-resolution rules, so a future
// change to either ruleset has something concrete to diff against.
struct raw_path_sanitize_case
{
	std::vector<std::vector<std::string>> paths;
	std::vector<std::string> expected;
	std::size_t expected_renamed;
};

std::vector<raw_path_sanitize_case> const raw_path_sanitize_cases = {
	{// every directory name here is chosen to be distinct, so none of
		// these accidentally collide with one another.
		{
			// a valid name is passed through unchanged (positive case)
			{"valid_name", "leaf.txt"},
			// '/' embedded in a path element is silently dropped, not
			// replaced with '_' -- it does not act as a path separator here
			{"a/b", "leaf2.txt"},
			// a control character is replaced with '_', one '_' per invalid
			// character (negative case: invalid, but not rejected outright)
			{std::string("x\x01y", 3), "leaf3.txt"},
			// ".." sanitizes to a directory that's entirely made of dots, which
			// is reverted (never a real directory traversal)
			{"..", "leaf4.txt"},
			// an invalid character in the leaf itself, not just a directory
			{"dir5", std::string("bad\x02name.txt", 12)},
		},
		{
			"root/valid_name/leaf.txt",
			"root/ab/leaf2.txt",
			"root/x_y/leaf3.txt",
			"root/_/leaf4.txt",
			"root/dir5/bad_name.txt",
		},
		0},
	{// two different, individually invalid raw directory names ('/' and
		// '\\', both entirely filtered out by the sanitizer) collapse to the
		// identical sanitized name "_". Current behavior: directories are
		// exempt from collision-renaming (unlike files), so both are left
		// alone and simply fold together into the same "_" directory. A
		// third, top-level file whose raw name also sanitizes to "_" then
		// collides with that directory and, unlike the directories
		// themselves, must be renamed.
		{
			{"/", "fileA"},
			{"\\", "fileB"},
			{"/"},
		},
		{
			"root/_/fileA",
			"root/_/fileB",
			// the two merged directories are not renamed; only the
			// colliding file is
			"root/_.1",
		},
		1},
	{// two different, individually invalid raw filenames (each with a
		// distinct control character) sanitize to the identical name.
		// Unlike directories, files are never allowed to collide, so the
		// second one is disambiguated exactly like a plain
		// case-insensitive collision would be.
		{
			{"dir", std::string("x\x01", 2)},
			{"dir", std::string("x\x02", 2)},
		},
		{
			"root/dir/x_",
			"root/dir/x_.1",
		},
		1},
};

void test_raw_path_sanitize_case(raw_path_sanitize_case const& t)
{
	auto const buf = make_v1_torrent_raw(t.paths);

	error_code ec;
	auto const atp = load_torrent_buffer(buf, ec, load_torrent_limits{});
	TEST_CHECK(!ec);
	TEST_CHECK(atp.ti);
	if (!atp.ti)
		return;

	TEST_EQUAL(atp.renamed_files.size(), t.expected_renamed);
	TEST_EQUAL(atp.ti->layout().num_files(), int(t.expected.size()));

	for (lt::file_index_t const i : atp.ti->layout().file_range())
	{
		std::string const p = resolved_path(atp, i);
		std::string const& expected = t.expected[std::size_t(static_cast<int>(i))];
		std::printf("%s == %s\n", p.c_str(), expected.c_str());
		TEST_EQUAL(p, expected);
	}
}

} // anonymous namespace

TORRENT_TEST(load_torrent_sanitize_regression)
{
	for (auto const& t : raw_path_sanitize_cases)
		test_raw_path_sanitize_case(t);
}

namespace {

	// a small number of test_torrents entries validate fields that live
	// outside the info dict:
	// * creation_date.torrent / no_creation_date.torrent check the deprecated
	//   torrent_info::creation_date(), which is only ever populated when
	//   parsing a full .torrent file
	// * similar.torrent / collection.torrent put their "similar"/"collections"
	//   lists at the top level of the .torrent file, per BEP38 (the ".2"
	//   variants of these fixtures put the same lists inside the info dict
	//   instead, and are not skipped)
	// Metadata received at run-time (via ut_metadata / set_metadata()) only
	// ever contains the info dict, so those specific checks cannot pass
	// through this path, and they are unrelated to filename sanitization or
	// deduplication, so they are skipped here.
	bool skip_set_metadata_test(char const* file)
	{
		return file == "creation_date.torrent"_sv || file == "no_creation_date.torrent"_sv
			|| file == "similar.torrent"_sv || file == "collection.torrent"_sv;
	}

	// metadata received at run-time (e.g. via the ut_metadata extension, here
	// simulated with torrent_handle::set_metadata()) must go through the same
	// filename sanitization and duplicate-filename resolution as loading the
	// same .torrent file from disk. Re-use each test_torrents entry's own
	// validation callback to check that.
	void test_set_metadata_resolve_duplicate_filenames(
		lt::session& ses, test_torrent_t const& t, std::string const& filename)
	{
		lt::add_torrent_params const ref = lt::load_torrent_file(filename);

		auto const is = ref.ti->info_section();
		std::vector<char> const info_section(is.begin(), is.end());

		// simulate a genuine magnet link add: no metadata (and none of the
		// knowledge, like the disambiguated file names, that only comes from
		// having already parsed a full .torrent file), but keep the fields
		// that come from outside the info dict (trackers, web seeds, DHT
		// nodes, ...), the same way a real magnet URI might supply them
		lt::add_torrent_params atp = ref;
		atp.ti.reset();
		atp.renamed_files.clear();
		atp.save_path = ".";

		lt::torrent_handle h = ses.add_torrent(atp);
		TEST_CHECK(h.is_valid());
		h.set_metadata(info_section);

		lt::alert const* m = wait_for_alert(ses, lt::metadata_received_alert::alert_type, t.file);
		TEST_CHECK(m);

		// query the actual file names the torrent would use, the same way an
		// application resuming this torrent later would see them
		h.save_resume_data(lt::torrent_handle::save_info_dict);
		lt::alert const* r = wait_for_alert(ses, lt::save_resume_data_alert::alert_type, t.file);
		TEST_CHECK(r);
		auto const* rda = lt::alert_cast<lt::save_resume_data_alert>(r);
		TEST_CHECK(rda);
		TEST_CHECK(rda->params.ti);

		if (t.test && rda->params.ti)
		{
			lt::add_torrent_params result = atp;
			result.ti = rda->params.ti;
			result.renamed_files = rda->params.renamed_files;
			t.test(std::move(result));
		}

		ses.remove_torrent(h);
	}

} // anonymous namespace

TORRENT_TEST(set_metadata_resolve_duplicate_filenames)
{
	std::string const root_dir = parent_path(current_path());

	lt::session_params p = settings();
	p.settings.set_int(lt::settings_pack::alert_mask,
		lt::alert_category::status | lt::alert_category::error | lt::alert_category::storage);
	p.settings.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:6881");
	lt::session ses(p);

	for (auto const& t : test_torrents)
	{
		if (skip_set_metadata_test(t.file)) continue;
		std::printf("set_metadata: %s\n", t.file);
		std::string const filename = combine_path(combine_path(root_dir, "test_torrents"), t.file);
		test_set_metadata_resolve_duplicate_filenames(ses, t, filename);
	}
}

TORRENT_TEST(empty_file)
{
	TEST_THROW(load_torrent_buffer(""));
}

TORRENT_TEST(empty_file2)
{
	try
	{
		auto atp = load_torrent_buffer("");
		TEST_ERROR("expected exception thrown");
	}
	catch (system_error const& e)
	{
		std::printf("Expected error: %s\n", e.code().message().c_str());
	}
}

TORRENT_TEST(load_torrent_empty_file)
{
	try
	{
		auto atp = load_torrent_buffer({});
		TEST_ERROR("expected exception thrown");
	}
	catch (system_error const& e)
	{
		std::printf("Expected error: %s\n", e.code().message().c_str());
	}
}

TORRENT_TEST(copy)
{
	using namespace lt;

	std::shared_ptr<torrent_info const> a = load_torrent_file(
		combine_path(parent_path(current_path()), combine_path("test_torrents", "sample.torrent")))
												.ti;

	// the padding file's name in the .torrent is "0", but pad files never
	// store a name, it's always synthesized from their size
	aux::vector<char const*, file_index_t> expected_files = {
		"sample/text_file2.txt",
		"sample/.____padding_file/16359",
		"sample/text_file.txt",
	};

#if TORRENT_ABI_VERSION < 4
	aux::vector<sha1_hash, file_index_t> file_hashes = {sha1_hash(), sha1_hash(), sha1_hash()};
#endif

	file_storage const& fs = a->layout();
	for (auto const i : fs.file_range())
	{
		std::string p = fs.file_path(i);
		convert_path_to_posix(p);
		TEST_EQUAL(p, expected_files[i]);
		std::printf("%s\n", p.c_str());
	}

	// copy the torrent_info object
	std::shared_ptr<torrent_info> b = std::make_shared<torrent_info>(*a);
	a.reset();

	TEST_EQUAL(b->num_files(), 3);

	file_storage const& fs2 = b->layout();
	for (auto const i : fs2.file_range())
	{
		std::string p = fs2.file_path(i);
		convert_path_to_posix(p);
		TEST_EQUAL(p, expected_files[i]);
		std::printf("%s\n", p.c_str());
	}
}

struct A
{
	int val;
};

TORRENT_TEST(copy_ptr)
{
	aux::copy_ptr<A> a(new A{4});
	aux::copy_ptr<A> b(a);

	TEST_EQUAL(a->val, b->val);
	TEST_CHECK(&*a != &*b);
	a->val = 5;
	TEST_EQUAL(b->val, 4);
}

#if TORRENT_ABI_VERSION < 4
TORRENT_TEST(torrent_info_with_hashes_roundtrip)
{
	std::string const root_dir = parent_path(current_path());
	std::string const filename = combine_path(combine_path(root_dir, "test_torrents"), "v2_only.torrent");

	error_code ec;
	std::vector<char> data;
	TEST_CHECK(load_file(filename, data, ec) == 0);

	auto ti = std::make_shared<torrent_info>(data, ec, from_span);
	TEST_CHECK(!ec);
	if (ec) std::printf(" loading(\"%s\") -> failed %s\n", filename.c_str()
		, ec.message().c_str());

	TEST_CHECK(ti->v2());
	TEST_CHECK(!ti->v1());
	TEST_EQUAL(ti->v2_piece_hashes_verified(), true);

	add_torrent_params atp;
	atp.ti = ti;
	atp.save_path = ".";

	session ses(settings());
	torrent_handle h = ses.add_torrent(atp);

	TEST_CHECK(ti->v2());
	TEST_CHECK(!ti->v1());

	{
		auto ti2 = h.torrent_file();
		TEST_CHECK(ti2->v2());
		TEST_CHECK(!ti2->v1());
		TEST_EQUAL(ti2->v2_piece_hashes_verified(), false);
	}

	ti = h.torrent_file_with_hashes();

	TEST_CHECK(ti->v2());
	TEST_CHECK(!ti->v1());
	TEST_EQUAL(ti->v2_piece_hashes_verified(), true);

	std::vector<char> out_buffer = serialize(*ti);

	TEST_EQUAL(out_buffer, data);
}
#endif

TORRENT_TEST(write_torrent_file_session_roundtrip)
{
	std::string const root_dir = combine_path(parent_path(current_path()), "test_torrents");

	auto const files = {
		"base.torrent",
		"empty_path.torrent",
		"parent_path.torrent",
		"hidden_parent_path.torrent",
		"single_multi_file.torrent",
		"slash_path.torrent",
		"slash_path2.torrent",
		"slash_path3.torrent",
		"backslash_path.torrent",
		"long_name.torrent",
		"duplicate_files.torrent",
		"pad_file.torrent",
		"creation_date.torrent",
		"no_creation_date.torrent",
		"url_seed.torrent",
		"url_seed_multi_single_file.torrent",
		"empty_path_multi.torrent",
		"invalid_name2.torrent",
		"invalid_name3.torrent",
		"symlink1.torrent",
		"symlink2.torrent",
		"unordered.torrent",
		"symlink_zero_size.torrent",
		"pad_file_no_path.torrent",
		"large.torrent",
		"absolute_filename.torrent",
		"invalid_filename.torrent",
		"invalid_filename2.torrent",
		"invalid_directory_name.torrent",
		"overlapping_symlinks.torrent",
		"v2.torrent",
		"v2_multipiece_file.torrent",
		"v2_only.torrent",
		"v2_invalid_filename.torrent",
		"v2_multiple_files.torrent",
		"v2_symlinks.torrent",
		"v2_hybrid.torrent",
		"empty-files-1.torrent",
		"empty-files-2.torrent",
		"empty-files-3.torrent",
		"empty-files-4.torrent",
		"empty-files-5.torrent",
		"similar.torrent",
		"collection.torrent",
		"collection2.torrent",
		"similar.torrent",
		"similar2.torrent",
		"dht_nodes.torrent",
	};

	for (auto const& name : files)
	{
		std::string const filename = combine_path(root_dir, name);

		std::printf("loading(\"%s\")\n", name);
		error_code ec;
		std::vector<char> data;
		TEST_CHECK(load_file(filename, data, ec) == 0);
		TEST_CHECK(!ec);

		add_torrent_params atp = load_torrent_buffer(data);
		atp.save_path = ".";

		session ses(settings());
		torrent_handle h = ses.add_torrent(atp);

		h.save_resume_data(torrent_handle::save_info_dict);
		alert const* a = wait_for_alert(ses, save_resume_data_alert::alert_type);

		TORRENT_ASSERT(a);
		{
			auto p = static_cast<save_resume_data_alert const*>(a)->params;
			// dht nodes don't really round-trip cleanly. We don't specifically
			// record the node list from the torrent file
			p.dht_nodes = atp.dht_nodes;
			entry e = write_torrent_file(p, write_flags::include_dht_nodes);
			std::vector<char> const out_buffer = bencode(e);

			TEST_CHECK(out_buffer == write_torrent_file_buf(p, write_flags::include_dht_nodes));

			if (out_buffer != data)
			{
				std::cout << "GOT: (" << out_buffer.size() << ")\n";
				for (char b : out_buffer)
					std::cout << (std::isprint(std::uint8_t(b)) ? b : '.');
				std::cout << '\n';

				std::cout << "EXPECTED: (" << data.size() << ")\n";
				for (char b : data)
					std::cout << (std::isprint(std::uint8_t(b)) ? b : '.');
				std::cout << '\n';
			}
			TEST_CHECK(out_buffer == data);
		}

		{
			add_torrent_params p = load_torrent_file(filename);
			entry e = write_torrent_file(p, write_flags::include_dht_nodes);
			std::vector<char> const out_buffer = bencode(e);

			TEST_CHECK(out_buffer == write_torrent_file_buf(p, write_flags::include_dht_nodes));

			if (out_buffer != data)
			{
				std::cout << "GOT: (" << out_buffer.size() << ")\n";
				for (char b : out_buffer)
					std::cout << (std::isprint(std::uint8_t(b)) ? b : '.');
				std::cout << '\n';

				std::cout << "EXPECTED: (" << data.size() << ")\n";
				for (char b : data)
					std::cout << (std::isprint(std::uint8_t(b)) ? b : '.');
				std::cout << '\n';
			}
			TEST_CHECK(out_buffer == data);
		}
	}
}
