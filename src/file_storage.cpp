/*

Copyright (c) 2003-2016, Arvid Norberg
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
#include "libtorrent/file.hpp"
#include "libtorrent/utf8.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/crc.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include <cstdio>
#include <algorithm>
#include <functional>

#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
#define TORRENT_SEPARATOR '\\'
#define TORRENT_SEPARATOR_STR "\\"
#else
#define TORRENT_SEPARATOR '/'
#define TORRENT_SEPARATOR_STR "/"
#endif

using namespace std::placeholders;

namespace libtorrent
{
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

	namespace
	{
		bool compare_file_offset(internal_file_entry const& lhs
			, internal_file_entry const& rhs)
		{
			return lhs.offset < rhs.offset;
		}
	}

	// path is not supposed to include the name of the torrent itself.
	void file_storage::update_path_index(internal_file_entry& e
		, std::string const& path, bool const set_name)
	{
		if (is_complete(path))
		{
			TORRENT_ASSERT(set_name);
			e.set_name(path.c_str());
			e.path_index = -2;
			return;
		}

		TORRENT_ASSERT(path[0] != '/');

		// sorry about this messy string handling, but I did
		// profile it, and it was expensive
		char const* leaf = filename_cstr(path.c_str());
		char const* branch_path = "";
		int branch_len = 0;
		if (leaf > path.c_str())
		{
			// split the string into the leaf filename
			// and the branch path
			branch_path = path.c_str();
			branch_len = int(leaf - path.c_str());

			// trim trailing slashes
			if (branch_len > 0 && branch_path[branch_len - 1] == TORRENT_SEPARATOR)
				--branch_len;
		}
		if (branch_len <= 0)
		{
			if (set_name) e.set_name(leaf);
			e.path_index = -1;
			return;
		}

		if (branch_len >= int(m_name.size())
			&& std::memcmp(branch_path, m_name.c_str(), m_name.size()) == 0)
		{
			// the +1 is to skip the trailing '/' (or '\')
			int const offset = int(m_name.size())
				+ (int(m_name.size()) == branch_len ? 0 : 1);
			branch_path += offset;
			branch_len -= offset;
			e.no_root_dir = false;
		}
		else
		{
			e.no_root_dir = true;
		}

		// do we already have this path in the path list?
		auto p = std::find_if(m_paths.rbegin(), m_paths.rend()
			, [&] (std::string const& str)
			{
				if (int(str.size()) != branch_len) return false;
				return std::memcmp(str.c_str(), branch_path, branch_len) == 0;
			});

		if (p == m_paths.rend())
		{
			// no, we don't. add it
			e.path_index = int(m_paths.size());
			TORRENT_ASSERT(branch_len == 0 || branch_path[0] != '/');

			// poor man's emplace back
			m_paths.resize(m_paths.size() + 1);
			m_paths.back().assign(branch_path, branch_len);
		}
		else
		{
			// yes we do. use it
			e.path_index = int(p.base() - m_paths.begin() - 1);
		}
		if (set_name) e.set_name(leaf);
	}

#ifndef TORRENT_NO_DEPRECATE
	file_entry::file_entry(): offset(0), size(0), file_base(0)
		, mtime(0), pad_file(false), hidden_attribute(false)
		, executable_attribute(false)
		, symlink_attribute(false)
	{}

	file_entry::~file_entry() = default;
#endif // TORRENT_NO_DEPRECATE

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
		if (name_len == name_is_owned) free(const_cast<char*>(name));
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
		if (fe.name_len == name_is_owned)
			name = allocate_string_copy(fe.name);
		else
			name = fe.name;
	}

	internal_file_entry& internal_file_entry::operator=(internal_file_entry const& fe)
	{
		offset = fe.offset;
		size = fe.size;
		path_index = fe.path_index;
		symlink_index = fe.symlink_index;
		pad_file = fe.pad_file;
		hidden_attribute = fe.hidden_attribute;
		executable_attribute = fe.executable_attribute;
		symlink_attribute = fe.symlink_attribute;
		no_root_dir = fe.no_root_dir;
		set_name(fe.filename().to_string().c_str());
		return *this;
	}

	internal_file_entry::internal_file_entry(internal_file_entry&& fe)
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
		fe.name_len = name_is_owned;
		fe.name = nullptr;
	}

	internal_file_entry& internal_file_entry::operator=(internal_file_entry&& fe)
	{
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

		fe.name_len = name_is_owned;
		fe.name = nullptr;
		return *this;
	}

	file_storage::file_storage(file_storage&&) = default;
	file_storage& file_storage::operator=(file_storage&&) = default;

	// if borrow_chars >= 0, don't take ownership over n, just
	// point to it. It points to borrow_chars number of characters.
	// if borrow_chars == -1, n is a 0-terminated string that
	// should be copied.
	void internal_file_entry::set_name(char const* n, bool const borrow_string
		, int string_len)
	{
		TORRENT_ASSERT(string_len >= 0);

		// we have limited space in the length field. truncate string
		// if it's too long
		if (string_len >= name_is_owned) string_len = name_is_owned - 1;

		// free the current string, before assigning the new one
		if (name_len == name_is_owned) free(const_cast<char*>(name));
		if (n == nullptr)
		{
			TORRENT_ASSERT(borrow_string == false);
			name = nullptr;
		}
		else if (borrow_string)
		{
			name = n;
			name_len = string_len;
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

	void file_storage::apply_pointer_offset(ptrdiff_t const off)
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

#ifndef TORRENT_NO_DEPRECATE

	void file_storage::add_file(file_entry const& fe, char const* filehash)
	{
		int flags = 0;
		if (fe.pad_file) flags |= file_storage::flag_pad_file;
		if (fe.hidden_attribute) flags |= file_storage::flag_hidden;
		if (fe.executable_attribute) flags |= file_storage::flag_executable;
		if (fe.symlink_attribute) flags |= file_storage::flag_symlink;

		add_file_borrow(nullptr, 0, fe.path, fe.size, flags, filehash, fe.mtime
			, fe.symlink_path);
	}

#if TORRENT_USE_WSTRING
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
		, int file_flags, std::time_t mtime, string_view symlink_path)
	{
		add_file(wchar_utf8(file), file_size, file_flags, mtime, symlink_path);
	}

	void file_storage::rename_file(file_index_t index, std::wstring const& new_filename)
	{
		rename_file_deprecated(index, new_filename);
	}
#endif // TORRENT_USE_WSTRING
#endif // TORRENT_NO_DEPRECATE

	void file_storage::rename_file(file_index_t const index
		, std::string const& new_filename)
	{
		TORRENT_ASSERT_PRECOND(index >= file_index_t(0) && index < end_file());
		update_path_index(m_files[index], new_filename);
	}

#ifndef TORRENT_NO_DEPRECATE
	file_storage::iterator file_storage::file_at_offset_deprecated(std::int64_t offset) const
	{
		// find the file iterator and file offset
		internal_file_entry target;
		target.offset = offset;
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
		// find the file iterator and file offset
		internal_file_entry target;
		target.offset = offset;
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
		TORRENT_ASSERT_PRECOND(num_files() > 0);
		std::vector<file_slice> ret;

		if (m_files.empty()) return ret;

		// find the file iterator and file offset
		internal_file_entry target;
		target.offset = static_cast<int>(piece) * std::int64_t(m_piece_length) + offset;
		TORRENT_ASSERT_PRECOND(std::int64_t(target.offset + size) <= m_total_size);
		TORRENT_ASSERT(!compare_file_offset(target, m_files.front()));

		// in case the size is past the end, fix it up
		if (std::int64_t(target.offset + size) > m_total_size)
			size = int(m_total_size - target.offset);

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
				file_slice f;
				f.file_index = file_index_t(int(file_iter - m_files.begin()));
				f.offset = file_offset
#ifndef TORRENT_NO_DEPRECATE
					+ file_base_deprecated(f.file_index)
#endif
					;
				f.size = (std::min)(std::uint64_t(file_iter->size) - file_offset, std::uint64_t(size));
				TORRENT_ASSERT(f.size <= size);
				size -= int(f.size);
				file_offset += f.size;
				ret.push_back(f);
			}

			TORRENT_ASSERT(size >= 0);
		}
		return ret;
	}

#ifndef TORRENT_NO_DEPRECATE
	file_entry file_storage::at(int index) const
	{
		return at_deprecated(index);
	}

	file_entry file_storage::at_deprecated(int index) const
	{
		TORRENT_ASSERT_PRECOND(index >= 0 && index < int(m_files.size()));
		file_entry ret;
		internal_file_entry const& ife = m_files[index];
		ret.path = file_path(index);
		ret.offset = ife.offset;
		ret.size = ife.size;
		ret.file_base = file_base(index);
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
#endif // TORRENT_NO_DEPRECATE

	peer_request file_storage::map_file(file_index_t const file_index
		, std::int64_t const file_offset, int size) const
	{
		TORRENT_ASSERT_PRECOND(file_index < end_file());
		TORRENT_ASSERT(m_num_pieces >= 0);

		peer_request ret;
		if (file_index >= end_file())
		{
			ret.piece = piece_index_t{m_num_pieces};
			ret.start = 0;
			ret.length = 0;
			return ret;
		}

		std::int64_t const offset = file_offset + this->file_offset(file_index);

		if (offset >= total_size())
		{
			ret.piece = piece_index_t{m_num_pieces};
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
		, int file_flags, std::time_t mtime, string_view symlink_path)
	{
		add_file_borrow(nullptr, 0, path, file_size, file_flags, nullptr, mtime
			, symlink_path);
	}

	void file_storage::add_file_borrow(char const* filename, int const filename_len
		, std::string const& path, std::int64_t const file_size
		, std::uint32_t const file_flags, char const* filehash
		, std::int64_t const mtime, string_view symlink_path)
	{
		TORRENT_ASSERT_PRECOND(file_size >= 0);
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
				m_name = split_path(path, true);
		}

		// this is poor-man's emplace_back()
		m_files.resize(m_files.size() + 1);
		internal_file_entry& e = m_files.back();

		// the last argument specified whether the function should also set
		// the filename. If it does, it will copy the leaf filename from path.
		// if filename is nullptr, we should copy it. If it isn't, we're borrowing
		// it and we can save the copy by setting it after this call to
		// update_path_index().
		update_path_index(e, path, filename == nullptr);

		// filename is allowed to be nullptr, in which case we just use path
		if (filename)
			e.set_name(filename, true, filename_len);

		e.size = file_size;
		e.offset = m_total_size;
		e.pad_file = (file_flags & file_storage::flag_pad_file) != 0;
		e.hidden_attribute = (file_flags & file_storage::flag_hidden) != 0;
		e.executable_attribute = (file_flags & file_storage::flag_executable) != 0;
		e.symlink_attribute = (file_flags & file_storage::flag_symlink) != 0;

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
		return m_symlinks[file_index_t(fe.symlink_index)];
	}

	std::time_t file_storage::mtime(file_index_t const index) const
	{
		if (index >= m_mtime.end_index()) return 0;
		return m_mtime[index];
	}

	namespace
	{
		template <class CRC>
		void process_string_lowercase(CRC& crc, string_view str)
		{
			for (char const c : str)
				crc.process_byte(to_lower(c));
		}

		template <class CRC>
		void process_path_lowercase(
			std::unordered_set<std::uint32_t>& table
			, CRC crc
			, char const* str, int len)
		{
			if (len == 0) return;
			for (int i = 0; i < len; ++i, ++str)
			{
				if (*str == TORRENT_SEPARATOR)
					table.insert(crc.checksum());
				crc.process_byte(to_lower(*str));
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

		for (int i = 0; i != int(m_paths.size()); ++i)
		{
			std::string const& p = m_paths[i];
			process_path_lowercase(table, crc, p.c_str(), int(p.size()));
		}
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

	int file_storage::file_flags(file_index_t const index) const
	{
		TORRENT_ASSERT_PRECOND(index >= file_index_t(0) && index < end_file());
		internal_file_entry const& fe = m_files[index];
		return (fe.pad_file ? flag_pad_file : 0)
			| (fe.hidden_attribute ? flag_hidden : 0)
			| (fe.executable_attribute ? flag_executable : 0)
			| (fe.symlink_attribute ? flag_symlink : 0);
	}

	bool file_storage::file_absolute_path(file_index_t const index) const
	{
		TORRENT_ASSERT_PRECOND(index >= file_index_t(0) && index < end_file());
		internal_file_entry const& fe = m_files[index];
		return fe.path_index == -2;
	}

#ifndef TORRENT_NO_DEPRECATE
	void file_storage::set_file_base(int index, std::int64_t off)
	{
		TORRENT_ASSERT_PRECOND(index >= file_index_t(0) && index < end_file());
		if (int(m_file_base.size()) <= index) m_file_base.resize(index + 1, 0);
		m_file_base[index] = off;
	}

	std::int64_t file_storage::file_base_deprecated(int index) const
	{
		if (index >= int(m_file_base.size())) return 0;
		return m_file_base[index];
	}

	std::int64_t file_storage::file_base(int index) const
	{
		if (index >= int(m_file_base.size())) return 0;
		return m_file_base[index];
	}

	sha1_hash file_storage::hash(internal_file_entry const& fe) const
	{
		int index = int(&fe - &m_files[0]);
		if (index >= int(m_file_hashes.size())) return sha1_hash(nullptr);
		return sha1_hash(m_file_hashes[index]);
	}

	std::string const& file_storage::symlink(internal_file_entry const& fe) const
	{
		TORRENT_ASSERT_PRECOND(fe.symlink_index < int(m_symlinks.size()));
		return m_symlinks[file_index_t(fe.symlink_index)];
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

	void file_storage::set_file_base(internal_file_entry const& fe, std::int64_t off)
	{
		int index = int(&fe - &m_files[0]);
		TORRENT_ASSERT_PRECOND(index >= 0 && index < int(m_files.size()));
		if (int(m_file_base.size()) <= index) m_file_base.resize(index + 1, 0);
		m_file_base[index] = off;
	}

	std::int64_t file_storage::file_base(internal_file_entry const& fe) const
	{
		int index = int(&fe - &m_files[0]);
		if (index >= int(m_file_base.size())) return 0;
		return m_file_base[index];
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
#endif // TORRENT_NO_DEPRECATE

	void file_storage::reorder_file(int const index, int const dst)
	{
		TORRENT_ASSERT(index < int(m_files.size()));
		TORRENT_ASSERT(dst < int(m_files.size()));
		TORRENT_ASSERT(dst < index);

		std::iter_swap(m_files.begin() + index, m_files.begin() + dst);
		if (!m_mtime.empty())
		{
			TORRENT_ASSERT(m_mtime.size() == m_files.size());
			if (int(m_mtime.size()) < index) m_mtime.resize(index+1, 0);
			std::iter_swap(m_mtime.begin() + dst, m_mtime.begin() + index);
		}
		if (!m_file_hashes.empty())
		{
			TORRENT_ASSERT(m_file_hashes.size() == m_files.size());
			if (int(m_file_hashes.size()) < index) m_file_hashes.resize(index + 1, nullptr);
			std::iter_swap(m_file_hashes.begin() + dst, m_file_hashes.begin() + index);
		}
#ifndef TORRENT_NO_DEPRECATE
		if (!m_file_base.empty())
		{
			TORRENT_ASSERT(m_file_base.size() == m_files.size());
			if (int(m_file_base.size()) < index) m_file_base.resize(index + 1, 0);
			std::iter_swap(m_file_base.begin() + dst, m_file_base.begin() + index);
		}
#endif // TORRENT_DEPRECATED
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
					if ((k->size % alignment) == 0)
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
						i->offset = off;
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
			i->offset = off;
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
		e.size = size;
		e.offset = offset;
		char name[30];
		std::snprintf(name, sizeof(name), ".pad" TORRENT_SEPARATOR_STR "%d"
			, pad_file_counter);
		std::string path = combine_path(m_name, name);
		e.set_name(path.c_str());
		e.pad_file = true;
		offset += size;
		++pad_file_counter;

		if (!m_mtime.empty()) m_mtime.resize(index + 1, 0);
		if (!m_file_hashes.empty()) m_file_hashes.resize(index + 1, nullptr);
#ifndef TORRENT_NO_DEPRECATE
		if (!m_file_base.empty()) m_file_base.resize(index + 1, 0);
#endif

		if (index != cur_index) reorder_file(index, cur_index);
	}

	namespace aux
	{

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

