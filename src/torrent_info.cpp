/*

Copyright (c) 2003-2022, Arvid Norberg
Copyright (c) 2016-2018, 2020-2021, Alden Torres
Copyright (c) 2016, 2019, Andrei Kurushin
Copyright (c) 2016-2017, Pavel Pimenov
Copyright (c) 2016-2019, Steven Siloti
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/config.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/aux_/string_util.hpp" // is_space, is_i2p_url
#include "libtorrent/bencode.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/aux_/open_mode.hpp"
#include "libtorrent/aux_/utf8.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/aux_/random.hpp"
#include "libtorrent/aux_/invariant_check.hpp"
#include "libtorrent/aux_/escape_string.hpp" // maybe_url_encode
#include "libtorrent/aux_/throw.hpp"
#include "libtorrent/magnet_uri.hpp"
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/hex.hpp" // to_hex
#include "libtorrent/aux_/numeric_cast.hpp"
#include "libtorrent/aux_/file_pointer.hpp"
#include "libtorrent/disk_interface.hpp" // for default_block_size
#include "libtorrent/span.hpp"
#include "libtorrent/aux_/parse_url.hpp" // for is_valid_tracker_url

#include "libtorrent/load_torrent.hpp" // for aux::parse_torrent_file()
#if TORRENT_ABI_VERSION < 4
#include "libtorrent/aux_/resolve_duplicate_filenames.hpp"
#endif

#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <algorithm>
#include <set>
#include <ctime>
#include <array>

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/functional/hash.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent {

#if TORRENT_ABI_VERSION < 4
	TORRENT_EXPORT from_span_t from_span;
#endif
	TORRENT_EXPORT from_info_section_t from_info_section;

	namespace {

	// Which characters are valid is primarily determined by the
	// filesystem, so this logic is an approximation. Note that forward- and
	// backslash are filtered unconditionally and separately from this function.
	bool valid_path_character(std::int32_t const c)
	{
#ifdef TORRENT_WINDOWS
		// On windows, both the filesystem and the operating system impose
		// restrictions.
		constexpr string_view invalid_chars = "?<>\"|\b*:"_sv;
#elif defined TORRENT_ANDROID
		// The Android kernel probably has similar restrictions as Linux (i.e.
		// very few) but it appears some user-space system libraries impose
		// additional restrictions, and it's probably more common to use FAT32
		// style filesystems, which also further restricts valid characters
		// https://cs.android.com/android/platform/superproject/+/master:frameworks/base/core/java/android/os/FileUtils.java;l=997?q=isValidFatFilenameChar
		constexpr string_view invalid_chars = "\"*:<>?|"_sv;
#else
		constexpr string_view invalid_chars = ""_sv;
#endif
		// C0 controls and DEL
		if (c < 32 || c == 0x7f) return false;
		// C1 controls
		if (c >= 0x80 && c <= 0x9f) return false;
		if (c > 127) return true;
		return std::find(invalid_chars.begin(), invalid_chars.end(), static_cast<char>(c))
			== invalid_chars.end();
	}

	bool filter_path_character(std::int32_t const c)
	{
		// Unicode formatting characters that are zero-width, invisible, or
		// change writing direction. These can be used to spoof filenames so
		// that they appear identical to legitimate names but resolve to a
		// different path:
		// https://security.stackexchange.com/questions/158802/how-can-this-executable-have-an-avi-extension
		// - U+061C: Arabic letter mark
		// - U+200B-200D: ZWSP, ZWNJ, ZWJ
		// - U+200E-200F: LRM, RLM
		// - U+202A-202E: LRE, RLE, PDF, LRO, RLO (bidi embedding/override)
		// - U+2060-2064: word joiner and invisible math operators
		// - U+2066-2069: LRI, RLI, FSI, PDI (bidi isolates, Unicode 6.3+)
		// - U+FEFF: byte order mark / zero-width no-break space
		if (c == 0x061c || (c >= 0x200b && c <= 0x200f) || (c >= 0x202a && c <= 0x202e)
			|| (c >= 0x2060 && c <= 0x2064) || (c >= 0x2066 && c <= 0x2069) || c == 0xfeff)
			return true;

		if (c > 127) return false;
		return c == '/' || c == '\\' || c == '\0';
	}

	} // anonymous namespace

namespace aux {

// fixes invalid UTF-8 sequences, returning a sanitized copy of "source".
// Since the common case is that "source" is already valid UTF-8, this
// avoids re-encoding every codepoint into a scratch buffer just to
// discard it; only once an invalid sequence is found do we start
// building the sanitized copy, seeded with the valid prefix copied
// verbatim from "source".
std::string sanitize_encoding(string_view const source)
{
	if (source.empty())
		return {};

	std::string ret;
	bool valid_encoding = true;

	string_view ptr = source;
	while (!ptr.empty())
	{
		string_view const start = ptr;

		// decode a single utf-8 character
		auto [codepoint, len] = parse_utf8_codepoint(ptr);

		// this was the last character, and nothing was
		// written to the destination buffer (i.e. the source character was
		// truncated)
		if (codepoint == -1)
		{
			if (valid_encoding)
			{
				// this is the first invalid sequence found. capture
				// everything decoded so far verbatim and continue
				// rebuilding from here
				ret.reserve(source.size() + 5);
				ret.assign(source.data(), std::size_t(start.data() - source.data()));
				valid_encoding = false;
			}
			codepoint = '_';
		}

		ptr = ptr.substr(std::min(std::size_t(len), ptr.size()));

		// encode codepoint into utf-8, unless everything decoded so far
		// has been valid, in which case "ret" is left untouched
		if (!valid_encoding)
			append_utf8_codepoint(ret, codepoint);
	}

	// the common case: the source was already valid utf-8, just copy it
	if (valid_encoding)
		return std::string(source);
	return ret;
}

	// sanitizes a single path element. The result can never be empty. Empty
	// files have special meaning in v2 torrents (it means the previous path
	// element was the filename). Also, if we're adding the torrent name as
	// the first path element, in a multi-file torrent, we must have a
	// directory name.
	//
	// returns true if "element" needed no sanitization at all, in which case
	// "path" is left untouched and the caller can use "element" itself
	// (borrowed, no copy). Returns false if "path" holds the sanitized
	// result instead (this is the only case where "path" is written to).
	// This lets the common case, an already-valid element, skip allocating
	// and copying into "path" entirely.
	bool sanitize_path_element(std::string& path, string_view element, bool const force_element)
	{
		if (element.size() == 1 && element[0] == '.' && !force_element)
			return false;

		if (element.empty())
		{
			path += "_";
			return false;
		}

#if !TORRENT_USE_UNC_PATHS && defined TORRENT_WINDOWS
#pragma message ("building for windows without UNC paths is deprecated")

		// if we're not using UNC paths on windows, there
		// are certain filenames we're not allowed to use
		static const char const* reserved_names[] =
		{
			"con", "prn", "aux", "clock$", "nul",
			"com0", "com1", "com2", "com3", "com4",
			"com5", "com6", "com7", "com8", "com9",
			"lpt0", "lpt1", "lpt2", "lpt3", "lpt4",
			"lpt5", "lpt6", "lpt7", "lpt8", "lpt9"
		};
		int num_names = sizeof(reserved_names)/sizeof(reserved_names[0]);

		// this is not very efficient, but it only affects some specific
		// windows builds for now anyway (not even the default windows build)
		std::string pe(element);
		char const* file_end = strrchr(pe.c_str(), '.');
		std::string name = file_end
			? std::string(pe.data(), file_end)
			: pe;
		std::transform(name.begin(), name.end(), name.begin(), &to_lower);
		char const** str = std::find(reserved_names, reserved_names + num_names, name);
		if (str != reserved_names + num_names)
		{
			pe = "_" + pe;
			element = string_view();
		}
#endif
#ifdef TORRENT_WINDOWS
		// this counts the number of unicode characters
		// we've added (which is different from the number
		// of bytes)
		int unicode_chars = 0;
#endif

		int added = 0;
		// the number of dots we've added
		char num_dots = 0;
		bool found_extension = false;

		// until "changed" becomes true, nothing has been written to "path"
		// (it stays empty), because every byte processed so far is a
		// byte-for-byte match of "element" itself. The moment something
		// needs to diverge (a character dropped, substituted, or the
		// element truncated) materialize() copies the bytes processed up to
		// that point into "path", and every subsequent byte is written
		// there too.
		bool changed = false;
		auto const materialize = [&](std::size_t const prefix_len) {
			if (changed)
				return;
			// the sanitized output can never exceed the input length (dropped
			// or substituted characters only shrink it), plus a byte or two
			// for a trailing "_" appended after the loop. reserving up front
			// avoids repeated reallocation as bytes are appended below.
			path.reserve(element.size() + 2);
			path.assign(element.data(), prefix_len);
			changed = true;
		};

		int seq_len = 0;
		for (std::size_t i = 0; i < element.size(); i += std::size_t(seq_len))
		{
			std::int32_t code_point;
			std::tie(code_point, seq_len) = parse_utf8_codepoint(element.substr(i));

			if (code_point >= 0 && filter_path_character(code_point))
			{
				materialize(i);
				continue;
			}

			if (code_point < 0 || !valid_path_character(code_point))
			{
				materialize(i);
				// invalid utf8 sequence, replace with "_"
				path += '_';
				++added;
#ifdef TORRENT_WINDOWS
				++unicode_chars;
#endif
				continue;
			}

			// validation passed, add it to the output string (if it's
			// already diverged from "element"; otherwise this byte is
			// already implicitly "copied" by leaving "path" untouched)
			if (changed)
			{
				path.append(element.data() + i, std::size_t(seq_len));
			}

			if (code_point == '.') ++num_dots;

			added += seq_len;
#ifdef TORRENT_WINDOWS
			++unicode_chars;
#endif

			// any given path element should not
			// be more than 255 characters
			// if we exceed 240, pick up any potential
			// file extension and add that too
#ifdef TORRENT_WINDOWS
			if (unicode_chars >= 240 && !found_extension)
#else
			if (added >= 240 && !found_extension)
#endif
			{
				int dot = -1;
				for (int j = int(element.size()) - 1;
					j > std::max(int(element.size()) - 10, int(i)); --j)
				{
					if (element[aux::numeric_cast<std::size_t>(j)] != '.') continue;
					dot = j;
					break;
				}
				// there is no extension
				if (dot == -1)
				{
					materialize(i + std::size_t(seq_len));
					break;
				}
				found_extension = true;
				TORRENT_ASSERT(dot > 0);
				// if the extension isn't immediately adjacent, the bytes
				// in between are dropped, which requires materializing
				if (std::size_t(dot) != i + std::size_t(seq_len))
					materialize(i + std::size_t(seq_len));
				i = std::size_t(dot - seq_len);
			}
		}

		if (added == num_dots && added <= 2)
		{
			if (changed)
			{
				// revert the invalid filename
				path.clear();
			}
			if (force_element)
			{
				// replace it with an underscore
				path += "_";
			}
			// the result ("_" or nothing) never equals the all-dots
			// "element" that got us here
			return false;
		}

#ifdef TORRENT_WINDOWS
		// remove trailing spaces and dots. These aren't allowed in
		// filenames on windows. Count how many need trimming from
		// whichever of "path" or "element" currently holds the content,
		// so an element with no trailing space/dot doesn't lose the
		// borrow optimization.
		{
			string_view current = changed ? string_view(path) : element;
			std::size_t const orig_size = current.size();
			while (!current.empty() && (current.back() == ' ' || current.back() == '.'))
				current.remove_suffix(1);
			int const trim = int(orig_size - current.size());
			if (trim > 0)
			{
				if (changed)
					path.resize(path.size() - std::size_t(trim));
				else
					materialize(element.size() - std::size_t(trim));
				added -= trim;
				TORRENT_ASSERT(added >= 0);
			}
		}

		if (force_element && added == 0)
		{
			path += "_";
		}
#endif

		if (changed && path.empty())
			path = "_";

		return !changed;
	}
}

namespace {

// n must already be resolved to the "attr" entry of a file dict (or a
// default-constructed node, if there was none)
file_flags_t get_file_attributes(bdecode_node const& attr)
{
	file_flags_t file_flags = {};
	if (attr.type() == bdecode_node::string_t)
	{
		for (char const c : attr.string_value())
		{
			switch (c)
			{
				case 'l':
					file_flags |= file_storage::flag_symlink;
					break;
				case 'x':
					file_flags |= file_storage::flag_executable;
					break;
				case 'h':
					file_flags |= file_storage::flag_hidden;
					break;
				case 'p':
					file_flags |= file_storage::flag_pad_file;
					break;
			}
		}
	}
	return file_flags;
}

// dict_find_*() re-scans the whole dictionary from the start on every
// call; a file dict (or the top-level info dict) is looked up for
// several distinct keys, so resolving all of them in one pass over
// dict_items() avoids paying for that scan once per key. Every field
// keeps the raw node (rather than the type-checked result dict_find_*()
// would give), so callers apply the same type check dict_find_*() would
// have via as_list()/as_dict()/as_string()/node_int_value() below.
std::int64_t node_int_value(bdecode_node const& n, std::int64_t const default_val)
{
	return (n.type() == bdecode_node::int_t) ? n.int_value() : default_val;
}

bdecode_node as_list(bdecode_node const& n)
{
	return (n.type() == bdecode_node::list_t) ? n : bdecode_node();
}

bdecode_node as_dict(bdecode_node const& n)
{
	return (n.type() == bdecode_node::dict_t) ? n : bdecode_node();
}

bdecode_node as_string(bdecode_node const& n)
{
	return (n.type() == bdecode_node::string_t) ? n : bdecode_node();
}

// every key looked up in a single file's dict, across get_file_attributes(),
// extract_single_file(), extract_single_file2() and parse_symlink_path()
struct file_dict_fields
{
	bdecode_node attr;
	bdecode_node length;
	bdecode_node mtime;
	bdecode_node name_utf8;
	bdecode_node name;
	bdecode_node path_utf8;
	bdecode_node path;
	bdecode_node symlink_path;
	bdecode_node pieces_root;
};

file_dict_fields scan_file_dict(bdecode_node const& dict)
{
	file_dict_fields f;
	for (auto const& [key, value] : dict.dict_items())
	{
		if (key == "attr" && !f.attr)
			f.attr = value;
		else if (key == "length" && !f.length)
			f.length = value;
		else if (key == "mtime" && !f.mtime)
			f.mtime = value;
		else if (key == "name.utf-8" && !f.name_utf8)
			f.name_utf8 = value;
		else if (key == "name" && !f.name)
			f.name = value;
		else if (key == "path.utf-8" && !f.path_utf8)
			f.path_utf8 = value;
		else if (key == "path" && !f.path)
			f.path = value;
		else if (key == "symlink path" && !f.symlink_path)
			f.symlink_path = value;
		else if (key == "pieces root" && !f.pieces_root)
			f.pieces_root = value;
	}
	return f;
}

	// parses the "symlink path" list into symlink_path, as a sequence of
	// raw (unsanitized) path components, dropping "." and ".." components
	// for backwards compatibility with non-conforming torrents. Clears
	// flag_symlink if "symlink path" is missing, or resolves to no
	// components at all (BEP 47 requires a non-empty list, but we tolerate
	// the torrent as a non-symlink instead of rejecting it outright).
	//
	// components are left fully unsanitized, matching how file/directory
	// raw components are extracted, so resolve_one() can match them
	// directly, byte for byte, against dir_cache without re-sanitizing.
bool parse_symlink_path(bdecode_node const& symlink_path_node,
	load_torrent_limits const& cfg,
	file_flags_t& file_flags,
	std::vector<string_view>& symlink_path,
	error_code& ec)
{
	if (bdecode_node const s_p = symlink_path_node)
	{
		int count = 0;
		for (bdecode_node const item : s_p.list_items())
		{
			if (item.type() != bdecode_node::string_t)
			{
				ec = errors::torrent_invalid_name;
				return false;
			}
			if (++count > cfg.max_directory_depth)
			{
				ec = errors::torrent_directory_too_deep;
				return false;
			}
			string_view const pe = item.string_value();
			// BEP 47 forbids "." and ".." in symlink path components.
			// We tolerate them for backwards compatibility with
			// non-conforming torrents by dropping them, leaving the
			// remaining components to be interpreted relative to the
			// torrent root.
			if (pe == "." || pe == "..")
				continue;
			symlink_path.push_back(pe);
		}
		if (symlink_path.empty())
		{
			// technically this is an invalid torrent: an empty
			// "symlink path" list, or one made up entirely of "." /
			// ".." components, leaves no real target
			file_flags &= ~file_storage::flag_symlink;
		}
	}
	else
	{
		// technically this is an invalid torrent. "symlink path" must exist
		file_flags &= ~file_storage::flag_symlink;
	}
	// symlink targets are validated later, as it may point to a file or
	// directory we haven't parsed yet
	return true;
}

	// caches (parent, raw name) -> path_index_t within a single parse:
	// directories via cached_directory() (find-or-create, so files
	// sharing a directory reuse the same path_element instead of
	// file_storage allocating a fresh one for each), and every non-pad
	// file's own leaf via record_leaf() (insert-only, first-wins on
	// collision, two files can legitimately share a path before
	// resolve_duplicate_filenames() runs later). The combined map is also
	// how symlink targets get resolved after the whole file list/tree has
	// been parsed, see resolve_symlinks().
	//
	// keyed by the fully raw, unsanitized component text, so a lookup
	// never needs to sanitize first; sanitizing only happens on a cache
	// miss, when a new path_element is actually created. ``name`` always
	// borrows: it points into the persistent info-section buffer, which
	// outlives the whole parse.
	struct dir_cache_key
	{
		dir_cache_key(aux::path_index_t const p, string_view const n)
			: parent(p)
			, name(n)
		{}

		bool operator==(dir_cache_key const&) const = default;

		aux::path_index_t parent;
		string_view name;
	};

	struct dir_cache_hash
	{
		std::size_t operator()(dir_cache_key const& k) const
		{
			std::size_t seed = std::hash<aux::path_index_t>{}(k.parent);
			boost::hash_combine(seed, std::hash<string_view>{}(k.name));
			return seed;
		}
	};
	using dir_cache_t = std::unordered_map<dir_cache_key, aux::path_index_t, dir_cache_hash>;

	// looks up (parent, raw) in cache, creating (and caching) a new
	// directory via fs.make_directory() on a miss. Sanitizing ``raw`` is
	// deferred to the miss path, so a cache hit costs nothing beyond the
	// lookup itself. The underlying path_element still borrows ``raw``
	// directly when sanitizing didn't change it, only copying the
	// sanitized text when it did.
	aux::path_index_t cached_directory(
		file_storage& fs, dir_cache_t& cache, aux::path_index_t const parent, string_view const raw)
	{
		auto const it = cache.find(dir_cache_key(parent, raw));
		if (it != cache.end())
			return it->second;
		std::string sanitized;
		bool const unchanged = aux::sanitize_path_element(sanitized, raw, true);
		aux::path_index_t const dir =
			fs.make_directory(parent, unchanged ? raw : string_view(sanitized), unchanged);
		cache.emplace(dir_cache_key(parent, raw), dir);
		return dir;
	}

	// records a file's own leaf into cache, keyed by its raw name, for
	// later symlink-target resolution. Never dedupes (unlike
	// cached_directory()): first inserted entry for a given (parent, raw)
	// key wins.
	void record_leaf(dir_cache_t& cache,
		aux::path_index_t const parent,
		string_view const raw,
		aux::path_index_t const leaf)
	{
		cache.emplace(dir_cache_key(parent, raw), leaf);
	}

	// buffers record_leaf()'s arguments instead of inserting into dir_cache
	// right away. The vast majority of torrents have no symlinks at all, in
	// which case dir_cache never needs a single file leaf (only
	// cached_directory()'s own directory entries, which are inserted
	// eagerly, independent of this), so hashing and inserting every file's
	// leaf into dir_cache up front would be wasted work. Flushed into
	// dir_cache (see flush_leaf_stash()) only once the whole file list has
	// been parsed and it's known whether any symlink actually needs it.
	struct leaf_stash_entry
	{
		aux::path_index_t parent;
		string_view raw;
		aux::path_index_t leaf;
	};
	using leaf_stash_t = std::vector<leaf_stash_entry>;

	// replays every buffered leaf into cache, in the same order they were
	// originally seen, preserving record_leaf()'s first-wins semantics
	void flush_leaf_stash(dir_cache_t& cache, leaf_stash_t const& stash)
	{
		for (auto const& e : stash)
			record_leaf(cache, e.parent, e.raw, e.leaf);
	}

	// per-symlink bookkeeping stashed at parse time, consumed by
	// resolve_symlinks() once the whole file list/tree has been parsed
	// (so every real file's own path_element chain, leaf included,
	// exists). ``target`` is the not-yet-resolved target, as a sequence of
	// raw (unsanitized) path components, see parse_symlink_path(). Per
	// BEP 47, symlink targets are always relative to the torrent root,
	// not the symlink's own directory, so there's no per-symlink base
	// directory to keep.
	struct pending_symlink
	{
		aux::path_index_t own_leaf;
		std::vector<string_view> target;
	};
	using symlink_stash_t = std::unordered_map<file_index_t, pending_symlink>;

	// stashes a symlink's not-yet-resolved target for resolve_symlinks(),
	// enforcing max_symlinks (resolving a symlink target isn't free, so a
	// malicious torrent declaring a huge number of them is rejected
	// outright rather than accepted and slowly resolved).
	bool stash_symlink(symlink_stash_t& symlink_stash,
		file_index_t const this_file,
		aux::path_index_t const leaf,
		std::vector<string_view> target,
		load_torrent_limits const& cfg,
		error_code& ec)
	{
		if (int(symlink_stash.size()) >= cfg.max_symlinks)
		{
			ec = errors::too_many_symlinks;
			return false;
		}
		symlink_stash.emplace(this_file, pending_symlink{leaf, std::move(target)});
		return true;
	}

	// inserts the file_entry (or symlink placeholder) for one parsed file,
	// shared between the v1 and v2 file-list/file-tree parsers. ``name_raw``
	// is skipped as a leaf_stash entry for pad files, since they don't own a
	// real path_element (see extract_single_file()); v2 never reaches here
	// with a pad file, so the check is a no-op on that path.
	bool finish_file_entry(file_storage& files,
		aux::path_index_t const dir,
		string_view const name,
		bool const name_borrow,
		string_view const name_raw,
		file_flags_t const file_flags,
		std::int64_t const file_size,
		std::time_t const mtime,
		std::int32_t const pieces_root_offset,
		std::vector<string_view> symlink_path,
		leaf_stash_t& leaf_stash,
		symlink_stash_t& symlink_stash,
		load_torrent_limits const& cfg,
		error_code& ec)
	{
		file_index_t const this_file = files.end_file();
		if (file_flags & file_storage::flag_symlink)
		{
			aux::path_index_t const leaf =
				files.add_symlink(ec, name, name_borrow, dir, file_flags, mtime);
			if (ec)
				return false;
			leaf_stash.push_back({dir, name_raw, leaf});
			if (!symlink_path.empty()
				&& !stash_symlink(symlink_stash, this_file, leaf, std::move(symlink_path), cfg, ec))
				return false;
		}
		else
		{
			aux::path_index_t const leaf = files.add_file(
				ec, name, name_borrow, dir, file_size, file_flags, mtime, pieces_root_offset);
			if (ec)
				return false;
			if (!(file_flags & file_storage::flag_pad_file))
				leaf_stash.push_back({dir, name_raw, leaf});
		}
		return true;
	}

	// a symlink used as a directory hop partway through another
	// symlink's target (e.g. a macOS framework's "Versions/Current"
	// standing in for "Versions/A") needs its own target substituted in
	// to keep walking; a symlink named as a target's final component is
	// left alone (fully legitimate, resolves to that symlink's own
	// path_index_t as-is). Chasing such hops is capped at a small,
	// fixed depth (max_symlink_hops, see load_torrent_limits), since real
	// uses are one hop deep. The cap bounds the total work for a single
	// symlink regardless of how deep or cyclic a .torrent shapes a
	// chain; a cycle simply burns the budget and fails like any other
	// dead end.
	//
	// resolves a single stashed symlink's target against every
	// path_element created while parsing files (directories via
	// cached_directory(), leaves via record_leaf()).
	bool resolve_one(dir_cache_t const& cache,
		std::unordered_map<aux::path_index_t, pending_symlink const*> const& owner,
		std::vector<string_view> const& target,
		load_torrent_limits const& cfg,
		aux::path_index_t& out)
	{
		// walks ``target`` directly, with no copy; a real use never
		// dereferences a hop, so ``spliced`` is only materialized the
		// first time one actually needs splicing in
		std::vector<string_view> spliced;
		std::vector<string_view> const* components = &target;
		std::size_t idx = 0;
		aux::path_index_t cur = aux::path_element::torrent_root;
		int hops_left = cfg.max_symlink_hops;
		while (idx < components->size())
		{
			auto const it = cache.find(dir_cache_key(cur, (*components)[idx]));
			if (it == cache.end())
				return false;
			cur = it->second;
			++idx;
			if (idx == components->size())
				break;

			auto const oi = owner.find(cur);
			if (oi == owner.end())
				continue;

			if (hops_left-- == 0)
				return false;
			pending_symlink const& dep = *oi->second;
			if (components != &spliced)
			{
				spliced.assign(components->begin() + std::ptrdiff_t(idx), components->end());
				components = &spliced;
				idx = 0;
			}
			spliced.insert(
				spliced.begin() + std::ptrdiff_t(idx), dep.target.begin(), dep.target.end());
			cur = aux::path_element::torrent_root;
		}
		out = cur;
		return true;
	}

	void resolve_symlinks(file_storage& files,
		dir_cache_t const& cache,
		symlink_stash_t const& stash,
		load_torrent_limits const& cfg)
	{
		std::unordered_map<aux::path_index_t, pending_symlink const*> owner;
		owner.reserve(stash.size());
		for (auto const& item : stash)
			owner.emplace(item.second.own_leaf, &item.second);

		for (auto const& item : stash)
		{
			aux::path_index_t resolved;
			bool const ok = resolve_one(cache, owner, item.second.target, cfg, resolved);
			files.internal_set_symlink_target(item.first, ok ? resolved : item.second.own_leaf);
		}
	}

	bool extract_single_file2(bdecode_node const& dict,
		file_storage& files,
		aux::path_index_t const dir,
		string_view const name,
		bool const name_borrow,
		string_view const raw,
		std::ptrdiff_t const info_offset,
		load_torrent_limits const& cfg,
		leaf_stash_t& leaf_stash,
		symlink_stash_t& symlink_stash,
		error_code& ec)
	{
		if (dict.type() != bdecode_node::dict_t)
		{
			ec = errors::torrent_file_parse_failed;
			return false;
		}

		file_dict_fields const f = scan_file_dict(dict);

		file_flags_t file_flags = get_file_attributes(f.attr);

		if (file_flags & file_storage::flag_pad_file)
		{
			ec = errors::torrent_invalid_pad_file;
			return false;
		}

		// symlinks have an implied "size" of zero. i.e. they use up 0 bytes of
		// the torrent payload space
		std::int64_t const file_size =
			(file_flags & file_storage::flag_symlink) ? 0 : node_int_value(f.length, -1);

		// if a file is too big, it will cause integer overflow in our
		// calculations of the size of the merkle tree (which is all 'int'
		// indices)
		if (file_size < 0
			|| (file_size / default_block_size) >= file_storage::max_num_pieces
			|| file_size > file_storage::max_file_size)
		{
			ec = errors::torrent_invalid_length;
			return false;
		}

		auto const mtime = static_cast<std::time_t>(node_int_value(f.mtime, 0));

		std::int32_t pieces_root_offset = file_storage::no_root_hash;

		std::vector<string_view> symlink_path;
		if ((file_flags & file_storage::flag_symlink)
			&& !parse_symlink_path(as_list(f.symlink_path), cfg, file_flags, symlink_path, ec))
			return false;

		if (symlink_path.empty() && file_size > 0)
		{
			bdecode_node const root = as_string(f.pieces_root);
			if (!root || root.string_length() != sha256_hash::size())
			{
				ec = errors::torrent_missing_pieces_root;
				return false;
			}
			if (sha256_hash(root.string_value().data()).is_all_zeros())
			{
				ec = errors::torrent_missing_pieces_root;
				return false;
			}
			std::ptrdiff_t const off = root.string_offset() - info_offset;
			TORRENT_ASSERT(off >= 0);
			TORRENT_ASSERT(off < std::numeric_limits<std::int32_t>::max());
			pieces_root_offset = static_cast<std::int32_t>(off);
		}

		return finish_file_entry(files,
			dir,
			name,
			name_borrow,
			raw,
			file_flags,
			file_size,
			mtime,
			pieces_root_offset,
			std::move(symlink_path),
			leaf_stash,
			symlink_stash,
			cfg,
			ec);
	}

	// 'top_level' is extracting the file for a single-file torrent. The
	// distinction is that the filename is found in "name" rather than
	// "path". dir_cache persists across every file of a single file list
	// (see extract_files()), deduping directories shared between files.
	bool extract_single_file(bdecode_node const& dict,
		file_storage& files,
		dir_cache_t& dir_cache,
		leaf_stash_t& leaf_stash,
		symlink_stash_t& symlink_stash,
		std::ptrdiff_t const info_offset,
		char const* info_buffer,
		bool const top_level,
		load_torrent_limits const& cfg,
		error_code& ec)
	{
		if (dict.type() != bdecode_node::dict_t)
		{
			ec = errors::torrent_file_parse_failed;
			return false;
		}

		file_dict_fields const f = scan_file_dict(dict);

		file_flags_t file_flags = get_file_attributes(f.attr);

		// a pad file reserves piece-alignment bytes, but a symlink is
		// always zero-length, so there's nothing for the pad attribute to
		// mean; reject the combination rather than guess which one the
		// producer intended
		if ((file_flags & file_storage::flag_pad_file) && (file_flags & file_storage::flag_symlink))
		{
			ec = errors::torrent_invalid_pad_file;
			return false;
		}

		// symlinks have an implied "size" of zero. i.e. they use up 0 bytes of
		// the torrent payload space
		std::int64_t const file_size =
			(file_flags & file_storage::flag_symlink) ? 0 : node_int_value(f.length, -1);

		// if a file is too big, it will cause integer overflow in our
		// calculations of the size of the merkle tree (which is all 'int'
		// indices)
		if (file_size < 0
			|| (file_size / default_block_size) >= std::numeric_limits<int>::max() / 2
			|| file_size > file_storage::max_file_size)
		{
			ec = errors::torrent_invalid_length;
			return false;
		}

		auto const mtime(static_cast<std::time_t>(node_int_value(f.mtime, 0)));

		aux::path_index_t dir = aux::path_element::torrent_root;
		string_view name;
		bool name_borrow = false;
		// owns the sanitized leaf text when name_borrow is false
		std::string name_scratch;
		// the leaf's own raw (unsanitized) text, used as the dir_cache key;
		// unused (default constructed) for pad files
		string_view name_raw;

		if (top_level)
		{
			// prefer the name.utf-8 because if it exists, it is more likely to be
			// correctly encoded
			bdecode_node p = as_string(f.name_utf8);
			if (!p)
				p = as_string(f.name);
			if (!p || p.string_length() == 0)
			{
				ec = errors::torrent_missing_name;
				return false;
			}

			string_view const raw = {info_buffer + (p.string_offset() - info_offset),
				static_cast<std::size_t>(p.string_length())};

			bool const unchanged = aux::sanitize_path_element(name_scratch, raw);
			if (!unchanged && name_scratch.empty())
			{
				ec = errors::torrent_missing_name;
				return false;
			}

			// single-file torrent convention: no directory nesting, and
			// file_storage::name() is not prepended when reconstructing
			// this file's path (it already equals the file's own name)
			dir = aux::path_element::no_root_dir;
			name_borrow = unchanged;
			name = unchanged ? raw : string_view(name_scratch);
			name_raw = raw;
		}
		else
		{
			bdecode_node p = as_list(f.path_utf8);
			if (!p)
				p = as_list(f.path);

			bool const has_path = p && p.list_items().begin() != bdecode_node::items_end_t{};

			if (has_path)
			{
				auto it = p.list_items().begin();
				auto const end = p.list_items().end();
				for (int idx = 0; it != end; ++it, ++idx)
				{
					if (idx >= cfg.max_directory_depth)
					{
						ec = errors::torrent_directory_too_deep;
						return false;
					}

					bdecode_node const e = *it;
					bool const is_last = it.is_last();

					if (e.type() != bdecode_node::string_t)
					{
						ec = errors::torrent_invalid_name;
						return false;
					}

					string_view const raw = {info_buffer + (e.string_offset() - info_offset),
						static_cast<std::size_t>(e.string_length())};

					// each path component becomes its own path_element,
					// whose stored name length is a std::uint16_t
					if (raw.size() >= (1 << 12))
					{
						ec = errors::torrent_invalid_name;
						return false;
					}

					if (is_last)
					{
						name_raw = raw;
						std::string sanitized;
						name_borrow = aux::sanitize_path_element(sanitized, raw, true);
						if (name_borrow)
							name = raw;
						else
						{
							name_scratch = std::move(sanitized);
							name = name_scratch;
						}
					}
					else
					{
						dir = cached_directory(files, dir_cache, dir, raw);
					}
				}
			}
			else if (file_flags & file_storage::flag_pad_file)
			{
				// pad files don't need a path element, they're stored under
				// the (virtual) pad-file directory. add_file()
				// ignores name/name_borrow for pad files entirely,
				// synthesizing the leaf from size on demand instead, so
				// there's nothing to compute here
				dir = aux::path_element::pad_directory;
			}
			else
			{
				ec = errors::torrent_missing_name;
				return false;
			}
		}

		// bitcomet pad file
		if (name.find("_____padding_file_") != string_view::npos)
			file_flags |= file_storage::flag_pad_file;

		std::vector<string_view> symlink_path;
		if ((file_flags & file_storage::flag_symlink)
			&& !parse_symlink_path(as_list(f.symlink_path), cfg, file_flags, symlink_path, ec))
			return false;

		return finish_file_entry(files,
			dir,
			name,
			name_borrow,
			name_raw,
			file_flags,
			file_size,
			mtime,
			file_storage::no_root_hash,
			std::move(symlink_path),
			leaf_stash,
			symlink_stash,
			cfg,
			ec);
	}

	bool extract_files2(bdecode_node const& tree,
		file_storage& target,
		ptrdiff_t const info_offset,
		char const* info_buffer,
		bool is_multi_file,
		load_torrent_limits const& cfg,
		error_code& ec)
	{
		if (tree.type() != bdecode_node::dict_t)
		{
			ec = errors::torrent_file_parse_failed;
			return false;
		}

		// the v2 file tree can be arbitrarily deeply nested. Walk it
		// iteratively with an explicit parse stack so the C++ call stack
		// doesn't grow with the directory depth. The depth is still bounded
		// by max_directory_depth to defend against malicious torrents.
		struct stack_frame
		{
			bdecode_node::dict_iterator it;
			aux::path_index_t dir;
		};

		// dedupes directories shared between sibling entries at the same
		// tree depth (the tree structure itself already dedupes across
		// depths, unlike v1's flat per-file path lists)
		dir_cache_t dir_cache;
		leaf_stash_t leaf_stash;
		symlink_stash_t symlink_stash;

		std::vector<stack_frame> stack;
		stack.push_back({tree.dict_items().begin(), aux::path_element::torrent_root});

		while (!stack.empty())
		{
			stack_frame& frame = stack.back();
			if (frame.it == bdecode_node::items_end_t{})
			{
				stack.pop_back();
				continue;
			}

			// true only for the very first entry of the whole tree walk;
			// is_multi_file is forced true below once that entry is
			// consumed, so this must be re-read every iteration rather
			// than hoisted
			bool const is_first = !is_multi_file;

			bdecode_node const key = frame.it.key_node();
			bdecode_node const value = frame.it.value_node();
			bool const is_last = frame.it.is_last();
			++frame.it;

			if (value.type() != bdecode_node::dict_t || key.string_value().empty())
			{
				ec = errors::torrent_file_parse_failed;
				return false;
			}

			string_view const raw = {info_buffer + (key.string_offset() - info_offset),
				static_cast<size_t>(key.string_length())};

			// each path component becomes its own path_element, whose
			// stored name length is a std::uint16_t
			if (raw.size() >= (1 << 12))
			{
				ec = errors::torrent_invalid_name;
				return false;
			}

			// a leaf ("file") entry's value dict has exactly one entry,
			// keyed by the empty string, mapping to the file's metadata.
			// determine this with a lookahead instead of dict_size(), since
			// we're about to consume these entries one way or another anyway
			bool leaf_node = false;
			bdecode_node leaf_value;
			auto const child_it = value.dict_items().begin();
			bool const child_nonempty = child_it != bdecode_node::items_end_t{};
			if (child_nonempty)
			{
				auto const first = *child_it;
				if (first.first.empty() && child_it.is_last())
				{
					leaf_node = true;
					leaf_value = first.second;
				}
			}

			// the single-file shortcut only applies when the root file-tree
			// dict has exactly one entry, and this is that entry
			bool const single_file = leaf_node && is_first && is_last;

			if (leaf_node)
			{
				// every file is a fresh path_element, so unlike a directory
				// component, its name always needs sanitizing regardless of
				// dir_cache
				std::string sanitized;
				bool const name_borrow = aux::sanitize_path_element(sanitized, raw, true);
				string_view const name = name_borrow ? raw : string_view(sanitized);

				// single_file: no directory nesting, and file_storage::name()
				// is not prepended when reconstructing this file's path
				if (!extract_single_file2(leaf_value,
						target,
						single_file ? aux::path_element::no_root_dir : frame.dir,
						name,
						name_borrow,
						raw,
						info_offset,
						cfg,
						leaf_stash,
						symlink_stash,
						ec))
				{
					return false;
				}
			}
			else
			{
				// descend into the subdirectory. This matches the depth check
				// in the original recursive implementation: an N-th level
				// recursive call had depth==N, which is allowed up to
				// max_directory_depth. stack.size() here equals the current
				// depth + 1, so we bail when the about-to-be-pushed child
				// would exceed the limit.
				if (int(stack.size()) > cfg.max_directory_depth)
				{
					ec = errors::torrent_directory_too_deep;
					return false;
				}

				// don't push a frame we would immediately pop. An empty
				// subdirectory contributes no files; just skip it.
				if (child_nonempty)
				{
					aux::path_index_t const child_dir =
						cached_directory(target, dir_cache, frame.dir, raw);
					// note: `frame` is invalidated by push_back below; we're done with it
					stack.push_back({value.dict_items().begin(), child_dir});
				}
			}

			// the single-file shortcut only applies at the top of the file
			// tree. After the first iteration we are either inside a
			// subdirectory or past the only entry of a multi-entry root, so
			// the shortcut can no longer fire.
			is_multi_file = true;
		}

		if (!symlink_stash.empty())
			flush_leaf_stash(dir_cache, leaf_stash);
		resolve_symlinks(target, dir_cache, symlink_stash, cfg);
		return true;
	}

	bool extract_files(bdecode_node const& list,
		file_storage& target,
		std::ptrdiff_t info_offset,
		char const* info_buffer,
		load_torrent_limits const& cfg,
		error_code& ec)
	{
		if (list.type() != bdecode_node::list_t)
		{
			ec = errors::torrent_file_parse_failed;
			return false;
		}
		target.reserve(list.list_size());

		// persists across every file in the list, deduping directories
		// shared between them
		dir_cache_t dir_cache;
		leaf_stash_t leaf_stash;
		symlink_stash_t symlink_stash;

		for (bdecode_node const item : list.list_items())
		{
			if (!extract_single_file(item,
					target,
					dir_cache,
					leaf_stash,
					symlink_stash,
					info_offset,
					info_buffer,
					false,
					cfg,
					ec))
				return false;
		}
		// resolves every stashed symlink target, rewriting invalid ones to
		// point to themselves
		if (!symlink_stash.empty())
			flush_leaf_stash(dir_cache, leaf_stash);
		resolve_symlinks(target, dir_cache, symlink_stash, cfg);
		return true;
	}

	// every key looked up in the top-level info dict by parse_info_section_impl()
	struct info_dict_fields
	{
		bdecode_node meta_version;
		bdecode_node files;
		bdecode_node length;
		bdecode_node piece_length;
		bdecode_node name_utf8;
		bdecode_node name;
		bdecode_node file_tree;
		bdecode_node pieces;
		bdecode_node priv;
#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS
		bdecode_node similar;
		bdecode_node collections;
#endif
		bdecode_node ssl_cert;
	};

	info_dict_fields scan_info_dict(bdecode_node const& info)
	{
		info_dict_fields f;
		for (auto const& [key, value] : info.dict_items())
		{
			if (key == "meta version" && !f.meta_version)
				f.meta_version = value;
			else if (key == "files" && !f.files)
				f.files = value;
			else if (key == "length" && !f.length)
				f.length = value;
			else if (key == "piece length" && !f.piece_length)
				f.piece_length = value;
			else if (key == "name.utf-8" && !f.name_utf8)
				f.name_utf8 = value;
			else if (key == "name" && !f.name)
				f.name = value;
			else if (key == "file tree" && !f.file_tree)
				f.file_tree = value;
			else if (key == "pieces" && !f.pieces)
				f.pieces = value;
			else if (key == "private" && !f.priv)
				f.priv = value;
#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS
			else if (key == "similar" && !f.similar)
				f.similar = value;
			else if (key == "collections" && !f.collections)
				f.collections = value;
#endif
			else if (key == "ssl-cert" && !f.ssl_cert)
				f.ssl_cert = value;
		}
		return f;
	}

#if TORRENT_ABI_VERSION < 4
	int load_file(std::string const& filename, std::vector<char>& v
		, error_code& ec, int const max_buffer_size = 80000000)
	{
		ec.clear();
#ifdef TORRENT_WINDOWS
		aux::file_pointer f(::_wfopen(convert_to_native_path_string(filename).c_str(), L"rb"));
#else
		aux::file_pointer f(std::fopen(filename.c_str(), "rb"));
#endif
		if (f.file() == nullptr)
		{
			ec.assign(errno, generic_category());
			return -1;
		}

		if (std::fseek(f.file(), 0, SEEK_END) < 0)
		{
			ec.assign(errno, generic_category());
			return -1;
		}
		std::int64_t const s = std::ftell(f.file());
		if (s < 0)
		{
			ec.assign(errno, generic_category());
			return -1;
		}
		if (s > max_buffer_size)
		{
			ec = errors::metadata_too_large;
			return -1;
		}
		if (std::fseek(f.file(), 0, SEEK_SET) < 0)
		{
			ec.assign(errno, generic_category());
			return -1;
		}
		v.resize(std::size_t(s));
		if (s == 0) return 0;
		std::size_t const read = std::fread(v.data(), 1, v.size(), f.file());
		if (read != std::size_t(s))
		{
			if (std::feof(f.file()))
			{
				v.resize(read);
				return 0;
			}
			ec.assign(errno, generic_category());
			return -1;
		}
		return 0;
	}
#endif

} // anonymous namespace

TORRENT_VERSION_NAMESPACE_4

	torrent_info::torrent_info(torrent_info const&) = default;
	torrent_info& torrent_info::operator=(torrent_info&&) noexcept = default;

#if TORRENT_ABI_VERSION < 4
	void torrent_info::remap_files(file_storage const& f)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(is_loaded());
		// the new specified file storage must have the exact
		// same size as the current file storage
		TORRENT_ASSERT_PRECOND(m_files.total_size() == f.total_size());

		// v2 torrents cannot be remapped. The file sizes are tied to the merkle
		// trees.
		TORRENT_ASSERT_PRECOND(!m_files.v2());
		TORRENT_ASSERT_PRECOND(!f.v2());

		if (m_files.total_size() != f.total_size()) return;
		if (m_files.v2() || f.v2()) return;

		m_modified_files.reset(new file_storage(f));
		m_modified_files->set_num_pieces(m_files.num_pieces());
		m_modified_files->set_piece_length(m_files.piece_length());
	}
#endif

#if TORRENT_ABI_VERSION == 1
#ifndef BOOST_NO_EXCEPTIONS
	torrent_info::torrent_info(char const* buffer, int size, int)
		: torrent_info(span<char const>{buffer, size}, from_span)
	{}
#endif

	// standard constructor that parses a torrent file
	torrent_info::torrent_info(entry const& torrent_file)
	{
		std::vector<char> tmp;
		std::back_insert_iterator<std::vector<char>> out(tmp);
		bencode(out, torrent_file);

		bdecode_node e;
		error_code ec;
		if (tmp.empty() || bdecode(&tmp[0], &tmp[0] + tmp.size(), e, ec) != 0)
		{
#ifndef BOOST_NO_EXCEPTIONS
			aux::throw_ex<system_error>(ec);
#else
			return;
#endif
		}
#ifndef BOOST_NO_EXCEPTIONS
		if (!parse_torrent_file(e, ec, load_torrent_limits{}))
			aux::throw_ex<system_error>(ec);
#else
		parse_torrent_file(e, ec, load_torrent_limits{});
#endif
		INVARIANT_CHECK;
	}

	torrent_info::torrent_info(bdecode_node const& torrent_file, error_code& ec, int)
		: torrent_info(torrent_file, ec)
	{}

	torrent_info::torrent_info(std::string const& filename, error_code& ec, int)
		: torrent_info(filename, ec)
	{}

	torrent_info::torrent_info(char const* buffer, int size, error_code& ec, int)
		: torrent_info(span<char const>{buffer, size}, ec, from_span)
	{}
#endif // TORRENT_ABI_VERSION

#if TORRENT_ABI_VERSION < 4
#ifndef BOOST_NO_EXCEPTIONS
	torrent_info::torrent_info(bdecode_node const& torrent_file)
		: torrent_info(torrent_file, load_torrent_limits{})
	{}

	torrent_info::torrent_info(char const* buffer, int size)
		: torrent_info(span<char const>{buffer, size}, from_span)
	{}

	torrent_info::torrent_info(span<char const> buffer, from_span_t)
		: torrent_info(buffer, load_torrent_limits{}, from_span)
	{}

	torrent_info::torrent_info(std::string const& filename)
		: torrent_info(filename, load_torrent_limits{})
	{}

	torrent_info::torrent_info(bdecode_node const& torrent_file
		, load_torrent_limits const& cfg)
	{
		error_code ec;
		if (!parse_torrent_file(torrent_file, ec, cfg))
			aux::throw_ex<system_error>(ec);

		INVARIANT_CHECK;
	}

	torrent_info::torrent_info(span<char const> buffer
		, load_torrent_limits const& cfg, from_span_t)
	{
		error_code ec;
		bdecode_node e = bdecode(buffer, ec, nullptr
			, cfg.max_decode_depth, cfg.max_decode_tokens);
		if (ec) aux::throw_ex<system_error>(ec);

		if (!parse_torrent_file(e, ec, cfg))
			aux::throw_ex<system_error>(ec);

		INVARIANT_CHECK;
	}

	torrent_info::torrent_info(std::string const& filename
		, load_torrent_limits const& cfg)
	{
		std::vector<char> buf;
		error_code ec;
		int ret = load_file(filename, buf, ec, cfg.max_buffer_size);
		if (ret < 0) aux::throw_ex<system_error>(ec);

		bdecode_node e = bdecode(buf, ec, nullptr, cfg.max_decode_depth
			, cfg.max_decode_tokens);
		if (ec) aux::throw_ex<system_error>(ec);

		if (!parse_torrent_file(e, ec, cfg))
			aux::throw_ex<system_error>(ec);

		INVARIANT_CHECK;
	}
#endif

	torrent_info::torrent_info(bdecode_node const& torrent_file
		, error_code& ec)
	{
		parse_torrent_file(torrent_file, ec, load_torrent_limits{});
		INVARIANT_CHECK;
	}

	torrent_info::torrent_info(char const* buffer, int size, error_code& ec)
		: torrent_info(span<char const>{buffer, size}, ec, from_span)
	{}

	torrent_info::torrent_info(span<char const> buffer
		, error_code& ec, from_span_t)
	{
		bdecode_node e = bdecode(buffer, ec);
		if (ec) return;
		parse_torrent_file(e, ec, load_torrent_limits{});

		INVARIANT_CHECK;
	}

	torrent_info::torrent_info(std::string const& filename, error_code& ec)
	{
		std::vector<char> buf;
		int ret = load_file(filename, buf, ec);
		if (ret < 0) return;

		bdecode_node e = bdecode(buf, ec);
		if (ec) return;
		parse_torrent_file(e, ec, load_torrent_limits{});

		INVARIANT_CHECK;
	}
#endif

	torrent_info::torrent_info(info_hash_t const& info_hash)
		: m_info_hash(info_hash)
	{}

	torrent_info::torrent_info(bdecode_node const& info_section, error_code& ec
		, load_torrent_limits const& cfg, from_info_section_t)
	{
		parse_info_section_impl(info_section, ec, cfg);
	}

	torrent_info::~torrent_info() = default;

#if TORRENT_ABI_VERSION < 4
	file_storage const& torrent_info::files() const
	{
		return files_impl();
	}

	file_storage const& torrent_info::orig_files() const
	{
		TORRENT_ASSERT(is_loaded());
		return m_files;
	}
#endif

	file_storage const& torrent_info::layout() const
	{
		TORRENT_ASSERT(is_loaded());
		return m_files;
	}

#if TORRENT_ABI_VERSION < 4
	void torrent_info::rename_file(file_index_t index, std::string const& new_filename)
	{
		TORRENT_ASSERT(is_loaded());
		if (m_files.file_path(index) == new_filename) return;

		copy_on_write();
		error_code ec;
		m_modified_files->rename_file_impl(index, new_filename, ec);
		// this deprecated overload has no way to report failure; a
		// new_filename with this many directory components isn't a
		// legitimate rename target
		TORRENT_ASSERT(!ec);
	}

	// internal
	void torrent_info::set_piece_layers(aux::vector<aux::vector<char>, file_index_t> pl)
	{
		m_piece_layers = pl;
		m_flags |= deprecated_v2_has_piece_hashes;
	}
#endif

	sha1_hash torrent_info::hash_for_piece(piece_index_t const index) const
	{ return sha1_hash(hash_for_piece_ptr(index)); }

#if TORRENT_ABI_VERSION < 4
	void torrent_info::copy_on_write()
	{
		TORRENT_ASSERT(is_loaded());
		INVARIANT_CHECK;

		if (m_modified_files) return;
		m_modified_files.reset(new file_storage(m_files));
	}
#endif

#if TORRENT_ABI_VERSION <= 2
	void torrent_info::swap(torrent_info& ti)
	{
		INVARIANT_CHECK;

		torrent_info tmp = std::move(ti);
		ti = std::move(*this);
		*this = std::move(tmp);
	}

	boost::shared_array<char const> torrent_info::metadata() const
	{
		return m_info_section;
	}
#endif

	string_view torrent_info::ssl_cert() const
	{
		if (!(m_flags & ssl_torrent)) return "";
		TORRENT_ASSERT(m_ssl_cert_offset >= 0);
		TORRENT_ASSERT(m_ssl_cert_offset + m_ssl_cert_size <= m_info_section_size);
		return {m_info_section.get() + m_ssl_cert_offset
			, static_cast<std::size_t>(m_ssl_cert_size)};
	}

#if TORRENT_ABI_VERSION < 3
	bool torrent_info::parse_info_section(bdecode_node const& info, error_code& ec)
	{
		return parse_info_section(info, ec, load_torrent_limits{}.max_pieces);
	}
#endif

#if TORRENT_ABI_VERSION < 4
	bool torrent_info::parse_info_section(bdecode_node const& info
		, error_code& ec, int const max_pieces)
	{
		load_torrent_limits cfg;
		cfg.max_pieces = max_pieces;
		parse_info_section_impl(info, ec, cfg);
		return !ec;
	}
#endif

	void torrent_info::parse_info_section_impl(
		bdecode_node const& info, error_code& ec, load_torrent_limits const& cfg_in)
	{
		int const max_pieces = cfg_in.max_pieces;
		// path_element::depth is a std::uint16_t, so no chain built while
		// parsing may exceed that width; max_directory_depth bounds
		// directory nesting only, and a file's own leaf sits one level
		// below that, hence the -1 headroom
		load_torrent_limits cfg = cfg_in;
		cfg.max_directory_depth = std::min(
			cfg_in.max_directory_depth, int(std::numeric_limits<std::uint16_t>::max()) - 1);
		// resolve_one()'s hop budget countdown (hops_left-- == 0) never
		// triggers for a negative value, silently defeating the bound
		TORRENT_ASSERT_PRECOND(cfg.max_symlink_hops >= 0);
		cfg.max_symlink_hops = std::max(cfg_in.max_symlink_hops, 0);
		if (info.type() != bdecode_node::dict_t)
		{
			ec = errors::torrent_info_no_dict;
			return;
		}

		auto section = info.data_section();
		if (section.size() >= std::numeric_limits<int>::max())
		{
			ec = errors::metadata_too_large;
			return;
		}

		if (section.empty() || section[0] != 'd' || section[section.size() - 1] != 'e')
		{
			ec = errors::invalid_bencoding;
			return;
		}

		// copy the info section
		m_info_section_size = aux::numeric_cast<int>(section.size());
		char* ptr = new char[aux::numeric_cast<std::size_t>(m_info_section_size)];
		std::memcpy(ptr, section.data(), aux::numeric_cast<std::size_t>(m_info_section_size));
		m_info_section.reset(ptr);

		// this is the offset from the start of the torrent file buffer to the
		// info-dictionary (within the torrent file).
		// we need this because we copy just the info dictionary buffer and pull
		// out parsed data (strings) from the bdecode_node and need to make them
		// point into our copy of the buffer.
		std::ptrdiff_t const info_offset = info.data_offset();

		info_dict_fields const info_fields = scan_info_dict(info);

		// check for a version key
		int const version = int(node_int_value(info_fields.meta_version, -1));
		if (version > 0)
		{
			char error_string[200];
			if (info.has_soft_error(error_string))
			{
				ec = errors::invalid_bencoding;
				return;
			}

			if (version > 2)
			{
				ec = errors::torrent_unknown_version;
				return;
			}
		}

		// a v1 info-hash is only meaningful for version < 2, or for a
		// hybrid (v1+v2) torrent that also carries a "files"/"length"
		// listing alongside "meta version" >= 2, mirroring the check
		// further down that decides whether this ends up a v2-only torrent.
		bdecode_node const files_node = as_list(info_fields.files);
		bdecode_node const length_key = info_fields.length;
		bool const need_v1_hash = version < 2 || bool(files_node) || bool(length_key);
		bool const need_v2_hash = version >= 2;

		// hash the info-field to calculate info-hash
		if (need_v1_hash)
			m_info_hash.v1 = hasher(section).final();
		else
			m_info_hash.v1.clear();

		if (need_v2_hash)
			m_info_hash.v2 = hasher256(section).final();
		else
			m_info_hash.v2.clear();

		// extract piece length
		std::int64_t const piece_length = node_int_value(info_fields.piece_length, -1);
		if (piece_length <= 0 || piece_length > file_storage::max_piece_size)
		{
			ec = errors::torrent_missing_piece_length;
			return;
		}

		// according to BEP 52: "It must be a power of two and at least 16KiB."
		if (version > 1 && (piece_length < default_block_size
			|| (piece_length & (piece_length - 1)) != 0))
		{
			ec = errors::torrent_missing_piece_length;
			return;
		}

		file_storage files(m_info_section.get());
		files.set_piece_length(static_cast<int>(piece_length));

		// extract file name (or the directory name if it's a multi file libtorrent)
		bdecode_node name_ent = as_string(info_fields.name_utf8);
		if (!name_ent)
			name_ent = as_string(info_fields.name);
		if (!name_ent)
		{
			ec = errors::torrent_missing_name;
			// mark the torrent as invalid
			m_files.set_piece_length(0);
			return;
		}

		std::string name;
		bool const name_unchanged = aux::sanitize_path_element(name, name_ent.string_value());
		if (name_unchanged)
			name = std::string(name_ent.string_value());
		if (name.empty())
		{
			// the fallback name is always based on the v1 (SHA-1) info-hash,
			// even for v2-only torrents where it's otherwise not needed. Compute
			// it lazily here rather than unconditionally up-front, to avoid the
			// extra hash in the common case where the name field is valid.
			// TODO: this should using the v2 hash for v2 torrents, but
			// changing it is not backwards compatible. Version the sanitizer,
			// letting the caller pick which sanitizer version to use. Then
			// this can be fixed in an updated version.
			name = aux::to_hex(m_info_hash.has_v1() ? m_info_hash.v1 : hasher(section).final());
		}
		files.set_name(name);

		// extract file list

		// files has nothing but piece_length set at this point, hand that off
		// to v1_files as a starting point for the v1 extraction below, so both
		// v1 and v2 files can be extracted and compared against each other
		file_storage v1_files;
		v1_files.set_name(name);
		if (version >= 2)
			v1_files.set_piece_length(files.piece_length());

		bdecode_node const file_tree_node = as_dict(info_fields.file_tree);
		if (version >= 2 && file_tree_node)
		{
			if (!extract_files2(file_tree_node,
					files,
					info_offset,
					m_info_section.get(),
					bool(files_node),
					cfg,
					ec))
			{
				// mark the torrent as invalid
				m_files.set_piece_length(0);
				return;
			}

			if (files.num_files() > 1)
				m_flags |= multifile;
			else
				m_flags &= ~multifile;
		}
		else if (version >= 2)
		{
			// mark the torrent as invalid
			m_files.set_piece_length(0);
			ec = errors::torrent_missing_file_tree;
			return;
		}
		else if (file_tree_node)
		{
			// mark the torrent as invalid
			m_files.set_piece_length(0);
			ec = errors::torrent_missing_meta_version;
			return;
		}

		if (!files_node)
		{
			// if this is a v2 torrent it is ok for the length key to be missing
			// that means it is a v2 only torrent
			if (version < 2 || length_key)
			{
				// if there's no list of files, there has to be a length
				// field.
				dir_cache_t single_file_dir_cache;
				leaf_stash_t single_file_leaf_stash;
				// a genuine single-file torrent's lone file has nothing else
				// it could resolve a symlink target to, so this stash is
				// deliberately never passed to resolve_symlinks(), since the
				// add-time self-pointing placeholder is already the only
				// possible outcome
				symlink_stash_t single_file_symlink_stash;
				if (!extract_single_file(info,
						version == 2 ? v1_files : files,
						single_file_dir_cache,
						single_file_leaf_stash,
						single_file_symlink_stash,
						info_offset,
						m_info_section.get(),
						true,
						cfg,
						ec))
				{
					// mark the torrent as invalid
					m_files.set_piece_length(0);
					return;
				}

				m_flags &= ~multifile;
			}
			// else: this is a v2 only torrent, the v1 info-hash was already
			// left cleared, and there's no v1 file length/listing to extract
		}
		else
		{
			if (!extract_files(files_node,
					version == 2 ? v1_files : files,
					info_offset,
					m_info_section.get(),
					cfg,
					ec))
			{
				// mark the torrent as invalid
				m_files.set_piece_length(0);
				return;
			}
			m_flags |= multifile;
		}
		if (files.num_files() == 0)
		{
			ec = errors::no_files_in_torrent;
			// mark the torrent as invalid
			m_files.set_piece_length(0);
			return;
		}
		if (files.name().empty())
		{
			ec = errors::torrent_missing_name;
			// mark the torrent as invalid
			m_files.set_piece_length(0);
			return;
		}

		// ensure hybrid torrents have compatible v1 and v2 file storages
		if (version >= 2 && v1_files.num_files() > 0)
		{
			// previous versions of libtorrent did not not create hybrid
			// torrents with "tail-padding". When loading, accept both.
			if (files.num_files() == v1_files.num_files() + 1)
			{
				files.remove_tail_padding();
			}

			if (!aux::files_compatible(files, v1_files))
			{
				// mark the torrent as invalid
				m_files.set_piece_length(0);
				ec = errors::torrent_inconsistent_files;
				return;
			}
		}

		// extract SHA-1 hashes for all pieces
		// we want this division to round upwards, that's why we have the
		// extra addition

		if (files.total_size() / files.piece_length() > file_storage::max_num_pieces)
		{
			ec = errors::too_many_pieces_in_torrent;
			// mark the torrent as invalid
			m_files.set_piece_length(0);
			return;
		}

		files.set_num_pieces(int((files.total_size() + files.piece_length() - 1)
			/ files.piece_length()));

		// we expect the piece hashes to be < 2 GB in size
		if (files.num_pieces() >= std::numeric_limits<int>::max() / 20
			|| files.num_pieces() > max_pieces)
		{
			ec = errors::too_many_pieces_in_torrent;
			// mark the torrent as invalid
			m_files.set_piece_length(0);
			return;
		}

		bdecode_node const pieces = as_string(info_fields.pieces);
		if (!pieces)
		{
			// whenever a v1 info-hash is computed (a v1 torrent, or a hybrid
			// that also carries a "files"/"length" listing) the torrent claims
			// to be verifiable with v1 piece hashes. Without a "pieces" string
			// there are none, yet v1() would report true and hash_for_piece()
			// would index past the info section. Require "pieces" in that case.
			if (need_v1_hash)
			{
				ec = errors::torrent_missing_pieces;
				// mark the torrent as invalid
				m_files.set_piece_length(0);
				return;
			}
		}
		else
		{
			if (pieces.string_length() != files.num_pieces() * 20)
			{
				ec = errors::torrent_invalid_hashes;
				// mark the torrent as invalid
				m_files.set_piece_length(0);
				return;
			}

			std::ptrdiff_t const hash_offset = pieces.string_offset() - info_offset;
			TORRENT_ASSERT(hash_offset < std::numeric_limits<std::int32_t>::max());
			TORRENT_ASSERT(hash_offset >= 0);
			m_piece_hashes = static_cast<std::int32_t>(hash_offset);
			TORRENT_ASSERT(m_piece_hashes > 0);
			TORRENT_ASSERT(m_piece_hashes < m_info_section_size);
		}

		m_flags |=
			(node_int_value(info_fields.priv, 0) != 0) ? private_torrent : torrent_info_flags_t{};

#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS
		bdecode_node const similar = as_list(info_fields.similar);
		if (similar)
		{
			for (bdecode_node const item : similar.list_items())
			{
				if (item.type() != bdecode_node::string_t)
					continue;

				if (item.string_length() != 20)
					continue;
				m_similar_torrents.push_back(
					static_cast<std::int32_t>(item.string_offset() - info_offset));
			}
		}

		bdecode_node const collections = as_list(info_fields.collections);
		if (collections)
		{
			for (bdecode_node const str : collections.list_items())
			{
				if (str.type() != bdecode_node::string_t) continue;

				m_collections.emplace_back(
					aux::numeric_cast<std::int32_t>(str.string_offset() - info_offset)
					, str.string_length());
			}
		}
#endif // TORRENT_DISABLE_MUTABLE_TORRENTS

		if (bdecode_node const ssl_cert = as_string(info_fields.ssl_cert))
		{
			m_flags |= ssl_torrent;
			m_ssl_cert_offset = aux::numeric_cast<std::int32_t>(ssl_cert.string_offset() - info_offset);
			m_ssl_cert_size = ssl_cert.string_length();
		}

		if (files.total_size() == 0)
		{
			ec = errors::torrent_invalid_length;
			// mark the torrent as invalid
			m_files.set_piece_length(0);
			return;
		}

		// now, commit the files structure we just parsed out
		// into the torrent_info object.
		m_files.swap(files);

		TORRENT_ASSERT(m_info_hash.has_v2() == m_files.v2());
	}

#if TORRENT_ABI_VERSION < 4
	span<char const> torrent_info::piece_layer(file_index_t f) const
	{
		TORRENT_ASSERT_PRECOND(f >= file_index_t(0));
		if (f >= m_piece_layers.end_index()) return {};
		if (m_files.pad_file_at(f)) return {};

		if (m_files.file_size(f) <= piece_length())
		{
			auto const root_ptr = m_files.root_ptr(f);
			if (root_ptr == nullptr) return {};
			return {root_ptr, lt::sha256_hash::size()};
		}
		return m_piece_layers[f];
	}

	void torrent_info::free_piece_layers()
	{
		m_piece_layers.clear();
		m_piece_layers.shrink_to_fit();

		m_flags &= ~deprecated_v2_has_piece_hashes;
	}

	void torrent_info::internal_set_creator(string_view const c)
	{ m_created_by = std::string(c); }

	void torrent_info::internal_set_comment(string_view const s)
	{ m_comment = std::string(s); }

	void torrent_info::internal_set_creation_date(std::time_t const t)
	{ m_creation_date = t; }
#endif

#if TORRENT_ABI_VERSION < 4
	bdecode_node torrent_info::info(char const* key) const
	{
		if (m_info_dict.type() == bdecode_node::none_t)
		{
			error_code ec;
			bdecode(m_info_section.get(), m_info_section.get()
				+ m_info_section_size, m_info_dict, ec);
			if (ec) return {};
		}
		return m_info_dict.dict_find(key);
	}
#endif

#if TORRENT_ABI_VERSION < 4
	bool torrent_info::parse_torrent_file(bdecode_node const& torrent_file
		, error_code& ec, load_torrent_limits const& cfg)
	{
		add_torrent_params atp;
		std::shared_ptr<torrent_info> ti = aux::parse_torrent_file(torrent_file, ec, cfg, atp);
		if (ec) return false;

		if (ti)
		{
			auto const renamed_files = aux::resolve_duplicate_filenames(ti->layout(), cfg.max_duplicate_filenames, ec);
			if (ec) return false;
			// For backwards compatibility, make sure the file_storage has updated
			// filenames as well
			for (auto const& entry : renamed_files)
			{
				ti->rename_file(entry.first, entry.second);
			}
			*this = std::move(*ti);
		}
		else
		{
			// this is a magnet-link "torrent" file
			m_info_hash = atp.info_hashes;
			return true;
		}

		m_comment = atp.comment;
		m_created_by = atp.created_by;
		m_creation_date = atp.creation_date;
		int tier = 0;
		for (std::size_t i = 0; i < atp.trackers.size(); ++i)
		{
			if (i < atp.tracker_tiers.size()) tier = atp.tracker_tiers[i];
			announce_entry ent;
			ent.url = atp.trackers[i];
			ent.tier = std::uint8_t(tier);
			m_urls.push_back(std::move(ent));
		}

		if (atp.flags & torrent_flags::i2p_torrent)
		{
			m_flags |= i2p;
		}

		if (v2())
		{
			auto& trees = atp.merkle_trees;
			auto& mask = atp.merkle_tree_mask;
			auto& verified = atp.verified_leaf_hashes;

			aux::vector<aux::vector<char>, file_index_t> v2_hashes;

			auto const& fs = layout();
			bitfield const empty_verified;
			std::vector<sha256_hash> scratch_space;
			hasher256 scratch_hasher;
			for (file_index_t i : fs.file_range())
			{
				if (fs.pad_file_at(i) || fs.file_size(i) <= fs.piece_length())
				{
					v2_hashes.emplace_back();
					continue;
				}

				if (i >= atp.merkle_trees.end_index()) break;
				bitfield const& verified_bitmask = (i >= verified.end_index()) ? empty_verified : verified[i];

				aux::merkle_tree tree(fs.file_num_blocks(i), fs.blocks_per_piece(), fs.root_ptr(i));
				if (i < mask.end_index() && !mask[i].empty())
				{
					tree.load_sparse_tree(
						trees[i], mask[i], verified_bitmask, scratch_space, scratch_hasher);
				}
				else
				{
					tree.load_tree(trees[i], verified_bitmask);
				}

				auto const& layer = tree.get_piece_layer(scratch_space, scratch_hasher);
				std::vector<char> out_layer;
				out_layer.reserve(layer.size() * sha256_hash::size());
				for (auto const& h : layer)
				{
					// we're missing a piece layer. We can't return a valid
					// torrent
					if (h.is_all_zeros()) break;
					out_layer.insert(out_layer.end(), h.data(), h.data() + sha256_hash::size());
				}
				v2_hashes.emplace_back(std::move(out_layer));
			}
			set_piece_layers(v2_hashes);
		}

		for (auto const& url : atp.url_seeds)
		{
			web_seed_entry ent(url);
			ent.type = web_seed_entry::url_seed;
			m_web_seeds.push_back(std::move(ent));
		}

		for (auto const& url : atp.http_seeds)
		{
			web_seed_entry ent(url);
			ent.type = web_seed_entry::http_seed;
			m_web_seeds.push_back(std::move(ent));
		}

		for (auto const& n : atp.dht_nodes)
		{
			m_nodes.emplace_back(n);
		}

		// TODO: collections
		// TODO: similar
		return true;
	}

	void torrent_info::add_tracker(std::string const& url, int const tier)
	{
		add_tracker(url, tier, announce_entry::source_client);
	}

	void torrent_info::add_tracker(std::string const& url, int const tier
		, announce_entry::tracker_source const source)
	{
		TORRENT_ASSERT_PRECOND(!url.empty());
		auto const i = std::find_if(m_urls.begin(), m_urls.end()
			, [&url](announce_entry const& ae) { return ae.url == url; });
		if (i != m_urls.end()) return;
		if (!lt::aux::is_valid_tracker_url(url)) return;

		announce_entry e(url);
		e.tier = std::uint8_t(tier);
		e.source = source;
		m_urls.push_back(e);

		std::sort(m_urls.begin(), m_urls.end()
			, [] (announce_entry const& lhs, announce_entry const& rhs)
			{ return lhs.tier < rhs.tier; });
	}

	void torrent_info::clear_trackers()
	{
		m_urls.clear();
	}
#endif

#if TORRENT_ABI_VERSION == 1
	std::vector<std::string> torrent_info::url_seeds() const
	{
		std::vector<std::string> ret;
		for (auto const& w : m_web_seeds)
			ret.push_back(w.url);
		return ret;
	}

	std::vector<std::string> torrent_info::http_seeds() const
	{
		return {};
	}
#endif // TORRENT_ABI_VERSION

#if TORRENT_ABI_VERSION < 4
	void torrent_info::add_url_seed(std::string const& url,
		std::string const& extern_auth,
		web_seed_entry::headers_t const& ext_headers)
	{
		m_web_seeds.emplace_back(url, extern_auth, ext_headers);
	}

	void torrent_info::add_http_seed(
		std::string const&, std::string const&, web_seed_entry::headers_t const&)
	{}

	void torrent_info::set_web_seeds(std::vector<web_seed_entry> seeds)
	{
		m_web_seeds = std::move(seeds);
	}
#endif

	std::vector<sha1_hash> torrent_info::similar_torrents() const
	{
		std::vector<sha1_hash> ret;
#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS
		ret.reserve(m_similar_torrents.size() + m_owned_similar_torrents.size());

		for (auto const& st : m_similar_torrents)
			ret.emplace_back(m_info_section.get() + st);

		for (auto const& st : m_owned_similar_torrents)
			ret.push_back(st);
#endif

		return ret;
	}

	std::vector<std::string> torrent_info::collections() const
	{
		std::vector<std::string> ret;
#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS
		ret.reserve(m_collections.size() + m_owned_collections.size());

		for (auto const& c : m_collections)
			ret.emplace_back(m_info_section.get() + c.first, aux::numeric_cast<std::size_t>(c.second));

		for (auto const& c : m_owned_collections)
			ret.push_back(c);
#endif // TORRENT_DISABLE_MUTABLE_TORRENTS

		return ret;
	}

#if TORRENT_USE_INVARIANT_CHECKS
	void torrent_info::check_invariant() const
	{
		for (auto const i : m_files.file_range())
		{
			// pad files don't store a name, it's generated from their size
			// on demand, none of the checks below apply to them
			if (m_files.pad_file_at(i))
				continue;

			TORRENT_ASSERT(m_files.file_name(i).data() != nullptr);
			if (!m_files.owns_name(i))
			{
				// name needs to point into the allocated info section buffer
				TORRENT_ASSERT(m_files.file_name(i).data() >= m_info_section.get());
				TORRENT_ASSERT(m_files.file_name(i).data() < m_info_section.get() + m_info_section_size);
			}
			else
			{
				// name must be a null terminated string
				aux::file_name_view const name = m_files.file_name(i);
				TORRENT_ASSERT(name.data()[name.size()] == '\0');
			}
		}

		TORRENT_ASSERT(m_piece_hashes <= m_info_section_size);
	}
#endif

	sha1_hash torrent_info::info_hash() const noexcept
	{
		return m_info_hash.get_best();
	}

	bool torrent_info::v1() const { return m_info_hash.has_v1(); }
	bool torrent_info::v2() const { return m_info_hash.has_v2(); }

TORRENT_VERSION_NAMESPACE_4_END

}
