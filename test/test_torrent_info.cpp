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
#include "libtorrent/load_torrent.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/disk_interface.hpp" // for default_block_size
#include "libtorrent/aux_/escape_string.hpp" // for convert_path_to_posix
#include "libtorrent/aux_/piece_picker.hpp"
#include "libtorrent/hex.hpp" // to_hex
#include "libtorrent/write_resume_data.hpp" // write_torrent_file

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

static test_torrent_t const test_torrents[] =
{
	{ "base.torrent"},
	{ "empty_path.torrent" },
	{ "parent_path.torrent" },
	{ "hidden_parent_path.torrent" },
	{ "single_multi_file.torrent" },
	{ "slash_path.torrent", [](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.ti->num_files(), 1);
			TEST_EQUAL(atp.ti->files().file_path(file_index_t{0}), "temp" SEPARATOR "bar");
		}
	},
	{ "slash_path2.torrent", [](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.ti->num_files(), 1);
			TEST_EQUAL(atp.ti->files().file_path(file_index_t{0}), "temp" SEPARATOR "abc....def" SEPARATOR "bar");
		}
	},
	{ "slash_path3.torrent", [](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.ti->num_files(), 1);
			TEST_EQUAL(atp.ti->files().file_path(file_index_t{0}), "temp....abc");
		}
	},
	{ "backslash_path.torrent" },
	{ "url_list.torrent" },
	{ "url_list2.torrent" },
	{ "url_list3.torrent" },
	{ "httpseed.torrent" },
	{ "empty_httpseed.torrent" },
	{ "long_name.torrent" },
	{ "whitespace_url.torrent", [](lt::add_torrent_params atp) {
			// make sure we trimmed the url
			TEST_CHECK(atp.trackers.size() > 0);
			if (atp.trackers.size() > 0)
				TEST_CHECK(atp.trackers[0] == "udp://test.com/announce");
		}
	},
	{ "duplicate_files.torrent", [](lt::add_torrent_params atp) {
			// make sure we disambiguated the files
			TEST_EQUAL(atp.ti->num_files(), 2);
			TEST_CHECK(atp.ti->files().file_path(file_index_t{0}) == combine_path(combine_path("temp", "foo"), "bar.txt"));
			TEST_CHECK(atp.ti->files().file_path(file_index_t{1}) == combine_path(combine_path("temp", "foo"), "bar.1.txt"));
		}
	},
	{ "pad_file.torrent", [](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.ti->num_files(), 2);
			TEST_EQUAL(bool(atp.ti->files().file_flags(file_index_t{0}) & file_storage::flag_pad_file), false);
			TEST_EQUAL(bool(atp.ti->files().file_flags(file_index_t{1}) & file_storage::flag_pad_file), true);
		}
	},
	{ "creation_date.torrent", [](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.ti->creation_date(), 1234567);
		}
	},
	{ "no_creation_date.torrent", [](lt::add_torrent_params atp) {
			TEST_CHECK(!atp.ti->creation_date());
		}
	},
	{ "url_seed.torrent", [](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.url_seeds.size(), 1);
			TEST_EQUAL(atp.url_seeds[0], "http://test.com/file");
#if TORRENT_ABI_VERSION == 1
			// when using load_torrent, the web seeds are not stored in
			// the torrent_info object, just the add_torrent_params object
			TEST_EQUAL(atp.ti->http_seeds().size(), 0);
			TEST_EQUAL(atp.ti->url_seeds().size(), 0);
#endif
		}
	},
	{ "url_seed_multi.torrent", [](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.url_seeds.size(), 1);
			TEST_EQUAL(atp.url_seeds[0], "http://test.com/file/");
#if TORRENT_ABI_VERSION == 1
			// when using load_torrent, the web seeds are not stored in
			// the torrent_info object, just the add_torrent_params object
			TEST_EQUAL(atp.ti->http_seeds().size(), 0);
			TEST_EQUAL(atp.ti->url_seeds().size(), 0);
#endif
		}
	},
	{ "url_seed_multi_single_file.torrent", [](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.url_seeds.size(), 1);
			TEST_EQUAL(atp.url_seeds[0], "http://test.com/file/temp/foo/bar.txt");
		}
	},
	{ "url_seed_multi_space.torrent", [](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.url_seeds.size(), 1);
			TEST_EQUAL(atp.url_seeds[0], "http://test.com/test%20file/foo%20bar/");
#if TORRENT_ABI_VERSION == 1
			// when using load_torrent, the web seeds are not stored in
			// the torrent_info object, just the add_torrent_params object
			TEST_EQUAL(atp.ti->http_seeds().size(), 0);
			TEST_EQUAL(atp.ti->url_seeds().size(), 0);
#endif
		}
	},
	{ "url_seed_multi_space_nolist.torrent", [](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.url_seeds.size(), 1);
			TEST_EQUAL(atp.url_seeds[0], "http://test.com/test%20file/foo%20bar/");
#if TORRENT_ABI_VERSION == 1
			// when using load_torrent, the web seeds are not stored in
			// the torrent_info object, just the add_torrent_params object
			TEST_EQUAL(atp.ti->http_seeds().size(), 0);
			TEST_EQUAL(atp.ti->url_seeds().size(), 0);
#endif
		}
	},
	{ "empty_path_multi.torrent" },
	{ "duplicate_web_seeds.torrent", [](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.url_seeds.size(), 3);
		}
	},
	{ "invalid_name2.torrent", [](lt::add_torrent_params atp) {
			// if, after all invalid characters are removed from the name, it ends up
			// being empty, it's set to the info-hash. Some torrents also have an empty name
			// in which case it's also set to the info-hash
			TEST_EQUAL(atp.ti->name(), "b61560c2918f463768cd122b6d2fdd47b77bdb35");
		}
	},
	{ "invalid_name3.torrent", [](lt::add_torrent_params atp) {
			// windows does not allow trailing spaces in filenames
#ifdef TORRENT_WINDOWS
			TEST_EQUAL(atp.ti->name(), "foobar");
#else
			TEST_EQUAL(atp.ti->name(), "foobar ");
#endif
		}
	},
	{ "symlink1.torrent", [](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.ti->num_files(), 2);
			TEST_EQUAL(atp.ti->files().symlink(file_index_t{1}), "temp" SEPARATOR "a" SEPARATOR "b" SEPARATOR "bar");
		}
	},
	{ "symlink2.torrent", [](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.ti->num_files(), 5);
			TEST_EQUAL(atp.ti->files().symlink(file_index_t{0}), "Some.framework" SEPARATOR "Versions" SEPARATOR "A" SEPARATOR "SDL2");
			TEST_EQUAL(atp.ti->files().symlink(file_index_t{4}), "Some.framework" SEPARATOR "Versions" SEPARATOR "A");
		}
	},
	{ "unordered.torrent" },
	{ "symlink_zero_size.torrent", [](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.ti->num_files(), 2);
			TEST_EQUAL(atp.ti->files().symlink(1_file), "temp" SEPARATOR "a" SEPARATOR "b" SEPARATOR "bar");
		}
	},
	{ "pad_file_no_path.torrent", [](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.ti->num_files(), 2);
			TEST_EQUAL(atp.ti->files().file_path(file_index_t{1}), combine_path(".pad", "2124"));
		}
	},
	{ "large.torrent" },
	{ "absolute_filename.torrent", [](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.ti->num_files(), 2);
			TEST_EQUAL(atp.ti->files().file_path(file_index_t{0}), combine_path("temp", "abcde"));
			TEST_EQUAL(atp.ti->files().file_path(file_index_t{1}), combine_path("temp", "foobar"));
		}
	},
	{ "invalid_filename.torrent", [](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.ti->num_files(), 2);
		}
	},
	{ "invalid_filename2.torrent", [](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.ti->num_files(), 3);
		}
	},
	{ "overlapping_symlinks.torrent", [](lt::add_torrent_params atp) {
			TEST_CHECK(atp.ti->num_files() > 3);
			TEST_EQUAL(atp.ti->files().symlink(file_index_t{0}), "SDL2.framework" SEPARATOR "Versions" SEPARATOR "Current" SEPARATOR "Headers");
			TEST_EQUAL(atp.ti->files().symlink(file_index_t{1}), "SDL2.framework" SEPARATOR "Versions" SEPARATOR "Current" SEPARATOR "Resources");
			TEST_EQUAL(atp.ti->files().symlink(file_index_t{2}), "SDL2.framework" SEPARATOR "Versions" SEPARATOR "Current" SEPARATOR "SDL2");
		}
	},
	{ "v2.torrent", [](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.ti->num_files(), 1);
			TEST_EQUAL(atp.ti->files().file_path(file_index_t{ 0 }), "test64K"_sv);
			TEST_EQUAL(atp.ti->files().file_size(file_index_t{ 0 }), 65536);
			TEST_EQUAL(aux::to_hex(atp.ti->files().root(file_index_t{ 0 })), "60aae9c7b428f87e0713e88229e18f0adf12cd7b22a0dd8a92bb2485eb7af242"_sv);
			TEST_EQUAL(atp.ti->info_hashes().has_v1(), true);
			TEST_EQUAL(atp.ti->info_hashes().has_v2(), true);
			TEST_EQUAL(aux::to_hex(atp.ti->info_hashes().v2), "597b180c1a170a585dfc5e85d834d69013ceda174b8f357d5bb1a0ca509faf0a"_sv);
			TEST_CHECK(atp.ti->v2());
			TEST_CHECK(atp.ti->v1());
		}
	},
	{ "v2_multipiece_file.torrent", [](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.ti->num_files(), 1);
			TEST_EQUAL(atp.ti->files().file_path(file_index_t{ 0 }), "test1MB"_sv);
			TEST_EQUAL(atp.ti->files().file_size(file_index_t{ 0 }), 1048576);
			TEST_EQUAL(aux::to_hex(atp.ti->files().root(file_index_t{ 0 })), "515ea9181744b817744ded9d2e8e9dc6a8450c0b0c52e24b5077f302ffbd9008"_sv);
			TEST_EQUAL(atp.ti->info_hashes().has_v1(), true);
			TEST_EQUAL(atp.ti->info_hashes().has_v2(), true);
			TEST_EQUAL(aux::to_hex(atp.ti->info_hashes().v2), "108ac2c3718ce722e6896edc56c4afa98f1d711ecaace7aad74fca418ebd03de"_sv);
		}
	},
	{ "v2_only.torrent", [](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.ti->num_files(), 1);
			TEST_EQUAL(atp.ti->files().file_path(file_index_t{ 0 }), "test1MB"_sv);
			TEST_EQUAL(atp.ti->files().file_size(file_index_t{ 0 }), 1048576);
			TEST_EQUAL(aux::to_hex(atp.ti->files().root(file_index_t{ 0 })), "515ea9181744b817744ded9d2e8e9dc6a8450c0b0c52e24b5077f302ffbd9008"_sv);
			TEST_EQUAL(atp.ti->info_hashes().has_v1(), false);
			TEST_EQUAL(atp.ti->info_hashes().has_v2(), true);
			TEST_EQUAL(aux::to_hex(atp.ti->info_hashes().v2), "95e04d0c4bad94ab206efa884666fd89777dbe4f7bd9945af1829037a85c6192"_sv);
			TEST_CHECK(atp.ti->v2());
			TEST_CHECK(!atp.ti->v1());
		}
	},
	{ "v2_invalid_filename.torrent", [](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.ti->num_files(), 1);
			TEST_EQUAL(atp.ti->files().file_path(file_index_t{0}), "_estMB"_sv);
		}
	},
	{ "v2_multiple_files.torrent", [](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.merkle_trees.empty(), false);
			TEST_EQUAL(atp.ti->num_files(), 5);
			TEST_CHECK(atp.ti->v2());
			atp.ti->free_piece_layers();
			TEST_CHECK(atp.ti->v2());
			TEST_EQUAL(atp.ti->v2_piece_hashes_verified(), false);
		}
	},
	{ "v2_symlinks.torrent", [](lt::add_torrent_params atp) {
			TEST_CHECK(atp.ti->num_files() > 3);
			TEST_EQUAL(atp.ti->files().symlink(0_file), "SDL2.framework" SEPARATOR "Versions" SEPARATOR "Current" SEPARATOR "Headers");
			TEST_EQUAL(atp.ti->files().symlink(1_file), "SDL2.framework" SEPARATOR "Versions" SEPARATOR "Current" SEPARATOR "Resources");
			TEST_EQUAL(atp.ti->files().symlink(2_file), "SDL2.framework" SEPARATOR "Versions" SEPARATOR "Current" SEPARATOR "SDL2");
		}
	},
	{ "v2_hybrid.torrent", [](lt::add_torrent_params atp) {
			TEST_CHECK(atp.ti->info_hashes().has_v1());
			TEST_CHECK(atp.ti->info_hashes().has_v2());
		}
	},
	{ "empty-files-1.torrent", [](lt::add_torrent_params atp) {
			TEST_CHECK(atp.ti->info_hashes().has_v1());
			TEST_CHECK(atp.ti->info_hashes().has_v2());
		}
	},
	{ "empty-files-2.torrent", [](lt::add_torrent_params atp) {
			TEST_CHECK(atp.ti->info_hashes().has_v1());
			TEST_CHECK(atp.ti->info_hashes().has_v2());
		}
	},
	{ "empty-files-3.torrent", [](lt::add_torrent_params atp) {
			TEST_CHECK(atp.ti->info_hashes().has_v1());
			TEST_CHECK(atp.ti->info_hashes().has_v2());
		}
	},
	{ "empty-files-4.torrent", [](lt::add_torrent_params atp) {
			TEST_CHECK(atp.ti->info_hashes().has_v1());
			TEST_CHECK(atp.ti->info_hashes().has_v2());
		}
	},
	{ "empty-files-5.torrent", [](lt::add_torrent_params atp) {
			TEST_CHECK(atp.ti->info_hashes().has_v1());
			TEST_CHECK(atp.ti->info_hashes().has_v2());
		}
	},
	{ "v2_no_piece_layers.torrent", [](lt::add_torrent_params atp) {
			// it's OK to not have a piece layers field.
			// It's just like adding a magnet link
			TEST_CHECK(!atp.ti->info_hashes().has_v1());
			TEST_CHECK(atp.ti->info_hashes().has_v2());
		}
	},
	{ "v2_incomplete_piece_layer.torrent", [](lt::add_torrent_params atp) {
			// it's OK for some files to not have a piece layer.
			// It's just like adding a magnet link
			TEST_CHECK(!atp.ti->info_hashes().has_v1());
			TEST_CHECK(atp.ti->info_hashes().has_v2());
		}
	},
	{ "similar.torrent", [](lt::add_torrent_params atp) {
			TEST_CHECK((atp.ti->similar_torrents() == std::vector<lt::sha1_hash>{sha1_hash("aaaaaaaaaaaaaaaaaaaa")}));
		}
	},
	{ "similar2.torrent", [](lt::add_torrent_params atp) {
			TEST_CHECK((atp.ti->similar_torrents() == std::vector<lt::sha1_hash>{sha1_hash("aaaaaaaaaaaaaaaaaaaa")}));
		}
	},
	{ "collection.torrent", [](lt::add_torrent_params atp) {
			TEST_CHECK((atp.ti->collections() == std::vector<std::string>{"bar", "foo"}));
		}
	},
	{ "collection2.torrent", [](lt::add_torrent_params atp) {
			TEST_CHECK((atp.ti->collections() == std::vector<std::string>{"bar", "foo"}));
		}
	},
	{ "dht_nodes.torrent", [](lt::add_torrent_params atp) {
			using np = std::pair<std::string, int>;
			TEST_CHECK((atp.dht_nodes == std::vector<np>{np("127.0.0.1", 6881), np("192.168.1.1", 6881)}));
		}
	},
	{ "large_piece_size.torrent", [](lt::add_torrent_params atp) {
			TEST_EQUAL(atp.ti->piece_length(), (32767 * 0x4000));
		}
	},
};

struct test_failing_torrent_t
{
	char const* file;
	error_code error; // the expected error
};

test_failing_torrent_t test_error_torrents[] =
{
	{ "missing_piece_len.torrent", errors::torrent_missing_piece_length },
	{ "invalid_piece_len.torrent", errors::torrent_missing_piece_length },
	{ "negative_piece_len.torrent", errors::torrent_missing_piece_length },
	{ "no_name.torrent", errors::torrent_missing_name },
	{ "bad_name.torrent", errors::torrent_missing_name },
	{ "invalid_name.torrent", errors::torrent_missing_name },
	{ "invalid_info.torrent", errors::torrent_missing_info },
	{ "string.torrent", errors::torrent_is_no_dict },
	{ "negative_size.torrent", errors::torrent_invalid_length },
	{ "negative_file_size.torrent", errors::torrent_invalid_length },
	{ "invalid_path_list.torrent", errors::torrent_invalid_name},
	{ "missing_path_list.torrent", errors::torrent_missing_name },
	{ "invalid_pieces.torrent", errors::torrent_missing_pieces },
	{ "unaligned_pieces.torrent", errors::torrent_invalid_hashes },
	{ "invalid_file_size.torrent", errors::torrent_invalid_length },
	{ "invalid_symlink.torrent", errors::torrent_invalid_name },
	{ "many_pieces.torrent", errors::too_many_pieces_in_torrent },
	{ "no_files.torrent", errors::no_files_in_torrent},
	{ "zero.torrent", errors::torrent_invalid_length},
	{ "zero2.torrent", errors::torrent_invalid_length},
	{ "v2_mismatching_metadata.torrent", errors::torrent_inconsistent_files},
	{ "v2_no_power2_piece.torrent", errors::torrent_missing_piece_length},
	{ "v2_invalid_file.torrent", errors::torrent_file_parse_failed},
	{ "v2_deep_recursion.torrent", bdecode_errors::depth_exceeded},
	{ "v2_non_multiple_piece_layer.torrent", errors::torrent_invalid_piece_layer},
	{ "v2_piece_layer_invalid_file_hash.torrent", errors::torrent_invalid_piece_layer},
	{ "v2_invalid_piece_layer.torrent", errors::torrent_invalid_piece_layer},
	{ "v2_invalid_piece_layer_root.torrent", errors::torrent_invalid_piece_layer},
	{ "v2_unknown_piece_layer_entry.torrent", errors::torrent_invalid_piece_layer},
	{ "v2_invalid_piece_layer_size.torrent", errors::torrent_invalid_piece_layer},
	{ "v2_bad_file_alignment.torrent", errors::torrent_inconsistent_files},
	{ "v2_unordered_files.torrent", errors::invalid_bencoding},
	{ "v2_overlong_integer.torrent", errors::invalid_bencoding},
	{ "v2_missing_file_root_invalid_symlink.torrent", errors::torrent_missing_pieces_root},
	{ "v2_large_file.torrent", errors::torrent_invalid_length},
	{ "v2_large_offset.torrent", errors::too_many_pieces_in_torrent},
	{ "v2_piece_size.torrent", errors::torrent_missing_piece_length},
	{ "v2_invalid_pad_file.torrent", errors::torrent_invalid_pad_file},
	{ "v2_zero_root.torrent", errors::torrent_missing_pieces_root},
	{ "v2_zero_root_small.torrent", errors::torrent_missing_pieces_root},
	{ "duplicate_files2.torrent", errors::too_many_duplicate_filenames},
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
// TODO: verify_encoding with a string that triggers character replacement

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
#endif

TORRENT_TEST(sanitize_path_truncate)
{
	using lt::aux::sanitize_append_path_element;

	std::string path;
	sanitize_append_path_element(path,
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_");
	sanitize_append_path_element(path,
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcde.test");
	TEST_EQUAL(path,
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_" SEPARATOR
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_.test");
}

TORRENT_TEST(sanitize_path_truncate_utf)
{
	using lt::aux::sanitize_append_path_element;

	std::string path;
	// msvc doesn't like unicode string literals, so we encode it as UTF-8 explicitly
	sanitize_append_path_element(path,
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi" "\xE2" "\x80" "\x94" "abcde.jpg");
	TEST_EQUAL(path,
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi_abcdefghi_"
		"abcdefghi_abcdefghi_abcdefghi_abcdefghi" "\xE2" "\x80" "\x94" ".jpg");
}

TORRENT_TEST(sanitize_path_trailing_dots)
{
	std::string path;
	using lt::aux::sanitize_append_path_element;
	sanitize_append_path_element(path, "a");
	sanitize_append_path_element(path, "abc...");
	sanitize_append_path_element(path, "c");
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(path, "a" SEPARATOR "abc" SEPARATOR "c");
#else
	TEST_EQUAL(path, "a" SEPARATOR "abc..." SEPARATOR "c");
#endif

	path.clear();
	sanitize_append_path_element(path, "abc...");
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(path, "abc");
#else
	TEST_EQUAL(path, "abc...");
#endif

	path.clear();
	sanitize_append_path_element(path, "abc.");
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(path, "abc");
#else
	TEST_EQUAL(path, "abc.");
#endif


	path.clear();
	sanitize_append_path_element(path, "a. . .");
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(path, "a");
#else
	TEST_EQUAL(path, "a. . .");
#endif
}

TORRENT_TEST(sanitize_path_trailing_spaces)
{
	using lt::aux::sanitize_append_path_element;
	std::string path;
	sanitize_append_path_element(path, "a");
	sanitize_append_path_element(path, "abc   ");
	sanitize_append_path_element(path, "c");
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(path, "a" SEPARATOR "abc" SEPARATOR "c");
#else
	TEST_EQUAL(path, "a" SEPARATOR "abc   " SEPARATOR "c");
#endif

	path.clear();
	sanitize_append_path_element(path, "abc   ");
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(path, "abc");
#else
	TEST_EQUAL(path, "abc   ");
#endif

	path.clear();
	sanitize_append_path_element(path, "abc ");
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(path, "abc");
#else
	TEST_EQUAL(path, "abc ");
#endif
}

TORRENT_TEST(sanitize_path)
{
	using lt::aux::sanitize_append_path_element;
	std::string path;
	sanitize_append_path_element(path, "\0\0\xed\0\x80");
	TEST_EQUAL(path, "_");

	path.clear();
	sanitize_append_path_element(path, "/a/");
	sanitize_append_path_element(path, "b");
	sanitize_append_path_element(path, "c");
	TEST_EQUAL(path, "a" SEPARATOR "b" SEPARATOR "c");

	path.clear();
	sanitize_append_path_element(path, "a...b");
	TEST_EQUAL(path, "a...b");

	path.clear();
	sanitize_append_path_element(path, "a");
	sanitize_append_path_element(path, "..");
	sanitize_append_path_element(path, "c");
	TEST_EQUAL(path, "a" SEPARATOR "c");

	path.clear();
	sanitize_append_path_element(path, "a");
	sanitize_append_path_element(path, "..");
	TEST_EQUAL(path, "a");

	path.clear();
	sanitize_append_path_element(path, "/..");
	sanitize_append_path_element(path, ".");
	sanitize_append_path_element(path, "c");
	TEST_EQUAL(path, "c");

	path.clear();
	sanitize_append_path_element(path, "dev:");
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(path, "dev_");
#else
	TEST_EQUAL(path, "dev:");
#endif

	path.clear();
	sanitize_append_path_element(path, "c:");
	sanitize_append_path_element(path, "b");
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(path, "c_" SEPARATOR "b");
#else
	TEST_EQUAL(path, "c:" SEPARATOR "b");
#endif

	path.clear();
	sanitize_append_path_element(path, "c:");
	sanitize_append_path_element(path, ".");
	sanitize_append_path_element(path, "c");
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(path, "c_" SEPARATOR "c");
#else
	TEST_EQUAL(path, "c:" SEPARATOR "c");
#endif

	path.clear();
	sanitize_append_path_element(path, "\\c");
	sanitize_append_path_element(path, ".");
	sanitize_append_path_element(path, "c");
	TEST_EQUAL(path, "c" SEPARATOR "c");

	path.clear();
	sanitize_append_path_element(path, "\b");
	TEST_EQUAL(path, "_");

	path.clear();
	sanitize_append_path_element(path, "\b");
	sanitize_append_path_element(path, "filename");
	TEST_EQUAL(path, "_" SEPARATOR "filename");

	path.clear();
	sanitize_append_path_element(path, "filename");
	sanitize_append_path_element(path, "\b");
	TEST_EQUAL(path, "filename" SEPARATOR "_");

	path.clear();
	sanitize_append_path_element(path, "abc");
	sanitize_append_path_element(path, "");
	TEST_EQUAL(path, "abc" SEPARATOR "_");

	path.clear();
	sanitize_append_path_element(path, "abc");
	sanitize_append_path_element(path, "   ");
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(path, "abc");
#else
	TEST_EQUAL(path, "abc" SEPARATOR "   ");
#endif

	path.clear();
	sanitize_append_path_element(path, "");
	sanitize_append_path_element(path, "abc");
	TEST_EQUAL(path, "_" SEPARATOR "abc");

	path.clear();
	sanitize_append_path_element(path, "\b?filename=4");
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(path, "__filename=4");
#else
	TEST_EQUAL(path, "_?filename=4");
#endif

	path.clear();
	sanitize_append_path_element(path, "filename=4");
	TEST_EQUAL(path, "filename=4");

	// valid 2-byte sequence
	path.clear();
	sanitize_append_path_element(path, "filename\xc2\xa1");
	TEST_EQUAL(path, "filename\xc2\xa1");

	// truncated 2-byte sequence
	path.clear();
	sanitize_append_path_element(path, "filename\xc2");
	TEST_EQUAL(path, "filename_");

	// valid 3-byte sequence
	path.clear();
	sanitize_append_path_element(path, "filename\xe2\x9f\xb9");
	TEST_EQUAL(path, "filename\xe2\x9f\xb9");

	// truncated 3-byte sequence
	path.clear();
	sanitize_append_path_element(path, "filename\xe2\x9f");
	TEST_EQUAL(path, "filename_");

	// truncated 3-byte sequence
	path.clear();
	sanitize_append_path_element(path, "filename\xe2");
	TEST_EQUAL(path, "filename_");

	// valid 4-byte sequence
	path.clear();
	sanitize_append_path_element(path, "filename\xf0\x9f\x92\x88");
	TEST_EQUAL(path, "filename\xf0\x9f\x92\x88");

	// truncated 4-byte sequence
	path.clear();
	sanitize_append_path_element(path, "filename\xf0\x9f\x92");
	TEST_EQUAL(path, "filename_");

	// 5-byte utf-8 sequence (not allowed)
	path.clear();
	sanitize_append_path_element(path, "filename\xf8\x9f\x9f\x9f\x9f" "foobar");
	TEST_EQUAL(path, "filename_foobar");

	// redundant (overlong) 2-byte sequence
	// ascii code 0x2e encoded with a leading 0
	path.clear();
	sanitize_append_path_element(path, "filename\xc0\xae");
	TEST_EQUAL(path, "filename_");

	// redundant (overlong) 3-byte sequence
	// ascii code 0x2e encoded with two leading 0s
	path.clear();
	sanitize_append_path_element(path, "filename\xe0\x80\xae");
	TEST_EQUAL(path, "filename_");

	// redundant (overlong) 4-byte sequence
	// ascii code 0x2e encoded with three leading 0s
	path.clear();
	sanitize_append_path_element(path, "filename\xf0\x80\x80\xae");
	TEST_EQUAL(path, "filename_");

	// a filename where every character is filtered is not replaced by an understcore
	path.clear();
	sanitize_append_path_element(path, "//\\");
	TEST_EQUAL(path, "");

	// make sure suspicious unicode characters are filtered out
	path.clear();
	// that's utf-8 for U+200e LEFT-TO-RIGHT MARK
	sanitize_append_path_element(path, "foo\xe2\x80\x8e" "bar");
	TEST_EQUAL(path, "foobar");

	// make sure suspicious unicode characters are filtered out
	path.clear();
	// that's utf-8 for U+202b RIGHT-TO-LEFT EMBEDDING
	sanitize_append_path_element(path, "foo\xe2\x80\xab" "bar");
	TEST_EQUAL(path, "foobar");
}

TORRENT_TEST(sanitize_path_zeroes)
{
	using lt::aux::sanitize_append_path_element;
	std::string path;
	sanitize_append_path_element(path, "\0foo");
	TEST_EQUAL(path, "_");

	path.clear();
	sanitize_append_path_element(path, "\0\0\0\0");
	TEST_EQUAL(path, "_");
}

TORRENT_TEST(sanitize_path_colon)
{
	using lt::aux::sanitize_append_path_element;
	std::string path;
	sanitize_append_path_element(path, "foo:bar");
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(path, "foo_bar");
#else
	TEST_EQUAL(path, "foo:bar");
#endif
}

TORRENT_TEST(verify_encoding)
{
	using aux::verify_encoding;

	// verify_encoding
	std::string test = "\b?filename=4";
	TEST_CHECK(verify_encoding(test));
	TEST_CHECK(test == "\b?filename=4");

	test = "filename=4";
	TEST_CHECK(verify_encoding(test));
	TEST_CHECK(test == "filename=4");

	// valid 2-byte sequence
	test = "filename\xc2\xa1";
	TEST_CHECK(verify_encoding(test));
	std::printf("%s\n", test.c_str());
	TEST_CHECK(test == "filename\xc2\xa1");

	// truncated 2-byte sequence
	test = "filename\xc2";
	TEST_CHECK(!verify_encoding(test));
	std::printf("%s\n", test.c_str());
	TEST_CHECK(test == "filename_");

	// valid 3-byte sequence
	test = "filename\xe2\x9f\xb9";
	TEST_CHECK(verify_encoding(test));
	std::printf("%s\n", test.c_str());
	TEST_CHECK(test == "filename\xe2\x9f\xb9");

	// truncated 3-byte sequence
	test = "filename\xe2\x9f";
	TEST_CHECK(!verify_encoding(test));
	std::printf("%s\n", test.c_str());
	TEST_CHECK(test == "filename_");

	// truncated 3-byte sequence
	test = "filename\xe2";
	TEST_CHECK(!verify_encoding(test));
	std::printf("%s\n", test.c_str());
	TEST_CHECK(test == "filename_");

	// valid 4-byte sequence
	test = "filename\xf0\x9f\x92\x88";
	TEST_CHECK(verify_encoding(test));
	std::printf("%s\n", test.c_str());
	TEST_CHECK(test == "filename\xf0\x9f\x92\x88");

	// truncated 4-byte sequence
	test = "filename\xf0\x9f\x92";
	TEST_CHECK(!verify_encoding(test));
	std::printf("%s\n", test.c_str());
	TEST_CHECK(test == "filename_");

	// 5-byte utf-8 sequence (not allowed)
	test = "filename\xf8\x9f\x9f\x9f\x9f""foobar";
	TEST_CHECK(!verify_encoding(test));
	std::printf("%s\n", test.c_str());
	TEST_CHECK(test == "filename_foobar");

	// redundant (overlong) 2-byte sequence
	// ascii code 0x2e encoded with a leading 0
	test = "filename\xc0\xae";
	TEST_CHECK(!verify_encoding(test));
	std::printf("%s\n", test.c_str());
	TEST_CHECK(test == "filename_");

	// redundant (overlong) 3-byte sequence
	// ascii code 0x2e encoded with two leading 0s
	test = "filename\xe0\x80\xae";
	TEST_CHECK(!verify_encoding(test));
	std::printf("%s\n", test.c_str());
	TEST_CHECK(test == "filename_");

	// redundant (overlong) 4-byte sequence
	// ascii code 0x2e encoded with three leading 0s
	test = "filename\xf0\x80\x80\xae";
	TEST_CHECK(!verify_encoding(test));
	std::printf("%s\n", test.c_str());
	TEST_CHECK(test == "filename_");

	// missing byte header
	test = "filename\xed\0\x80";
	TEST_CHECK(!verify_encoding(test));
	fprintf(stdout, "%s\n", test.c_str());
	TEST_CHECK(test == "filename_");
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
	torrent_info ti1(buf1, from_span);
	std::cout << ti1.name() << std::endl;
	TEST_CHECK(ti1.name() == "test1");

#ifdef TORRENT_WINDOWS
	info["name.utf-8"] = "c:/test1/test2/test3";
#else
	info["name.utf-8"] = "/test1/test2/test3";
#endif
	torrent["info"] = info;
	std::vector<char> const buf2 = bencode(torrent);
	torrent_info ti2(buf2, from_span);
	std::cout << ti2.name() << std::endl;
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(ti2.name(), "c_test1test2test3");
#else
	TEST_EQUAL(ti2.name(), "test1test2test3");
#endif

	info["name.utf-8"] = "test2/../test3/.././../../test4";
	torrent["info"] = info;
	std::vector<char> const buf3 = bencode(torrent);
	torrent_info ti3(buf3, from_span);
	std::cout << ti3.name() << std::endl;
	TEST_EQUAL(ti3.name(), "test2..test3.......test4");

	std::string root_dir = parent_path(current_working_directory());
	for (auto const& t : test_torrents)
	{
		std::printf("loading %s\n", t.file);
		std::string filename = combine_path(combine_path(root_dir, "test_torrents")
			, t.file);
		lt::add_torrent_params atp = lt::load_torrent_file(filename);
		sanity_check(atp.ti);

#if TORRENT_ABI_VERSION < 4
		// trackers are loaded into atp.trackers
		TEST_CHECK(atp.ti->trackers().empty());

		// web seeds are loaded into atp.url_seeds
		TEST_CHECK(atp.ti->web_seeds().empty());
#endif

		// piece layers are loaded into atp.merkle_trees and
		// atp.merkle_trees_mask
		TEST_CHECK(!atp.ti->v2_piece_hashes_verified());

		auto ti = atp.ti;
		if (t.test) t.test(std::move(atp));

		file_storage const& fs = ti->files();
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
	std::string const root_dir = parent_path(current_working_directory());
	for (auto const& e : test_error_torrents)
	{
		error_code ec;
		std::printf("loading %s\n", e.file);
		std::string const filename = combine_path(combine_path(root_dir, "test_torrents")
			, e.file);

		auto ti = std::make_shared<torrent_info>(filename, ec);
		std::printf("E:        \"%s\"\nexpected: \"%s\"\n", ec.message().c_str()
			, e.error.message().c_str());
		// Some checks only happen in the load_torrent_*() functions, not in the
		// torrent_info constructor. For these, it's OK for ec to not report an
		// error
		if (e.error != errors::torrent_invalid_piece_layer || ec)
		{
			TEST_EQUAL(ec.message(), e.error.message());
			TEST_EQUAL(ti->is_valid(), false);
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

namespace {

struct file_t
{
	std::string filename;
	int size;
	file_flags_t flags;
	string_view expected_filename;
};

std::vector<lt::aux::vector<file_t, lt::file_index_t>> const test_cases
{
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
		// pad files are allowed to collide, as long as they have the same size
		{"test/.pad/1234", 0x4000, file_storage::flag_pad_file, "test/.pad/1234"},
		{"test/filler-1", 0x4000, {}, "test/filler-1"},
		{"test/.pad/1234", 0x4000, file_storage::flag_pad_file, "test/.pad/1234"},
		{"test/filler-2", 0x4000, {}, "test/filler-2"},
	},
	{
		// pad files of different sizes are NOT allowed to collide
		{"test/.pad/1234", 0x8000, file_storage::flag_pad_file, "test/.pad/1234"},
		{"test/filler-1", 0x4000, {}, "test/filler-1"},
		{"test/.pad/1234", 0x4000, file_storage::flag_pad_file, "test/.pad/1234.1"},
		{"test/filler-2", 0x4000, {}, "test/filler-2"},
	},
	{
		// pad files are NOT allowed to collide with normal files
		{"test/.pad/1234", 0x4000, {}, "test/.pad/1234"},
		{"test/filler-1", 0x4000, {}, "test/filler-1"},
		{"test/.pad/1234", 0x4000, file_storage::flag_pad_file, "test/.pad/1234.1"},
		{"test/filler-2", 0x4000, {}, "test/filler-2"},
	},
	{
		// normal files are NOT allowed to collide with pad files
		{"test/.pad/1234", 0x4000, file_storage::flag_pad_file, "test/.pad/1234"},
		{"test/filler-1", 0x4000, {}, "test/filler-1"},
		{"test/.pad/1234", 0x4000, {}, "test/.pad/1234.1"},
		{"test/filler-2", 0x4000, {}, "test/filler-2"},
	},
	{
		// pad files are NOT allowed to collide with directories
		{"test/.pad/1234", 0x4000, file_storage::flag_pad_file, "test/.pad/1234.1"},
		{"test/filler-1", 0x4000, {}, "test/filler-1"},
		{"test/.pad/1234/filler-2", 0x4000, {}, "test/.pad/1234/filler-2"},
	},
};

void test_resolve_duplicates(aux::vector<file_t, file_index_t> const& test)
{
	std::vector<lt::create_file_entry> fs;
	for (auto const& f : test) fs.emplace_back(f.filename, f.size, f.flags);

	// This test creates torrents with duplicate (identical) filenames, which
	// isn't supported by v2 torrents, so we can only test this with v1 torrents
	lt::create_torrent t(std::move(fs), 0x4000, create_torrent::v1_only);

	for (auto const i : t.piece_range())
		t.set_hash(i, sha1_hash::max());

	std::vector<char> const tmp = t.generate_buf();
	torrent_info ti(tmp, from_span);
	for (auto const i : t.file_range())
	{
		std::string p = ti.files().file_path(i);
		convert_path_to_posix(p);
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

TORRENT_TEST(empty_file)
{
	error_code ec;
	auto ti = std::make_shared<torrent_info>("", ec, from_span);
	TEST_CHECK(ec);
}

TORRENT_TEST(empty_file2)
{
	try
	{
		auto ti = std::make_shared<torrent_info>("", from_span);
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

	std::shared_ptr<torrent_info> a = std::make_shared<torrent_info>(
		combine_path(parent_path(current_working_directory())
		, combine_path("test_torrents", "sample.torrent")));

	aux::vector<char const*, file_index_t> expected_files =
	{
		"sample/text_file2.txt",
		"sample/.____padding_file/0",
		"sample/text_file.txt",
	};

#if TORRENT_ABI_VERSION < 4
	aux::vector<sha1_hash, file_index_t> file_hashes =
	{
		sha1_hash(nullptr),
		sha1_hash(nullptr),
		sha1_hash("abababababababababab")
	};
#endif

	file_storage const& fs = a->files();
	for (auto const i : fs.file_range())
	{
		std::string p = fs.file_path(i);
		convert_path_to_posix(p);
		TEST_EQUAL(p, expected_files[i]);
		std::printf("%s\n", p.c_str());

#if TORRENT_ABI_VERSION < 4
		TEST_EQUAL(a->files().hash(i), file_hashes[i]);
#endif
	}

	// copy the torrent_info object
	std::shared_ptr<torrent_info> b = std::make_shared<torrent_info>(*a);
	a.reset();

	TEST_EQUAL(b->num_files(), 3);

	file_storage const& fs2 = b->files();
	for (auto const i : fs2.file_range())
	{
		std::string p = fs2.file_path(i);
		convert_path_to_posix(p);
		TEST_EQUAL(p, expected_files[i]);
		std::printf("%s\n", p.c_str());

#if TORRENT_ABI_VERSION < 4
		TEST_EQUAL(fs2.hash(i), file_hashes[i]);
#endif
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
	std::string const root_dir = parent_path(current_working_directory());
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
	std::string const root_dir = combine_path(parent_path(current_working_directory()), "test_torrents");

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

		auto ti = std::make_shared<torrent_info>(data, ec, from_span);
		TEST_CHECK(!ec);
		if (ec) std::printf(" -> failed %s\n", ec.message().c_str());

		add_torrent_params atp;
		atp.ti = ti;
		atp.save_path = ".";

		session ses(settings());
		torrent_handle h = ses.add_torrent(atp);

		h.save_resume_data(torrent_handle::save_info_dict);
		alert const* a = wait_for_alert(ses, save_resume_data_alert::alert_type);

		TORRENT_ASSERT(a);
		{
			auto const& p = static_cast<save_resume_data_alert const*>(a)->params;
			entry e = write_torrent_file(p, write_flags::include_dht_nodes);
			std::vector<char> const out_buffer = bencode(e);

			TEST_CHECK(out_buffer == write_torrent_file_buf(p, write_flags::include_dht_nodes));

			if (out_buffer != data)
			{
				std::cout << "GOT:\n";
				for (char b : out_buffer)
					std::cout << (std::isprint(std::uint8_t(b)) ? b : '.');
				std::cout << '\n';

				std::cout << "EXPECTED:\n";
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
				std::cout << "GOT:\n";
				for (char b : out_buffer)
					std::cout << (std::isprint(std::uint8_t(b)) ? b : '.');
				std::cout << '\n';

				std::cout << "EXPECTED:\n";
				for (char b : data)
					std::cout << (std::isprint(std::uint8_t(b)) ? b : '.');
				std::cout << '\n';
			}
			TEST_CHECK(out_buffer == data);
		}
	}
}

