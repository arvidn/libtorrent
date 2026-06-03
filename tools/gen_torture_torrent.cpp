/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

// gen_torture_torrent - generate "torture" .torrent files that push the
// limits on number of pieces, number of files, directory depth, and name
// lengths. Useful for stress-testing parsing, validation, storage, and
// resume code paths.
//
// The piece and merkle-root hashes set in the generated torrent are derived
// deterministically from indices; they do not match any real file content.
// The output is a syntactically valid .torrent file, but the data it
// references does not exist on disk.

#include <iostream>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include "libtorrent/create_torrent.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/file_storage.hpp"

namespace {

	enum class torrent_version
	{
		v1,
		v2,
		hybrid
	};

	struct config
	{
		int num_files = 10;
		std::int64_t file_size = 1024 * 1024;
		int piece_size = 1024 * 1024;
		int dir_depth = 0;
		int branching = 1;
		int dir_name_len = 16;
		int file_name_len = 16;
		// optional suffix appended to every generated file name (e.g.
		// ".txt"). It eats into the length budget of --file-name-len, so
		// a 260-char name with a 4-char suffix is "f_<idx>_xxx...xxx.txt"
		// with the padding filling the gap. The point is to put a '.'
		// near the end of long names so sanitize_append_path_element()
		// takes the extension-pickup branch when added >= 240.
		std::string file_name_suffix;
		// if true, generated directory and file names are padded with the
		// invalid-UTF-8 byte 0xFF instead of 'x'. Forces every padded byte
		// through the "invalid utf8 sequence, replace with _" branch in
		// sanitize_append_path_element().
		bool invalid_utf8_names = false;
		// the last 'num_symlinks' files are emitted with the symlink flag
		// instead of as regular files. 'symlink_mode' picks what they
		// point at:
		//   "file":  all symlinks point at file 0 (the original behaviour)
		//   "chain": each symlink points at the previous file (the first
		//            one at the last regular file, then symlink i+1 at
		//            symlink i). Forces sanitize_symlinks() into its
		//            second pass and the lsplit_path walking loop.
		//   "dir":   all symlinks point at the directory containing file
		//            0 (requires dir-depth >= 1 to differ from the
		//            torrent root). Triggers the lazily-built sorted
		//            paths + lower_bound dance in is_directory().
		int num_symlinks = 0;
		std::string symlink_mode = "file";
		// number of additional file entries whose paths differ only in
		// capitalization, on top of num_files. libtorrent's
		// resolve_duplicate_filenames() compares paths case-insensitively,
		// so they all collide; works for v1, v2 and hybrid.
		int num_duplicates = 0;
		// number of synthetic tracker URLs to add (in addition to anything
		// passed via --tracker). They are distributed across num_tiers
		// tiers round-robin, so num_tiers == num_trackers gives one
		// tracker per tier (worst case for the announce-list nested-loop
		// parse in load_torrent.cpp).
		int num_trackers = 0;
		int num_tiers = 1;
		// number of synthetic URL seeds (BEP 19), DHT bootstrap nodes,
		// similar-torrent hashes (BEP 38) and collection strings (BEP 38).
		// Each exercises a distinct list-walk in load_torrent.cpp /
		// parse_torrent_file().
		int num_url_seeds = 0;
		int num_dht_nodes = 0;
		int num_similar = 0;
		int num_collections = 0;
		// length in bytes of synthetic 'comment' / 'created by' strings.
		// They go through aux::verify_encoding() in parse_torrent_file().
		// 0 means "do not set the field".
		int comment_len = 0;
		int created_by_len = 0;
		std::string name = "torture";
		torrent_version version = torrent_version::hybrid;
		std::string output;
		std::vector<std::string> trackers;
	};

	void usage()
	{
		std::cout << R"(gen_torture_torrent - generate torture .torrent files

usage: gen_torture_torrent [options]

options:
  --num-files N        number of files in the torrent (default 10)
  --file-size S        size of each file in bytes (default 1M).
                       Accepts K, M, G suffixes
  --piece-size P       piece size in bytes (default 1M, must be a
                       power of 2 >= 16K). Accepts K, M, G suffixes
  --dir-depth D        nested directory depth (default 0)
  --branching B        branching factor at each non-leaf directory
                       level (default 1). Files are evenly
                       distributed across the B^D leaves
  --dir-name-len L     length of each directory name (default 16)
  --file-name-len L    length of each file name (default 16)
  --num-symlinks N     mark the last N files as symlinks. Must be less
                       than --num-files since file 0 has to be a regular
                       file (default 0). See --symlink-mode for what
                       the targets look like
  --symlink-mode M     symlink-target layout (default 'file'):
                         file  - all point at file 0 (a regular file)
                         chain - symlink i points at file i-1, forming
                                 a chain back to the last regular file.
                                 Stresses the 2nd pass in
                                 sanitize_symlinks() and its dir_links
                                 traversal
                         dir   - all point at the directory containing
                                 file 0. Stresses is_directory() (which
                                 lazily sorts m_paths and uses
                                 lower_bound). Most useful with
                                 --dir-depth >= 1 so the target differs
                                 from the torrent root
  --num-duplicates N   add N extra file entries whose names differ only
                       in capitalization, to exercise duplicate-
                       filename resolution (which compares case-
                       insensitively). Works for v1, v2 and hybrid
                       (default 0)
  --file-name-suffix S append S to every generated file name (e.g.
                       ".txt"). The suffix counts against
                       --file-name-len, so a 260-char name with a
                       4-char suffix has 4 fewer pad bytes. Used to
                       get a '.' into the last 10 chars of long names
                       so sanitize_append_path_element() takes the
                       extension-pickup branch
  --invalid-utf8-names pad directory and file names with the
                       invalid-utf8 byte 0xFF instead of 'x'. Every
                       padded byte then goes through the "replace with
                       _" branch of sanitize_append_path_element()
  --num-trackers N     add N synthetic tracker URLs (default 0)
  --num-tiers T        distribute the synthetic trackers across T tiers
                       round-robin (default 1, i.e. all in tier 0).
                       T == --num-trackers gives one tracker per tier
  --num-url-seeds N    add N synthetic url-list entries (default 0)
  --num-dht-nodes N    add N synthetic DHT bootstrap nodes (default 0)
  --num-similar N      add N synthetic 'similar' info-hashes
                       (BEP 38, default 0)
  --num-collections N  add N synthetic 'collections' strings
                       (BEP 38, default 0)
  --comment-len N      generate a synthetic 'comment' field of N bytes
                       (default 0 = no comment)
  --created-by-len N   generate a synthetic 'created by' field of N
                       bytes (default 0 = no created-by)
  --name NAME          torrent name and root directory (default torture)
  --version V          v1, v2 or hybrid (default hybrid)
  --output PATH        output .torrent file (default <name>.torrent)
  --tracker URL        add a tracker. May be repeated
  --help               show this help and exit

Examples:

  Many files under a few nested directories with long names:
    gen_torture_torrent --num-files 10000 --dir-depth 4 --branching 1 \
                        --dir-name-len 200 --file-size 16K

  A bushy tree of small files:
    gen_torture_torrent --num-files 5000 --dir-depth 4 --branching 5

  Many pieces in a single file:
    gen_torture_torrent --num-files 1 --file-size 8G --piece-size 16K
)";
	}

	std::int64_t parse_size(std::string const& s)
	{
		if (s.empty()) throw std::runtime_error("empty size value");
		std::size_t pos = 0;
		std::int64_t const val = std::stoll(s, &pos);
		if (pos == s.size()) return val;
		if (pos + 1 != s.size()) throw std::runtime_error("trailing garbage in size value: " + s);
		switch (s[pos])
		{
			case 'k':
			case 'K':
				return val * 1024;
			case 'm':
			case 'M':
				return val * 1024 * 1024;
			case 'g':
			case 'G':
				return val * std::int64_t(1024) * 1024 * 1024;
			default:
				throw std::runtime_error("unrecognized size suffix: " + std::string(1, s[pos]));
		}
	}

	// Build a deterministic 20-byte sha1-shaped hash from a piece index. The
	// constant fill is non-zero because create_torrent uses an all-zero hash
	// internally to mean "not yet set".
	lt::sha1_hash make_fake_sha1(std::uint32_t const idx)
	{
		std::array<char, 20> out;
		out.fill('\xa5');
		out[0] = char(idx & 0xff);
		out[1] = char((idx >> 8) & 0xff);
		out[2] = char((idx >> 16) & 0xff);
		out[3] = char((idx >> 24) & 0xff);
		return lt::sha1_hash(out.data());
	}

	// Same idea for v2 per-piece merkle roots.
	lt::sha256_hash make_fake_sha256(std::uint32_t const file_idx, std::uint32_t const piece_idx)
	{
		std::array<char, 32> out;
		out.fill('\x5a');
		out[0] = char(file_idx & 0xff);
		out[1] = char((file_idx >> 8) & 0xff);
		out[2] = char((file_idx >> 16) & 0xff);
		out[3] = char((file_idx >> 24) & 0xff);
		out[4] = char(piece_idx & 0xff);
		out[5] = char((piece_idx >> 8) & 0xff);
		out[6] = char((piece_idx >> 16) & 0xff);
		out[7] = char((piece_idx >> 24) & 0xff);
		return lt::sha256_hash(out.data());
	}

	// Pad 'base' with 'pad_char' up to 'target_len'. If 'base' is already at
	// least that long, leave it as-is.
	std::string pad(std::string base, int const target_len, char const pad_char)
	{
		if (int(base.size()) < target_len) base.resize(std::size_t(target_len), pad_char);
		return base;
	}

	std::string dir_name(int const level, int const idx, int const len, char const pad_char)
	{
		char buf[64];
		std::snprintf(buf, sizeof(buf), "L%d_%d_", level, idx);
		return pad(buf, len, pad_char);
	}

	// File names are pad('f_<idx>_', file_name_len - suffix.size()) + suffix,
	// so the requested total length is preserved while the suffix sits at the
	// end (where the extension-pickup logic in sanitize_append_path_element()
	// looks for it).
	std::string file_name(
		int const idx, int const len, char const pad_char, std::string const& suffix)
	{
		char buf[64];
		std::snprintf(buf, sizeof(buf), "f_%d_", idx);
		int const pad_len = std::max(int(std::strlen(buf)), len - int(suffix.size()));
		std::string out = pad(std::string(buf), pad_len, pad_char);
		out += suffix;
		return out;
	}

	// Generate the i-th case variant of a fixed alphabetic base name. Each
	// bit of i decides whether the corresponding letter is uppercased.
	// libtorrent's resolve_duplicate_filenames() compares paths case-
	// insensitively, so all 2^N variants of an N-letter base name are
	// treated as duplicates -- even though they are distinct keys in the
	// v2 file tree dict. This is how we get v2/hybrid duplicate-filename
	// stress without violating v2's "no two children share a key" rule.
	std::string dup_filename(int i)
	{
		// 13 alphabetic chars -> 8192 distinct case variants, far more than
		// can fit under the default max_duplicate_filenames collision limit.
		std::string out = "duplicatefile";
		for (std::size_t j = 0; j < out.size() && i != 0; ++j, i >>= 1)
		{
			if (i & 1) out[j] = char(out[j] - 'a' + 'A');
		}
		return out;
	}

	// Append every leaf-directory path (relative to the torrent root) to 'out'.
	// If depth == 0, the one "leaf" is the empty string. Each directory level
	// uses dir_name(level, i, dir_name_len), so siblings have distinct names
	// but the same names repeat in different subtrees.
	void enumerate_leaves(std::vector<std::string>& out,
		std::string prefix,
		int const level,
		int const depth,
		int const branching,
		int const dir_name_len,
		char const pad_char)
	{
		if (level == depth)
		{
			out.push_back(std::move(prefix));
			return;
		}
		for (int i = 0; i < branching; ++i)
		{
			std::string sub = prefix;
			if (!sub.empty()) sub += '/';
			sub += dir_name(level, i, dir_name_len, pad_char);
			enumerate_leaves(
				out, std::move(sub), level + 1, depth, branching, dir_name_len, pad_char);
		}
	}

} // anonymous namespace

int main(int argc, char const* argv[])
try
{
	config cfg;

	for (int i = 1; i < argc; ++i)
	{
		auto require_arg = [&](char const* opt) -> char const* {
			if (i + 1 >= argc)
			{
				std::cerr << opt << " requires an argument\n";
				std::exit(1);
			}
			return argv[++i];
		};

		std::string_view const a = argv[i];
		if (a == "--help" || a == "-h")
		{
			usage();
			return 0;
		}
		else if (a == "--num-files")
			cfg.num_files = std::atoi(require_arg("--num-files"));
		else if (a == "--file-size")
			cfg.file_size = parse_size(require_arg("--file-size"));
		else if (a == "--piece-size")
			cfg.piece_size = int(parse_size(require_arg("--piece-size")));
		else if (a == "--dir-depth")
			cfg.dir_depth = std::atoi(require_arg("--dir-depth"));
		else if (a == "--branching")
			cfg.branching = std::atoi(require_arg("--branching"));
		else if (a == "--dir-name-len")
			cfg.dir_name_len = std::atoi(require_arg("--dir-name-len"));
		else if (a == "--file-name-len")
			cfg.file_name_len = std::atoi(require_arg("--file-name-len"));
		else if (a == "--num-symlinks")
			cfg.num_symlinks = std::atoi(require_arg("--num-symlinks"));
		else if (a == "--symlink-mode")
			cfg.symlink_mode = require_arg("--symlink-mode");
		else if (a == "--num-duplicates")
			cfg.num_duplicates = std::atoi(require_arg("--num-duplicates"));
		else if (a == "--file-name-suffix")
			cfg.file_name_suffix = require_arg("--file-name-suffix");
		else if (a == "--invalid-utf8-names")
			cfg.invalid_utf8_names = true;
		else if (a == "--num-trackers")
			cfg.num_trackers = std::atoi(require_arg("--num-trackers"));
		else if (a == "--num-tiers")
			cfg.num_tiers = std::atoi(require_arg("--num-tiers"));
		else if (a == "--num-url-seeds")
			cfg.num_url_seeds = std::atoi(require_arg("--num-url-seeds"));
		else if (a == "--num-dht-nodes")
			cfg.num_dht_nodes = std::atoi(require_arg("--num-dht-nodes"));
		else if (a == "--num-similar")
			cfg.num_similar = std::atoi(require_arg("--num-similar"));
		else if (a == "--num-collections")
			cfg.num_collections = std::atoi(require_arg("--num-collections"));
		else if (a == "--comment-len")
			cfg.comment_len = std::atoi(require_arg("--comment-len"));
		else if (a == "--created-by-len")
			cfg.created_by_len = std::atoi(require_arg("--created-by-len"));
		else if (a == "--name")
			cfg.name = require_arg("--name");
		else if (a == "--output")
			cfg.output = require_arg("--output");
		else if (a == "--tracker")
			cfg.trackers.emplace_back(require_arg("--tracker"));
		else if (a == "--version")
		{
			std::string_view const v = require_arg("--version");
			if (v == "v1")
				cfg.version = torrent_version::v1;
			else if (v == "v2")
				cfg.version = torrent_version::v2;
			else if (v == "hybrid")
				cfg.version = torrent_version::hybrid;
			else
			{
				std::cerr << "unrecognized --version: " << v << " (expected v1, v2 or hybrid)\n";
				return 1;
			}
		}
		else
		{
			std::cerr << "unrecognized option: " << a << "\n\n";
			usage();
			return 1;
		}
	}

	if (cfg.num_files <= 0)
	{
		std::cerr << "--num-files must be >= 1\n";
		return 1;
	}
	if (cfg.file_size < 0)
	{
		std::cerr << "--file-size must be >= 0\n";
		return 1;
	}
	if (cfg.piece_size < 16 * 1024 || (cfg.piece_size & (cfg.piece_size - 1)))
	{
		std::cerr << "--piece-size must be a power of 2 >= 16384\n";
		return 1;
	}
	if (cfg.dir_depth < 0)
	{
		std::cerr << "--dir-depth must be >= 0\n";
		return 1;
	}
	if (cfg.branching < 1)
	{
		std::cerr << "--branching must be >= 1\n";
		return 1;
	}
	if (cfg.num_symlinks < 0 || cfg.num_duplicates < 0 || cfg.num_trackers < 0
		|| cfg.num_url_seeds < 0 || cfg.num_dht_nodes < 0 || cfg.num_similar < 0
		|| cfg.num_collections < 0 || cfg.comment_len < 0 || cfg.created_by_len < 0)
	{
		std::cerr << "--num-* and --*-len values must be >= 0\n";
		return 1;
	}
	if (cfg.num_tiers < 1)
	{
		std::cerr << "--num-tiers must be >= 1\n";
		return 1;
	}
	if (cfg.num_symlinks >= cfg.num_files)
	{
		// file 0 has to be a regular file -- it is the target every
		// symlink in the tail points at. Allowing num_symlinks ==
		// num_files would make every entry a symlink with an empty
		// target path.
		std::cerr << "--num-symlinks must be less than --num-files"
					 " (file 0 is needed as the symlink target)\n";
		return 1;
	}
	if (cfg.symlink_mode != "file" && cfg.symlink_mode != "chain" && cfg.symlink_mode != "dir")
	{
		std::cerr << "--symlink-mode must be one of: file, chain, dir\n";
		return 1;
	}

	// --file-name-len has to fit the longest 'f_<idx>_' prefix plus the
	// suffix. Otherwise file_name() silently produces names longer than
	// requested, defeating cases tuned to a specific length (e.g. the
	// >240-char branch in sanitize_append_path_element).
	{
		char buf[64];
		std::snprintf(buf, sizeof(buf), "f_%d_", cfg.num_files - 1);
		if (cfg.file_name_len < int(std::strlen(buf)) + int(cfg.file_name_suffix.size()))
		{
			std::cerr << "--file-name-len too small for prefix + suffix\n";
			return 1;
		}
	}
	// Same for --dir-name-len vs the longest 'L<level>_<idx>_' prefix.
	if (cfg.dir_depth > 0)
	{
		char buf[64];
		std::snprintf(buf, sizeof(buf), "L%d_%d_", cfg.dir_depth - 1, cfg.branching - 1);
		if (cfg.dir_name_len < int(std::strlen(buf)))
		{
			std::cerr << "--dir-name-len too small for prefix\n";
			return 1;
		}
	}

	if (cfg.output.empty()) cfg.output = cfg.name + ".torrent";

	char const pad_char = cfg.invalid_utf8_names ? '\xff' : 'x';

	std::vector<std::string> leaves;
	enumerate_leaves(
		leaves, std::string(), 0, cfg.dir_depth, cfg.branching, cfg.dir_name_len, pad_char);

	int const num_leaves = int(leaves.size());
	int const base = cfg.num_files / num_leaves;
	int const remainder = cfg.num_files % num_leaves;
	std::cout << "directory leaves: " << num_leaves << ", files per leaf: " << base
			  << " (+1 in the first " << remainder << " leaves)\n";

	std::vector<lt::create_file_entry> files;
	files.reserve(std::size_t(cfg.num_files + cfg.num_duplicates));

	// build the regular files first. The first file (file 0) is always a
	// regular file; it is used as the symlink target for "file" mode and the
	// chain anchor for "chain" mode. Files in the tail of the list become
	// symlinks if --num-symlinks > 0.
	int const num_regular = cfg.num_files - cfg.num_symlinks;

	// Each emitted file's path, indexed by file_idx. Needed for "chain" mode
	// (target = paths[i-1]) and for "dir" mode (target = parent of paths[0]).
	std::vector<std::string> paths;
	paths.reserve(std::size_t(cfg.num_files));

	int file_idx = 0;
	for (int li = 0; li < num_leaves && file_idx < cfg.num_files; ++li)
	{
		int const count_in_leaf = base + (li < remainder ? 1 : 0);
		for (int k = 0; k < count_in_leaf && file_idx < cfg.num_files; ++k, ++file_idx)
		{
			std::string p = cfg.name;
			if (!leaves[std::size_t(li)].empty())
			{
				p += '/';
				p += leaves[std::size_t(li)];
			}
			p += '/';
			p += file_name(file_idx, cfg.file_name_len, pad_char, cfg.file_name_suffix);

			paths.push_back(p);

			if (file_idx < num_regular)
			{
				files.push_back({std::move(p), cfg.file_size, {}, 0, {}});
			}
			else
			{
				// symlink: size is zero (the file's data is not stored in
				// the torrent), the flag_symlink bit is set, and the target
				// path must reference another file or directory already in
				// the torrent.
				std::string target;
				if (cfg.symlink_mode == "chain")
				{
					// point at the file with the previous index. For the
					// first symlink that is the last regular file; for
					// subsequent ones it is another symlink, so resolution
					// goes through sanitize_symlinks()'s second pass.
					target = paths[std::size_t(file_idx - 1)];
				}
				else if (cfg.symlink_mode == "dir")
				{
					// target the directory containing file 0. With
					// dir_depth=0 this collapses to the torrent root.
					std::string const& first = paths.front();
					auto const slash = first.find_last_of('/');
					target = slash == std::string::npos ? std::string() : first.substr(0, slash);
				}
				else
				{
					target = paths.front(); // file 0
				}
				files.push_back(
					{std::move(p), 0, lt::file_storage::flag_symlink, 0, std::move(target)});
			}
		}
	}

	// duplicate-filename stress: emit num_duplicates extra entries whose
	// filenames are case variants of a single base. They are distinct
	// strings (so a v2 file tree dict can hold them all) but compare equal
	// to libtorrent's case-insensitive resolve_duplicate_filenames(), so
	// the rename path fires for every variant past the first.
	for (int d = 0; d < cfg.num_duplicates; ++d)
	{
		std::string p = cfg.name;
		p += '/';
		p += dup_filename(d);
		files.push_back({std::move(p), cfg.file_size, {}, 0, {}});
	}

	lt::create_flags_t flags{};
	if (cfg.version == torrent_version::v1)
		flags = lt::create_torrent::v1_only;
	else if (cfg.version == torrent_version::v2)
		flags = lt::create_torrent::v2_only;
	// hybrid: no version flag

	lt::create_torrent t(std::move(files), cfg.piece_size, flags);

	std::cout << "pieces: " << t.num_pieces() << "\n"
			  << "files (after canonicalization): " << static_cast<int>(t.end_file()) << "\n"
			  << "total size: " << t.total_size() << " bytes\n";

	bool const do_v1 = (cfg.version != torrent_version::v2);
	bool const do_v2 = (cfg.version != torrent_version::v1);

	if (do_v1)
	{
		// v1 piece_range covers every piece in the canonicalized layout,
		// including any pad-file pieces (in hybrid mode); they all need
		// hashes set.
		for (auto const p : t.piece_range())
			t.set_hash(p, make_fake_sha1(std::uint32_t(static_cast<int>(p))));
	}

	if (do_v2)
	{
		// v2 per-piece merkle roots are stored per (file, piece-in-file).
		// Skip pad files and zero-size files; they have no v2 hashes.
		for (auto const fi : t.file_range())
		{
			auto const& fe = t.file_at(fi);
			if (fe.flags & lt::file_storage::flag_pad_file) continue;
			if (fe.size == 0) continue;
			for (auto const pif : t.file_piece_range(fi))
			{
				t.set_hash2(fi,
					pif,
					make_fake_sha256(
						std::uint32_t(static_cast<int>(fi)), std::uint32_t(static_cast<int>(pif))));
			}
		}
	}

	for (auto const& url : cfg.trackers)
		t.add_tracker(url);

	// synthesize additional trackers. Use distinct hostnames so each entry
	// is a unique URL and survives any de-duplication in add_tracker.
	// Distribute across cfg.num_tiers tiers round-robin; with num_tiers==1
	// (the default) everything ends up in tier 0 as before.
	for (int i = 0; i < cfg.num_trackers; ++i)
	{
		char buf[80];
		std::snprintf(buf, sizeof(buf), "http://tracker%d.torture.example/announce", i);
		t.add_tracker(buf, i % cfg.num_tiers);
	}

	// synthesize url-list (BEP 19) entries.
	for (int i = 0; i < cfg.num_url_seeds; ++i)
	{
		char buf[80];
		std::snprintf(buf, sizeof(buf), "http://seed%d.torture.example/", i);
		t.add_url_seed(buf);
	}

	// synthesize DHT bootstrap nodes (per BEP 5 they're (host, port) pairs).
	for (int i = 0; i < cfg.num_dht_nodes; ++i)
	{
		char host[64];
		std::snprintf(host, sizeof(host), "node%d.torture.example", i);
		t.add_node({std::string(host), 6881 + (i % 1024)});
	}

	// synthesize similar-torrent hashes (BEP 38). Use the deterministic fake
	// sha1 helper so each hash is distinct (and not all zeros, which is
	// rejected by the loader).
	for (int i = 0; i < cfg.num_similar; ++i)
		t.add_similar_torrent(make_fake_sha1(std::uint32_t(i) + 0x80000000u));

	// synthesize collection strings (BEP 38). Distinct names, otherwise
	// add_collection() dedup would collapse them.
	for (int i = 0; i < cfg.num_collections; ++i)
	{
		char buf[64];
		std::snprintf(buf, sizeof(buf), "torture-collection-%d", i);
		t.add_collection(buf);
	}

	// synthesize a 'comment' / 'created by' field. We use ASCII-printable
	// padding so aux::verify_encoding() takes the fast path; the cost being
	// measured is the per-byte UTF-8 validation itself, not error recovery.
	if (cfg.comment_len > 0)
	{
		std::string c(std::size_t(cfg.comment_len), 'c');
		t.set_comment(c.c_str());
	}
	if (cfg.created_by_len > 0)
	{
		std::string c(std::size_t(cfg.created_by_len), 'b');
		t.set_creator(c.c_str());
	}

	auto const buf = t.generate_buf();

	std::ofstream out(cfg.output, std::ios::binary);
	if (!out)
	{
		std::cerr << "could not open output: " << cfg.output << "\n";
		return 1;
	}
	out.write(buf.data(), std::streamsize(buf.size()));
	if (!out)
	{
		std::cerr << "write to output failed: " << cfg.output << "\n";
		return 1;
	}

	std::cout << "wrote " << buf.size() << " bytes to " << cfg.output << "\n";
	return 0;
}
catch (std::exception const& e)
{
	std::cerr << "error: " << e.what() << "\n";
	return 1;
}
