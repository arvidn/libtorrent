/*

Copyright (c) 2008-2022, Arvid Norberg
Copyright (c) 2009, Georg Rudoy
Copyright (c) 2016-2018, 2020, Alden Torres
Copyright (c) 2017-2019, Steven Siloti
Copyright (c) 2021, Mark Scott
Copyright (c) 2022, Konstantin Morozov
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/file_storage.hpp"
#include "libtorrent/aux_/string_util.hpp" // for allocate_string_copy
#include "libtorrent/index_range.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"
#include "libtorrent/aux_/alloca.hpp"
#include "libtorrent/disk_interface.hpp" // for default_block_size
#include "libtorrent/aux_/merkle.hpp"
#include "libtorrent/aux_/throw.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/crc.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include <cstring>
#include <algorithm>
#include <functional>
#include <set>
#include <unordered_set>
#include <atomic>

// resolve_duplicate_filenames_slow() hashes an actual, native-separator
// file_path() string against these crc32 hashes, so this must match
// append_path()'s separator (path.cpp) or a real collision goes undetected
#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
#define TORRENT_SEPARATOR '\\'
#else
#define TORRENT_SEPARATOR '/'
#endif

using namespace std::placeholders;

namespace libtorrent {

namespace {

	bool compare_file_offset(aux::file_entry const& lhs
		, aux::file_entry const& rhs)
	{
		return lhs.offset < rhs.offset;
	}

	// the sentinels a path_index_t chain walk (or depth count) stops at
	bool is_root_path_index(aux::path_index_t const idx)
	{
		return idx == aux::path_element::torrent_root || idx == aux::path_element::path_is_absolute
			|| idx == aux::path_element::no_root_dir;
	}
}

TORRENT_VERSION_NAMESPACE_4

	file_storage::file_storage() = default;
	file_storage::file_storage(char const* const info_section)
		: m_info_section(info_section)
	{}
	file_storage::~file_storage() = default;

	// even though the special member functions are identical to what the
	// compiler would have generated, they are put here to explicitly make
	// them part of libtorrent and properly exported by the .dll.
	// copy-assignment is deleted, since aux::file_entry has no need for it
	// and dropping it keeps file_entry's ownership logic simpler.
	file_storage::file_storage(file_storage const&) = default;
	file_storage::file_storage(file_storage&&) noexcept = default;
	file_storage& file_storage::operator=(file_storage&&) & = default;

	void file_storage::reserve(int num_files)
	{
		m_files.reserve(num_files);
	}

	int file_storage::piece_size(piece_index_t const index) const
	{
		TORRENT_ASSERT_PRECOND(index >= piece_index_t(0) && index < end_piece());
		if (index == last_piece())
		{
			std::int64_t const size_except_last
				= (num_pieces() - 1) * std::int64_t(piece_length());
			std::int64_t const size = total_size() - size_except_last;
			TORRENT_ASSERT(size > 0);
			TORRENT_ASSERT(size <= piece_length());
			return int(size);
		}
		else
			return piece_length();
	}

	int file_storage::piece_size2(piece_index_t const index) const
	{
		TORRENT_ASSERT_PRECOND(index >= piece_index_t{} && index < end_piece());
		TORRENT_ASSERT(max_file_offset / piece_length() > static_cast<int>(index));
		// find the file iterator and file offset
		aux::file_entry target;
		TORRENT_ASSERT(max_file_offset / piece_length() > static_cast<int>(index));
		target.offset = aux::numeric_cast<std::uint64_t>(std::int64_t(piece_length()) * static_cast<int>(index));
		TORRENT_ASSERT(!compare_file_offset(target, m_files.front()));

		auto const file_iter = std::upper_bound(
			m_files.begin(), m_files.end(), target, compare_file_offset);

		TORRENT_ASSERT(file_iter != m_files.begin());
		if (file_iter == m_files.end()) return piece_size(index);

		// this static cast is safe because the resulting value is capped by
		// piece_length(), which fits in an int
		return static_cast<int>(
			std::min(static_cast<std::uint64_t>(piece_length()), file_iter->offset - target.offset));
	}

	int file_storage::blocks_in_piece2(piece_index_t const index) const
	{
		// the number of default_block_size in a piece size, rounding up
		return (piece_size2(index) + default_block_size - 1) / default_block_size;
	}

	int file_storage::blocks_per_piece() const
	{
		return (m_piece_length + default_block_size - 1) / default_block_size;
	}

	// splits a definitely-relative ``path`` (path is supposed to include
	// the name of the torrent itself) into a chain of owned directory
	// path_elements, not including path's own last component, rooted at
	// torrent_root if the chain matches name(), or at the no_root_dir
	// sentinel otherwise. Returns the terminal directory's index (compare
	// against aux::path_element::no_root_dir to tell which of those two
	// applied), and path's own last component in ``leaf_out``. Since this is
	// the only place a path_element chain grows one component at a time with
	// no bound on ``path``'s number of components (unlike the path_index_t-
	// based add_file() overload, whose caller is expected to bound directory
	// depth itself, see torrent_info.cpp's max_directory_depth), it's also
	// the only place that needs to guard against overflowing path_element::
	// depth; sets ``ec`` and returns {} if ``path`` is too deep.
	aux::path_index_t file_storage::resolve_owned_directory(
		std::string const& path, string_view& leaf_out, error_code& ec)
	{
		TORRENT_ASSERT(!is_complete(path));
		TORRENT_ASSERT(path[0] != '/');

		// split the string into the leaf filename
		// and the branch path
		auto [branch_path, leaf] = rsplit_path(path);
		leaf_out = leaf;

		if (branch_path.empty())
		{
			// this file has no directory components at all (single-file
			// torrent convention: path is just the file's own name), so
			// there's no torrent-name root directory to include either
			return aux::path_element::no_root_dir;
		}

		// if the path *does* contain the name of the torrent (as we expect)
		// strip it before adding the remaining components to the tree
		bool const no_root_dir_out = (lsplit_path(branch_path).first != m_name);
		aux::path_index_t const parent =
			no_root_dir_out ? aux::path_element::no_root_dir : aux::path_element::torrent_root;
		if (!no_root_dir_out)
			branch_path = lsplit_path(branch_path).second;

		aux::path_index_t dir = parent;
		while (!branch_path.empty())
		{
			if (!is_root_path_index(dir)
				&& m_path_elements[dir].depth >= std::numeric_limits<std::uint16_t>::max() - 1)
			{
				ec = errors::torrent_directory_too_deep;
				return {};
			}
			auto [component, rest] = lsplit_path(branch_path);
			dir = make_directory(dir, component, false);
			branch_path = rest;
		}
		return dir;
	}

	aux::path_index_t file_storage::make_directory(
		aux::path_index_t const parent, string_view const name, bool const borrow)
	{
		// nothing is ever nested under the pad-file directory
		TORRENT_ASSERT(parent != aux::path_element::pad_directory);
		auto const ret = m_path_elements.end_index();
		m_path_elements.emplace_back();
		aux::path_element& elem = m_path_elements.back();
		elem.parent = parent;
		elem.depth = is_root_path_index(parent)
			? std::uint16_t(1)
			: aux::numeric_cast<std::uint16_t>(m_path_elements[parent].depth + 1);
		if (borrow)
		{
			TORRENT_ASSERT(name.size() < aux::path_element::name_is_owned);
			elem.name_len = aux::numeric_cast<std::uint16_t>(name.size());
			elem.name_ptr = name.data();
		}
		else
		{
			elem.name_len = aux::path_element::name_is_owned;
			elem.name_ptr = aux::allocate_string_copy(name);
		}
		return ret;
	}

	string_view file_storage::path_element_name(aux::path_element const& e) const
	{
		if (e.name_len != aux::path_element::name_is_owned)
			return {e.name_ptr, e.name_len};
		return e.name_ptr ? string_view(e.name_ptr) : string_view();
	}

	aux::path_index_t file_storage::reconstruct_path(aux::path_index_t leaf, std::string& out) const
	{
		if (leaf == aux::path_element::pad_directory)
		{
			// the pad-file directory is always name()/.pad, so bake the name
			// in here rather than making every caller special-case pad files
			append_path(out, m_name);
			append_path(out, ".pad");
			return aux::path_element::no_root_dir;
		}

		// each element's own depth is cached at creation time (see
		// make_directory()), so the chain length is known up front: the
		// walk below only needs to run once, filling ``chain`` back-to-front
		// so a single forward pass over it visits components root-first
		// without a separate reversal. Stack-allocated (falling back to the
		// heap past TORRENT_ALLOCA's cutoff) rather than a heap-allocated
		// vector, since this runs on hot paths like files_compatible()
		int const depth = is_root_path_index(leaf) ? 0 : int(m_path_elements[leaf].depth);

		TORRENT_ALLOCA(chain, aux::path_index_t, depth);
		aux::path_index_t idx = leaf;
		for (int i = depth - 1; i >= 0; --i)
		{
			chain[i] = idx;
			idx = m_path_elements[idx].parent;
		}

		for (aux::path_index_t const e : chain)
			append_path(out, path_element_name(m_path_elements[e]));

		return idx;
	}

TORRENT_VERSION_NAMESPACE_4_END

	std::string renamed_files::file_path(
		file_storage const& fs
		, file_index_t const index
		, std::string const& save_path) const
	{
		auto i = m_renamed_files.find(index);
		if (i == m_renamed_files.end()) return fs.file_path(index, save_path);

		TORRENT_ASSERT_PRECOND(index >= file_index_t(0) && index < fs.end_file());
		aux::rename_entry const& re = i->second;

		std::string ret;

		switch (re.mode)
		{
			case aux::rename_entry::full_path:
			{
				ret.reserve(save_path.size() + fs.name().size() + re.path.size() + 2);
				ret.assign(save_path);
				append_path(ret, fs.name());
				append_path(ret, re.path);
				break;
			}
			case aux::rename_entry::no_root_path:
			{
				ret.reserve(save_path.size() + re.path.size() + 1);
				ret.assign(save_path);
				append_path(ret, re.path);
				break;
			}
			case aux::rename_entry::absolute_path:
				ret.assign(re.path);
				break;
		}
		return ret;
	}

	aux::file_name_view renamed_files::file_name(
		file_storage const& fs, file_index_t const index) const
	{
		auto i = m_renamed_files.find(index);
		if (i == m_renamed_files.end()) return fs.file_name(index);

		TORRENT_ASSERT_PRECOND(index >= file_index_t(0) && index < fs.end_file());
		aux::rename_entry const& re = i->second;
		return rsplit_path(re.path).second;
	}

	bool renamed_files::file_absolute_path(file_storage const& fs, file_index_t const index) const
	{
		auto i = m_renamed_files.find(index);
		if (i == m_renamed_files.end())
		{
#if TORRENT_ABI_VERSION < 5
			return fs.file_absolute_path(index);
#else
			TORRENT_UNUSED(fs);
			return false;
#endif
		}
		return i->second.mode == aux::rename_entry::absolute_path;
	}

	void renamed_files::rename_file(file_storage const& fs, file_index_t index, std::string const& new_filename)
	{
		TORRENT_ASSERT(new_filename.size() > 0);
		if (new_filename.size() == 0)
			return;

		// pad files are managed internally and cannot be renamed
		if (fs.pad_file_at(index))
			return;

		if (is_complete(new_filename))
		{
			auto& entry = m_renamed_files[index];
			entry.path = new_filename;
			entry.mode = aux::rename_entry::absolute_path;
			return;
		}

		TORRENT_ASSERT(new_filename[0] != '/');

		// split the string into the leaf filename
		// and the branch path
		auto [root_path, path] = lsplit_path(new_filename);

		// if the new filename doesn't contain the name of the torrent itself,
		// we either moved the file out of the root path, or it's a single file
		// torrent anyway. This makes it only relative to the save path, and
		// not include the name of the torrent.
		if (root_path != fs.name())
		{
			auto& entry = m_renamed_files[index];
			entry.path = new_filename;
			entry.mode = aux::rename_entry::no_root_path;
			return;
		}

		auto& entry = m_renamed_files[index];
		entry.path = path;
		entry.mode = aux::rename_entry::full_path;
	}

	std::map<file_index_t, std::string> renamed_files::export_filenames(file_storage const& fs) const
	{
		std::map<file_index_t, std::string> ret;
		for (auto const& [idx, ent] : m_renamed_files)
		{
			if (ent.mode == aux::rename_entry::full_path)
			{
				// The stored path is the portion after the torrent name. Reconstruct
				// the full user-facing path so import_filenames() can round-trip it
				// correctly via rename_file().
				std::string p(fs.name());
				append_path(p, ent.path);
				ret.emplace(idx, std::move(p));
			}
			else
			{
				ret.emplace(idx, ent.path);
			}
		}
		return ret;
	}

	void renamed_files::import_filenames(file_storage const& fs, std::map<file_index_t, std::string> const& renamed_files)
	{
		for (auto const& f : renamed_files)
		{
			if (f.first < file_index_t(0) || f.first >= fs.end_file()) continue;
			rename_file(fs, file_index_t(f.first), f.second);
		}
	}

namespace aux {

file_name_view::file_name_view(std::uint64_t const pad_size)
	: m_name(std::to_string(pad_size))
{}

path_element::path_element(path_element const& pe)
	: parent(pe.parent)
	, name_len(pe.name_len)
	, depth(pe.depth)
	, name_ptr(pe.name_len == name_is_owned
			  ? aux::allocate_string_copy(pe.name_ptr ? string_view(pe.name_ptr) : string_view())
			  : pe.name_ptr)
{}

path_element::path_element(path_element&& pe) noexcept
	: parent(pe.parent)
	, name_len(pe.name_len)
	, depth(pe.depth)
	, name_ptr(pe.name_ptr)
{
	pe.name_len = 0;
	pe.name_ptr = nullptr;
}

path_element::~path_element()
{
	if (name_len == name_is_owned)
		delete[] name_ptr;
}

} // aux namespace

TORRENT_VERSION_NAMESPACE_4

#if TORRENT_ABI_VERSION < 4
void file_storage::rename_file_impl(
	file_index_t const index, std::string const& new_filename, error_code& ec)
{
	TORRENT_ASSERT_PRECOND(index >= file_index_t(0) && index < end_file());
	// pad files are managed internally and cannot be renamed
	if (m_files[index].pad_file)
		return;

	string_view leaf;
	aux::path_index_t dir;
	if (is_complete(new_filename))
	{
		// an absolute new_filename detaches the file from save_path,
		// see torrent_info::rename_file()
		dir = aux::path_element::path_is_absolute;
		leaf = new_filename;
	}
	else
	{
		dir = resolve_owned_directory(new_filename, leaf, ec);
		if (ec)
			return;
	}
	aux::file_entry& e = m_files[index];
	e.path_element_index = make_directory(dir, leaf, false);
}
#endif // TORRENT_ABI_VERSION

	file_index_t file_storage::file_index_at_offset(std::int64_t const offset) const
	{
		TORRENT_ASSERT_PRECOND(offset >= 0);
		TORRENT_ASSERT_PRECOND(offset < m_total_size);
		TORRENT_ASSERT(offset <= max_file_offset);
		// find the file iterator and file offset
		aux::file_entry target;
		target.offset = aux::numeric_cast<std::uint64_t>(offset);
		TORRENT_ASSERT(!compare_file_offset(target, m_files.front()));

		auto file_iter = std::upper_bound(
			m_files.begin(), m_files.end(), target, compare_file_offset);

		TORRENT_ASSERT(file_iter != m_files.begin());
		--file_iter;
		return file_index_t{int(file_iter - m_files.begin())};
	}

	file_index_t file_storage::file_index_at_piece(piece_index_t const piece) const
	{
		return file_index_at_offset(static_cast<int>(piece) * std::int64_t(piece_length()));
	}

	file_index_t file_storage::last_file_index_at_piece(piece_index_t const piece) const
	{
		return file_index_at_offset(static_cast<int>(piece) * std::int64_t(piece_length()) + piece_size(piece) - 1);
	}

	file_index_t file_storage::file_index_for_root(sha256_hash const& root_hash) const
	{
		// TODO: maybe it would be nice to have a better index here
		for (file_index_t const i : file_range())
		{
			if (root(i) == root_hash) return i;
		}
		return file_index_t{-1};
	}

	piece_index_t file_storage::piece_index_at_file(file_index_t f) const
	{
		return piece_index_t{aux::numeric_cast<int>(file_offset(f) / piece_length())};
	}

	piece_index_t file_storage::last_piece_index_at_file(file_index_t f) const
	{
		return piece_index_t{aux::numeric_cast<int>((file_offset(f) + file_size(f) - 1) / piece_length())};
	}

#if TORRENT_ABI_VERSION <= 2
	char const* file_storage::file_name_ptr(file_index_t const index) const
	{
		// pad files have no stored name, it's generated from their size on
		// demand, see file_name()
		if (m_files[index].pad_file)
			return nullptr;
		return path_element_name(m_path_elements[m_files[index].path_element_index]).data();
	}

	int file_storage::file_name_len(file_index_t const index) const
	{
		// pad files have no stored name (file_name_ptr() returns nullptr for
		// them)
		if (m_files[index].pad_file)
			return 0;
		aux::path_element const& e = m_path_elements[m_files[index].path_element_index];
		if (e.name_len == aux::path_element::name_is_owned)
			return -1;
		return int(e.name_len);
	}
#endif

	std::vector<file_slice> file_storage::map_block(piece_index_t const piece
		, std::int64_t const offset, std::int64_t size) const
	{
		TORRENT_ASSERT_PRECOND(piece >= piece_index_t{0});
		TORRENT_ASSERT_PRECOND(piece < end_piece());
		TORRENT_ASSERT_PRECOND(num_files() > 0);
		TORRENT_ASSERT_PRECOND(size >= 0);
		std::vector<file_slice> ret;

		if (m_files.empty()) return ret;

		// find the file iterator and file offset
		aux::file_entry target;
		TORRENT_ASSERT(max_file_offset / m_piece_length > static_cast<int>(piece));
		target.offset = aux::numeric_cast<std::uint64_t>(static_cast<int>(piece) * std::int64_t(m_piece_length) + offset);
		TORRENT_ASSERT_PRECOND(std::int64_t(target.offset) <= m_total_size - size);
		TORRENT_ASSERT(!compare_file_offset(target, m_files.front()));

		// in case the size is past the end, fix it up
		if (std::int64_t(target.offset) > m_total_size - size)
			size = m_total_size - std::int64_t(target.offset);

		auto file_iter = std::upper_bound(
			m_files.begin(), m_files.end(), target, compare_file_offset);

		TORRENT_ASSERT(file_iter != m_files.begin());
		--file_iter;

		std::int64_t file_offset = std::int64_t(target.offset) - std::int64_t(file_iter->offset);
		for (; size > 0; file_offset -= file_iter->size, ++file_iter)
		{
			TORRENT_ASSERT(file_iter != m_files.end());
			if (file_offset < std::int64_t(file_iter->size))
			{
				file_slice f{};
				f.file_index = file_index_t(int(file_iter - m_files.begin()));
				f.offset = file_offset;
				f.size = std::min(std::int64_t(file_iter->size) - file_offset, std::int64_t(size));
				TORRENT_ASSERT(f.size <= size);
				size -= f.size;
				file_offset += f.size;
				ret.push_back(f);
			}

			TORRENT_ASSERT(size >= 0);
		}
		return ret;
	}

	int file_storage::num_files() const noexcept
	{ return int(m_files.size()); }

	// returns the index of the one-past-end file in the file storage
	file_index_t file_storage::end_file() const noexcept
	{ return m_files.end_index(); }

	file_index_t file_storage::last_file() const noexcept
	{ return --m_files.end_index(); }

	index_range<file_index_t> file_storage::file_range() const noexcept
	{ return m_files.range(); }

	index_range<piece_index_t> file_storage::piece_range() const noexcept
	{ return {piece_index_t{0}, end_piece()}; }

	peer_request file_storage::map_file(file_index_t const file_index
		, std::int64_t const file_offset, int const size) const
	{
		TORRENT_ASSERT_PRECOND(file_index < end_file());
		TORRENT_ASSERT(m_num_pieces >= 0);

		peer_request ret{};
		if (file_index >= end_file())
		{
			ret.piece = end_piece();
			ret.start = 0;
			ret.length = 0;
			return ret;
		}

		std::int64_t const offset = file_offset + this->file_offset(file_index);

		if (offset >= total_size())
		{
			ret.piece = end_piece();
			ret.start = 0;
			ret.length = 0;
		}
		else
		{
			ret.piece = piece_index_t(int(offset / piece_length()));
			ret.start = int(offset % piece_length());
			ret.length = size;
			if (offset + size > total_size())
				ret.length = int(total_size() - offset);
		}
		return ret;
	}

#ifndef BOOST_NO_EXCEPTIONS
#if TORRENT_ABI_VERSION < 5
	void file_storage::add_file(std::string const& path, std::int64_t const file_size
		, file_flags_t const file_flags, std::time_t const mtime, string_view const symlink_path
		, char const* root_hash)
	{
		std::int32_t const root_hash_offset = root_hash_to_offset(root_hash);
		error_code ec;
		add_file_borrow_impl(
			ec, {}, path, file_size, file_flags, mtime, symlink_path, root_hash_offset);
		if (ec) aux::throw_ex<system_error>(ec);
	}
#endif

	void file_storage::add_file_borrow(string_view filename,
		std::string const& path,
		std::int64_t const file_size,
		file_flags_t const file_flags,
		std::int64_t const mtime,
		string_view const symlink_path,
		std::int32_t const root_hash_offset)
	{
		error_code ec;
		add_file_borrow_impl(
			ec, filename, path, file_size, file_flags, mtime, symlink_path, root_hash_offset);
		if (ec)
			aux::throw_ex<system_error>(ec);
	}
#endif // BOOST_NO_EXCEPTIONS

#if TORRENT_ABI_VERSION < 5
	void file_storage::add_file(error_code& ec, std::string const& path
		, std::int64_t const file_size, file_flags_t const file_flags, std::time_t const mtime
		, string_view symlink_path, char const* root_hash)
	{
		std::int32_t const root_hash_offset = root_hash_to_offset(root_hash);
		add_file_borrow_impl(
			ec, {}, path, file_size, file_flags, mtime, symlink_path, root_hash_offset);
	}
#endif

	void file_storage::add_file_borrow(error_code& ec,
		string_view filename,
		std::string const& path,
		std::int64_t const file_size,
		file_flags_t const file_flags,
		std::int64_t const mtime,
		string_view const symlink_path,
		std::int32_t const root_hash_offset)
	{
		add_file_borrow_impl(
			ec, filename, path, file_size, file_flags, mtime, symlink_path, root_hash_offset);
	}

	aux::path_index_t file_storage::add_file(error_code& ec,
		string_view filename,
		bool const borrow,
		aux::path_index_t const dir,
		std::int64_t const file_size,
		file_flags_t const file_flags,
		std::int64_t const mtime,
		std::int32_t const root_hash_offset)
	{
		return add_file_impl(
			ec, filename, borrow, dir, file_size, file_flags, mtime, root_hash_offset);
	}

	std::int32_t file_storage::root_hash_to_offset(char const* root_hash) const
	{
		return root_hash ? aux::numeric_cast<std::int32_t>(root_hash - m_info_section)
						 : no_root_hash;
	}

	// backwards-compatibility wrapper: the deprecated add_file() overloads
	// take a joined path string rather than a path_index_t. This layer
	// never borrows, every path_element it creates is heap-copied, and sits
	// entirely on top of add_file_impl(), the same primitive
	// make_directory()/add_file() use.
	void file_storage::add_file_borrow_impl(error_code& ec,
		string_view filename,
		std::string const& path,
		std::int64_t const file_size,
		file_flags_t const file_flags,
		std::int64_t const mtime,
		string_view const symlink_path,
		std::int32_t const root_hash_offset)
	{
		TORRENT_ASSERT_PRECOND(!is_complete(filename));

		if (!filename.empty())
		{
			if (filename.size() >= (1 << 12))
			{
				ec = make_error_code(boost::system::errc::filename_too_long);
				return;
			}
		}
		else if (lt::filename(path).size() >= (1 << 12))
		{
			ec = make_error_code(boost::system::errc::filename_too_long);
			return;
		}

		if (!has_parent_path(path))
		{
			// you have already added at least one file with a
			// path to the file (branch_path), which means that
			// all the other files need to be in the same top
			// directory as the first file.
			TORRENT_ASSERT_PRECOND(m_files.empty());
			m_name = path;
		}
		else
		{
			if (m_files.empty())
				m_name = lsplit_path(path).first;
		}

		aux::path_index_t dir;
		string_view leaf;
		if (is_complete(path))
		{
			dir = aux::path_element::path_is_absolute;
			leaf = path;
		}
		else
		{
			dir = resolve_owned_directory(path, leaf, ec);
			if (ec)
				return;
		}
		if (!filename.empty())
			leaf = filename;

		// a non-empty symlink_path is by itself sufficient to treat this as
		// a symlink, independent of flag_symlink, so a caller-supplied
		// target is never silently discarded
		if ((file_flags & file_storage::flag_symlink) || !symlink_path.empty())
		{
			add_symlink(ec, leaf, false, dir, file_flags, mtime, symlink_path);
		}
		else
		{
			add_file_impl(ec, leaf, false, dir, file_size, file_flags, mtime, root_hash_offset);
		}
	}

	aux::path_index_t file_storage::add_file_impl(error_code& ec,
		string_view filename,
		bool const borrow,
		aux::path_index_t const dir,
		std::int64_t const file_size,
		file_flags_t const file_flags,
		std::int64_t const mtime,
		std::int32_t const root_hash_offset)
	{
		TORRENT_ASSERT_PRECOND(file_size >= 0);
		TORRENT_ASSERT_PRECOND(!(file_flags & file_storage::flag_symlink));
		// filename is stored verbatim as this element's leaf text, so it may
		// only look like a complete path when dir says so (i.e. it really is
		// the whole path, see file_absolute_path())
		TORRENT_ASSERT_PRECOND(
			dir == aux::path_element::path_is_absolute || !is_complete(filename));

		if (file_size > max_file_size)
		{
			ec = make_error_code(boost::system::errc::file_too_large);
			return {};
		}

		if (max_file_offset - m_total_size < file_size)
		{
			ec = make_error_code(errors::torrent_invalid_length);
			return {};
		}

		bool const is_pad_file = bool(file_flags & file_storage::flag_pad_file);

		if (!is_pad_file && filename.size() >= (1 << 12))
		{
			ec = make_error_code(boost::system::errc::filename_too_long);
			return {};
		}

		// files without a root_hash are assumed to be v1, except symlinks
		// (which never reach this primitive, see add_symlink()). They
		// don't have a root hash and can be either v1 or v2
		if (file_size > 0)
		{
			bool const v2 = (root_hash_offset != no_root_hash);
			// This condition is true of all files we've added so far have been
			// symlinks. i.e. this is the first "real" file we're adding.
			// or if m_total_size == 0, all files we've added so far have been
			// empty (which also are are v1/v2-ambigous)
			if (m_files.size() == std::size_t(m_num_symlinks) || m_total_size == 0)
			{
				m_v2 = v2;
			}
			else if (m_v2 != v2)
			{
				// you cannot mix v1 and v2 files when building torrent_storage. Either
				// all files are v1 or all files are v2
				ec = m_v2 ? make_error_code(errors::torrent_missing_pieces_root)
						  : make_error_code(errors::torrent_inconsistent_files);
				return {};
			}
		}

		// only the deprecated add_file_borrow() path can build a ``dir``
		// this deep, it has no depth limit of its own
		if (!is_pad_file && dir < m_path_elements.end_index()
			&& m_path_elements[dir].depth >= std::numeric_limits<std::uint16_t>::max())
		{
			ec = errors::torrent_directory_too_deep;
			return {};
		}

		m_files.emplace_back();
		aux::file_entry& e = m_files.back();

		if (is_pad_file)
		{
			// pad files never store a name, whatever leaf name they were
			// given is discarded; it's synthesized from their size on
			// demand instead, so dir refers directly to their (parent)
			// directory
			e.path_element_index = dir;
		}
		else
		{
			e.path_element_index = make_directory(dir, filename, borrow);
		}

		e.size = aux::numeric_cast<std::uint64_t>(file_size);
		e.offset = aux::numeric_cast<std::uint64_t>(m_total_size);
		e.pad_file = is_pad_file;
		e.hidden_attribute = bool(file_flags & file_storage::flag_hidden);
		e.executable_attribute = bool(file_flags & file_storage::flag_executable);
		TORRENT_ASSERT(root_hash_offset == no_root_hash || m_info_section != nullptr);
		e.root_offset = root_hash_offset;

		set_last_file_mtime(mtime);

		aux::path_index_t const ret = e.path_element_index;

		m_total_size += e.size;

		if (!is_pad_file)
			m_size_on_disk += e.size;

		TORRENT_ASSERT(m_total_size >= m_size_on_disk);

		// when making v2 torrents, pad the end of each file (if necessary) to
		// ensure it ends on a piece boundary.
		// we do this at the end of files rather in-front of files to conform to
		// the BEP52 reference implementation
		if (m_v2 && (m_total_size % piece_length()) != 0)
		{
			auto const pad_size = piece_length() - (m_total_size % piece_length());
			TORRENT_ASSERT(int(pad_size) != piece_length());
			TORRENT_ASSERT(int(pad_size) > 0);
			if (m_total_size > max_file_offset - pad_size)
			{
				ec = make_error_code(errors::torrent_invalid_length);
				return ret;
			}

			m_files.emplace_back();
			// e is invalid from here down!
			auto& pad = m_files.back();
			pad.size = static_cast<std::uint64_t>(pad_size);
			TORRENT_ASSERT(m_total_size <= max_file_offset);
			TORRENT_ASSERT(m_total_size > 0);
			pad.offset = static_cast<std::uint64_t>(m_total_size);
			pad.path_element_index = aux::path_element::pad_directory;
			pad.pad_file = true;
			m_total_size += pad_size;
		}

		return ret;
	}

	aux::path_index_t file_storage::add_symlink(error_code& ec,
		string_view const filename,
		bool const borrow,
		aux::path_index_t const dir,
		file_flags_t const file_flags,
		std::int64_t const mtime)
	{
		// unlike add_file_impl(), a symlink's own path is never allowed to
		// be absolute: a self-pointing symlink (see below) would then have
		// no relative representation to fall back on
		TORRENT_ASSERT_PRECOND(
			dir != aux::path_element::path_is_absolute && !is_complete(filename));

		if (filename.size() >= (1 << 12))
		{
			ec = make_error_code(boost::system::errc::filename_too_long);
			return {};
		}

		// symlinks are always empty files, so none of add_file_impl()'s
		// size validation, v1/v2 root-hash ambiguity handling, or
		// piece-boundary end-of-file padding applies to them
		aux::path_index_t const leaf = make_directory(dir, filename, borrow);

		// deferred placeholder: self-point until resolved later by the
		// caller via internal_set_symlink_target(), once the rest of the
		// file list/tree has been parsed
		m_files.emplace_back(aux::file_entry{
			.offset = aux::numeric_cast<std::uint64_t>(m_total_size),
			.hidden_attribute = bool(file_flags & file_storage::flag_hidden),
			.executable_attribute = bool(file_flags & file_storage::flag_executable),
			.symlink_attribute = true,
			.path_element_index = leaf,
			.symlink_element_index = leaf,
		});

		set_last_file_mtime(mtime);

		++m_num_symlinks;

		return leaf;
	}

	aux::path_index_t file_storage::add_symlink(error_code& ec,
		string_view const filename,
		bool const borrow,
		aux::path_index_t const dir,
		file_flags_t const file_flags,
		std::int64_t const mtime,
		string_view const target)
	{
		// unlike add_file_impl(), a symlink's own path is never allowed to
		// be absolute: a self-pointing symlink (see below) would then have
		// no relative representation to fall back on
		TORRENT_ASSERT_PRECOND(
			dir != aux::path_element::path_is_absolute && !is_complete(filename));

		if (filename.size() >= (1 << 12))
		{
			ec = make_error_code(boost::system::errc::filename_too_long);
			return {};
		}

		m_files.emplace_back();
		aux::file_entry& e = m_files.back();

		// symlinks are always empty files, so none of add_file_impl()'s
		// size validation, v1/v2 root-hash ambiguity handling, or
		// piece-boundary end-of-file padding applies to them
		e.path_element_index = make_directory(dir, filename, borrow);
		e.size = 0;
		e.offset = aux::numeric_cast<std::uint64_t>(m_total_size);
		e.hidden_attribute = bool(file_flags & file_storage::flag_hidden);
		e.executable_attribute = bool(file_flags & file_storage::flag_executable);
		e.symlink_attribute = true;

		// an absolute target is never valid, see add_file()
		TORRENT_ASSERT_PRECOND(target.empty() || !is_complete(target));

		// a non-empty target is relative to the torrent root (like a real
		// file's path, see add_file()), so name() is prepended when
		// reconstructing it (see symlink()); it's kept as a single,
		// unsplit path_element rather than one per directory level, since
		// unlike a real file's path, nothing ever needs to address or
		// dedupe individual components of a symlink target
		e.symlink_element_index = (!target.empty() && !is_complete(target))
			? make_directory(aux::path_element::torrent_root, target, false)
			: e.path_element_index;

		set_last_file_mtime(mtime);

		++m_num_symlinks;

		return e.path_element_index;
	}

	void file_storage::set_last_file_mtime(std::int64_t const mtime)
	{
		if (!mtime)
			return;
		if (m_mtime.size() < m_files.size())
			m_mtime.resize(m_files.size());
		m_mtime[last_file()] = std::time_t(mtime);
	}

	void file_storage::internal_set_symlink_target(
		file_index_t const index, aux::path_index_t const target)
	{
		TORRENT_ASSERT_PRECOND(index >= file_index_t(0) && index < end_file());
		aux::file_entry& e = m_files[index];
		TORRENT_ASSERT(e.symlink_attribute);
		e.symlink_element_index = target;
	}

	// this is here for backwards compatibility with hybrid torrents created
	// with libtorrent 2.0.0-2.0.7, which would not add tail-padding
	void file_storage::remove_tail_padding()
	{
		file_index_t f = end_file();
		while (f > file_index_t{0})
		{
			--f;
			// empty files and symlinks are skipped
			if (file_size(f) == 0) continue;
			if (pad_file_at(f))
			{
				m_total_size -= file_size(f);
				m_files.erase(m_files.begin() + int(f));
				while (f < end_file())
				{
					m_files[f].offset = static_cast<std::uint64_t>(m_total_size);
					TORRENT_ASSERT(m_files[f].size == 0);
					++f;
				}
			}
			// if the last non-empty file isn't a pad file, don't do anything
			return;
		}
		// nothing found
	}

#if TORRENT_ABI_VERSION < 4
	sha1_hash file_storage::hash(file_index_t const index) const
	{
		TORRENT_ASSERT_PRECOND(index >= file_index_t{} && index < end_file());
		TORRENT_UNUSED(index);
		return {};
	}
#endif

	sha256_hash file_storage::root(file_index_t const index) const
	{
		TORRENT_ASSERT_PRECOND(index >= file_index_t{} && index < end_file());
		char const* const p = root_ptr(index);
		if (p == nullptr)
			return {};
		return sha256_hash(p);
	}

	char const* file_storage::root_ptr(file_index_t const index) const
	{
		TORRENT_ASSERT_PRECOND(index >= file_index_t{} && index < end_file());
		aux::file_entry const& fe = m_files[index];
		// root_offset aliases symlink_element_index; a symlink never has a
		// merkle root
		if (fe.symlink_attribute)
			return nullptr;
		std::int32_t const off = fe.root_offset;
		if (off == no_root_hash)
			return nullptr;
		TORRENT_ASSERT(off >= 0);
		TORRENT_ASSERT(m_info_section != nullptr);
		return m_info_section + off;
	}

	std::string file_storage::symlink(file_index_t const index) const
	{
		TORRENT_ASSERT_PRECOND(index >= file_index_t{} && index < end_file());
		aux::file_entry const& fe = m_files[index];
		if (!fe.symlink_attribute)
			return {};

		// a symlink's own path is never absolute, see add_symlink()
		TORRENT_ASSERT(m_path_elements[fe.symlink_element_index].parent
			!= aux::path_element::path_is_absolute);

		std::string path;
		aux::path_index_t const root = reconstruct_path(fe.symlink_element_index, path);

		std::string ret;
		// same rule file_path() uses: the target isn't prepended with
		// name() when it doesn't root there (a single-file torrent's lone
		// file, or a path that doesn't share the torrent's own name)
		if (root != aux::path_element::no_root_dir)
			ret = m_name;
		append_path(ret, path);
		return ret;
	}

#if TORRENT_ABI_VERSION < 4
	std::string file_storage::internal_symlink(file_index_t const index) const
	{
		TORRENT_ASSERT_PRECOND(index >= file_index_t{} && index < end_file());
		aux::file_entry const& fe = m_files[index];
		TORRENT_ASSERT(fe.symlink_attribute);

		// a symlink's own path is never absolute, see add_symlink()
		TORRENT_ASSERT(m_path_elements[fe.symlink_element_index].parent
			!= aux::path_element::path_is_absolute);

		std::string ret;
		reconstruct_path(fe.symlink_element_index, ret);
		return ret;
	}
#endif

	std::time_t file_storage::mtime(file_index_t const index) const
	{
		TORRENT_ASSERT_PRECOND(index >= file_index_t{} && index < end_file());
		if (index >= m_mtime.end_index()) return 0;
		return m_mtime[index];
	}

namespace {

		template <class CRC>
		void process_string_lowercase(CRC& crc, string_view str)
		{
			for (char const c : str)
				crc.process_byte(aux::to_lower(c) & 0xff);
		}
	}

	file_storage::element_hashes file_storage::compute_element_hashes() const
	{
		using crc32_t = boost::crc_optimal<32, 0x1EDC6F41, 0xFFFFFFFF, 0xFFFFFFFF, true, true>;

		crc32_t root_crc;
		process_string_lowercase(root_crc, m_name);

		// a path_element's parent always has a lower index than the element
		// itself (a parent must already exist before a child can reference
		// it), so a single forward pass lets each element extend its
		// parent's already-computed boundary crc (the crc of its full path,
		// rooted at m_name, with no trailing separator). live_crcs holds
		// the (still extendable) crc objects, so a child can keep building
		// on its parent's; the returned element_hashes::crc only needs the
		// finalized checksum of each.
		aux::vector<crc32_t, aux::path_index_t> live_crcs(m_path_elements.size(), root_crc);
		aux::vector<std::uint32_t, aux::path_index_t> crcs(m_path_elements.size(), std::uint32_t());
		aux::vector<bool, aux::path_index_t> is_dir(m_path_elements.size(), false);

		for (auto const idx : m_path_elements.range())
		{
			aux::path_element const& e = m_path_elements[idx];
			bool const top_level = is_root_path_index(e.parent);

			crc32_t crc;
			if (e.parent == aux::path_element::no_root_dir
				|| e.parent == aux::path_element::path_is_absolute)
			{
				// neither roots at m_name: a no-root-dir chain starts from
				// an empty path, and an absolute path is never split into
				// components (its lone element's own text is the whole
				// path), so append_path() never prepends anything or
				// inserts a separator before either one
				process_string_lowercase(crc, path_element_name(e));
			}
			else
			{
				crc = top_level ? root_crc : live_crcs[e.parent];
				crc.process_byte(TORRENT_SEPARATOR);
				process_string_lowercase(crc, path_element_name(e));
			}
			live_crcs[idx] = crc;
			crcs[idx] = crc.checksum();

			if (!top_level)
				is_dir[e.parent] = true;
		}

		// pad files are deliberately not accounted for here: they never
		// touch disk, so a naming collision involving one, or involving a
		// directory only ever referenced by one, is never a real conflict
		// (see resolve_duplicate_filenames()).
		return {std::move(crcs), std::move(is_dir)};
	}

	std::uint32_t file_storage::file_hash(element_hashes const& eh, file_index_t const idx) const
	{
		// a pad file never touches disk, so a naming collision involving
		// one is never a real conflict (see resolve_duplicate_filenames()),
		// and is therefore never hashed here; its path_element_index can
		// even be one of the path_element sentinels rather than a real
		// m_path_elements index (e.g. the pad_directory sentinel, or
		// torrent_root for one living directly under the root), so this
		// would be more than a lookup away from eh.crc anyway.
		TORRENT_ASSERT_PRECOND(!m_files[idx].pad_file);
		return eh.crc[m_files[idx].path_element_index];
	}

	std::optional<file_storage::element_hashes> file_storage::has_duplicate_filenames() const
	{
		element_hashes eh = compute_element_hashes();

		// seed with every directory's hash. Two different directories are
		// allowed to collide with each other (see resolve_duplicate_filenames.cpp
		// for why that's fine, and legitimately not limited to add_file()'s
		// lack of dedup), so this only needs the hash values, not a
		// multimap of them; only a *file* landing on an already-seen hash
		// (directory or file) is a real collision.
		std::unordered_set<std::uint32_t> seen;
		seen.reserve(m_path_elements.size() + m_files.size());

		for (auto const idx : m_path_elements.range())
			if (eh.is_dir[idx])
				seen.insert(eh.crc[idx]);

		for (auto const i : file_range())
		{
			// pad files are allowed to collide with anything, see
			// file_hash()
			if (m_files[i].pad_file)
				continue;
			if (!seen.insert(file_hash(eh, i)).second)
				return eh;
		}
		return std::nullopt;
	}

	std::string file_storage::internal_directory_path(aux::path_index_t const index) const
	{
		std::string path;
		aux::path_index_t const root = reconstruct_path(index, path);

		std::string ret;
		if (root != aux::path_element::no_root_dir)
			ret = m_name;
		append_path(ret, path);
		return ret;
	}

	std::string file_storage::file_path(
		file_index_t const index, std::string const& save_path) const
	{
		TORRENT_ASSERT_PRECOND(index >= file_index_t(0) && index < end_file());
		aux::file_entry const& fe = m_files[index];

		if (!fe.pad_file
			&& m_path_elements[fe.path_element_index].parent == aux::path_element::path_is_absolute)
			return std::string(path_element_name(m_path_elements[fe.path_element_index]));

		std::string path;
		aux::path_index_t const root = reconstruct_path(fe.path_element_index, path);

		std::string ret = save_path;
		// single-file torrents' lone file, and paths that don't share the
		// torrent's own name() don't have name() prepended. pad files have
		// name() baked into path already, by reconstruct_path()
		if (root != aux::path_element::no_root_dir)
			append_path(ret, m_name);
		append_path(ret, path);
		if (fe.pad_file)
			append_path(ret, std::to_string(fe.size));

		// a single return statement, just to make NRVO more likely to kick in
		return ret;
	}

	aux::file_name_view file_storage::file_name(file_index_t const index) const
	{
		TORRENT_ASSERT_PRECOND(index >= file_index_t(0) && index < end_file());
		aux::file_entry const& fe = m_files[index];
		if (fe.pad_file)
			return aux::file_name_view(fe.size);
		return {path_element_name(m_path_elements[fe.path_element_index])};
	}

	std::int64_t file_storage::file_size(file_index_t const index) const
	{
		TORRENT_ASSERT_PRECOND(index >= file_index_t(0) && index < end_file());
		return m_files[index].size;
	}

	bool file_storage::pad_file_at(file_index_t const index) const
	{
		TORRENT_ASSERT_PRECOND(index >= file_index_t(0) && index < end_file());
		return m_files[index].pad_file;
	}

	std::int64_t file_storage::file_offset(file_index_t const index) const
	{
		TORRENT_ASSERT_PRECOND(index >= file_index_t(0) && index < end_file());
		return std::int64_t(m_files[index].offset);
	}

	int file_storage::file_num_pieces(file_index_t const index) const
	{
		TORRENT_ASSERT_PRECOND(index >= file_index_t(0) && index < end_file());
		TORRENT_ASSERT_PRECOND(m_piece_length > 0);
		auto const& f = m_files[index];

		if (f.size == 0) return 0;

		// this function only works for v2 torrents, where files are guaranteed to
		// be aligned to pieces
		TORRENT_ASSERT(f.pad_file == false);
		TORRENT_ASSERT((static_cast<std::int64_t>(f.offset) % m_piece_length) == 0);
		return aux::numeric_cast<int>(
			(static_cast<std::int64_t>(f.size) + m_piece_length - 1) / m_piece_length);
	}

	index_range<piece_index_t::diff_type> file_storage::file_piece_range(file_index_t const file) const
	{
		return {piece_index_t::diff_type{0}, piece_index_t::diff_type{file_num_pieces(file)}};
	}

	int file_storage::file_num_blocks(file_index_t const index) const
	{
		TORRENT_ASSERT_PRECOND(index >= file_index_t(0) && index < end_file());
		TORRENT_ASSERT_PRECOND(m_piece_length > 0);
		auto const& f = m_files[index];

		if (f.size == 0) return 0;

		// this function only works for v2 torrents, where files are guaranteed to
		// be aligned to pieces
		TORRENT_ASSERT(f.pad_file == false);
		TORRENT_ASSERT((static_cast<std::int64_t>(f.offset) % m_piece_length) == 0);
		return int((f.size + default_block_size - 1) / default_block_size);
	}

	int file_storage::file_first_piece_node(file_index_t index) const
	{
		TORRENT_ASSERT_PRECOND(index >= file_index_t(0) && index < end_file());
		TORRENT_ASSERT_PRECOND(m_piece_length > 0);
		int const piece_layer_size = merkle_num_leafs(file_num_pieces(index));
		return merkle_num_nodes(piece_layer_size) - piece_layer_size;
	}

	int file_storage::file_first_block_node(file_index_t index) const
	{
		TORRENT_ASSERT_PRECOND(index >= file_index_t(0) && index < end_file());
		TORRENT_ASSERT_PRECOND(m_piece_length > 0);
		int const leaf_layer_size = merkle_num_leafs(file_num_blocks(index));
		return merkle_num_nodes(leaf_layer_size) - leaf_layer_size;
	}

	file_flags_t file_storage::file_flags(file_index_t const index) const
	{
		TORRENT_ASSERT_PRECOND(index >= file_index_t(0) && index < end_file());
		aux::file_entry const& fe = m_files[index];
		return (fe.pad_file ? file_storage::flag_pad_file : file_flags_t{})
			| (fe.hidden_attribute ? file_storage::flag_hidden : file_flags_t{})
			| (fe.executable_attribute ? file_storage::flag_executable : file_flags_t{})
			| (fe.symlink_attribute ? file_storage::flag_symlink : file_flags_t{});
	}

#if TORRENT_ABI_VERSION < 5
	bool file_storage::file_absolute_path(file_index_t const index) const
	{
		TORRENT_ASSERT_PRECOND(index >= file_index_t(0) && index < end_file());
		aux::file_entry const& fe = m_files[index];
		// pad files are always synthetic, relative entries
		if (fe.pad_file)
			return false;
		return m_path_elements[fe.path_element_index].parent == aux::path_element::path_is_absolute;
	}
#endif

#if TORRENT_ABI_VERSION == 1
	file_index_t file_storage::index_of(aux::file_entry const& fe) const
	{
		auto const index = file_index_t(int(&fe - &m_files.front()));
		TORRENT_ASSERT_PRECOND(index >= file_index_t{} && index < end_file());
		return index;
	}

	sha1_hash file_storage::hash(aux::file_entry const&) const { return sha1_hash(nullptr); }

	std::string file_storage::symlink(aux::file_entry const& fe) const
	{
		TORRENT_ASSERT_PRECOND(fe.symlink_attribute);

		// a symlink's own path is never absolute, see add_symlink()
		TORRENT_ASSERT(m_path_elements[fe.symlink_element_index].parent
			!= aux::path_element::path_is_absolute);

		std::string ret;
		reconstruct_path(fe.symlink_element_index, ret);
		return ret;
	}

	std::time_t file_storage::mtime(aux::file_entry const& fe) const
	{
		auto const index = index_of(fe);
		if (index >= m_mtime.end_index())
			return 0;
		return m_mtime[index];
	}

	int file_storage::file_index(aux::file_entry const& fe) const
	{
		return static_cast<int>(index_of(fe));
	}

	std::string file_storage::file_path(aux::file_entry const& fe
		, std::string const& save_path) const
	{
		return file_path(index_of(fe), save_path);
	}

	std::string file_storage::file_name(aux::file_entry const& fe) const
	{
		return std::string(file_name(index_of(fe)));
	}

	std::int64_t file_storage::file_size(aux::file_entry const& fe) const
	{
		return fe.size;
	}

	bool file_storage::pad_file_at(aux::file_entry const& fe) const
	{
		return fe.pad_file;
	}

	std::int64_t file_storage::file_offset(aux::file_entry const& fe) const
	{
		return std::int64_t(fe.offset);
	}
#endif // TORRENT_ABI_VERSION

	void file_storage::swap(file_storage& ti) noexcept
	{
		using std::swap;
		swap(ti.m_files, m_files);
		swap(ti.m_num_symlinks, m_num_symlinks);
		swap(ti.m_mtime, m_mtime);
		swap(ti.m_path_elements, m_path_elements);
		swap(ti.m_name, m_name);
		swap(ti.m_total_size, m_total_size);
		swap(ti.m_size_on_disk, m_size_on_disk);
		swap(ti.m_info_section, m_info_section);
		swap(ti.m_num_pieces, m_num_pieces);
		swap(ti.m_piece_length, m_piece_length);
		swap(ti.m_v2, m_v2);
	}

#if TORRENT_ABI_VERSION < 4
	// using file_storage when creating torrents is deprecated. Use a vector of
	// create_file_entry with create_torrent instead.
	void file_storage::canonicalize()
	{
		canonicalize_impl(false);
	}

	void file_storage::canonicalize_impl(bool const backwards_compatible)
	{
		TORRENT_ASSERT(piece_length() >= 16 * 1024);

		// use this vector to track the new ordering of files
		// this allows the use of STL algorithms despite them
		// not supporting a custom swap functor
		aux::vector<file_index_t, file_index_t> new_order(end_file());
		for (auto i : file_range())
			new_order[i] = i;

		// remove any existing pad files
		{
			auto pad_begin = std::partition(new_order.begin(), new_order.end()
				, [this](file_index_t i) { return !m_files[i].pad_file; });
			new_order.erase(pad_begin, new_order.end());
		}

		// precompute each candidate file's own directory path once, so the
		// sort comparator below is a plain string compare instead of
		// reconstructing both sides' directory path on every comparison
		aux::vector<std::string, file_index_t> dir_path(end_file());
		for (auto const i : new_order)
			reconstruct_path(m_path_elements[m_files[i].path_element_index].parent, dir_path[i]);

		// sort files by path/name
		std::sort(
			new_order.begin(), new_order.end(), [this, &dir_path](file_index_t l, file_index_t r) {
				auto const& lf = m_files[l];
				auto const& rf = m_files[r];
				aux::path_element const& le = m_path_elements[lf.path_element_index];
				aux::path_element const& re = m_path_elements[rf.path_element_index];
				if (lf.path_element_index != rf.path_element_index)
				{
					int const ret = path_compare(
						dir_path[l], path_element_name(le), dir_path[r], path_element_name(re));
					if (ret != 0)
						return ret < 0;
				}
				return path_element_name(le) < path_element_name(re);
			});

		aux::vector<aux::file_entry, file_index_t> new_files;
		aux::vector<std::time_t, file_index_t> new_mtime;

		// reserve enough space for the worst case after padding
		new_files.reserve(new_order.size() * 2 - 1);
		if (!m_mtime.empty())
			new_mtime.reserve(new_order.size() * 2 - 1);

		// re-compute offsets and insert pad files as necessary
		std::int64_t off = 0;
		std::int64_t on_disk = 0;

		auto add_pad_file = [&](file_index_t const i) {
			if ((off % piece_length()) != 0 && m_files[i].size > 0)
			{
				auto const pad_size = piece_length() - (off % piece_length());
				TORRENT_ASSERT(pad_size < piece_length());
				TORRENT_ASSERT(pad_size > 0);
				new_files.emplace_back();
				auto& pad = new_files.back();
				pad.size = static_cast<std::uint64_t>(pad_size);
				pad.offset = static_cast<std::uint64_t>(off);
				off += pad_size;
				pad.path_element_index = aux::path_element::pad_directory;
				pad.pad_file = true;

				if (!m_mtime.empty())
					new_mtime.push_back(0);
			}
		};

		for (file_index_t i : new_order)
		{
			if (backwards_compatible)
				add_pad_file(i);

			TORRENT_ASSERT(!m_files[i].pad_file);
			new_files.emplace_back(std::move(m_files[i]));

			if (i < m_mtime.end_index())
				new_mtime.push_back(m_mtime[i]);
			else if (!m_mtime.empty())
				new_mtime.push_back(0);

			auto& file = new_files.back();
			TORRENT_ASSERT(off < max_file_offset - static_cast<std::int64_t>(file.size));
			file.offset = static_cast<std::uint64_t>(off);
			off += file.size;
			on_disk += file.size;

			// we don't pad single-file torrents. That would make it impossible
			// to have single-file hybrid torrents.
			if (!backwards_compatible && num_files() > 1)
				add_pad_file(i);
		}

		m_files = std::move(new_files);
		m_mtime = std::move(new_mtime);

		m_total_size = off;
		m_size_on_disk = on_disk;
		TORRENT_ASSERT(m_total_size >= m_size_on_disk);
	}
#endif

TORRENT_VERSION_NAMESPACE_4_END

namespace aux {

	bool files_compatible(file_storage const& lhs, file_storage const& rhs)
	{
		if (lhs.num_files() != rhs.num_files())
			return false;

		if (lhs.total_size() != rhs.total_size())
			return false;

		if (lhs.piece_length() != rhs.piece_length())
			return false;

		// for compatibility, only non-empty, non-pad files matter (symlinks
		// are always empty, but always relevant, since their target still
		// needs to match). those files all need to match in index, name,
		// size and offset
		for (file_index_t i : lhs.file_range())
		{
			bool const lhs_relevant = !lhs.pad_file_at(i)
				&& (lhs.file_size(i) > 0 || (lhs.file_flags(i) & file_storage::flag_symlink));
			bool const rhs_relevant = !rhs.pad_file_at(i)
				&& (rhs.file_size(i) > 0 || (rhs.file_flags(i) & file_storage::flag_symlink));

			if (lhs_relevant != rhs_relevant)
				return false;

			if (!lhs_relevant) continue;

			// we deliberately ignore file attributes like "hidden",
			// "executable" and mtime here. It's not critical they match
			if (lhs.pad_file_at(i) != rhs.pad_file_at(i)
				|| lhs.file_size(i) != rhs.file_size(i)
				|| lhs.file_path(i) != rhs.file_path(i)
				|| lhs.file_offset(i) != rhs.file_offset(i))
			{
				return false;
			}

			if ((lhs.file_flags(i) & file_storage::flag_symlink)
				&& lhs.symlink(i) != rhs.symlink(i))
			{
				return false;
			}
		}
		return true;
	}

	template <typename FileStorage>
	index_range<piece_index_t> file_piece_range_exclusive(
		FileStorage const& fs, file_index_t const file)
	{
		peer_request const range = fs.map_file(file, 0, 1);
		std::int64_t const file_size = fs.file_size(file);
		std::int64_t const piece_size = fs.piece_length();
		piece_index_t const begin_piece = range.start == 0 ? range.piece : piece_index_t(static_cast<int>(range.piece) + 1);
		// the last piece is potentially smaller than the other pieces, so the
		// generic logic doesn't really work. If this file is the last file, the
		// last piece doesn't overlap with any other file and it's entirely
		// contained within the last file.
		piece_index_t const end_piece = (file == file_index_t(fs.num_files() - 1))
			? piece_index_t(fs.num_pieces())
			: piece_index_t(int((static_cast<int>(range.piece) * piece_size + range.start + file_size + 1) / piece_size));
		return {begin_piece, end_piece};
	}

	template <typename FileStorage>
	index_range<piece_index_t> file_piece_range_inclusive(
		FileStorage const& fs, file_index_t const file)
	{
		peer_request const range = fs.map_file(file, 0, 1);
		std::int64_t const file_size = fs.file_size(file);
		std::int64_t const piece_size = fs.piece_length();
		piece_index_t const end_piece = piece_index_t(int((static_cast<int>(range.piece)
			* piece_size + range.start + file_size - 1) / piece_size + 1));
		return {range.piece, end_piece};
	}

	// explicit instantiations for the file-layout types these are used with
	template TORRENT_EXTRA_EXPORT index_range<piece_index_t> file_piece_range_exclusive(
		file_storage const&, file_index_t);
	template TORRENT_EXTRA_EXPORT index_range<piece_index_t> file_piece_range_inclusive(
		file_storage const&, file_index_t);
	template TORRENT_EXTRA_EXPORT index_range<piece_index_t> file_piece_range_inclusive(
		filenames const&, file_index_t);

	int calc_num_pieces(file_storage const& fs)
	{
		return aux::numeric_cast<int>(
			(fs.total_size() + fs.piece_length() - 1) / fs.piece_length());
	}

	} // namespace aux
}
