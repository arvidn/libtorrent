/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

// Fuzzes aux::resolve_duplicate_filenames() and checks the invariants it
// exists to uphold: every file's finalized name (renamed or not) must be
// unique on disk, case-insensitively, and must not equal any directory
// path implied by the file tree (directories are never renamed). Pad
// files are excluded from the file-vs-file check since same-size pad
// files are intentionally allowed to alias.
//
// Input decodes into a small number of files from a tiny shared
// name/extension alphabet, rather than raw path bytes. Extensions shaped
// like the ".N" suffix resolve_duplicate_filenames() itself generates let
// small mutations reach an exact collision, including a later file
// colliding with an already-renamed one, instead of relying on chance.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <unordered_set>

#include "libtorrent/aux_/resolve_duplicate_filenames.hpp"
#include "libtorrent/aux_/string_util.hpp" // for to_lower
#include "libtorrent/error_code.hpp"
#include "libtorrent/file_storage.hpp"

using namespace lt;

namespace {

std::array<char const*, 5> const names = {"a", "b", "A.b", "temp", "TMP"};
std::array<char const*, 7> const exts = {"", ".txt", ".Txt", ".1", ".1.txt", ".2", ".2.Txt"};
int const num_names = int(names.size());
int const num_exts = int(exts.size());

} // anonymous namespace

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, size_t size)
{
	file_storage fs;
	std::unordered_set<std::string> directories;
	std::size_t pos = 0;
	while (pos < size && fs.num_files() < 64)
	{
		std::uint8_t const ctrl = data[pos++];
		bool const pad = (ctrl & 0x01) != 0;
		int const depth = (ctrl >> 2) & 0x3;
		if (pos + std::size_t(depth) + 2 > size)
			break;

		// every prefix of the directory portion of the path (up to each
		// '/') is a directory implied by the file tree, mirroring the
		// prefixes resolve_duplicate_filenames_slow() hashes from fs.paths().
		std::string path = "test";
		directories.insert(path);
		for (int d = 0; d < depth; ++d)
		{
			path += '/';
			path += names[data[pos++] % num_names];
			std::string dir = path;
			std::transform(dir.begin(), dir.end(), dir.begin(), &aux::to_lower);
			directories.insert(std::move(dir));
		}
		path += '/';
		path += names[data[pos++] % num_names];
		path += exts[data[pos++] % num_exts];

		std::int64_t const file_size = pad ? ((ctrl & 0x02) ? 0x2000 : 0x1000) : 0x4000;
		file_flags_t const flags = pad ? file_storage::flag_pad_file : file_flags_t{};

		error_code ec;
		fs.add_file(ec, path, file_size, flags);
	}

	error_code ec;
	std::map<file_index_t, std::string> const renamed_map =
		aux::resolve_duplicate_filenames(fs, 500, ec);
	if (ec)
		return 0;

	renamed_files rf;
	rf.import_filenames(fs, renamed_map);
	filenames const names_view(fs, rf);

	std::unordered_set<std::string> resolved;
	for (auto const i : names_view.file_range())
	{
		if (names_view.file_flags(i) & file_storage::flag_pad_file)
			continue;

		std::string name = names_view.file_path(i);
		std::transform(name.begin(), name.end(), name.begin(), &aux::to_lower);
		if (!resolved.insert(name).second)
			std::abort();
		if (directories.count(name))
			std::abort();
	}

	return 0;
}
