/*

Copyright (c) 2003-2018, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#include "libtorrent/file_storage.hpp"
#include "libtorrent/string_util.hpp" // for allocate_string_copy
#include "libtorrent/utf8.hpp"
#include "libtorrent/index_range.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/crc.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include <cstdio>
#include <algorithm>
#include <functional>
#include <set>
#include <atomic>

#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
#define TORRENT_SEPARATOR '\\'
#else
#define TORRENT_SEPARATOR '/'
#endif

using namespace std::placeholders;

namespace libtorrent {

	constexpr file_flags_t file_storage::flag_pad_file;
	constexpr file_flags_t file_storage::flag_hidden;
	constexpr file_flags_t file_storage::flag_executable;
	constexpr file_flags_t file_storage::flag_symlink;

#if TORRENT_ABI_VERSION == 1
	constexpr file_flags_t file_storage::pad_file;
	constexpr file_flags_t file_storage::attribute_hidden;
	constexpr file_flags_t file_storage::attribute_executable;
	constexpr file_flags_t file_storage::attribute_symlink;
#endif

	file_storage::file_storage()
		: m_piece_length(0)
		, m_num_pieces(0)
		, m_total_size(0)
	{}

	file_storage::~file_storage() = default;

	// even though this copy constructor and the copy assignment
	// operator are identical to what the compiler would have
	// generated, they are put here to explicitly make them part
	// of libtorrent and properly exported by the .dll.
	file_storage::file_storage(file_storage const&) = default;
	file_storage& file_storage::operator=(file_storage const&) = default;
	file_storage::file_storage(file_storage&&) noexcept = default;

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

namespace {

	bool compare_file_offset(internal_file_entry const& lhs
		, internal_file_entry const& rhs)
	{
		return lhs.offset < rhs.offset;
	}

}

	// path is supposed to include the name of the torrent itself.
	// or an absolute path, to move a file outside of the download directory
	void file_storage::update_path_index(internal_file_entry& e
		, std::string const& path, bool const set_name)
	{
		if (is_complete(path))
		{
			TORRENT_ASSERT(set_name);
			e.set_name(path);
			e.path_index = -2;
			return;
		}

		TORRENT_ASSERT(path[0] != '/');

		// split the string into the leaf filename
		// and the branch path
		string_view leaf;
		string_view branch_path;
		std::tie(branch_path, leaf) = rsplit_path(path);

		if (branch_path.empty())
		{
			if (set_name) e.set_name(leaf);
			e.path_index = -1;
			return;
		}

		// if the path *does* contain the name of the torrent (as we expect)
		// strip it before adding it to m_paths
		if (lsplit_path(branch_path).first == m_name)
		{
			branch_path = lsplit_path(branch_path).second;
			// strip duplicate separators
			while (!branch_path.empty() && (branch_path.front() == TORRENT_SEPARATOR
#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
				|| branch_path.front() == '/'
#endif
				))
				branch_path.remove_prefix(1);
			e.no_root_dir = false;
		}
		else
		{
			e.no_root_dir = true;
		}

		e.path_index = get_or_add_path(branch_path);
		if (set_name) e.set_name(leaf);
	}

	int file_storage::get_or_add_path(string_view const path)
	{
		// do we already have this path in the path list?
		auto const p = std::find(m_paths.rbegin(), m_paths.rend(), path);

		if (p == m_paths.rend())
		{
			// no, we don't. add it
			int const ret = int(m_paths.size());
			TORRENT_ASSERT(path.size() == 0 || path[0] != '/');
			m_paths.emplace_back(path.data(), path.size());
			return ret;
		}
		else
		{
			// yes we do. use it
			return int(p.base() - m_paths.begin() - 1);
		}
	}

#if TORRENT_ABI_VERSION == 1
	file_entry::file_entry(): offset(0), size(0)
		, mtime(0), pad_file(false), hidden_attribute(false)
		, executable_attribute(false)
		, symlink_attribute(false)
	{}

	file_entry::~file_entry() = default;
#endif // TORRENT_ABI_VERSION

	internal_file_entry::internal_file_entry()
		: offset(0)
		, symlink_index(not_a_symlink)
		, no_root_dir(false)
		, size(0)
		, name_len(name_is_owned)
		, pad_file(false)
		, hidden_attribute(false)
		, executable_attribute(false)
		, symlink_attribute(false)
		, name(nullptr)
		, path_index(-1)
	{}

	internal_file_entry::~internal_file_entry()
	{
		if (name_len == name_is_owned) delete[] name;
	}

	internal_file_entry::internal_file_entry(internal_file_entry const& fe)
		: offset(fe.offset)
		, symlink_index(fe.symlink_index)
		, no_root_dir(fe.no_root_dir)
		, size(fe.size)
		, name_len(fe.name_len)
		, pad_file(fe.pad_file)
		, hidden_attribute(fe.hidden_attribute)
		, executable_attribute(fe.executable_attribute)
		, symlink_attribute(fe.symlink_attribute)
		, name(nullptr)
		, path_index(fe.path_index)
	{
		bool const borrow = fe.name_len != name_is_owned;
		set_name(fe.filename(), borrow);
	}

	internal_file_entry& internal_file_entry::operator=(internal_file_entry const& fe) &
	{
		if (&fe == this) return *this;
		offset = fe.offset;
		size = fe.size;
		path_index = fe.path_index;
		symlink_index = fe.symlink_index;
		pad_file = fe.pad_file;
		hidden_attribute = fe.hidden_attribute;
		executable_attribute = fe.executable_attribute;
		symlink_attribute = fe.symlink_attribute;
		no_root_dir = fe.no_root_dir;
		// if the name is not owned, don't allocate memory, we can point into the
		// same metadata buffer
		bool const borrow = fe.name_len != name_is_owned;
		set_name(fe.filename(), borrow);
		return *this;
	}

	internal_file_entry::internal_file_entry(internal_file_entry&& fe) noexcept
		: offset(fe.offset)
		, symlink_index(fe.symlink_index)
		, no_root_dir(fe.no_root_dir)
		, size(fe.size)
		, name_len(fe.name_len)
		, pad_file(fe.pad_file)
		, hidden_attribute(fe.hidden_attribute)
		, executable_attribute(fe.executable_attribute)
		, symlink_attribute(fe.symlink_attribute)
		, name(fe.name)
		, path_index(fe.path_index)
	{
		fe.name_len = 0;
		fe.name = nullptr;
	}

	internal_file_entry& internal_file_entry::operator=(internal_file_entry&& fe) & noexcept
	{
		if (&fe == this) return *this;
		offset = fe.offset;
		size = fe.size;
		path_index = fe.path_index;
		symlink_index = fe.symlink_index;
		pad_file = fe.pad_file;
		hidden_attribute = fe.hidden_attribute;
		executable_attribute = fe.executable_attribute;
		symlink_attribute = fe.symlink_attribute;
		no_root_dir = fe.no_root_dir;
		name = fe.name;
		name_len = fe.name_len;

		fe.name_len = 0;
		fe.name = nullptr;
		return *this;
	}

	// if borrow_string is true, don't take ownership over n, just
	// point to it.
	// if borrow_string is false, n will be copied and owned by the
	// internal_file_entry.
	void internal_file_entry::set_name(string_view n, bool const borrow_string)
	{
		// free the current string, before assigning the new one
		if (name_len == name_is_owned) delete[] name;
		if (n.empty())
		{
			TORRENT_ASSERT(borrow_string == false);
			name = nullptr;
		}
		else if (borrow_string)
		{
			// we have limited space in the length field. truncate string
			// if it's too long
			if (n.size() >= name_is_owned) n = n.substr(name_is_owned - 1);

			name = n.data();
			name_len = aux::numeric_cast<std::uint64_t>(n.size());
		}
		else
		{
			name = allocate_string_copy(n);
			name_len = name_is_owned;
		}
	}

	string_view internal_file_entry::filename() const
	{
		if (name_len != name_is_owned) return {name, std::size_t(name_len)};
		return name ? string_view(name) : string_view();
	}

	void file_storage::apply_pointer_offset(std::ptrdiff_t const off)
	{
		for (auto& f : m_files)
		{
			if (f.name_len == internal_file_entry::name_is_owned) continue;
			f.name += off;
		}

		for (auto& h : m_file_hashes)
		{
			if (h == nullptr) continue;
			h += off;
		}
	}

#if TORRENT_ABI_VERSION == 1

	void file_storage::add_file_borrow(char const* filename, int filename_len
		, std::string const& path, std::int64_t file_size, file_flags_t file_flags
		, char const* filehash, std::int64_t mtime, string_view symlink_path)
	{
		TORRENT_ASSERT(filename_len >= 0);
		add_file_borrow({filename, std::size_t(filename_len)}, path, file_size
			, file_flags, filehash, mtime, symlink_path);
	}

	void file_storage::add_file(file_entry const& fe, char const* filehash)
	{
		file_flags_t flags = {};
		if (fe.pad_file) flags |= file_storage::flag_pad_file;
		if (fe.hidden_attribute) flags |= file_storage::flag_hidden;
		if (fe.executable_attribute) flags |= file_storage::flag_executable;
		if (fe.symlink_attribute) flags |= file_storage::flag_symlink;

		add_file_borrow({}, fe.path, fe.size, flags, filehash, fe.mtime
			, fe.symlink_path);
	}

	void file_storage::set_name(std::wstring const& n)
	{
		m_name = wchar_utf8(n);
	}

	void file_storage::rename_file_deprecated(file_index_t index, std::wstring const& new_filename)
	{
		TORRENT_ASSERT_PRECOND(index >= file_index_t(0) && index < end_file());
		update_path_index(m_files[index], wchar_utf8(new_filename));
	}

	void file_storage::add_file(std::wstring const& file, std::int64_t file_size
		, file_flags_t const file_flags, std::time_t mtime, string_view symlink_path)
	{
		add_file(wchar_utf8(file), file_size, file_flags, mtime, symlink_path);
	}

	void file_storage::rename_file(file_index_t index, std::wstring const& new_filename)
	{
		rename_file_deprecated(index, new_filename);
	}
#endif // TORRENT_ABI_VERSION

	void file_storage::rename_file(file_index_t const index
		, std::string const& new_filename)
	{
		TORRENT_ASSERT_PRECOND(index >= file_index_t(0) && index < end_file());
		update_path_index(m_files[index], new_filename);
	}

#if TORRENT_ABI_VERSION == 1
	file_storage::iterator file_storage::file_at_offset_deprecated(std::int64_t offset) const
	{
		// find the file iterator and file offset
		internal_file_entry target;
		target.offset = aux::numeric_cast<std::uint64_t>(offset);
		TORRENT_ASSERT(!compare_file_offset(target, m_files.front()));

		auto file_iter = std::upper_bound(
			begin_deprecated(), end_deprecated(), target, compare_file_offset);

		TORRENT_ASSERT(file_iter != begin_deprecated());
		--file_iter;
		return file_iter;
	}

	file_storage::iterator file_storage::file_at_offset(std::int64_t offset) const
	{
		return file_at_offset_deprecated(offset);
	}
#endif

	file_index_t file_storage::file_index_at_offset(std::int64_t const offset) const
	{
		TORRENT_ASSERT_PRECOND(offset >= 0);
		TORRENT_ASSERT_PRECOND(offset < m_total_size);
		// find the file iterator and file offset
		internal_file_entry target;
		target.offset = aux::numeric_cast<std::uint64_t>(offset);
		TORRENT_ASSERT(!compare_file_offset(target, m_files.front()));

		auto file_iter = std::upper_bound(
			m_files.begin(), m_files.end(), target, compare_file_offset);

		TORRENT_ASSERT(file_iter != m_files.begin());
		--file_iter;
		return file_index_t(int(file_iter - m_files.begin()));
	}

	char const* file_storage::file_name_ptr(file_index_t const index) const
	{
		return m_files[index].name;
	}

	int file_storage::file_name_len(file_index_t const index) const
	{
		if (m_files[index].name_len == internal_file_entry::name_is_owned)
			return -1;
		return m_files[index].name_len;
	}

	std::vector<file_slice> file_storage::map_block(piece_index_t const piece
		, std::int64_t const offset, int size) const
	{
		TORRENT_ASSERT_PRECOND(piece >= piece_index_t{0});
		TORRENT_ASSERT_PRECOND(piece < end_piece());
		TORRENT_ASSERT_PRECOND(num_files() > 0);
		std::vector<file_slice> ret;

		if (m_files.empty()) return ret;

		// find the file iterator and file offset
		internal_file_entry target;
		target.offset = aux::numeric_cast<std::uint64_t>(static_cast<int>(piece) * std::int64_t(m_piece_length) + offset);
		TORRENT_ASSERT_PRECOND(std::int64_t(target.offset) + size <= m_total_size);
		TORRENT_ASSERT(!compare_file_offset(target, m_files.front()));

		// in case the size is past the end, fix it up
		if (std::int64_t(target.offset) + size > m_total_size)
			size = aux::numeric_cast<int>(m_total_size - std::int64_t(target.offset));

		auto file_iter = std::upper_bound(
			m_files.begin(), m_files.end(), target, compare_file_offset);

		TORRENT_ASSERT(file_iter != m_files.begin());
		--file_iter;

		std::int64_t file_offset = target.offset - file_iter->offset;
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
				size -= int(f.size);
				file_offset += f.size;
				ret.push_back(f);
			}

			TORRENT_ASSERT(size >= 0);
		}
		return ret;
	}

#if TORRENT_ABI_VERSION == 1
	file_entry file_storage::at(int index) const
	{
		return at_deprecated(index);
	}

	internal_file_entry const& file_storage::internal_at(int const index) const
	{
		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < int(m_files.size()));
		return m_files[file_index_t(index)];
	}

	file_entry file_storage::at_deprecated(int index) const
	{
		TORRENT_ASSERT_PRECOND(index >= 0 && index < int(m_files.size()));
		file_entry ret;
		internal_file_entry const& ife = m_files[index];
		ret.path = file_path(index);
		ret.offset = ife.offset;
		ret.size = ife.size;
		ret.mtime = mtime(index);
		ret.pad_file = ife.pad_file;
		ret.hidden_attribute = ife.hidden_attribute;
		ret.executable_attribute = ife.executable_attribute;
		ret.symlink_attribute = ife.symlink_attribute;
		if (ife.symlink_index != internal_file_entry::not_a_symlink)
			ret.symlink_path = symlink(index);
		ret.filehash = hash(index);
		return ret;
	}
#endif // TORRENT_ABI_VERSION

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

	void file_storage::add_file(std::string const& path, std::int64_t file_size
		, file_flags_t const file_flags, std::time_t mtime, string_view symlink_path)
	{
		add_file_borrow({}, path, file_size, file_flags, nullptr, mtime
			, symlink_path);
	}

	void file_storage::add_file_borrow(string_view filename
		, std::string const& path, std::int64_t const file_size
		, file_flags_t const file_flags, char const* filehash
		, std::int64_t const mtime, string_view symlink_path)
	{
		TORRENT_ASSERT_PRECOND(file_size >= 0);
		TORRENT_ASSERT_PRECOND(!is_complete(filename));
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
				m_name = lsplit_path(path).first.to_string();
		}

		// this is poor-man's emplace_back()
		m_files.resize(m_files.size() + 1);
		internal_file_entry& e = m_files.back();

		// the last argument specified whether the function should also set
		// the filename. If it does, it will copy the leaf filename from path.
		// if filename is empty, we should copy it. If it isn't, we're borrowing
		// it and we can save the copy by setting it after this call to
		// update_path_index().
		update_path_index(e, path, filename.empty());

		// filename is allowed to be empty, in which case we just use path
		if (!filename.empty())
			e.set_name(filename, true);

		e.size = aux::numeric_cast<std::uint64_t>(file_size);
		e.offset = aux::numeric_cast<std::uint64_t>(m_total_size);
		e.pad_file = bool(file_flags & file_storage::flag_pad_file);
		e.hidden_attribute = bool(file_flags & file_storage::flag_hidden);
		e.executable_attribute = bool(file_flags & file_storage::flag_executable);
		e.symlink_attribute = bool(file_flags & file_storage::flag_symlink);

		if (filehash)
		{
			if (m_file_hashes.size() < m_files.size()) m_file_hashes.resize(m_files.size());
			m_file_hashes[last_file()] = filehash;
		}
		if (!symlink_path.empty()
			&& m_symlinks.size() < internal_file_entry::not_a_symlink - 1)
		{
			e.symlink_index = m_symlinks.size();
			m_symlinks.emplace_back(symlink_path.to_string());
		}
		else
		{
			e.symlink_attribute = false;
		}
		if (mtime)
		{
			if (m_mtime.size() < m_files.size()) m_mtime.resize(m_files.size());
			m_mtime[last_file()] = std::time_t(mtime);
		}

		m_total_size += e.size;
	}

	sha1_hash file_storage::hash(file_index_t const index) const
	{
		if (index >= m_file_hashes.end_index()) return sha1_hash();
		return sha1_hash(m_file_hashes[index]);
	}

	std::string const& file_storage::symlink(file_index_t const index) const
	{
		TORRENT_ASSERT_PRECOND(index >= file_index_t(0) && index < end_file());
		internal_file_entry const& fe = m_files[index];
		TORRENT_ASSERT(fe.symlink_index < int(m_symlinks.size()));

		auto const& link = m_symlinks[fe.symlink_index];

		// TODO: 3 this is a hack to retain ABI compatibility with 1.2.1
		// in next major release, make this return by value
		static std::string storage[4];
		static std::atomic<size_t> counter{0};
		std::string& ret = storage[(counter++) % 4];
		ret.reserve(m_name.size() + link.size() + 1);
		ret.assign(m_name);
		append_path(ret, link);
		return ret;
	}

	std::time_t file_storage::mtime(file_index_t const index) const
	{
		if (index >= m_mtime.end_index()) return 0;
		return m_mtime[index];
	}

namespace {

		template <class CRC>
		void process_string_lowercase(CRC& crc, string_view str)
		{
			for (char const c : str)
				crc.process_byte(to_lower(c) & 0xff);
		}

		template <class CRC>
		void process_path_lowercase(
			std::unordered_set<std::uint32_t>& table
			, CRC crc, string_view str)
		{
			if (str.empty()) return;
			for (char const c : str)
			{
				if (c == TORRENT_SEPARATOR)
					table.insert(crc.checksum());
				crc.process_byte(to_lower(c) & 0xff);
			}
			table.insert(crc.checksum());
		}
	}

	void file_storage::all_path_hashes(
		std::unordered_set<std::uint32_t>& table) const
	{
		boost::crc_optimal<32, 0x1EDC6F41, 0xFFFFFFFF, 0xFFFFFFFF, true, true> crc;

		if (!m_name.empty())
		{
			process_string_lowercase(crc, m_name);
			TORRENT_ASSERT(m_name[m_name.size() - 1] != TORRENT_SEPARATOR);
			crc.process_byte(TORRENT_SEPARATOR);
		}

		for (auto const& p : m_paths)
			process_path_lowercase(table, crc, p);
	}

	std::uint32_t file_storage::file_path_hash(file_index_t const index
		, std::string const& save_path) const
	{
		TORRENT_ASSERT_PRECOND(index >= file_index_t(0) && index < end_file());
		internal_file_entry const& fe = m_files[index];

		boost::crc_optimal<32, 0x1EDC6F41, 0xFFFFFFFF, 0xFFFFFFFF, true, true> crc;

		if (fe.path_index == -2)
		{
			// -2 means this is an absolute path filename
			process_string_lowercase(crc, fe.filename());
		}
		else if (fe.path_index == -1)
		{
			// -1 means no path
			if (!save_path.empty())
			{
				process_string_lowercase(crc, save_path);
				TORRENT_ASSERT(save_path[save_path.size() - 1] != TORRENT_SEPARATOR);
				crc.process_byte(TORRENT_SEPARATOR);
			}
			process_string_lowercase(crc, fe.filename());
		}
		else if (fe.no_root_dir)
		{
			if (!save_path.empty())
			{
				process_string_lowercase(crc, save_path);
				TORRENT_ASSERT(save_path[save_path.size() - 1] != TORRENT_SEPARATOR);
				crc.process_byte(TORRENT_SEPARATOR);
			}
			std::string const& p = m_paths[fe.path_index];
			if (!p.empty())
			{
				process_string_lowercase(crc, p);
				TORRENT_ASSERT(p[p.size() - 1] != TORRENT_SEPARATOR);
				crc.process_byte(TORRENT_SEPARATOR);
			}
			process_string_lowercase(crc, fe.filename());
		}
		else
		{
			if (!save_path.empty())
			{
				process_string_lowercase(crc, save_path);
				TORRENT_ASSERT(save_path[save_path.size() - 1] != TORRENT_SEPARATOR);
				crc.process_byte(TORRENT_SEPARATOR);
			}
			process_string_lowercase(crc, m_name);
			TORRENT_ASSERT(m_name.size() > 0);
			TORRENT_ASSERT(m_name[m_name.size() - 1] != TORRENT_SEPARATOR);
			crc.process_byte(TORRENT_SEPARATOR);

			std::string const& p = m_paths[fe.path_index];
			if (!p.empty())
			{
				process_string_lowercase(crc, p);
				TORRENT_ASSERT(p.size() > 0);
				TORRENT_ASSERT(p[p.size() - 1] != TORRENT_SEPARATOR);
				crc.process_byte(TORRENT_SEPARATOR);
			}
			process_string_lowercase(crc, fe.filename());
		}

		return crc.checksum();
	}

	std::string file_storage::file_path(file_index_t const index, std::string const& save_path) const
	{
		TORRENT_ASSERT_PRECOND(index >= file_index_t(0) && index < end_file());
		internal_file_entry const& fe = m_files[index];

		std::string ret;

		// -2 means this is an absolute path filename
		if (fe.path_index == -2)
		{
			ret = fe.filename().to_string();
		}
		else if (fe.path_index == -1)
		{
			// -1 means no path
			ret.reserve(save_path.size() + fe.filename().size() + 1);
			ret.assign(save_path);
			append_path(ret, fe.filename());
		}
		else if (fe.no_root_dir)
		{
			std::string const& p = m_paths[fe.path_index];

			ret.reserve(save_path.size() + p.size() + fe.filename().size() + 2);
			ret.assign(save_path);
			append_path(ret, p);
			append_path(ret, fe.filename());
		}
		else
		{
			std::string const& p = m_paths[fe.path_index];

			ret.reserve(save_path.size() + m_name.size() + p.size() + fe.filename().size() + 3);
			ret.assign(save_path);
			append_path(ret, m_name);
			append_path(ret, p);
			append_path(ret, fe.filename());
		}

		// a single return statement, just to make NRVO more likely to kick in
		return ret;
	}

	std::string file_storage::internal_file_path(file_index_t const index) const
	{
		TORRENT_ASSERT_PRECOND(index >= file_index_t(0) && index < end_file());
		internal_file_entry const& fe = m_files[index];

		if (fe.path_index >= 0)
		{
			std::string ret;
			std::string const& p = m_paths[fe.path_index];
			ret.reserve(p.size() + fe.filename().size() + 2);
			append_path(ret, p);
			append_path(ret, fe.filename());
			return ret;
		}
		else
		{
			return fe.filename().to_string();
		}
	}

	string_view file_storage::file_name(file_index_t const index) const
	{
		TORRENT_ASSERT_PRECOND(index >= file_index_t(0) && index < end_file());
		internal_file_entry const& fe = m_files[index];
		return fe.filename();
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
		return m_files[index].offset;
	}

	file_flags_t file_storage::file_flags(file_index_t const index) const
	{
		TORRENT_ASSERT_PRECOND(index >= file_index_t(0) && index < end_file());
		internal_file_entry const& fe = m_files[index];
		return (fe.pad_file ? file_storage::flag_pad_file : file_flags_t{})
			| (fe.hidden_attribute ? file_storage::flag_hidden : file_flags_t{})
			| (fe.executable_attribute ? file_storage::flag_executable : file_flags_t{})
			| (fe.symlink_attribute ? file_storage::flag_symlink : file_flags_t{});
	}

	bool file_storage::file_absolute_path(file_index_t const index) const
	{
		TORRENT_ASSERT_PRECOND(index >= file_index_t(0) && index < end_file());
		internal_file_entry const& fe = m_files[index];
		return fe.path_index == -2;
	}

#if TORRENT_ABI_VERSION == 1
	sha1_hash file_storage::hash(internal_file_entry const& fe) const
	{
		int index = int(&fe - &m_files[0]);
		if (index >= int(m_file_hashes.size())) return sha1_hash(nullptr);
		return sha1_hash(m_file_hashes[index]);
	}

	std::string const& file_storage::symlink(internal_file_entry const& fe) const
	{
		TORRENT_ASSERT_PRECOND(fe.symlink_index < int(m_symlinks.size()));
		return m_symlinks[fe.symlink_index];
	}

	std::time_t file_storage::mtime(internal_file_entry const& fe) const
	{
		int index = int(&fe - &m_files[0]);
		if (index >= int(m_mtime.size())) return 0;
		return m_mtime[index];
	}

	int file_storage::file_index(internal_file_entry const& fe) const
	{
		int index = int(&fe - &m_files[0]);
		TORRENT_ASSERT_PRECOND(index >= 0 && index < int(m_files.size()));
		return index;
	}

	std::string file_storage::file_path(internal_file_entry const& fe
		, std::string const& save_path) const
	{
		int const index = int(&fe - &m_files[0]);
		return file_path(index, save_path);
	}

	std::string file_storage::file_name(internal_file_entry const& fe) const
	{
		return fe.filename().to_string();
	}

	std::int64_t file_storage::file_size(internal_file_entry const& fe) const
	{
		return fe.size;
	}

	bool file_storage::pad_file_at(internal_file_entry const& fe) const
	{
		return fe.pad_file;
	}

	std::int64_t file_storage::file_offset(internal_file_entry const& fe) const
	{
		return fe.offset;
	}

	file_entry file_storage::at(file_storage::iterator i) const
	{ return at_deprecated(int(i - m_files.begin())); }
#endif // TORRENT_ABI_VERSION

	void file_storage::reorder_file(int const index, int const dst)
	{
		TORRENT_ASSERT(index < int(m_files.size()));
		TORRENT_ASSERT(dst < int(m_files.size()));
		TORRENT_ASSERT(dst < index);

		std::iter_swap(m_files.begin() + index, m_files.begin() + dst);
		if (!m_mtime.empty())
		{
			TORRENT_ASSERT(m_mtime.size() == m_files.size());
			if (int(m_mtime.size()) < index) m_mtime.resize(index + 1, 0);
			std::iter_swap(m_mtime.begin() + dst, m_mtime.begin() + index);
		}
		if (!m_file_hashes.empty())
		{
			TORRENT_ASSERT(m_file_hashes.size() == m_files.size());
			if (int(m_file_hashes.size()) < index) m_file_hashes.resize(index + 1, nullptr);
			std::iter_swap(m_file_hashes.begin() + dst, m_file_hashes.begin() + index);
		}
	}

	void file_storage::swap(file_storage& ti) noexcept
	{
		using std::swap;
		swap(ti.m_files, m_files);
		swap(ti.m_file_hashes, m_file_hashes);
		swap(ti.m_symlinks, m_symlinks);
		swap(ti.m_mtime, m_mtime);
		swap(ti.m_paths, m_paths);
		swap(ti.m_name, m_name);
		swap(ti.m_total_size, m_total_size);
		swap(ti.m_num_pieces, m_num_pieces);
		swap(ti.m_piece_length, m_piece_length);
	}

	void file_storage::optimize(int const pad_file_limit, int alignment
		, bool const tail_padding)
	{
		if (alignment == -1)
			alignment = m_piece_length;

		// TODO: padfiles should be removed

		std::int64_t off = 0;
		int padding_file = 0;
		for (auto i = m_files.begin(); i != m_files.end(); ++i)
		{
			if ((off % alignment) == 0)
			{
				// this file position is aligned, pick the largest
				// available file to put here. If we encounter a file whose size is
				// divisible by `alignment`, we pick that immediately, since that
				// will not affect whether we're at an aligned position and will
				// improve packing of files
				auto best_match = i;
				for (auto k = i; k != m_files.end(); ++k)
				{
					// a file whose size fits the alignment always takes priority,
					// since it will let us keep placing aligned files
					if ((k->size % aux::numeric_cast<std::uint64_t>(alignment)) == 0)
					{
						best_match = k;
						break;
					}
					// otherwise, pick the largest file, to have as many bytes be
					// aligned.
					if (best_match->size < k->size) best_match = k;
				}

				if (best_match != i)
				{
					int const index = int(best_match - m_files.begin());
					int const cur_index = int(i - m_files.begin());
					reorder_file(index, cur_index);
					i = m_files.begin() + cur_index;
				}
			}
			else if (pad_file_limit >= 0
				&& i->size > std::uint32_t(pad_file_limit)
				&& i->pad_file == false)
			{
				// if we have pad files enabled, and this file is
				// not piece-aligned and the file size exceeds the
				// limit, and it's not a padding file itself.
				// so add a padding file in front of it
				int const pad_size = alignment - (off % alignment);

				// find the largest file that fits in pad_size
				auto best_match = m_files.end();

				// if pad_file_limit is 0, it means all files are padded, there's
				// no point in trying to find smaller files to use as filling
				if (pad_file_limit > 0)
				{
					for (auto j = i + 1; j < m_files.end(); ++j)
					{
						if (j->size > std::uint32_t(pad_size)) continue;
						if (best_match == m_files.end() || j->size > best_match->size)
							best_match = j;
					}

					if (best_match != m_files.end())
					{
						// we found one
						// We cannot have found i, because i->size > pad_file_limit
						// which is forced to be no less than alignment. We only
						// look for files <= pad_size, which never is greater than
						// alignment
						TORRENT_ASSERT(best_match != i);
						int index = int(best_match - m_files.begin());
						int cur_index = int(i - m_files.begin());
						reorder_file(index, cur_index);
						i = m_files.begin() + cur_index;
						i->offset = aux::numeric_cast<std::uint64_t>(off);
						off += i->size;
						continue;
					}
				}

				// we could not find a file that fits in pad_size
				// add a padding file
				// note that i will be set to point to the
				// new pad file. Once we're done adding it, we need
				// to increment i to point to the current file again
				// first add the pad file to the end of the file list
				// then swap it in place. This minimizes the amount
				// of copying of internal_file_entry, which is somewhat
				// expensive (until we have move semantics)
				add_pad_file(pad_size, i, off, padding_file);

				TORRENT_ASSERT((off % alignment) == 0);
				continue;
			}
			i->offset = aux::numeric_cast<std::uint64_t>(off);
			off += i->size;

			if (tail_padding
				&& i->size > std::uint32_t(pad_file_limit)
				&& (off % alignment) != 0)
			{
				// skip the file we just put in place, so we put the pad
				// file after it
				++i;

				// tail-padding is enabled, and the offset after this file is not
				// aligned. The last file must be padded too, in order to match an
				// equivalent tail-padded file.
				add_pad_file(alignment - (off % alignment), i, off, padding_file);

				TORRENT_ASSERT((off % alignment) == 0);

				if (i == m_files.end()) break;
			}
		}
		m_total_size = off;
	}

	void file_storage::add_pad_file(int const size
		, std::vector<internal_file_entry>::iterator& i
		, std::int64_t& offset
		, int& pad_file_counter)
	{
		int const cur_index = int(i - m_files.begin());
		int const index = int(m_files.size());
		m_files.push_back(internal_file_entry());
		internal_file_entry& e = m_files.back();
		// i may have been invalidated, refresh it
		i = m_files.begin() + cur_index;
		e.size = aux::numeric_cast<std::uint64_t>(size);
		e.offset = aux::numeric_cast<std::uint64_t>(offset);
		e.path_index = get_or_add_path(".pad");
		char name[15];
		std::snprintf(name, sizeof(name), "%d", pad_file_counter);
		e.set_name(name);
		e.pad_file = true;
		offset += size;
		++pad_file_counter;

		if (!m_mtime.empty()) m_mtime.resize(index + 1, 0);
		if (!m_file_hashes.empty()) m_file_hashes.resize(index + 1, nullptr);

		if (index != cur_index) reorder_file(index, cur_index);
	}

	void file_storage::sanitize_symlinks()
	{
		// symlinks are unusual, this function is optimized assuming there are no
		// symbolic links in the torrent. If we find one symbolic link, we'll
		// build the hash table of files it's allowed to refer to, but don't pay
		// that price up-front.
		std::unordered_map<std::string, file_index_t> file_map;
		bool file_map_initialized = false;

		// lazily instantiated set of all valid directories a symlink may point to
		// TODO: in C++17 this could be string_view
		std::unordered_set<std::string> dir_map;
		bool dir_map_initialized = false;

		// symbolic links that points to directories
		std::unordered_map<std::string, std::string> dir_links;

		// we validate symlinks in (potentially) 2 passes over the files.
		// remaining symlinks to validate after the first pass
		std::vector<file_index_t> symlinks_to_validate;

		for (auto const i : file_range())
		{
			if (!(file_flags(i) & file_storage::flag_symlink)) continue;

			if (!file_map_initialized)
			{
				for (auto const j : file_range())
					file_map.insert({internal_file_path(j), j});
				file_map_initialized = true;
			}

			internal_file_entry const& fe = m_files[i];
			TORRENT_ASSERT(fe.symlink_index < int(m_symlinks.size()));

			// symlink targets are only allowed to point to files or directories in
			// this torrent.
			{
				std::string target = m_symlinks[fe.symlink_index];

				if (is_complete(target))
				{
					// a symlink target is not allowed to be an absolute path, ever
					// this symlink is invalid, make it point to itself
					m_symlinks[fe.symlink_index] = internal_file_path(i);
					continue;
				}

				auto const iter = file_map.find(target);
				if (iter != file_map.end())
				{
					m_symlinks[fe.symlink_index] = target;
					if (file_flags(iter->second) & file_storage::flag_symlink)
					{
						// we don't know whether this symlink is a file or a
						// directory, so make the conservative assumption that it's a
						// directory
						dir_links[internal_file_path(i)] = target;
					}
					continue;
				}

				// it may point to a directory that doesn't have any files (but only
				// other directories), in which case it won't show up in m_paths
				if (!dir_map_initialized)
				{
					for (auto const& p : m_paths)
						for (string_view pv = p; !pv.empty(); pv = rsplit_path(pv).first)
							dir_map.insert(pv.to_string());
					dir_map_initialized = true;
				}

				if (dir_map.count(target))
				{
					// it points to a sub directory within the torrent, that's OK
					m_symlinks[fe.symlink_index] = target;
					dir_links[internal_file_path(i)] = target;
					continue;
				}

			}

			// for backwards compatibility, allow paths relative to the link as
			// well
			if (fe.path_index >= 0)
			{
				std::string target = m_paths[fe.path_index];
				append_path(target, m_symlinks[fe.symlink_index]);
				// if it points to a directory, that's OK
				auto const it = std::find(m_paths.begin(), m_paths.end(), target);
				if (it != m_paths.end())
				{
					m_symlinks[fe.symlink_index] = *it;
					dir_links[internal_file_path(i)] = *it;
					continue;
				}

				if (dir_map.count(target))
				{
					// it points to a sub directory within the torrent, that's OK
					m_symlinks[fe.symlink_index] = target;
					dir_links[internal_file_path(i)] = target;
					continue;
				}

				auto const iter = file_map.find(target);
				if (iter != file_map.end())
				{
					m_symlinks[fe.symlink_index] = target;
					if (file_flags(iter->second) & file_storage::flag_symlink)
					{
						// we don't know whether this symlink is a file or a
						// directory, so make the conservative assumption that it's a
						// directory
						dir_links[internal_file_path(i)] = target;
					}
					continue;
				}
			}

			// we don't know whether this symlink is a file or a
			// directory, so make the conservative assumption that it's a
			// directory
			dir_links[internal_file_path(i)] = m_symlinks[fe.symlink_index];
			symlinks_to_validate.push_back(i);
		}

		// in case there were some "complex" symlinks, we nee a second pass to
		// validate those. For example, symlinks whose target rely on other
		// symlinks
		for (auto const i : symlinks_to_validate)
		{
			internal_file_entry const& fe = m_files[i];
			TORRENT_ASSERT(fe.symlink_index < int(m_symlinks.size()));

			std::string target = m_symlinks[fe.symlink_index];

			// to avoid getting stuck in an infinite loop, we only allow traversing
			// a symlink once
			std::set<std::string> traversed;

			// this is where we check every path element for existence. If it's not
			// among the concrete paths, it may be a symlink, which is also OK
			// note that we won't iterate through this for the last step, where the
			// filename is included. The filename is validated after the loop
			for (string_view branch = lsplit_path(target).first;
				branch.size() < target.size();
				branch = lsplit_path(target, branch.size() + 1).first)
			{
				// this is a concrete directory
				if (dir_map.count(branch.to_string())) continue;

				auto const iter = dir_links.find(branch.to_string());
				if (iter == dir_links.end()) goto failed;
				if (traversed.count(branch.to_string())) goto failed;
				traversed.insert(branch.to_string());

				// this path element is a symlink. substitute the branch so far by
				// the link target
				target = combine_path(iter->second, target.substr(branch.size() + 1));

				// start over with the new (concrete) path
				branch = {};
			}

			// the final (resolved) target must be a valid file
			// or directory
			if (file_map.count(target) == 0
				&& dir_map.count(target) == 0) goto failed;

			// this is OK
			continue;

failed:

			// this symlink is invalid, make it point to itself
			m_symlinks[fe.symlink_index] = internal_file_path(i);
		}
	}


namespace aux {

	std::tuple<piece_index_t, piece_index_t>
	file_piece_range_exclusive(file_storage const& fs, file_index_t const file)
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
		return std::make_tuple(begin_piece, end_piece);
	}

	std::tuple<piece_index_t, piece_index_t>
	file_piece_range_inclusive(file_storage const& fs, file_index_t const file)
	{
		peer_request const range = fs.map_file(file, 0, 1);
		std::int64_t const file_size = fs.file_size(file);
		std::int64_t const piece_size = fs.piece_length();
		piece_index_t const end_piece = piece_index_t(int((static_cast<int>(range.piece)
			* piece_size + range.start + file_size - 1) / piece_size + 1));
		return std::make_tuple(range.piece, end_piece);
	}

	} // namespace aux
}
