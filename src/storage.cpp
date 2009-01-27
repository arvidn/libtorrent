/*

Copyright (c) 2003, Arvid Norberg, Daniel Wallin
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

#include "libtorrent/pch.hpp"

#include <ctime>
#include <iterator>
#include <algorithm>
#include <set>
#include <functional>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/lexical_cast.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/ref.hpp>
#include <boost/bind.hpp>
#include <boost/version.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#if BOOST_VERSION >= 103500
#include <boost/system/system_error.hpp>
#endif

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/config.hpp"
#include "libtorrent/storage.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/file.hpp"
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/file_pool.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/disk_buffer_holder.hpp"

//#define TORRENT_PARTIAL_HASH_LOG

#ifdef TORRENT_DEBUG
#include <ios>
#include <iostream>
#include <iomanip>
#include <cstdio>
#endif

#if defined(__APPLE__)
// for getattrlist()
#include <sys/attr.h>
#include <unistd.h>
// for statfs()
#include <sys/param.h>
#include <sys/mount.h>
#endif

#if defined(__linux__)
#include <sys/statfs.h>
#endif

#if defined(__FreeBSD__)
// for statfs()
#include <sys/param.h>
#include <sys/mount.h>
#endif

#if TORRENT_USE_WPATH

#ifdef BOOST_WINDOWS
#include <windows.h>
#endif

#include <boost/filesystem/exception.hpp>
#include "libtorrent/utf8.hpp"
#include "libtorrent/buffer.hpp"

namespace libtorrent
{
	std::wstring safe_convert(std::string const& s)
	{
		try
		{
			return libtorrent::utf8_wchar(s);
		}
		catch (std::exception)
		{
			std::wstring ret;
			const char* end = &s[0] + s.size();
			for (const char* i = &s[0]; i < end;)
			{
				wchar_t c = '.';
				int result = std::mbtowc(&c, i, end - i);
				if (result > 0) i += result;
				else ++i;
				ret += c;
			}
			return ret;
		}
	}
}
#endif

namespace fs = boost::filesystem;
using boost::bind;
using namespace ::boost::multi_index;
using boost::multi_index::multi_index_container;

#if defined TORRENT_DEBUG && defined TORRENT_STORAGE_DEBUG
namespace
{
	using namespace libtorrent;

	void print_to_log(const std::string& s)
	{
		static std::ofstream log("log.txt");
		log << s;
		log.flush();
	}
}
#endif

namespace libtorrent
{
	template <class Path>
	void recursive_copy(Path const& old_path, Path const& new_path, error_code& ec)
	{
		using boost::filesystem::basic_directory_iterator;
#ifndef BOOST_NO_EXCEPTIONS
		try {
#endif
		TORRENT_ASSERT(!ec);
		if (is_directory(old_path))
		{
			create_directory(new_path);
			for (basic_directory_iterator<Path> i(old_path), end; i != end; ++i)
			{
#if BOOST_VERSION < 103600
				recursive_copy(i->path(), new_path / i->path().leaf(), ec);
#else
				recursive_copy(i->path(), new_path / i->path().filename(), ec);
#endif
				if (ec) return;
			}
		}
		else
		{
			copy_file(old_path, new_path);
		}
#ifndef BOOST_NO_EXCEPTIONS
		} catch (std::exception& e) { ec = error_code(errno, get_posix_category()); }
#endif
	}

	template <class Path>
	void recursive_remove(Path const& old_path)
	{
		using boost::filesystem::basic_directory_iterator;
#ifndef BOOST_NO_EXCEPTIONS
		try {
#endif
		if (is_directory(old_path))
		{
			for (basic_directory_iterator<Path> i(old_path), end; i != end; ++i)
				recursive_remove(i->path());
			remove(old_path);
		}
		else
		{
			remove(old_path);
		}
#ifndef BOOST_NO_EXCEPTIONS
		} catch (std::exception& e) {}
#endif
	}
	std::vector<std::pair<size_type, std::time_t> > get_filesizes(
		file_storage const& s, fs::path p)
	{
		p = complete(p);
		std::vector<std::pair<size_type, std::time_t> > sizes;
		for (file_storage::iterator i = s.begin()
			, end(s.end());i != end; ++i)
		{
			size_type size = 0;
			std::time_t time = 0;
#if TORRENT_USE_WPATH
			fs::wpath f = safe_convert((p / i->path).string());
#else
			fs::path f = p / i->path;
#endif
#ifndef BOOST_NO_EXCEPTIONS
			try
#else
			if (exists(f))
#endif
			{
				size = file_size(f);
				time = last_write_time(f);
			}
#ifndef BOOST_NO_EXCEPTIONS
			catch (std::exception&) {}
#endif
			sizes.push_back(std::make_pair(size, time));
		}
		return sizes;
	}

	// matches the sizes and timestamps of the files passed in
	// in non-compact mode, actual file sizes and timestamps
	// are allowed to be bigger and more recent than the fast
	// resume data. This is because full allocation will not move
	// pieces, so any older version of the resume data will
	// still be a correct subset of the actual data on disk.
	bool match_filesizes(
		file_storage const& fs
		, fs::path p
		, std::vector<std::pair<size_type, std::time_t> > const& sizes
		, bool compact_mode
		, std::string* error)
	{
		if ((int)sizes.size() != fs.num_files())
		{
			if (error) *error = "mismatching number of files";
			return false;
		}
		p = complete(p);

		std::vector<std::pair<size_type, std::time_t> >::const_iterator s
			= sizes.begin();
		for (file_storage::iterator i = fs.begin()
			, end(fs.end());i != end; ++i, ++s)
		{
			size_type size = 0;
			std::time_t time = 0;

#if TORRENT_USE_WPATH
			fs::wpath f = safe_convert((p / i->path).string());
#else
			fs::path f = p / i->path;
#endif
#ifndef BOOST_NO_EXCEPTIONS
			try
#else
			if (exists(f))
#endif
			{
				size = file_size(f);
				time = last_write_time(f);
			}
#ifndef BOOST_NO_EXCEPTIONS
			catch (std::exception&) {}
#endif
			if ((compact_mode && size != s->first)
				|| (!compact_mode && size < s->first))
			{
				if (error) *error = "filesize mismatch for file '"
					+ i->path.external_file_string()
					+ "', size: " + boost::lexical_cast<std::string>(size)
					+ ", expected to be " + boost::lexical_cast<std::string>(s->first)
					+ " bytes";
				return false;
			}
			// allow one second 'slack', because of FAT volumes
			// in sparse mode, allow the files to be more recent
			// than the resume data, but only by 5 minutes
			if ((compact_mode && (time > s->second + 1 || time < s->second - 1)) ||
				(!compact_mode && (time > s->second + 5 * 60) || time < s->second - 1))
			{
				if (error) *error = "timestamp mismatch for file '"
					+ i->path.external_file_string()
					+ "', modification date: " + boost::lexical_cast<std::string>(time)
					+ ", expected to have modification date "
					+ boost::lexical_cast<std::string>(s->second);
				return false;
			}
		}
		return true;
	}

	class storage : public storage_interface, boost::noncopyable
	{
	public:
		storage(file_storage const& fs, fs::path const& path, file_pool& fp)
			: m_files(fs)
			, m_pool(fp)
		{
			TORRENT_ASSERT(m_files.begin() != m_files.end());
			m_save_path = fs::complete(path);
			TORRENT_ASSERT(m_save_path.is_complete());
		}

		bool rename_file(int index, std::string const& new_filename);
		bool release_files();
		bool delete_files();
		bool initialize(bool allocate_files);
		bool move_storage(fs::path save_path);
		int read(char* buf, int slot, int offset, int size);
		int write(const char* buf, int slot, int offset, int size);
		bool move_slot(int src_slot, int dst_slot);
		bool swap_slots(int slot1, int slot2);
		bool swap_slots3(int slot1, int slot2, int slot3);
		bool verify_resume_data(lazy_entry const& rd, std::string& error);
		bool write_resume_data(entry& rd) const;
		sha1_hash hash_for_slot(int slot, partial_hash& ph, int piece_size);

		int read_impl(char* buf, int slot, int offset, int size, bool fill_zero);

		~storage()
		{ m_pool.release(this); }

		file_storage const& files() const { return m_mapped_files?*m_mapped_files:m_files; }

		boost::scoped_ptr<file_storage> m_mapped_files;
		file_storage const& m_files;

		std::vector<boost::uint8_t> m_file_priority;
		fs::path m_save_path;
		// the file pool is typically stored in
		// the session, to make all storage
		// instances use the same pool
		file_pool& m_pool;
		
		// temporary storage for moving pieces
		buffer m_scratch_buffer;
	};

	sha1_hash storage::hash_for_slot(int slot, partial_hash& ph, int piece_size)
	{
		TORRENT_ASSERT(!error());
#ifdef TORRENT_DEBUG
		hasher partial;
		hasher whole;
		int slot_size1 = piece_size;
		m_scratch_buffer.resize(slot_size1);
		read_impl(&m_scratch_buffer[0], slot, 0, slot_size1, false);
		if (error()) return sha1_hash(0);
		if (ph.offset > 0)
			partial.update(&m_scratch_buffer[0], ph.offset);
		whole.update(&m_scratch_buffer[0], slot_size1);
		hasher partial_copy = ph.h;
		TORRENT_ASSERT(ph.offset == 0 || partial_copy.final() == partial.final());
#endif
		int slot_size = piece_size - ph.offset;
		if (slot_size > 0)
		{
			m_scratch_buffer.resize(slot_size);
			read_impl(&m_scratch_buffer[0], slot, ph.offset, slot_size, false);
			if (error()) return sha1_hash(0);
			ph.h.update(&m_scratch_buffer[0], slot_size);
		}
#ifdef TORRENT_DEBUG
		sha1_hash ret = ph.h.final();
		TORRENT_ASSERT(ret == whole.final());
		return ret;
#else
		return ph.h.final();
#endif
	}

	bool storage::initialize(bool allocate_files)
	{
		error_code ec;
		// first, create all missing directories
		fs::path last_path;
		for (file_storage::iterator file_iter = files().begin(),
			end_iter = files().end(); file_iter != end_iter; ++file_iter)
		{
			fs::path dir = (m_save_path / file_iter->path).branch_path();

			if (dir != last_path)
			{
				last_path = dir;

#if TORRENT_USE_WPATH
				fs::wpath wp = safe_convert(last_path.string());
				if (!exists(wp))
					create_directories(wp);
#else
				if (!exists(last_path))
					create_directories(last_path);
#endif
			}

#ifndef BOOST_NO_EXCEPTIONS
			try {
#endif

#if TORRENT_USE_WPATH
			fs::wpath file_path = safe_convert((m_save_path / file_iter->path).string());
#else
			fs::path file_path = m_save_path / file_iter->path;
#endif
			// if the file is empty, just create it either way.
			// if the file already exists, but is larger than what
			// it's supposed to be, also truncate it
			if (allocate_files
				|| file_iter->size == 0
				|| (exists(file_path) && file_size(file_path) > file_iter->size))
			{
				error_code ec;
				boost::shared_ptr<file> f = m_pool.open_file(this
					, m_save_path / file_iter->path, file::in | file::out, ec);
				if (ec) set_error(m_save_path / file_iter->path, ec);
				else if (f)
				{
					f->set_size(file_iter->size, ec);
					if (ec) set_error(m_save_path / file_iter->path, ec);
				}
			}
#ifndef BOOST_NO_EXCEPTIONS
			}
			catch (std::exception& e)
			{
				set_error(m_save_path / file_iter->path
					, error_code(errno, get_posix_category()));
				return true;
			}
#endif
		}
		std::vector<boost::uint8_t>().swap(m_file_priority);
		// close files that were opened in write mode
		m_pool.release(this);
		return false;
	}

	bool storage::rename_file(int index, std::string const& new_filename)
	{
		if (index < 0 || index >= m_files.num_files()) return true;
		fs::path old_name = m_save_path / files().at(index).path;
		m_pool.release(old_name);

#if TORRENT_USE_WPATH
		fs::wpath old_path = safe_convert(old_name.string());
		fs::wpath new_path = safe_convert((m_save_path / new_filename).string());
#else
		fs::path const& old_path = old_name;
		fs::path new_path = m_save_path / new_filename;
#endif

#ifndef BOOST_NO_EXCEPTIONS
		try
		{
#endif
			// if old path doesn't exist, just rename the file
			// in our file_storage, so that when it is created
			// it will get the new name
			create_directories(new_path.branch_path());
			if (exists(old_path)) rename(old_path, new_path);
/*
			error_code ec;
			rename(old_path, new_path, ec);
			if (ec)
			{
				set_error(old_path, ec);
				return;
			}
*/
			if (!m_mapped_files)
			{ m_mapped_files.reset(new file_storage(m_files)); }
			m_mapped_files->rename_file(index, new_filename);
#ifndef BOOST_NO_EXCEPTIONS
		}
#if BOOST_VERSION >= 103500
		catch (boost::system::system_error& e)
		{
			set_error(old_name, e.code());
			return true;
		}
#endif
		catch (std::exception& e)
		{
			set_error(old_name, error_code(errno, get_posix_category()));
			return true;
		}
#endif
		return false;
	}

	bool storage::release_files()
	{
		m_pool.release(this);
		buffer().swap(m_scratch_buffer);
		return false;
	}

	bool storage::delete_files()
	{
		// make sure we don't have the files open
		m_pool.release(this);
		buffer().swap(m_scratch_buffer);

		int error = 0;
		std::string error_file;

		// delete the files from disk
		std::set<std::string> directories;
		typedef std::set<std::string>::iterator iter_t;
		for (file_storage::iterator i = files().begin()
			, end(files().end()); i != end; ++i)
		{
			std::string p = (m_save_path / i->path).string();
			fs::path bp = i->path.branch_path();
			std::pair<iter_t, bool> ret;
			ret.second = true;
			while (ret.second && !bp.empty())
			{
				std::pair<iter_t, bool> ret = directories.insert((m_save_path / bp).string());
				bp = bp.branch_path();
			}
#if TORRENT_USE_WPATH
			try
			{ fs::remove(safe_convert(p)); }
			catch (std::exception& e)
			{
				error = errno;
				error_file = p;
			}
#else
			if (std::remove(p.c_str()) != 0 && errno != ENOENT)
			{
				error = errno;
				error_file = p;
			}
#endif
		}

		// remove the directories. Reverse order to delete
		// subdirectories first

		for (std::set<std::string>::reverse_iterator i = directories.rbegin()
			, end(directories.rend()); i != end; ++i)
		{
#if TORRENT_USE_WPATH
			try
			{ fs::remove(safe_convert(*i)); }
			catch (std::exception& e)
			{
				error = errno;
				error_file = *i;
			}
#else
			if (std::remove(i->c_str()) != 0 && errno != ENOENT)
			{
				error = errno;
				error_file = *i;
			}
#endif
		}

		if (error)
		{
			m_error = error_code(error, get_posix_category());
			m_error_file.swap(error_file);
			return true;
		}
		return false;
	}

	bool storage::write_resume_data(entry& rd) const
	{
		TORRENT_ASSERT(rd.type() == entry::dictionary_t);

		std::vector<std::pair<size_type, std::time_t> > file_sizes
			= get_filesizes(files(), m_save_path);

		entry::list_type& fl = rd["file sizes"].list();
		for (std::vector<std::pair<size_type, std::time_t> >::iterator i
			= file_sizes.begin(), end(file_sizes.end()); i != end; ++i)
		{
			entry::list_type p;
			p.push_back(entry(i->first));
			p.push_back(entry(i->second));
			fl.push_back(entry(p));
		}
		
		if (m_mapped_files)
		{
			entry::list_type& fl = rd["mapped_files"].list();
			for (file_storage::iterator i = m_mapped_files->begin()
				, end(m_mapped_files->end()); i != end; ++i)
			{
				fl.push_back(i->path.string());
			}
		}

		return false;
	}

	bool storage::verify_resume_data(lazy_entry const& rd, std::string& error)
	{
		lazy_entry const* file_priority = rd.dict_find_list("file_priority");
		if (file_priority && file_priority->list_size()
			== files().num_files())
		{
			m_file_priority.resize(file_priority->list_size());
			for (int i = 0; i < file_priority->list_size(); ++i)
				m_file_priority[i] = file_priority->list_int_value_at(i, 1);
		}

		std::vector<std::pair<size_type, std::time_t> > file_sizes;
		lazy_entry const* file_sizes_ent = rd.dict_find_list("file sizes");
		if (file_sizes_ent == 0)
		{
			error = "missing or invalid 'file sizes' entry in resume data";
			return false;
		}
		
		for (int i = 0; i < file_sizes_ent->list_size(); ++i)
		{
			lazy_entry const* e = file_sizes_ent->list_at(i);
			if (e->type() != lazy_entry::list_t
				|| e->list_size() != 2
				|| e->list_at(0)->type() != lazy_entry::int_t
				|| e->list_at(1)->type() != lazy_entry::int_t)
				continue;
			file_sizes.push_back(std::pair<size_type, std::time_t>(
				e->list_int_value_at(0), std::time_t(e->list_int_value_at(1))));
		}

		if (file_sizes.empty())
		{
			error = "the number of files in resume data is 0";
			return false;
		}
		
		bool seed = false;
		
		lazy_entry const* slots = rd.dict_find_list("slots");
		if (slots)
		{
			if (int(slots->list_size()) == m_files.num_pieces())
			{
				seed = true;
				for (int i = 0; i < slots->list_size(); ++i)
				{
					if (slots->list_int_value_at(i, -1) >= 0) continue;
					seed = false;
					break;
				}
			}
		}
		else if (lazy_entry const* pieces = rd.dict_find_string("pieces"))
		{
			if (int(pieces->string_length()) == m_files.num_pieces())
			{
				seed = true;
				char const* p = pieces->string_ptr();
				for (int i = 0; i < pieces->string_length(); ++i)
				{
					if ((p[i] & 1) == 1) continue;
					seed = false;
					break;
				}
			}
		}
		else
		{
			error = "missing 'slots' and 'pieces' entry in resume data";
			return false;
		}

		bool full_allocation_mode = false;
		if (rd.dict_find_string_value("allocation") != "compact")
			full_allocation_mode = true;

		if (seed)
		{
			if (files().num_files() != (int)file_sizes.size())
			{
				error = "the number of files does not match the torrent (num: "
					+ boost::lexical_cast<std::string>(file_sizes.size()) + " actual: "
					+ boost::lexical_cast<std::string>(files().num_files()) + ")";
				return false;
			}

			std::vector<std::pair<size_type, std::time_t> >::iterator
				fs = file_sizes.begin();
			// the resume data says we have the entire torrent
			// make sure the file sizes are the right ones
			for (file_storage::iterator i = files().begin()
				, end(files().end()); i != end; ++i, ++fs)
			{
				if (i->size != fs->first)
				{
					error = "file size for '" + i->path.external_file_string()
						+ "' was expected to be "
						+ boost::lexical_cast<std::string>(i->size) + " bytes";
					return false;
				}
			}
		}

		return match_filesizes(files(), m_save_path, file_sizes
			, !full_allocation_mode, &error);
	}

	// returns true on success
	bool storage::move_storage(fs::path save_path)
	{
#if TORRENT_USE_WPATH
		fs::wpath old_path;
		fs::wpath new_path;
#else
		fs::path old_path;
		fs::path new_path;
#endif

		save_path = complete(save_path);

#if TORRENT_USE_WPATH
		fs::wpath wp = safe_convert(save_path.string());
		if (!exists(wp))
			create_directory(wp);
		else if (!is_directory(wp))
			return false;
#else
		if (!exists(save_path))
			create_directory(save_path);
		else if (!is_directory(save_path))
			return false;
#endif

		m_pool.release(this);

#if TORRENT_USE_WPATH
		old_path = safe_convert((m_save_path / files().name()).string());
		new_path = safe_convert((save_path / files().name()).string());
#else
		old_path = m_save_path / files().name();
		new_path = save_path / files().name();
#endif

#ifndef BOOST_NO_EXCEPTIONS
		try
		{
#endif
			rename(old_path, new_path);
			m_save_path = save_path;
			return true;
#ifndef BOOST_NO_EXCEPTIONS
		}
		catch (std::exception& e)
		{
			error_code ec;
			recursive_copy(old_path, new_path, ec);
			if (ec)
			{
				set_error(m_save_path / files().name(), ec);
				return true;
			}
			m_save_path = save_path;
			recursive_remove(old_path);
		}
#endif
		return false;
	}

#ifdef TORRENT_DEBUG
/*
	void storage::shuffle()
	{
		int num_pieces = files().num_pieces();

		std::vector<int> pieces(num_pieces);
		for (std::vector<int>::iterator i = pieces.begin();
			i != pieces.end(); ++i)
		{
			*i = static_cast<int>(i - pieces.begin());
		}
		std::srand((unsigned int)std::time(0));
		std::vector<int> targets(pieces);
		std::random_shuffle(pieces.begin(), pieces.end());
		std::random_shuffle(targets.begin(), targets.end());

		for (int i = 0; i < (std::max)(num_pieces / 50, 1); ++i)
		{
			const int slot_index = targets[i];
			const int piece_index = pieces[i];
			const int slot_size =static_cast<int>(m_files.piece_size(slot_index));
			std::vector<char> buf(slot_size);
			read(&buf[0], piece_index, 0, slot_size);
			write(&buf[0], slot_index, 0, slot_size);
		}
	}
*/
#endif

	bool storage::move_slot(int src_slot, int dst_slot)
	{
		int piece_size = m_files.piece_size(dst_slot);
		m_scratch_buffer.resize(piece_size);
		int ret1 = read_impl(&m_scratch_buffer[0], src_slot, 0, piece_size, true);
		int ret2 = write(&m_scratch_buffer[0], dst_slot, 0, piece_size);
		return ret1 != piece_size || ret2 != piece_size;
	}

	bool storage::swap_slots(int slot1, int slot2)
	{
		// the size of the target slot is the size of the piece
		int piece_size = m_files.piece_length();
		int piece1_size = m_files.piece_size(slot2);
		int piece2_size = m_files.piece_size(slot1);
		m_scratch_buffer.resize(piece_size * 2);
		int ret1 = read_impl(&m_scratch_buffer[0], slot1, 0, piece1_size, true);
		int ret2 = read_impl(&m_scratch_buffer[piece_size], slot2, 0, piece2_size, true);
		int ret3 = write(&m_scratch_buffer[0], slot2, 0, piece1_size);
		int ret4 = write(&m_scratch_buffer[piece_size], slot1, 0, piece2_size);
		return ret1 != piece1_size || ret2 != piece2_size
			|| ret3 != piece1_size || ret4 != piece2_size;
	}

	bool storage::swap_slots3(int slot1, int slot2, int slot3)
	{
		// the size of the target slot is the size of the piece
		int piece_size = m_files.piece_length();
		int piece1_size = m_files.piece_size(slot2);
		int piece2_size = m_files.piece_size(slot3);
		int piece3_size = m_files.piece_size(slot1);
		m_scratch_buffer.resize(piece_size * 2);
		int ret1 = read_impl(&m_scratch_buffer[0], slot1, 0, piece1_size, true);
		int ret2 = read_impl(&m_scratch_buffer[piece_size], slot2, 0, piece2_size, true);
		int ret3 = write(&m_scratch_buffer[0], slot2, 0, piece1_size);
		int ret4 = read_impl(&m_scratch_buffer[0], slot3, 0, piece3_size, true);
		int ret5 = write(&m_scratch_buffer[piece_size], slot3, 0, piece2_size);
		int ret6 = write(&m_scratch_buffer[0], slot1, 0, piece3_size);
		return ret1 != piece1_size || ret2 != piece2_size
			|| ret3 != piece1_size || ret4 != piece3_size
			|| ret5 != piece2_size || ret6 != piece3_size;
	}

	int storage::read(
		char* buf
		, int slot
		, int offset
		, int size)
	{
		return read_impl(buf, slot, offset, size, false);
	}

	int storage::read_impl(
		char* buf
		, int slot
		, int offset
		, int size
		, bool fill_zero)
	{
		TORRENT_ASSERT(buf != 0);
		TORRENT_ASSERT(slot >= 0 && slot < m_files.num_pieces());
		TORRENT_ASSERT(offset >= 0);
		TORRENT_ASSERT(offset < m_files.piece_size(slot));
		TORRENT_ASSERT(size > 0);

#ifdef TORRENT_DEBUG
		std::vector<file_slice> slices
			= files().map_block(slot, offset, size);
		TORRENT_ASSERT(!slices.empty());
#endif

		size_type start = slot * (size_type)m_files.piece_length() + offset;
		TORRENT_ASSERT(start + size <= m_files.total_size());

		// find the file iterator and file offset
		size_type file_offset = start;
		std::vector<file_entry>::const_iterator file_iter;

		for (file_iter = files().begin();;)
		{
			if (file_offset < file_iter->size)
				break;

			file_offset -= file_iter->size;
			++file_iter;
		}

		int buf_pos = 0;
		error_code ec;
		boost::shared_ptr<file> in(m_pool.open_file(
			this, m_save_path / file_iter->path, file::in, ec));
		if (!in || ec)
		{
			set_error(m_save_path / file_iter->path, ec);
			return -1;
		}
		TORRENT_ASSERT(file_offset < file_iter->size);
		TORRENT_ASSERT(slices[0].offset == file_offset + file_iter->file_base);

		size_type new_pos = in->seek(file_offset + file_iter->file_base, file::begin, ec);
		if (new_pos != file_offset + file_iter->file_base || ec)
		{
			// the file was not big enough
			if (!fill_zero)
			{
				set_error(m_save_path / file_iter->path, ec);
				return -1;
			}
			std::memset(buf + buf_pos, 0, size - buf_pos);
			return size;
		}

#ifdef TORRENT_DEBUG
		size_type in_tell = in->tell(ec);
		TORRENT_ASSERT(in_tell == file_offset + file_iter->file_base && !ec);
#endif

		int left_to_read = size;
		int slot_size = static_cast<int>(m_files.piece_size(slot));

		if (offset + left_to_read > slot_size)
			left_to_read = slot_size - offset;

		TORRENT_ASSERT(left_to_read >= 0);

		size_type result = left_to_read;

#ifdef TORRENT_DEBUG
		int counter = 0;
#endif

		while (left_to_read > 0)
		{
			int read_bytes = left_to_read;
			if (file_offset + read_bytes > file_iter->size)
				read_bytes = static_cast<int>(file_iter->size - file_offset);

			if (read_bytes > 0)
			{
#ifdef TORRENT_DEBUG
				TORRENT_ASSERT(int(slices.size()) > counter);
				size_type slice_size = slices[counter].size;
				TORRENT_ASSERT(slice_size == read_bytes);
				TORRENT_ASSERT(files().at(slices[counter].file_index).path
					== file_iter->path);
#endif

				int actual_read = int(in->read(buf + buf_pos, read_bytes, ec));

				if (ec)
				{
					set_error(m_save_path / file_iter->path, ec);
					return -1;
				}

				if (read_bytes != actual_read)
				{
					// the file was not big enough
					if (!fill_zero)
					{
#ifdef TORRENT_WINDOWS
						ec = error_code(ERROR_READ_FAULT, get_system_category());
#else
						ec = error_code(EIO, get_posix_category());
#endif
						set_error(m_save_path / file_iter->path, ec);
						return actual_read;
					}
					if (actual_read > 0) buf_pos += actual_read;
					std::memset(buf + buf_pos, 0, size - buf_pos);
					return size;
				}

				left_to_read -= read_bytes;
				buf_pos += read_bytes;
				TORRENT_ASSERT(buf_pos >= 0);
				file_offset += read_bytes;
			}

			if (left_to_read > 0)
			{
				++file_iter;
#ifdef TORRENT_DEBUG
				// empty files are not returned by map_block, so if
				// this file was empty, don't increment the slice counter
				if (read_bytes > 0) ++counter;
#endif
				fs::path path = m_save_path / file_iter->path;

				file_offset = 0;
				error_code ec;
				in = m_pool.open_file( this, path, file::in, ec);
				if (!in || ec)
				{
					set_error(path, ec);
					return -1;
				}
				size_type pos = in->seek(file_iter->file_base, file::begin, ec);
				if (pos != file_iter->file_base || ec)
				{
					if (!fill_zero)
					{
						set_error(m_save_path / file_iter->path, ec);
						return -1;
					}
					std::memset(buf + buf_pos, 0, size - buf_pos);
					return size;
				}
			}
		}
		return result;
	}

	int storage::write(
		const char* buf
		, int slot
		, int offset
		, int size)
	{
		TORRENT_ASSERT(buf != 0);
		TORRENT_ASSERT(slot >= 0);
		TORRENT_ASSERT(slot < m_files.num_pieces());
		TORRENT_ASSERT(offset >= 0);
		TORRENT_ASSERT(size > 0);

#ifdef TORRENT_DEBUG
		std::vector<file_slice> slices
			= files().map_block(slot, offset, size);
		TORRENT_ASSERT(!slices.empty());
#endif

		size_type start = slot * (size_type)m_files.piece_length() + offset;

		// find the file iterator and file offset
		size_type file_offset = start;
		std::vector<file_entry>::const_iterator file_iter;

		for (file_iter = files().begin();;)
		{
			if (file_offset < file_iter->size)
				break;

			file_offset -= file_iter->size;
			++file_iter;
			TORRENT_ASSERT(file_iter != files().end());
		}

		fs::path p(m_save_path / file_iter->path);
		error_code ec;
		boost::shared_ptr<file> out = m_pool.open_file(
			this, p, file::out | file::in, ec);

		if (!out || ec)
		{
			set_error(p, ec);
			return -1;
		}
		TORRENT_ASSERT(file_offset < file_iter->size);
		TORRENT_ASSERT(slices[0].offset == file_offset + file_iter->file_base);

		size_type pos = out->seek(file_offset + file_iter->file_base, file::begin, ec);

		if (pos != file_offset + file_iter->file_base || ec)
		{
			set_error(p, ec);
			return -1;
		}

		int left_to_write = size;
		int slot_size = static_cast<int>(m_files.piece_size(slot));

		if (offset + left_to_write > slot_size)
			left_to_write = slot_size - offset;

		TORRENT_ASSERT(left_to_write >= 0);

		int buf_pos = 0;
#ifdef TORRENT_DEBUG
		int counter = 0;
#endif
		while (left_to_write > 0)
		{
			int write_bytes = left_to_write;
			if (file_offset + write_bytes > file_iter->size)
			{
				TORRENT_ASSERT(file_iter->size >= file_offset);
				write_bytes = static_cast<int>(file_iter->size - file_offset);
			}

			if (write_bytes > 0)
			{
				TORRENT_ASSERT(int(slices.size()) > counter);
				TORRENT_ASSERT(slices[counter].size == write_bytes);
				TORRENT_ASSERT(files().at(slices[counter].file_index).path
					== file_iter->path);

				TORRENT_ASSERT(buf_pos >= 0);
				TORRENT_ASSERT(write_bytes >= 0);
				error_code ec;
				size_type written = out->write(buf + buf_pos, write_bytes, ec);

				if (ec)
				{
					set_error(m_save_path / file_iter->path, ec);
					return -1;
				}

				if (write_bytes != written)
				{
					// the file was not big enough
#ifdef TORRENT_WINDOWS
					ec = error_code(ERROR_READ_FAULT, get_system_category());
#else
					ec = error_code(EIO, get_posix_category());
#endif
					set_error(m_save_path / file_iter->path, ec);
					return written;
				}

				left_to_write -= write_bytes;
				buf_pos += write_bytes;
				TORRENT_ASSERT(buf_pos >= 0);
				file_offset += write_bytes;
				TORRENT_ASSERT(file_offset <= file_iter->size);
			}

			if (left_to_write > 0)
			{
#ifdef TORRENT_DEBUG
				if (write_bytes > 0) ++counter;
#endif
				++file_iter;

				TORRENT_ASSERT(file_iter != files().end());
				fs::path p = m_save_path / file_iter->path;
				file_offset = 0;
				error_code ec;
				out = m_pool.open_file(
					this, p, file::out | file::in, ec);

				if (!out || ec)
				{
					set_error(p, ec);
					return -1;
				}

				size_type pos = out->seek(file_iter->file_base, file::begin, ec);

				if (pos != file_iter->file_base || ec)
				{
					set_error(p, ec);
					return -1;
				}
			}
		}
		return size;
	}

	storage_interface* default_storage_constructor(file_storage const& fs
		, fs::path const& path, file_pool& fp)
	{
		return new storage(fs, path, fp);
	}

	// -- piece_manager -----------------------------------------------------

	piece_manager::piece_manager(
		boost::shared_ptr<void> const& torrent
		, boost::intrusive_ptr<torrent_info const> info
		, fs::path const& save_path
		, file_pool& fp
		, disk_io_thread& io
		, storage_constructor_type sc
		, storage_mode_t sm)
		: m_info(info)
		, m_files(m_info->files())
		, m_storage(sc(m_files, save_path, fp))
		, m_storage_mode(sm)
		, m_save_path(complete(save_path))
		, m_state(state_none)
		, m_current_slot(0)
		, m_out_of_place(false)
		, m_scratch_piece(-1)
		, m_storage_constructor(sc)
		, m_io_thread(io)
		, m_torrent(torrent)
	{
	}

	piece_manager::~piece_manager()
	{
	}

	void piece_manager::async_save_resume_data(
		boost::function<void(int, disk_io_job const&)> const& handler)
	{
		disk_io_job j;
		j.storage = this;
		j.action = disk_io_job::save_resume_data;
		m_io_thread.add_job(j, handler);
	}

	void piece_manager::async_clear_read_cache(
		boost::function<void(int, disk_io_job const&)> const& handler)
	{
		disk_io_job j;
		j.storage = this;
		j.action = disk_io_job::clear_read_cache;
		m_io_thread.add_job(j, handler);
	}

	void piece_manager::async_release_files(
		boost::function<void(int, disk_io_job const&)> const& handler)
	{
		disk_io_job j;
		j.storage = this;
		j.action = disk_io_job::release_files;
		m_io_thread.add_job(j, handler);
	}

	void piece_manager::abort_disk_io()
	{
		m_io_thread.stop(this);
	}

	void piece_manager::async_delete_files(
		boost::function<void(int, disk_io_job const&)> const& handler)
	{
		disk_io_job j;
		j.storage = this;
		j.action = disk_io_job::delete_files;
		m_io_thread.add_job(j, handler);
	}

	void piece_manager::async_move_storage(fs::path const& p
		, boost::function<void(int, disk_io_job const&)> const& handler)
	{
		disk_io_job j;
		j.storage = this;
		j.action = disk_io_job::move_storage;
		j.str = p.string();
		m_io_thread.add_job(j, handler);
	}

	void piece_manager::async_check_fastresume(lazy_entry const* resume_data
		, boost::function<void(int, disk_io_job const&)> const& handler)
	{
		TORRENT_ASSERT(resume_data != 0);
		disk_io_job j;
		j.storage = this;
		j.action = disk_io_job::check_fastresume;
		j.buffer = (char*)resume_data;
		m_io_thread.add_job(j, handler);
	}

	void piece_manager::async_rename_file(int index, std::string const& name
		, boost::function<void(int, disk_io_job const&)> const& handler)
	{
		disk_io_job j;
		j.storage = this;
		j.piece = index;
		j.str = name;
		j.action = disk_io_job::rename_file;
		m_io_thread.add_job(j, handler);
	}

	void piece_manager::async_check_files(
		boost::function<void(int, disk_io_job const&)> const& handler)
	{
		disk_io_job j;
		j.storage = this;
		j.action = disk_io_job::check_files;
		m_io_thread.add_job(j, handler);
	}

	void piece_manager::async_read(
		peer_request const& r
		, boost::function<void(int, disk_io_job const&)> const& handler
		, int priority)
	{
		disk_io_job j;
		j.storage = this;
		j.action = disk_io_job::read;
		j.piece = r.piece;
		j.offset = r.start;
		j.buffer_size = r.length;
		j.buffer = 0;
		j.priority = priority;
		// if a buffer is not specified, only one block can be read
		// since that is the size of the pool allocator's buffers
		TORRENT_ASSERT(r.length <= 16 * 1024);
		m_io_thread.add_job(j, handler);
#ifdef TORRENT_DEBUG
		boost::recursive_mutex::scoped_lock l(m_mutex);
		// if this assert is hit, it suggests
		// that check_files was not successful
		TORRENT_ASSERT(slot_for(r.piece) >= 0);
#endif
	}

	void piece_manager::async_write(
		peer_request const& r
		, disk_buffer_holder& buffer
		, boost::function<void(int, disk_io_job const&)> const& handler)
	{
		TORRENT_ASSERT(r.length <= 16 * 1024);
		// the buffer needs to be allocated through the io_thread
		TORRENT_ASSERT(m_io_thread.is_disk_buffer(buffer.get()));

		disk_io_job j;
		j.storage = this;
		j.action = disk_io_job::write;
		j.piece = r.piece;
		j.offset = r.start;
		j.buffer_size = r.length;
		j.buffer = buffer.get();
		m_io_thread.add_job(j, handler);
		buffer.release();
	}

	void piece_manager::async_hash(int piece
		, boost::function<void(int, disk_io_job const&)> const& handler)
	{
		disk_io_job j;
		j.storage = this;
		j.action = disk_io_job::hash;
		j.piece = piece;

		m_io_thread.add_job(j, handler);
	}

	fs::path piece_manager::save_path() const
	{
		boost::recursive_mutex::scoped_lock l(m_mutex);
		return m_save_path;
	}

	sha1_hash piece_manager::hash_for_piece_impl(int piece)
	{
		partial_hash ph;

		std::map<int, partial_hash>::iterator i = m_piece_hasher.find(piece);
		if (i != m_piece_hasher.end())
		{
			ph = i->second;
			m_piece_hasher.erase(i);
		}

		int slot = slot_for(piece);
		TORRENT_ASSERT(slot != has_no_slot);
		return m_storage->hash_for_slot(slot, ph, m_files.piece_size(piece));
	}

	int piece_manager::move_storage_impl(fs::path const& save_path)
	{
		if (m_storage->move_storage(save_path))
		{
			m_save_path = fs::complete(save_path);
			return 0;
		}
		return -1;
	}

	void piece_manager::write_resume_data(entry& rd) const
	{
		boost::recursive_mutex::scoped_lock lock(m_mutex);

		INVARIANT_CHECK;

		m_storage->write_resume_data(rd);

		if (m_storage_mode == storage_mode_compact)
		{
			entry::list_type& slots = rd["slots"].list();
			slots.clear();
			std::vector<int>::const_reverse_iterator last; 
			for (last = m_slot_to_piece.rbegin();
				last != m_slot_to_piece.rend(); ++last)
			{
				if (*last != unallocated) break;
			}

			for (std::vector<int>::const_iterator i =
				m_slot_to_piece.begin();
				i != last.base(); ++i)
			{
				slots.push_back((*i >= 0) ? *i : unassigned);
			}
		}

		rd["allocation"] = m_storage_mode == storage_mode_sparse?"sparse"
			:m_storage_mode == storage_mode_allocate?"full":"compact";
	}

	void piece_manager::mark_failed(int piece_index)
	{
		INVARIANT_CHECK;

		if (m_storage_mode != storage_mode_compact) return;

		TORRENT_ASSERT(piece_index >= 0 && piece_index < (int)m_piece_to_slot.size());
		int slot_index = m_piece_to_slot[piece_index];
		TORRENT_ASSERT(slot_index >= 0);

		m_slot_to_piece[slot_index] = unassigned;
		m_piece_to_slot[piece_index] = has_no_slot;
		m_free_slots.push_back(slot_index);
	}

	int piece_manager::read_impl(
		char* buf
		, int piece_index
		, int offset
		, int size)
	{
		TORRENT_ASSERT(buf);
		TORRENT_ASSERT(offset >= 0);
		TORRENT_ASSERT(size > 0);
		int slot = slot_for(piece_index);
		return m_storage->read(buf, slot, offset, size);
	}

	int piece_manager::write_impl(
		const char* buf
	  , int piece_index
	  , int offset
	  , int size)
	{
		TORRENT_ASSERT(buf);
		TORRENT_ASSERT(offset >= 0);
		TORRENT_ASSERT(size > 0);
		TORRENT_ASSERT(piece_index >= 0 && piece_index < m_files.num_pieces());

		int slot = allocate_slot_for_piece(piece_index);
		int ret = m_storage->write(buf, slot, offset, size);
		// only save the partial hash if the write succeeds
		if (ret != size) return ret;

#ifdef TORRENT_PARTIAL_HASH_LOG
		std::ofstream out("partial_hash.log", std::ios::app);
#endif

		if (offset == 0)
		{
			partial_hash& ph = m_piece_hasher[piece_index];
			TORRENT_ASSERT(ph.offset == 0);
			ph.offset = size;
			ph.h.update(buf, size);
#ifdef TORRENT_PARTIAL_HASH_LOG
			out << time_now_string() << " NEW ["
				" s: " << this
				<< " p: " << piece_index
				<< " off: " << offset
				<< " size: " << size
				<< " entries: " << m_piece_hasher.size()
				<< " ]" << std::endl;
#endif
		}
		else
		{
			std::map<int, partial_hash>::iterator i = m_piece_hasher.find(piece_index);
			if (i != m_piece_hasher.end())
			{
#ifdef TORRENT_DEBUG
				TORRENT_ASSERT(i->second.offset > 0);
				int hash_offset = i->second.offset;
				TORRENT_ASSERT(offset >= hash_offset);
#endif
				if (offset == i->second.offset)
				{
#ifdef TORRENT_PARTIAL_HASH_LOG
					out << time_now_string() << " UPDATING ["
						" s: " << this
						<< " p: " << piece_index
						<< " off: " << offset
						<< " size: " << size
						<< " entries: " << m_piece_hasher.size()
						<< " ]" << std::endl;
#endif
					i->second.offset += size;
					i->second.h.update(buf, size);
				}
#ifdef TORRENT_PARTIAL_HASH_LOG
				else
				{
					out << time_now_string() << " SKIPPING (out of order) ["
						" s: " << this
						<< " p: " << piece_index
						<< " off: " << offset
						<< " size: " << size
						<< " entries: " << m_piece_hasher.size()
						<< " ]" << std::endl;
				}
#endif
			}
#ifdef TORRENT_PARTIAL_HASH_LOG
			else
			{
				out << time_now_string() << " SKIPPING (no entry) ["
					" s: " << this
					<< " p: " << piece_index
					<< " off: " << offset
					<< " size: " << size
					<< " entries: " << m_piece_hasher.size()
					<< " ]" << std::endl;
			}
#endif
		}
		
		return ret;
	}

	int piece_manager::identify_data(
		const std::vector<char>& piece_data
		, int current_slot)
	{
//		INVARIANT_CHECK;

		const int piece_size = static_cast<int>(m_files.piece_length());
		const int last_piece_size = static_cast<int>(m_files.piece_size(
			m_files.num_pieces() - 1));

		TORRENT_ASSERT((int)piece_data.size() >= last_piece_size);

		// calculate a small digest, with the same
		// size as the last piece. And a large digest
		// which has the same size as a normal piece
		hasher small_digest;
		small_digest.update(&piece_data[0], last_piece_size);
		hasher large_digest(small_digest);
		TORRENT_ASSERT(piece_size - last_piece_size >= 0);
		if (piece_size - last_piece_size > 0)
		{
			large_digest.update(
				&piece_data[last_piece_size]
				, piece_size - last_piece_size);
		}
		sha1_hash large_hash = large_digest.final();
		sha1_hash small_hash = small_digest.final();

		typedef std::multimap<sha1_hash, int>::const_iterator map_iter;
		map_iter begin1;
		map_iter end1;
		map_iter begin2;
		map_iter end2;

		// makes the lookups for the small digest and the large digest
		boost::tie(begin1, end1) = m_hash_to_piece.equal_range(small_hash);
		boost::tie(begin2, end2) = m_hash_to_piece.equal_range(large_hash);

		// copy all potential piece indices into this vector
		std::vector<int> matching_pieces;
		for (map_iter i = begin1; i != end1; ++i)
			matching_pieces.push_back(i->second);
		for (map_iter i = begin2; i != end2; ++i)
			matching_pieces.push_back(i->second);

		// no piece matched the data in the slot
		if (matching_pieces.empty())
			return unassigned;

		// ------------------------------------------
		// CHECK IF THE PIECE IS IN ITS CORRECT PLACE
		// ------------------------------------------

		if (std::find(
			matching_pieces.begin()
			, matching_pieces.end()
			, current_slot) != matching_pieces.end())
		{
			// the current slot is among the matching pieces, so
			// we will assume that the piece is in the right place
			const int piece_index = current_slot;

			int other_slot = m_piece_to_slot[piece_index];
			if (other_slot >= 0)
			{
				// we have already found a piece with
				// this index.

				// take one of the other matching pieces
				// that hasn't already been assigned
				int other_piece = -1;
				for (std::vector<int>::iterator i = matching_pieces.begin();
					i != matching_pieces.end(); ++i)
				{
					if (m_piece_to_slot[*i] >= 0 || *i == piece_index) continue;
					other_piece = *i;
					break;
				}
				if (other_piece >= 0)
				{
					// replace the old slot with 'other_piece'
					m_slot_to_piece[other_slot] = other_piece;
					m_piece_to_slot[other_piece] = other_slot;
				}
				else
				{
					// this index is the only piece with this
					// hash. The previous slot we found with
					// this hash must be the same piece. Mark
					// that piece as unassigned, since this slot
					// is the correct place for the piece.
					m_slot_to_piece[other_slot] = unassigned;
					if (m_storage_mode == storage_mode_compact)
						m_free_slots.push_back(other_slot);
				}
				TORRENT_ASSERT(m_piece_to_slot[piece_index] != current_slot);
				TORRENT_ASSERT(m_piece_to_slot[piece_index] >= 0);
				m_piece_to_slot[piece_index] = has_no_slot;
			}
			
			TORRENT_ASSERT(m_piece_to_slot[piece_index] == has_no_slot);

			return piece_index;
		}

		// find a matching piece that hasn't
		// already been assigned
		int free_piece = unassigned;
		for (std::vector<int>::iterator i = matching_pieces.begin();
			i != matching_pieces.end(); ++i)
		{
			if (m_piece_to_slot[*i] >= 0) continue;
			free_piece = *i;
			break;
		}

		if (free_piece >= 0)
		{
			TORRENT_ASSERT(m_piece_to_slot[free_piece] == has_no_slot);
			return free_piece;
		}
		else
		{
			TORRENT_ASSERT(free_piece == unassigned);
			return unassigned;
		}
	}

	int piece_manager::check_no_fastresume(std::string& error)
	{
		file_storage::iterator i = m_files.begin();
		file_storage::iterator end = m_files.end();

		for (; i != end; ++i)
		{
			bool file_exists = false;
			fs::path f = m_save_path / i->path;
#ifndef BOOST_NO_EXCEPTIONS
			try
			{
#endif
#if TORRENT_USE_WPATH
				fs::wpath wf = safe_convert(f.string());
				file_exists = exists(wf);
#else
				file_exists = exists(f);
#endif
#ifndef BOOST_NO_EXCEPTIONS
			}
			catch (std::exception& e)
			{
				error = f.string();
				error += ": ";
				error += e.what();
				TORRENT_ASSERT(!error.empty());
				return fatal_disk_error;
			}
#endif
			if (file_exists && i->size > 0)
			{
				m_state = state_full_check;
				m_piece_to_slot.clear();
				m_piece_to_slot.resize(m_files.num_pieces(), has_no_slot);
				m_slot_to_piece.clear();
				m_slot_to_piece.resize(m_files.num_pieces(), unallocated);
				if (m_storage_mode == storage_mode_compact)
				{
					m_unallocated_slots.clear();
					m_free_slots.clear();
				}
				TORRENT_ASSERT(int(m_piece_to_slot.size()) == m_files.num_pieces());
				return need_full_check;
			}
		}
	
		if (m_storage_mode == storage_mode_compact)
		{
			// in compact mode without checking, we need to
			// populate the unallocated list
			TORRENT_ASSERT(m_unallocated_slots.empty());
			for (int i = 0, end(m_files.num_pieces()); i < end; ++i)
				m_unallocated_slots.push_back(i);
			m_piece_to_slot.clear();
			m_piece_to_slot.resize(m_files.num_pieces(), has_no_slot);
			m_slot_to_piece.clear();
			m_slot_to_piece.resize(m_files.num_pieces(), unallocated);
		}
	
		return check_init_storage(error);
	}
	
	int piece_manager::check_init_storage(std::string& error)
	{
		if (m_storage->initialize(m_storage_mode == storage_mode_allocate))
		{
			error = m_storage->error().message();
			TORRENT_ASSERT(!error.empty());
			return fatal_disk_error;
		}
		m_state = state_finished;
		buffer().swap(m_scratch_buffer);
		buffer().swap(m_scratch_buffer2);
		if (m_storage_mode != storage_mode_compact)
		{
			// if no piece is out of place
			// since we're in full allocation mode, we can
			// forget the piece allocation tables
			std::vector<int>().swap(m_piece_to_slot);
			std::vector<int>().swap(m_slot_to_piece);
			std::vector<int>().swap(m_free_slots);
			std::vector<int>().swap(m_unallocated_slots);
		}
		return no_error;
	}

	// check if the fastresume data is up to date
	// if it is, use it and return true. If it 
	// isn't return false and the full check
	// will be run
	int piece_manager::check_fastresume(
		lazy_entry const& rd, std::string& error)
	{
		boost::recursive_mutex::scoped_lock lock(m_mutex);

		INVARIANT_CHECK;

		TORRENT_ASSERT(m_files.piece_length() > 0);

		// if we don't have any resume data, return
		if (rd.type() == lazy_entry::none_t) return check_no_fastresume(error);

		if (rd.type() != lazy_entry::dict_t)
		{
			error = "invalid fastresume data (not a dictionary)";
			return check_no_fastresume(error);
		}

		int block_size = (std::min)(16 * 1024, m_files.piece_length());
		int blocks_per_piece = rd.dict_find_int_value("blocks per piece", -1);
		if (blocks_per_piece != -1
			&& blocks_per_piece != m_files.piece_length() / block_size)
		{
			error = "invalid 'blocks per piece' entry";
			return check_no_fastresume(error);
		}

		storage_mode_t storage_mode = storage_mode_compact;
		if (rd.dict_find_string_value("allocation") != "compact")
			storage_mode = storage_mode_sparse;

		if (!m_storage->verify_resume_data(rd, error))
			return check_no_fastresume(error);

		// assume no piece is out of place (i.e. in a slot
		// other than the one it should be in)
		bool out_of_place = false;

		// if we don't have a piece map, we need the slots
		// if we're in compact mode, we also need the slots map
		if (storage_mode == storage_mode_compact || rd.dict_find("pieces") == 0)
		{
			// read slots map
			lazy_entry const* slots = rd.dict_find_list("slots");
			if (slots == 0)
			{
				error = "missing slot list";
				return check_no_fastresume(error);
			}

			if ((int)slots->list_size() > m_files.num_pieces())
			{
				error = "file has more slots than torrent (slots: "
					+ boost::lexical_cast<std::string>(slots->list_size()) + " size: "
					+ boost::lexical_cast<std::string>(m_files.num_pieces()) + " )";
				return check_no_fastresume(error);
			}

			if (m_storage_mode == storage_mode_compact)
			{
				int num_pieces = int(m_files.num_pieces());
				m_slot_to_piece.resize(num_pieces, unallocated);
				m_piece_to_slot.resize(num_pieces, has_no_slot);
				for (int i = 0; i < slots->list_size(); ++i)
				{
					lazy_entry const* e = slots->list_at(i);
					if (e->type() != lazy_entry::int_t)
					{
						error = "invalid entry type in slot list";
						return check_no_fastresume(error);
					}

					int index = int(e->int_value());
					if (index >= num_pieces || index < -2)
					{
						error = "too high index number in slot map (index: "
							+ boost::lexical_cast<std::string>(index) + " size: "
							+ boost::lexical_cast<std::string>(num_pieces) + ")";
						return check_no_fastresume(error);
					}
					if (index >= 0)
					{
						m_slot_to_piece[i] = index;
						m_piece_to_slot[index] = i;
						if (i != index) out_of_place = true;
					}
					else if (index == unassigned)
					{
						if (m_storage_mode == storage_mode_compact)
							m_free_slots.push_back(i);
					}
					else
					{
						TORRENT_ASSERT(index == unallocated);
						if (m_storage_mode == storage_mode_compact)
							m_unallocated_slots.push_back(i);
					}
				}
			}
			else
			{
				for (int i = 0; i < slots->list_size(); ++i)
				{
					lazy_entry const* e = slots->list_at(i);
					if (e->type() != lazy_entry::int_t)
					{
						error = "invalid entry type in slot list";
						return check_no_fastresume(error);
					}

					int index = int(e->int_value());
					if (index != i && index >= 0)
					{
						error = "invalid slot index";
						return check_no_fastresume(error);
					}
				}
			}

			// This will corrupt the storage
			// use while debugging to find
			// states that cannot be scanned
			// by check_pieces.
			//		m_storage->shuffle();

			if (m_storage_mode == storage_mode_compact)
			{
				if (m_unallocated_slots.empty()) switch_to_full_mode();
			}
			else
			{
				TORRENT_ASSERT(m_free_slots.empty());
				TORRENT_ASSERT(m_unallocated_slots.empty());

				if (out_of_place)
				{
					// in this case we're in full allocation mode, but
					// we're resuming a compact allocated storage
					m_state = state_expand_pieces;
					m_current_slot = 0;
					error = "pieces needs to be reordered";
					TORRENT_ASSERT(int(m_piece_to_slot.size()) == m_files.num_pieces());
					return need_full_check;
				}
			}

		}
		else if (m_storage_mode == storage_mode_compact)
		{
			// read piece map
			lazy_entry const* pieces = rd.dict_find("pieces");
			if (pieces == 0 || pieces->type() != lazy_entry::string_t)
			{
				error = "missing pieces entry";
				return check_no_fastresume(error);
			}

			if ((int)pieces->string_length() != m_files.num_pieces())
			{
				error = "file has more slots than torrent (slots: "
					+ boost::lexical_cast<std::string>(pieces->string_length()) + " size: "
					+ boost::lexical_cast<std::string>(m_files.num_pieces()) + " )";
				return check_no_fastresume(error);
			}

			int num_pieces = int(m_files.num_pieces());
			m_slot_to_piece.resize(num_pieces, unallocated);
			m_piece_to_slot.resize(num_pieces, has_no_slot);
			char const* have_pieces = pieces->string_ptr();
			for (int i = 0; i < num_pieces; ++i)
			{
				if (have_pieces[i] & 1)
				{
					m_slot_to_piece[i] = i;
					m_piece_to_slot[i] = i;
				}
				else
				{
					m_free_slots.push_back(i);
				}
			}
			if (m_unallocated_slots.empty()) switch_to_full_mode();
		}

		return check_init_storage(error);
	}

/*
   state chart:

   check_fastresume()  ----------+
                                 |
      |        |                 |
      |        v                 v
      |  +------------+   +---------------+
      |  | full_check |-->| expand_pieses |
      |  +------------+   +---------------+
      |        |                 |
      |        v                 |
      |  +--------------+        |
      +->|   finished   | <------+
         +--------------+
*/


	// performs the full check and full allocation
	// (if necessary). returns true if finished and
	// false if it should be called again
	// the second return value is the progress the
	// file check is at. 0 is nothing done, and 1
	// is finished
	int piece_manager::check_files(int& current_slot, int& have_piece, std::string& error)
	{
		if (m_state == state_none) return check_no_fastresume(error);

		TORRENT_ASSERT(int(m_piece_to_slot.size()) == m_files.num_pieces());

		current_slot = m_current_slot;
		have_piece = -1;
		if (m_state == state_expand_pieces)
		{
			INVARIANT_CHECK;

			if (m_scratch_piece >= 0)
			{
				int piece = m_scratch_piece;
				int other_piece = m_slot_to_piece[piece];
				m_scratch_piece = -1;

				if (other_piece >= 0)
				{
					if (m_scratch_buffer2.empty())
						m_scratch_buffer2.resize(m_files.piece_length());

					int piece_size = m_files.piece_size(other_piece);
					if (m_storage->read(&m_scratch_buffer2[0], piece, 0, piece_size)
						!= piece_size)
					{
						error = m_storage->error().message();
						TORRENT_ASSERT(!error.empty());
						return fatal_disk_error;
					}
					m_scratch_piece = other_piece;
					m_piece_to_slot[other_piece] = unassigned;
				}
				
				// the slot where this piece belongs is
				// free. Just move the piece there.
				int piece_size = m_files.piece_size(piece);
				if (m_storage->write(&m_scratch_buffer[0], piece, 0, piece_size) != piece_size)
				{
					error = m_storage->error().message();
					TORRENT_ASSERT(!error.empty());
					return fatal_disk_error;
				}
				m_piece_to_slot[piece] = piece;
				m_slot_to_piece[piece] = piece;

				if (other_piece >= 0)
					m_scratch_buffer.swap(m_scratch_buffer2);
		
				TORRENT_ASSERT(int(m_piece_to_slot.size()) == m_files.num_pieces());
				return need_full_check;
			}

			while (m_current_slot < m_files.num_pieces()
				&& (m_slot_to_piece[m_current_slot] == m_current_slot
				|| m_slot_to_piece[m_current_slot] < 0))
			{
				++m_current_slot;
			}

			if (m_current_slot == m_files.num_pieces())
			{
				return check_init_storage(error);
			}

			TORRENT_ASSERT(m_current_slot < m_files.num_pieces());

			int piece = m_slot_to_piece[m_current_slot];
			TORRENT_ASSERT(piece >= 0);
			int other_piece = m_slot_to_piece[piece];
			if (other_piece >= 0)
			{
				// there is another piece in the slot
				// where this one goes. Store it in the scratch
				// buffer until next iteration.
				if (m_scratch_buffer.empty())
					m_scratch_buffer.resize(m_files.piece_length());
			
				int piece_size = m_files.piece_size(other_piece);
				if (m_storage->read(&m_scratch_buffer[0], piece, 0, piece_size) != piece_size)
				{
					error = m_storage->error().message();
					TORRENT_ASSERT(!error.empty());
					return fatal_disk_error;
				}
				m_scratch_piece = other_piece;
				m_piece_to_slot[other_piece] = unassigned;
			}

			// the slot where this piece belongs is
			// free. Just move the piece there.
			m_storage->move_slot(m_current_slot, piece);
			m_piece_to_slot[piece] = piece;
			m_slot_to_piece[m_current_slot] = unassigned;
			m_slot_to_piece[piece] = piece;
		
			TORRENT_ASSERT(int(m_piece_to_slot.size()) == m_files.num_pieces());
			return need_full_check;
		}

		TORRENT_ASSERT(m_state == state_full_check);

		int skip = check_one_piece(have_piece);
		TORRENT_ASSERT(m_current_slot <= m_files.num_pieces());

		if (skip == -1)
		{
			error = m_storage->error().message();
			TORRENT_ASSERT(!error.empty());
			return fatal_disk_error;
		}

		if (skip)
		{
			clear_error();
			// skip means that the piece we checked failed to be read from disk
			// completely. We should skip all pieces belonging to that file.
			// find the file that failed, and skip all the pieces in that file
			size_type file_offset = 0;
			size_type current_offset = size_type(m_current_slot) * m_files.piece_length();
			for (file_storage::iterator i = m_files.begin()
				, end(m_files.end()); i != end; ++i)
			{
				file_offset += i->size;
				if (file_offset > current_offset) break;
			}

			TORRENT_ASSERT(file_offset > current_offset);
			int skip_blocks = static_cast<int>(
				(file_offset - current_offset + m_files.piece_length() - 1)
				/ m_files.piece_length());
			TORRENT_ASSERT(skip_blocks >= 1);

			if (m_storage_mode == storage_mode_compact)
			{
				for (int i = m_current_slot; i < m_current_slot + skip_blocks; ++i)
				{
					TORRENT_ASSERT(m_slot_to_piece[i] == unallocated);
					m_unallocated_slots.push_back(i);
				}
			}

			// current slot will increase by one at the end of the for-loop too
			m_current_slot += skip_blocks - 1;
			TORRENT_ASSERT(m_current_slot <= m_files.num_pieces());
		}

		++m_current_slot;
		current_slot = m_current_slot;

		if (m_current_slot >= m_files.num_pieces())
		{
			TORRENT_ASSERT(m_current_slot == m_files.num_pieces());

			// clear the memory we've been using
			std::vector<char>().swap(m_piece_data);
			std::multimap<sha1_hash, int>().swap(m_hash_to_piece);

			if (m_storage_mode != storage_mode_compact)
			{
				if (!m_out_of_place)
				{
					// if no piece is out of place
					// since we're in full allocation mode, we can
					// forget the piece allocation tables

					std::vector<int>().swap(m_piece_to_slot);
					std::vector<int>().swap(m_slot_to_piece);
					return check_init_storage(error);
				}
				else
				{
					// in this case we're in full allocation mode, but
					// we're resuming a compact allocated storage
					m_state = state_expand_pieces;
					m_current_slot = 0;
					current_slot = m_current_slot;
					TORRENT_ASSERT(int(m_piece_to_slot.size()) == m_files.num_pieces());
					return need_full_check;
				}
			}
			else if (m_unallocated_slots.empty())
			{
				switch_to_full_mode();
			}
			return check_init_storage(error);
		}

		TORRENT_ASSERT(int(m_piece_to_slot.size()) == m_files.num_pieces());
		return need_full_check;
	}

	// -1=error 0=ok 1=skip
	int piece_manager::check_one_piece(int& have_piece)
	{
		// ------------------------
		//    DO THE FULL CHECK
		// ------------------------

		TORRENT_ASSERT(int(m_piece_to_slot.size()) == m_files.num_pieces());
		TORRENT_ASSERT(int(m_slot_to_piece.size()) == m_files.num_pieces());
		TORRENT_ASSERT(have_piece == -1);

		// initialization for the full check
		if (m_hash_to_piece.empty())
		{
			for (int i = 0; i < m_files.num_pieces(); ++i)
				m_hash_to_piece.insert(std::make_pair(m_info->hash_for_piece(i), i));
		}

		m_piece_data.resize(int(m_files.piece_length()));
		int piece_size = m_files.piece_size(m_current_slot);
		int num_read = m_storage->read(&m_piece_data[0]
			, m_current_slot, 0, piece_size);

		if (num_read < 0)
		{
			if (m_storage->error()
#ifdef TORRENT_WINDOWS
				&& m_storage->error() != error_code(ERROR_FILE_NOT_FOUND, get_system_category()))
#else
				&& m_storage->error() != error_code(ENOENT, get_posix_category()))
#endif
				return -1;
			return 1;
		}

		// if the file is incomplete, skip the rest of it
		if (num_read != piece_size)
			return 1;

		int piece_index = identify_data(m_piece_data, m_current_slot);

		if (piece_index >= 0) have_piece = piece_index;

		if (piece_index != m_current_slot
			&& piece_index >= 0)
			m_out_of_place = true;

		TORRENT_ASSERT(piece_index == unassigned || piece_index >= 0);

		const bool this_should_move = piece_index >= 0 && m_slot_to_piece[piece_index] != unallocated;
		const bool other_should_move = m_piece_to_slot[m_current_slot] != has_no_slot;

		// check if this piece should be swapped with any other slot
		// this section will ensure that the storage is correctly sorted
		// libtorrent will never leave the storage in a state that
		// requires this sorting, but other clients may.

		// example of worst case:
		//                          | m_current_slot = 5
		//                          V
		//  +---+- - - +---+- - - +---+- -
		//  | x |      | 5 |      | 3 |     <- piece data in slots
		//  +---+- - - +---+- - - +---+- -
		//    3          y          5       <- slot index

		// in this example, the data in the m_current_slot (5)
		// is piece 3. It has to be moved into slot 3. The data
		// in slot y (piece 5) should be moved into the m_current_slot.
		// and the data in slot 3 (piece x) should be moved to slot y.

		// there are three possible cases.
		// 1. There's another piece that should be placed into this slot
		// 2. This piece should be placed into another slot.
		// 3. There's another piece that should be placed into this slot
		//    and this piece should be placed into another slot

		// swap piece_index with this slot

		// case 1
		if (this_should_move && !other_should_move)
		{
			TORRENT_ASSERT(piece_index != m_current_slot);

			const int other_slot = piece_index;
			TORRENT_ASSERT(other_slot >= 0);
			int other_piece = m_slot_to_piece[other_slot];

			m_slot_to_piece[other_slot] = piece_index;
			m_slot_to_piece[m_current_slot] = other_piece;
			m_piece_to_slot[piece_index] = piece_index;
			if (other_piece >= 0) m_piece_to_slot[other_piece] = m_current_slot;

			if (other_piece == unassigned)
			{
				std::vector<int>::iterator i =
					std::find(m_free_slots.begin(), m_free_slots.end(), other_slot);
				TORRENT_ASSERT(i != m_free_slots.end());
				if (m_storage_mode == storage_mode_compact)
				{
					m_free_slots.erase(i);
					m_free_slots.push_back(m_current_slot);
				}
			}

			bool ret = false;
			if (other_piece >= 0)
				ret |= m_storage->swap_slots(other_slot, m_current_slot);
			else
				ret |= m_storage->move_slot(m_current_slot, other_slot);

			if (ret) return 1;

			TORRENT_ASSERT(m_slot_to_piece[m_current_slot] == unassigned
				|| m_piece_to_slot[m_slot_to_piece[m_current_slot]] == m_current_slot);
		}
		// case 2
		else if (!this_should_move && other_should_move)
		{
			TORRENT_ASSERT(piece_index != m_current_slot);

			const int other_piece = m_current_slot;
			const int other_slot = m_piece_to_slot[other_piece];
			TORRENT_ASSERT(other_slot >= 0);

			m_slot_to_piece[m_current_slot] = other_piece;
			m_slot_to_piece[other_slot] = piece_index;
			m_piece_to_slot[other_piece] = m_current_slot;

			if (piece_index == unassigned
				&& m_storage_mode == storage_mode_compact)
				m_free_slots.push_back(other_slot);

			bool ret = false;
			if (piece_index >= 0)
			{
				m_piece_to_slot[piece_index] = other_slot;
				ret |= m_storage->swap_slots(other_slot, m_current_slot);
			}
			else
			{
				ret |= m_storage->move_slot(other_slot, m_current_slot);

			}
			if (ret) return 1;

			TORRENT_ASSERT(m_slot_to_piece[m_current_slot] == unassigned
				|| m_piece_to_slot[m_slot_to_piece[m_current_slot]] == m_current_slot);
		}
		else if (this_should_move && other_should_move)
		{
			TORRENT_ASSERT(piece_index != m_current_slot);
			TORRENT_ASSERT(piece_index >= 0);

			const int piece1 = m_slot_to_piece[piece_index];
			const int piece2 = m_current_slot;
			const int slot1 = piece_index;
			const int slot2 = m_piece_to_slot[piece2];

			TORRENT_ASSERT(slot1 >= 0);
			TORRENT_ASSERT(slot2 >= 0);
			TORRENT_ASSERT(piece2 >= 0);

			if (slot1 == slot2)
			{
				// this means there are only two pieces involved in the swap
				TORRENT_ASSERT(piece1 >= 0);

				// movement diagram:
				// +-------------------------------+
				// |                               |
				// +--> slot1 --> m_current_slot --+

				m_slot_to_piece[slot1] = piece_index;
				m_slot_to_piece[m_current_slot] = piece1;

				m_piece_to_slot[piece_index] = slot1;
				m_piece_to_slot[piece1] = m_current_slot;

				TORRENT_ASSERT(piece1 == m_current_slot);
				TORRENT_ASSERT(piece_index == slot1);

				m_storage->swap_slots(m_current_slot, slot1);

				TORRENT_ASSERT(m_slot_to_piece[m_current_slot] == unassigned
					|| m_piece_to_slot[m_slot_to_piece[m_current_slot]] == m_current_slot);
			}
			else
			{
				TORRENT_ASSERT(slot1 != slot2);
				TORRENT_ASSERT(piece1 != piece2);

				// movement diagram:
				// +-----------------------------------------+
				// |                                         |
				// +--> slot1 --> slot2 --> m_current_slot --+

				m_slot_to_piece[slot1] = piece_index;
				m_slot_to_piece[slot2] = piece1;
				m_slot_to_piece[m_current_slot] = piece2;

				m_piece_to_slot[piece_index] = slot1;
				m_piece_to_slot[m_current_slot] = piece2;

				if (piece1 == unassigned)
				{
					std::vector<int>::iterator i =
						std::find(m_free_slots.begin(), m_free_slots.end(), slot1);
					TORRENT_ASSERT(i != m_free_slots.end());
					if (m_storage_mode == storage_mode_compact)
					{
						m_free_slots.erase(i);
						m_free_slots.push_back(slot2);
					}
				}

				bool ret = false;
				if (piece1 >= 0)
				{
					m_piece_to_slot[piece1] = slot2;
					ret |= m_storage->swap_slots3(m_current_slot, slot1, slot2);
				}
				else
				{
					ret |= m_storage->move_slot(m_current_slot, slot1);
					ret |= m_storage->move_slot(slot2, m_current_slot);
				}

				if (ret) return 1;

				TORRENT_ASSERT(m_slot_to_piece[m_current_slot] == unassigned
					|| m_piece_to_slot[m_slot_to_piece[m_current_slot]] == m_current_slot);
			}
		}
		else
		{
			TORRENT_ASSERT(m_piece_to_slot[m_current_slot] == has_no_slot || piece_index != m_current_slot);
			TORRENT_ASSERT(m_slot_to_piece[m_current_slot] == unallocated);
			TORRENT_ASSERT(piece_index == unassigned || m_piece_to_slot[piece_index] == has_no_slot);

			// the slot was identified as piece 'piece_index'
			if (piece_index != unassigned)
				m_piece_to_slot[piece_index] = m_current_slot;
			else if (m_storage_mode == storage_mode_compact)
				m_free_slots.push_back(m_current_slot);

			m_slot_to_piece[m_current_slot] = piece_index;

			TORRENT_ASSERT(m_slot_to_piece[m_current_slot] == unassigned
				|| m_piece_to_slot[m_slot_to_piece[m_current_slot]] == m_current_slot);
		}
		return 0;
	}

	void piece_manager::switch_to_full_mode()
	{
		TORRENT_ASSERT(m_storage_mode == storage_mode_compact);	
		TORRENT_ASSERT(m_unallocated_slots.empty());	
		// we have allocated all slots, switch to
		// full allocation mode in order to free
		// some unnecessary memory.
		m_storage_mode = storage_mode_sparse;
		std::vector<int>().swap(m_unallocated_slots);
		std::vector<int>().swap(m_free_slots);
		std::vector<int>().swap(m_piece_to_slot);
		std::vector<int>().swap(m_slot_to_piece);
	}

	int piece_manager::allocate_slot_for_piece(int piece_index)
	{
		boost::recursive_mutex::scoped_lock lock(m_mutex);

		if (m_storage_mode != storage_mode_compact) return piece_index;

		INVARIANT_CHECK;

		TORRENT_ASSERT(piece_index >= 0);
		TORRENT_ASSERT(piece_index < (int)m_piece_to_slot.size());
		TORRENT_ASSERT(m_piece_to_slot.size() == m_slot_to_piece.size());

		int slot_index = m_piece_to_slot[piece_index];

		if (slot_index != has_no_slot)
		{
			TORRENT_ASSERT(slot_index >= 0);
			TORRENT_ASSERT(slot_index < (int)m_slot_to_piece.size());
			return slot_index;
		}

		if (m_free_slots.empty())
		{
			allocate_slots(1);
			TORRENT_ASSERT(!m_free_slots.empty());
		}

		std::vector<int>::iterator iter(
			std::find(
				m_free_slots.begin()
				, m_free_slots.end()
				, piece_index));

		if (iter == m_free_slots.end())
		{
			TORRENT_ASSERT(m_slot_to_piece[piece_index] != unassigned);
			TORRENT_ASSERT(!m_free_slots.empty());
			iter = m_free_slots.end() - 1;

			// special case to make sure we don't use the last slot
			// when we shouldn't, since it's smaller than ordinary slots
			if (*iter == m_files.num_pieces() - 1 && piece_index != *iter)
			{
				if (m_free_slots.size() == 1)
					allocate_slots(1);
				TORRENT_ASSERT(m_free_slots.size() > 1);
				// assumes that all allocated slots
				// are put at the end of the free_slots vector
				iter = m_free_slots.end() - 1;
			}
		}

		slot_index = *iter;
		m_free_slots.erase(iter);

		TORRENT_ASSERT(m_slot_to_piece[slot_index] == unassigned);

		m_slot_to_piece[slot_index] = piece_index;
		m_piece_to_slot[piece_index] = slot_index;

		// there is another piece already assigned to
		// the slot we are interested in, swap positions
		if (slot_index != piece_index
			&& m_slot_to_piece[piece_index] >= 0)
		{

#if defined TORRENT_DEBUG && defined TORRENT_STORAGE_DEBUG
			std::stringstream s;

			s << "there is another piece at our slot, swapping..";

			s << "\n   piece_index: ";
			s << piece_index;
			s << "\n   slot_index: ";
			s << slot_index;
			s << "\n   piece at our slot: ";
			s << m_slot_to_piece[piece_index];
			s << "\n";

			print_to_log(s.str());
			debug_log();
#endif

			int piece_at_our_slot = m_slot_to_piece[piece_index];
			TORRENT_ASSERT(m_piece_to_slot[piece_at_our_slot] == piece_index);

			std::swap(
				m_slot_to_piece[piece_index]
				, m_slot_to_piece[slot_index]);

			std::swap(
				m_piece_to_slot[piece_index]
				, m_piece_to_slot[piece_at_our_slot]);

			m_storage->move_slot(piece_index, slot_index);

			TORRENT_ASSERT(m_slot_to_piece[piece_index] == piece_index);
			TORRENT_ASSERT(m_piece_to_slot[piece_index] == piece_index);

			slot_index = piece_index;

#if defined TORRENT_DEBUG && defined TORRENT_STORAGE_DEBUG
			debug_log();
#endif
		}
		TORRENT_ASSERT(slot_index >= 0);
		TORRENT_ASSERT(slot_index < (int)m_slot_to_piece.size());

		if (m_free_slots.empty() && m_unallocated_slots.empty())
			switch_to_full_mode();
		
		return slot_index;
	}

	bool piece_manager::allocate_slots(int num_slots, bool abort_on_disk)
	{
		boost::recursive_mutex::scoped_lock lock(m_mutex);
		TORRENT_ASSERT(num_slots > 0);

		INVARIANT_CHECK;

		TORRENT_ASSERT(!m_unallocated_slots.empty());
		TORRENT_ASSERT(m_storage_mode == storage_mode_compact);

		bool written = false;

		for (int i = 0; i < num_slots && !m_unallocated_slots.empty(); ++i)
		{
//			INVARIANT_CHECK;

			int pos = m_unallocated_slots.front();
			TORRENT_ASSERT(m_slot_to_piece[pos] == unallocated);
			TORRENT_ASSERT(m_piece_to_slot[pos] != pos);

			int new_free_slot = pos;
			if (m_piece_to_slot[pos] != has_no_slot)
			{
				new_free_slot = m_piece_to_slot[pos];
				m_storage->move_slot(new_free_slot, pos);
				m_slot_to_piece[pos] = pos;
				m_piece_to_slot[pos] = pos;
				written = true;
			}
			m_unallocated_slots.erase(m_unallocated_slots.begin());
			m_slot_to_piece[new_free_slot] = unassigned;
			m_free_slots.push_back(new_free_slot);
			if (abort_on_disk && written) break;
		}

		TORRENT_ASSERT(m_free_slots.size() > 0);
		return written;
	}

	int piece_manager::slot_for(int piece) const
	{
		if (m_storage_mode != storage_mode_compact) return piece;
		TORRENT_ASSERT(piece < int(m_piece_to_slot.size()));
		TORRENT_ASSERT(piece >= 0);
		return m_piece_to_slot[piece];
	}

	int piece_manager::piece_for(int slot) const
	{
		if (m_storage_mode != storage_mode_compact) return slot;
		TORRENT_ASSERT(slot < int(m_slot_to_piece.size()));
		TORRENT_ASSERT(slot >= 0);
		return m_slot_to_piece[slot];
	}
		
#ifdef TORRENT_DEBUG
	void piece_manager::check_invariant() const
	{
		boost::recursive_mutex::scoped_lock lock(m_mutex);

		TORRENT_ASSERT(m_current_slot <= m_files.num_pieces());
		
		if (m_unallocated_slots.empty()
			&& m_free_slots.empty()
			&& m_state == state_finished)
		{
			TORRENT_ASSERT(m_storage_mode != storage_mode_compact
				|| m_files.num_pieces() == 0);
		}
		
		if (m_storage_mode != storage_mode_compact)
		{
			TORRENT_ASSERT(m_unallocated_slots.empty());
			TORRENT_ASSERT(m_free_slots.empty());
		}
		
		if (m_storage_mode != storage_mode_compact
			&& m_state != state_expand_pieces
			&& m_state != state_full_check)
		{
			TORRENT_ASSERT(m_piece_to_slot.empty());
			TORRENT_ASSERT(m_slot_to_piece.empty());
		}
		else
		{
			if (m_piece_to_slot.empty()) return;

			TORRENT_ASSERT((int)m_piece_to_slot.size() == m_files.num_pieces());
			TORRENT_ASSERT((int)m_slot_to_piece.size() == m_files.num_pieces());

			for (std::vector<int>::const_iterator i = m_free_slots.begin();
					i != m_free_slots.end(); ++i)
			{
				TORRENT_ASSERT(*i < (int)m_slot_to_piece.size());
				TORRENT_ASSERT(*i >= 0);
				TORRENT_ASSERT(m_slot_to_piece[*i] == unassigned);
				TORRENT_ASSERT(std::find(i+1, m_free_slots.end(), *i)
						== m_free_slots.end());
			}

			for (std::vector<int>::const_iterator i = m_unallocated_slots.begin();
					i != m_unallocated_slots.end(); ++i)
			{
				TORRENT_ASSERT(*i < (int)m_slot_to_piece.size());
				TORRENT_ASSERT(*i >= 0);
				TORRENT_ASSERT(m_slot_to_piece[*i] == unallocated);
				TORRENT_ASSERT(std::find(i+1, m_unallocated_slots.end(), *i)
						== m_unallocated_slots.end());
			}

			for (int i = 0; i < m_files.num_pieces(); ++i)
			{
				// Check domain of piece_to_slot's elements
				if (m_piece_to_slot[i] != has_no_slot)
				{
					TORRENT_ASSERT(m_piece_to_slot[i] >= 0);
					TORRENT_ASSERT(m_piece_to_slot[i] < (int)m_slot_to_piece.size());
				}

				// Check domain of slot_to_piece's elements
				if (m_slot_to_piece[i] != unallocated
						&& m_slot_to_piece[i] != unassigned)
				{
					TORRENT_ASSERT(m_slot_to_piece[i] >= 0);
					TORRENT_ASSERT(m_slot_to_piece[i] < (int)m_piece_to_slot.size());
				}

				// do more detailed checks on piece_to_slot
				if (m_piece_to_slot[i] >= 0)
				{
					TORRENT_ASSERT(m_slot_to_piece[m_piece_to_slot[i]] == i);
					if (m_piece_to_slot[i] != i)
					{
						TORRENT_ASSERT(m_slot_to_piece[i] == unallocated);
					}
				}
				else
				{
					TORRENT_ASSERT(m_piece_to_slot[i] == has_no_slot);
				}

				// do more detailed checks on slot_to_piece

				if (m_slot_to_piece[i] >= 0)
				{
					TORRENT_ASSERT(m_slot_to_piece[i] < (int)m_piece_to_slot.size());
					TORRENT_ASSERT(m_piece_to_slot[m_slot_to_piece[i]] == i);
#ifdef TORRENT_STORAGE_DEBUG
					TORRENT_ASSERT(
							std::find(
								m_unallocated_slots.begin()
								, m_unallocated_slots.end()
								, i) == m_unallocated_slots.end()
							);
					TORRENT_ASSERT(
							std::find(
								m_free_slots.begin()
								, m_free_slots.end()
								, i) == m_free_slots.end()
							);
#endif
				}
				else if (m_slot_to_piece[i] == unallocated)
				{
#ifdef TORRENT_STORAGE_DEBUG
					TORRENT_ASSERT(m_unallocated_slots.empty()
							|| (std::find(
									m_unallocated_slots.begin()
									, m_unallocated_slots.end()
									, i) != m_unallocated_slots.end())
							);
#endif
				}
				else if (m_slot_to_piece[i] == unassigned)
				{
#ifdef TORRENT_STORAGE_DEBUG
					TORRENT_ASSERT(
							std::find(
								m_free_slots.begin()
								, m_free_slots.end()
								, i) != m_free_slots.end()
							);
#endif
				}
				else
				{
					TORRENT_ASSERT(false && "m_slot_to_piece[i] is invalid");
				}
			}
		}
	}

#ifdef TORRENT_STORAGE_DEBUG
	void piece_manager::debug_log() const
	{
		std::stringstream s;

		s << "index\tslot\tpiece\n";

		for (int i = 0; i < m_files.num_pieces(); ++i)
		{
			s << i << "\t" << m_slot_to_piece[i] << "\t";
			s << m_piece_to_slot[i] << "\n";
		}

		s << "---------------------------------\n";

		print_to_log(s.str());
	}
#endif
#endif
} // namespace libtorrent

