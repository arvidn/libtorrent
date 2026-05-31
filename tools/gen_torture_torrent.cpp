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
#include <cstdio>
#include <cstdlib>
#include <cstdint>
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
		// the last 'num_symlinks' files are emitted with the symlink flag
		// instead of as regular files; they all point at file 0.
		int num_symlinks = 0;
		// number of additional file entries whose paths differ only in
		// capitalization, on top of num_files. libtorrent's
		// resolve_duplicate_filenames() compares paths case-insensitively,
		// so they all collide; works for v1, v2 and hybrid.
		int num_duplicates = 0;
		// number of synthetic tracker URLs to add (in addition to anything
		// passed via --tracker).
		int num_trackers = 0;
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
  --num-symlinks N     mark the last N files as symlinks pointing at
                       file 0. Must be less than --num-files since
                       file 0 has to be a regular file (default 0)
  --num-duplicates N   add N extra file entries whose names differ only
                       in capitalization, to exercise duplicate-
                       filename resolution (which compares case-
                       insensitively). Works for v1, v2 and hybrid
                       (default 0)
  --num-trackers N     add N synthetic tracker URLs (default 0)
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

	// Pad 'base' with 'x' characters up to 'target_len'. If 'base' is already
	// at least that long, leave it as-is.
	std::string pad(std::string base, int const target_len)
	{
		if (int(base.size()) < target_len) base.resize(std::size_t(target_len), 'x');
		return base;
	}

	std::string dir_name(int const level, int const idx, int const len)
	{
		char buf[64];
		std::snprintf(buf, sizeof(buf), "L%d_%d_", level, idx);
		return pad(buf, len);
	}

	std::string file_name(int const idx, int const len)
	{
		char buf[64];
		std::snprintf(buf, sizeof(buf), "f_%d_", idx);
		return pad(buf, len);
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
		int const dir_name_len)
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
			sub += dir_name(level, i, dir_name_len);
			enumerate_leaves(out, std::move(sub), level + 1, depth, branching, dir_name_len);
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
		else if (a == "--num-duplicates")
			cfg.num_duplicates = std::atoi(require_arg("--num-duplicates"));
		else if (a == "--num-trackers")
			cfg.num_trackers = std::atoi(require_arg("--num-trackers"));
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
	if (cfg.num_symlinks < 0 || cfg.num_duplicates < 0 || cfg.num_trackers < 0)
	{
		std::cerr << "--num-symlinks, --num-duplicates and --num-trackers must be >= 0\n";
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
	if (cfg.output.empty()) cfg.output = cfg.name + ".torrent";

	std::vector<std::string> leaves;
	enumerate_leaves(leaves, std::string(), 0, cfg.dir_depth, cfg.branching, cfg.dir_name_len);

	int const num_leaves = int(leaves.size());
	int const base = cfg.num_files / num_leaves;
	int const remainder = cfg.num_files % num_leaves;
	std::cout << "directory leaves: " << num_leaves << ", files per leaf: " << base
			  << " (+1 in the first " << remainder << " leaves)\n";

	std::vector<lt::create_file_entry> files;
	files.reserve(std::size_t(cfg.num_files + cfg.num_duplicates));

	// build the regular files first. The first file (file 0) is always a
	// regular file; it is used as the target for any generated symlinks.
	// Files in the tail of the list become symlinks if --num-symlinks > 0.
	int const num_regular = cfg.num_files - cfg.num_symlinks;
	std::string symlink_target; // path of file 0, populated below

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
			p += file_name(file_idx, cfg.file_name_len);

			if (file_idx < num_regular)
			{
				if (file_idx == 0) symlink_target = p;
				files.push_back({std::move(p), cfg.file_size, {}, 0, {}});
			}
			else
			{
				// symlink: size is zero (the file's data is not stored in
				// the torrent), the flag_symlink bit is set, and the target
				// path must reference another file already in the torrent.
				files.push_back(
					{std::move(p), 0, lt::file_storage::flag_symlink, 0, symlink_target});
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
	for (int i = 0; i < cfg.num_trackers; ++i)
	{
		char buf[80];
		std::snprintf(buf, sizeof(buf), "http://tracker%d.torture.example/announce", i);
		t.add_tracker(buf);
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
