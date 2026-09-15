/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

// Fuzzes aux::resolve_duplicate_filenames() (the whole-tree pass) and
// aux::resolve_directory_duplicates() (the deduplicate_per_directory
// pass) against the same generated file tree, checking each upholds its
// own uniqueness invariant. Pad files are excluded, since same-size pad
// files are intentionally allowed to alias.
//
// Input decodes into a small number of files from a tiny shared
// name/extension alphabet, rather than raw path bytes. Extensions shaped
// like the ".N" and "-N" suffixes the two passes themselves generate let
// small mutations reach an exact collision, including a later file
// colliding with an already-renamed one, instead of relying on chance.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>

#include "libtorrent/aux_/resolve_duplicate_filenames.hpp"
#include "libtorrent/aux_/string_util.hpp" // for to_lower
#include "libtorrent/error_code.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/torrent_info.hpp" // for load_torrent_limits

using namespace lt;

namespace {

// the "-1"/"-2"/"temp-1"/"TMP-1" entries are shaped like the "-N" suffix
// resolve_directory_duplicates() generates (base + "-" + cnt + ext, see
// next_dedup_candidate()), so a small mutation can land a literal name on
// an already-generated candidate instead of relying on chance, mirroring
// what the ".1"/".2" extensions below do for resolve_duplicate_filenames().
std::array<char const*, 9> const names = {
	"a", "b", "A.b", "temp", "TMP", "a-1", "b-1", "temp-1", "TMP-1"};
std::array<char const*, 7> const exts = {"", ".txt", ".Txt", ".1", ".1.txt", ".2", ".2.Txt"};
int const num_names = int(names.size());
int const num_exts = int(exts.size());

} // anonymous namespace

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, size_t size)
{
	file_storage fs;
	fs.set_name("test");
	// (parent, raw component) -> path_index_t, so files sharing a directory
	// reuse the same path_element instead of fs.make_directory() (which
	// never dedupes on its own) allocating a fresh one per file, mirroring
	// cached_directory() in torrent_info.cpp.
	std::map<std::pair<std::uint32_t, std::string>, path_index_t> dir_cache;
	std::size_t pos = 0;
	while (pos < size && fs.num_files() < 64)
	{
		std::uint8_t const ctrl = data[pos++];
		bool const pad = (ctrl & 0x01) != 0;
		int const depth = (ctrl >> 2) & 0x3;
		if (pos + std::size_t(depth) + 2 > size)
			break;

		path_index_t dir = aux::path_element::torrent_root;
		for (int d = 0; d < depth; ++d)
		{
			string_view const component = names[data[pos++] % num_names];
			auto const key =
				std::make_pair(static_cast<std::uint32_t>(dir), std::string(component));
			auto const it = dir_cache.find(key);
			if (it != dir_cache.end())
			{
				dir = it->second;
			}
			else
			{
				dir = fs.make_directory(dir, component);
				dir_cache.emplace(key, dir);
			}
		}
		string_view const leaf_name = names[data[pos++] % num_names];
		string_view const ext = exts[data[pos++] % num_exts];
		std::string const leaf = std::string(leaf_name) + std::string(ext);

		std::int64_t const file_size = pad ? ((ctrl & 0x02) ? 0x2000 : 0x1000) : 0x4000;
		file_flags_t const flags = pad ? file_storage::flag_pad_file : file_flags_t{};

		error_code ec;
		fs.add_file(ec, leaf, false, dir, file_size, flags);
	}

	error_code ec;
	std::map<file_index_t, std::string> const renamed_map =
		aux::resolve_duplicate_filenames(fs, 500, ec);
	if (ec)
		return 0;

	renamed_files rf;
	rf.import_filenames(fs, renamed_map);
	filenames const names_view(fs, rf);

	// the set of directory paths that actually manifest on disk, mirroring
	// compute_is_dir()'s own bit: a path_element referenced only by pad
	// files (or by nothing at all) never gets is_dir set, since pad files
	// never touch disk and so neither does a directory that only ever
	// holds them, see compute_element_hashes()'s doc comment. Deriving
	// this from fs directly, rather than tracking prefixes by hand while
	// generating the tree above, keeps the invariant in sync with whatever
	// resolve_duplicate_filenames() itself considers a real collision.
	aux::vector<std::uint32_t, path_index_t> const eh = fs.compute_element_hashes();
	aux::vector<bool, path_index_t> const is_dir = fs.compute_is_dir();
	std::unordered_set<std::string> directories;
	for (auto const idx : is_dir.range())
	{
		if (!is_dir[idx])
			continue;
		std::string dir_path = fs.internal_directory_path(idx);
		std::transform(dir_path.begin(), dir_path.end(), dir_path.begin(), &aux::to_lower);
		directories.insert(std::move(dir_path));
	}

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

	// deduplicate_per_directory's namespace is per-parent rather than
	// whole-tree: the same name may legitimately appear in two unrelated
	// directories, so the invariant here is scoped to siblings (elements
	// sharing a parent) instead of full resolved paths. Both files and
	// directories are renamed by this pass, so both are checked, keyed
	// by (parent, resolved lower-case name), reusing eh and fs, which
	// neither pass mutates.
	load_torrent_limits const cfg;
	error_code ec2;
	std::map<path_index_t, std::string> const dir_renamed_map =
		aux::resolve_directory_duplicates(fs, cfg, ec2);
	if (ec2)
		return 0;

	aux::vector<string_view, path_index_t> const orig_names = fs.all_path_element_names();
	auto const resolved_name = [&](path_index_t const idx) {
		auto const it = dir_renamed_map.find(idx);
		std::string name = it != dir_renamed_map.end() ? it->second : std::string(orig_names[idx]);
		std::transform(name.begin(), name.end(), name.begin(), &aux::to_lower);
		return name;
	};

	// every path_index_t here is either a real directory or a file's own
	// leaf (this fuzzer never adds a symlink, so there's no third kind of
	// path_element to worry about), so a single unfiltered walk covers
	// both without a second pass over fs.file_range().
	std::set<std::pair<std::uint32_t, std::string>> siblings;
	for (auto const idx : eh.range())
	{
		auto const key =
			std::make_pair(static_cast<std::uint32_t>(fs.parent_of(idx)), resolved_name(idx));
		if (!siblings.insert(key).second)
			std::abort();
	}

	return 0;
}
