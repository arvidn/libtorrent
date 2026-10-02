/*

Copyright (c) 2003-2011, 2013-2022, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <map>
#include <algorithm>
#include <unordered_set>

#include "libtorrent/string_view.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/torrent_info.hpp" // for load_torrent_limits
#include "libtorrent/aux_/string_util.hpp"
#include "libtorrent/aux_/resolve_duplicate_filenames.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/aux_/crc32c.hpp"

// file_storage::paths(), all_path_hashes() and file_path_hash() are
// deprecated as public API but are still called internally while
// resolving duplicate filenames.
#include "libtorrent/aux_/disable_deprecation_warnings_push.hpp"

namespace libtorrent::aux {

namespace {

	struct name_entry
	{
		// file_index_t{-1} means this entry is a directory, identified by
		// `dir` rather than a real file index
		file_index_t idx;
		path_index_t dir;
	};

	bool is_case_sensitive(load_torrent_limits const& cfg)
	{
		return !(cfg.sanitize_flags & path_sanitize_flags::case_insensitive_deduplication);
	}

	int compare_names(string_view const lhs, string_view const rhs, bool const case_sensitive)
	{
		return case_sensitive ? lhs.compare(rhs) : string_compare_no_case(lhs, rhs);
	}
	}

	std::map<file_index_t, std::string> resolve_duplicate_filenames_slow(file_storage const& fs,
		aux::vector<std::uint32_t, path_index_t> const& eh,
		int const max_duplicate_filenames,
		bool const case_sensitive,
		error_code& ec)
	{
		auto const names_equal = [case_sensitive](string_view const lhs, string_view const rhs) {
			return case_sensitive ? lhs == rhs : aux::string_equal_no_case(lhs, rhs);
		};

		// maps filename hash to either a file index or (if idx is negative) a
		// directory, to make sure no files are allowed to collide with them
		std::unordered_multimap<std::uint32_t, name_entry> files;

		std::map<file_index_t, std::string> ret;

		// two different directory path_elements can legitimately hash the
		// same here, so inserting them all into a multimap without
		// checking for an existing entry is correct, not just convenient.
		// The parser's dedup cache (cached_directory() in
		// torrent_info.cpp) keys a directory by its raw, unsanitized text,
		// while this hash is computed from the sanitized (and, unless
		// case-sensitive, lowercased) text, so two raw strings that only
		// differ in case, or that sanitize
		// to the same result (e.g. two different characters the sanitizer
		// both replace with '_'), get distinct path_elements with an
		// identical hash. That's harmless: two directories folding
		// together on disk loses nothing, unlike a file colliding with
		// anything, so it's never treated as a real collision below,
		// only a *file* landing on an already-seen hash is.
		files.reserve(eh.size() + aux::numeric_cast<std::size_t>(fs.num_files()));

		// computed as its own pass here, rather than folded into
		// compute_element_hashes(), since this is the only caller, reached
		// only once has_duplicate_filenames() has already confirmed a real
		// collision; the common, collision-free case never pays for it
		aux::vector<bool, path_index_t> const is_dir = fs.compute_is_dir();

		// eh and is_dir must come from the same file_storage state: eh is
		// indexed below by is_dir's range, so a caller-supplied eh
		// computed for a different fs (or a stale one) would be an
		// out-of-bounds access rather than a clean failure
		TORRENT_ASSERT_PRECOND(eh.size() == is_dir.size());

		for (auto const idx : is_dir.range())
			if (is_dir[idx])
				files.insert({eh[idx], {file_index_t{-1}, idx}});

		// keep track of the total number of name collisions. If there are too
		// many, it's probably a malicious torrent and we should just fail
		int num_collisions = 0;

		// bounds the total cost of scanning a hash bucket for a real match,
		// whether or not one is found. num_collisions above only counts
		// confirmed duplicates' failed rename attempts, so a torrent with
		// many distinct (non-colliding) names steered onto the same crc,
		// crc32 is linear, so this is cheap to construct deliberately,
		// could otherwise force O(n^2) scanning here without ever tripping
		// that counter. Scaled to the torrent's own non-pad file count so
		// it never bites a large, legitimate torrent: a benign hash keeps
		// buckets effectively O(1), so this only engages when something is
		// actually piling up. Pad files are excluded since they're skipped
		// below and never contribute to bucket_scan_cost, so counting them
		// here would let them inflate the budget without ever spending it.
		std::int64_t num_pad_files = 0;
		for (auto const i : fs.file_range())
			if (fs.pad_file_at(i))
				++num_pad_files;
		std::int64_t const max_bucket_scan = (std::int64_t(fs.num_files()) - num_pad_files) * 16;
		std::int64_t bucket_scan_cost = 0;

		for (auto const i : fs.file_range())
		{
			// pad files never touch disk, so a naming collision involving
			// one is never a real conflict: pad files are allowed to
			// collide with each other, and with a real file (or the
			// directory it implies) without the real file being renamed.
			// Skipping them here means they're simply never inserted into
			// `files` either, so nothing else ever needs to check for them.
			if (fs.pad_file_at(i))
				continue;

			// as long as this file already exists
			// increase the counter
			std::uint32_t const hash = fs.file_hash(eh, i);
			auto range = files.equal_range(hash);
			// the hash bucket is empty for the vast majority of files (no
			// other file or directory hashes to this value), so don't
			// reconstruct this file's own path unless there's actually
			// something to compare it against
			if (range.first == range.second)
			{
				files.insert({hash, {i, path_index_t{}}});
				continue;
			}

			bucket_scan_cost += std::distance(range.first, range.second);
			if (bucket_scan_cost > max_bucket_scan)
			{
				ec = errors::too_many_duplicate_filenames;
				return {};
			}

			std::string const this_name = fs.file_path(i);
			auto const match = std::find_if(
				range.first, range.second, [&](std::pair<std::uint32_t, name_entry> const& o) {
					// fs.file_path(idx) only gives a file's original name.
					// a file already renamed must be matched against the
					// name recorded in ret, or a later file with that
					// literal name would go undetected and collide with it.
					// directories are never renamed, so the idx < 0 branch
					// needs no such check.
					if (o.second.idx < file_index_t{})
						return names_equal(fs.internal_directory_path(o.second.dir), this_name);
					auto const renamed = ret.find(o.second.idx);
					return renamed != ret.end()
						? names_equal(renamed->second, this_name)
						: names_equal(fs.file_path(o.second.idx), this_name);
				});

			if (match == range.second)
			{
				files.insert({hash, {i, path_index_t{}}});
				continue;
			}

			std::string filename = this_name;
			std::string base = remove_extension(filename);
			std::string ext = extension(filename);
			int cnt = 0;
			for (;;)
			{
				++cnt;
				char new_ext[50];
				std::snprintf(new_ext, sizeof(new_ext), ".%d%s", cnt, ext.c_str());
				filename = base + new_ext;

				std::uint32_t const new_hash = aux::crc32c_finish(case_sensitive
						? aux::crc32c_mix(aux::crc32c_init, filename)
						: aux::crc32c_mix_lowercase(aux::crc32c_init, filename));
				if (files.find(new_hash) == files.end())
				{
					files.insert({new_hash, {i, path_index_t{}}});
					break;
				}
				++num_collisions;
				if (num_collisions > max_duplicate_filenames)
				{
					ec = errors::too_many_duplicate_filenames;
					return {};
				}
			}
			ret.insert({i, filename});
		}
		return ret;
	}

	std::map<file_index_t, std::string> resolve_duplicate_filenames(
		file_storage const& fs, load_torrent_limits const& cfg, error_code& ec)
	{
		// has_duplicate_filenames() always hashes the whole path tree, even
		// once it's found a collision, since resolve_duplicate_filenames_slow()
		// below needs every file's and directory's hash to find and rename
		// all conflicts, not just the first one.
		bool const cs = is_case_sensitive(cfg);
		auto eh = fs.has_duplicate_filenames(cs);
		if (!eh)
			return {};
		return resolve_duplicate_filenames_slow(fs, *eh, cfg.max_duplicate_filenames, cs, ec);
	}

	namespace {

	// finds the first "base-N[ext]" rejected by neither taken() nor the
	// shared collision budget, starting the search at cnt + 1 and
	// leaving cnt at whichever N was accepted, so a later call (another
	// member of the same colliding group) resumes the count instead of
	// restarting it. std::nullopt, with ec set, once num_collisions
	// exceeds cfg.max_duplicate_filenames.
	template <typename Taken>
	std::optional<std::string> next_dedup_candidate(string_view const base,
		string_view const ext,
		std::uint32_t& cnt,
		int& num_collisions,
		load_torrent_limits const& cfg,
		Taken&& taken,
		error_code& ec)
	{
		std::string candidate;
		for (;;)
		{
			++cnt;
			// reuses candidate's existing buffer across iterations,
			// rather than materializing a fresh concatenated string
			// each time
			candidate.assign(base);
			candidate += '-';
			candidate += std::to_string(cnt);
			candidate.append(ext);
			if (!taken(string_view(candidate)))
				break;
			if (++num_collisions > cfg.max_duplicate_filenames)
			{
				ec = errors::too_many_duplicate_filenames;
				return std::nullopt;
			}
		}
		return candidate;
	}

	// true if any two elements (directory or file/symlink leaf) share a
	// hash. Lets resolve_directory_duplicates() skip building and sorting
	// its combined entry list when there's nothing to resolve, at no
	// extra cost: it's the same scan either way.
	bool has_collision(aux::vector<std::uint32_t, path_index_t> const& eh)
	{
		std::unordered_set<std::uint32_t> seen;
		seen.reserve(eh.size());
		for (auto const c : eh)
			if (!seen.insert(c).second)
				return true;
		return false;
	}

	// a single directory or file/symlink leaf: its parent, its own
	// path_index_t, and name.
	struct combined_entry
	{
		path_index_t parent;
		path_index_t self;
		string_view name;
	};

	// compares a combined_entry against a bare path_index_t in either
	// argument order, so std::equal_range can binary-search "entries"
	// (sorted primarily by parent) for the sub-range belonging to one
	// parent
	struct parent_less
	{
		bool operator()(combined_entry const& e, path_index_t const p) const
		{
			return e.parent < p;
		}
		bool operator()(path_index_t const p, combined_entry const& e) const
		{
			return p < e.parent;
		}
	};
	}

	std::map<path_index_t, std::string> resolve_directory_duplicates(
		file_storage const& fs, load_torrent_limits const& cfg, error_code& ec)
	{
		std::map<path_index_t, std::string> ret;

		bool const cs = is_case_sensitive(cfg);
		aux::vector<std::uint32_t, path_index_t> const eh = fs.compute_element_hashes(cs);
		if (!has_collision(eh))
			return ret;

		aux::vector<string_view, path_index_t> const names = fs.all_path_element_names();

		std::vector<combined_entry> entries;
		entries.reserve(static_cast<std::size_t>(eh.size()));
		// the only path_index_t that isn't a real directory or file/symlink
		// leaf (a symlink target's own opaque path_element) can't reach
		// this function: it only exists via the deprecated
		// add_symlink(target) construction path, never a parsed .torrent.
		for (path_index_t const idx : eh.range())
			entries.push_back({fs.parent_of(idx), idx, names[idx]});

		// self is the tiebreak: a path_index_t says nothing about two
		// siblings' relative creation order, so sorting by it too is what
		// makes group_begin, below, deterministically the earliest-created
		// member of a colliding group, deciding which one keeps its name.
		std::sort(
			entries.begin(), entries.end(), [cs](combined_entry const& a, combined_entry const& b) {
				if (a.parent != b.parent)
					return a.parent < b.parent;
				int const c = compare_names(a.name, b.name, cs);
				if (c != 0)
					return c < 0;
				return a.self < b.self;
			});

		// shared across every colliding group in this call, matching
		// next_dedup_candidate()'s contract: too many failed rename
		// attempts overall fails the whole thing, not just one group
		int num_collisions = 0;

		std::size_t i = 0;
		while (i < entries.size())
		{
			std::size_t const group_begin = i;
			path_index_t const parent = entries[i].parent;
			string_view const group_name = entries[i].name;
			while (i < entries.size() && entries[i].parent == parent
				&& compare_names(entries[i].name, group_name, cs) == 0)
				++i;
			std::size_t const group_end = i;

			if (group_end - group_begin == 1)
				continue;

			auto const parent_range =
				std::equal_range(entries.begin(), entries.end(), parent, parent_less{});

			// guards against a fresh candidate clobbering a real, separate
			// collision: an entry's literal, not-yet-renamed name
			auto const already_exists = [&](string_view const candidate) {
				auto const it = std::lower_bound(parent_range.first,
					parent_range.second,
					candidate,
					[cs](combined_entry const& e, string_view const cand) {
						return compare_names(e.name, cand, cs) < 0;
					});
				return it != parent_range.second && compare_names(it->name, candidate, cs) == 0;
			};

			// group_begin (the earliest-created member) keeps its name;
			// every other member needs one, sharing a single counter so
			// a second, third, etc. collision on this same original name
			// resumes probing where the previous one left off
			std::uint32_t cnt = 0;
			for (std::size_t k = group_begin + 1; k < group_end; ++k)
			{
				// split on this member's own name, not the group's, so a
				// renamed candidate keeps its own case rather than
				// whichever member's case the group happened to be keyed
				// on
				auto const [base, ext] = split_base_ext(entries[k].name);
				auto candidate =
					next_dedup_candidate(base, ext, cnt, num_collisions, cfg, already_exists, ec);
				if (!candidate)
					return {};
				ret.emplace(entries[k].self, *std::move(candidate));
			}
		}
		return ret;
	}
}
