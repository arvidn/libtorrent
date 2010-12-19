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

#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/ref.hpp>
#include <boost/bind.hpp>
#include <boost/version.hpp>
#include <boost/scoped_array.hpp>
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
#include "libtorrent/alloca.hpp"
#include "libtorrent/allocator.hpp" // page_size

#include <cstdio>

//#define TORRENT_PARTIAL_HASH_LOG

#if TORRENT_USE_IOSTREAM
#include <boost/filesystem/fstream.hpp>
#include <ios>
#include <iostream>
#include <iomanip>
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

// for convert_to_wstring and convert_to_native
#include "libtorrent/escape_string.hpp"

namespace fs = boost::filesystem;

#if defined TORRENT_DEBUG && defined TORRENT_STORAGE_DEBUG && TORRENT_USE_IOSTREAM
namespace
{
	using namespace libtorrent;

	void print_to_log(std::string const& s)
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
		}
#if BOOST_VERSION >= 103500
		catch (boost::system::system_error& e)
		{
			ec = e.code();
		}
#else
		catch (boost::filesystem::filesystem_error& e)
		{
			ec = error_code(e.system_error(), get_system_category());
		}
#endif // BOOST_VERSION
#endif // BOOST_NO_EXCEPTIONS
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
		} catch (std::exception&) {}
#endif
	}
	std::vector<std::pair<size_type, std::time_t> > get_filesizes(
		file_storage const& s, fs::path p)
	{
#ifndef BOOST_NO_EXCEPTIONS
		try {
#endif
		p = complete(p);
#ifndef BOOST_NO_EXCEPTIONS
		} catch (std::exception&) {}
#endif
		std::vector<std::pair<size_type, std::time_t> > sizes;
		for (file_storage::iterator i = s.begin()
			, end(s.end());i != end; ++i)
		{
			size_type size = 0;
			std::time_t time = 0;
			if (i->pad_file)
			{
				sizes.push_back(std::make_pair(i->size, time));
				continue;
			}
#if TORRENT_USE_WPATH
			fs::wpath f = convert_to_wstring((p / i->path).string());
#else
			fs::path f = convert_to_native((p / i->path).string());
#endif
			// TODO: optimize
			if (exists(f))
#ifndef BOOST_NO_EXCEPTIONS
			try
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
		, error_code& error)
	{
		if ((int)sizes.size() != fs.num_files())
		{
			error = errors::mismatching_number_of_files;
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
			if (i->pad_file) continue;

#if TORRENT_USE_WPATH
			fs::wpath f = convert_to_wstring((p / i->path).string());
#else
			fs::path f = convert_to_native((p / i->path).string());
#endif
			// TODO: Optimize this! This will result in 3 stat calls per file!
			if (exists(f))
#ifndef BOOST_NO_EXCEPTIONS
			try
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
				error = errors::mismatching_file_size;
				return false;
			}
			// allow one second 'slack', because of FAT volumes
			// in sparse mode, allow the files to be more recent
			// than the resume data, but only by 5 minutes
			if ((compact_mode && (time > s->second + 1 || time < s->second - 1)) ||
				(!compact_mode && (time > s->second + 5 * 60 || time < s->second - 1)))
			{
				error = errors::mismatching_file_timestamp;
				return false;
			}
		}
		return true;
	}

	// for backwards compatibility, let the default readv and
	// writev implementations be implemented in terms of the
	// old read and write
	int storage_interface::readv(file::iovec_t const* bufs
		, int slot, int offset, int num_bufs)
	{
		int ret = 0;
		for (file::iovec_t const* i = bufs, *end(bufs + num_bufs); i < end; ++i)
		{
			int r = read((char*)i->iov_base, slot, offset, i->iov_len);
			offset += i->iov_len;
			if (r == -1) return -1;
			ret += r;
		}
		return ret;
	}

	int storage_interface::writev(file::iovec_t const* bufs, int slot
		, int offset, int num_bufs)
	{
		int ret = 0;
		for (file::iovec_t const* i = bufs, *end(bufs + num_bufs); i < end; ++i)
		{
			int r = write((char const*)i->iov_base, slot, offset, i->iov_len);
			offset += i->iov_len;
			if (r == -1) return -1;
			ret += r;
		}
		return ret;
	}

	int copy_bufs(file::iovec_t const* bufs, int bytes, file::iovec_t* target)
	{
		int size = 0;
		int ret = 1;
		for (;;)
		{
			*target = *bufs;
			size += bufs->iov_len;
			if (size >= bytes)
			{
				target->iov_len -= size - bytes;
				return ret;
			}
			++bufs;
			++target;
			++ret;
		}
	}

	void advance_bufs(file::iovec_t*& bufs, int bytes)
	{
		int size = 0;
		for (;;)
		{
			size += bufs->iov_len;
			if (size >= bytes)
			{
				((char*&)bufs->iov_base) += bufs->iov_len - (size - bytes);
				bufs->iov_len = size - bytes;
				return;
			}
			++bufs;
		}
	}

	int bufs_size(file::iovec_t const* bufs, int num_bufs)
	{
		int size = 0;
		for (file::iovec_t const* i = bufs, *end(bufs + num_bufs); i < end; ++i)
			size += i->iov_len;
		return size;
	}
	
	void clear_bufs(file::iovec_t const* bufs, int num_bufs)
	{
		for (file::iovec_t const* i = bufs, *end(bufs + num_bufs); i < end; ++i)
			std::memset(i->iov_base, 0, i->iov_len);
	}

#ifdef TORRENT_DEBUG
	int count_bufs(file::iovec_t const* bufs, int bytes)
	{
		int size = 0;
		int count = 1;
		if (bytes == 0) return 0;
		for (file::iovec_t const* i = bufs;; ++i, ++count)
		{
			size += i->iov_len;
			TORRENT_ASSERT(size <= bytes);
			if (size >= bytes) return count;
		}
	}
#endif

	class storage : public storage_interface, boost::noncopyable
	{
	public:
		storage(file_storage const& fs, file_storage const* mapped, fs::path const& path, file_pool& fp)
			: m_files(fs)
			, m_pool(fp)
			, m_page_size(page_size())
			, m_allocate_files(false)
		{
			if (mapped) m_mapped_files.reset(new file_storage(*mapped));

			TORRENT_ASSERT(m_files.begin() != m_files.end());
#ifndef BOOST_NO_EXCEPTIONS
			try {
#endif
			m_save_path = fs::complete(path);
#ifndef BOOST_NO_EXCEPTIONS
			} catch (std::exception&) {
			m_save_path = path;
			}
#endif
			TORRENT_ASSERT(m_save_path.is_complete());
		}

		bool has_any_file();
		bool rename_file(int index, std::string const& new_filename);
		bool release_files();
		bool delete_files();
		bool initialize(bool allocate_files);
		bool move_storage(fs::path save_path);
		int read(char* buf, int slot, int offset, int size);
		int write(char const* buf, int slot, int offset, int size);
		int sparse_end(int start) const;
		int readv(file::iovec_t const* bufs, int slot, int offset, int num_bufs);
		int writev(file::iovec_t const* buf, int slot, int offset, int num_bufs);
		size_type physical_offset(int slot, int offset);
		bool move_slot(int src_slot, int dst_slot);
		bool swap_slots(int slot1, int slot2);
		bool swap_slots3(int slot1, int slot2, int slot3);
		bool verify_resume_data(lazy_entry const& rd, error_code& error);
		bool write_resume_data(entry& rd) const;

		// this identifies a read or write operation
		// so that storage::readwrite() knows what to
		// do when it's actually touching the file
		struct fileop
		{
			size_type (file::*regular_op)(size_type file_offset
				, file::iovec_t const* bufs, int num_bufs, error_code& ec);
			size_type (storage::*unaligned_op)(boost::shared_ptr<file> const& f
				, size_type file_offset, file::iovec_t const* bufs, int num_bufs
				, error_code& ec);
			int cache_setting;
			int mode;
		};

		void delete_one_file(std::string const& p);
		int readwritev(file::iovec_t const* bufs, int slot, int offset
			, int num_bufs, fileop const&);

		~storage()
		{ m_pool.release(this); }

		size_type read_unaligned(boost::shared_ptr<file> const& file_handle
			, size_type file_offset, file::iovec_t const* bufs, int num_bufs, error_code& ec);
		size_type write_unaligned(boost::shared_ptr<file> const& file_handle
			, size_type file_offset, file::iovec_t const* bufs, int num_bufs, error_code& ec);

		file_storage const& files() const { return m_mapped_files?*m_mapped_files:m_files; }

		boost::scoped_ptr<file_storage> m_mapped_files;
		file_storage const& m_files;

		std::vector<boost::uint8_t> m_file_priority;
		fs::path m_save_path;
		// the file pool is typically stored in
		// the session, to make all storage
		// instances use the same pool
		file_pool& m_pool;

		int m_page_size;
		bool m_allocate_files;
	};

	int piece_manager::hash_for_slot(int slot, partial_hash& ph, int piece_size
		, int small_piece_size, sha1_hash* small_hash)
	{
		TORRENT_ASSERT(!error());
		int num_read = 0;
		int slot_size = piece_size - ph.offset;
		if (slot_size > 0)
		{
			int block_size = 16 * 1024;
			if (m_storage->disk_pool()) block_size = m_storage->disk_pool()->block_size();
			int size = slot_size;
			int num_blocks = (size + block_size - 1) / block_size;

			// when we optimize for speed we allocate all the buffers we
			// need for the rest of the piece, and read it all in one call
			// and then hash it. When optimizing for memory usage, we read
			// one block at a time and hash it. This ends up only using a
			// single buffer
			if (m_storage->settings().optimize_hashing_for_speed)
			{
				file::iovec_t* bufs = TORRENT_ALLOCA(file::iovec_t, num_blocks);
				for (int i = 0; i < num_blocks; ++i)
				{
					bufs[i].iov_base = m_storage->disk_pool()->allocate_buffer("hash temp");
					bufs[i].iov_len = (std::min)(block_size, size);
					size -= bufs[i].iov_len;
				}
				num_read = m_storage->readv(bufs, slot, ph.offset, num_blocks);

				for (int i = 0; i < num_blocks; ++i)
				{
					if (small_hash && small_piece_size <= block_size)
					{
						ph.h.update((char const*)bufs[i].iov_base, small_piece_size);
						*small_hash = hasher(ph.h).final();
						small_hash = 0; // avoid this case again
						if (bufs[i].iov_len > small_piece_size)
							ph.h.update((char const*)bufs[i].iov_base + small_piece_size
								, bufs[i].iov_len - small_piece_size);
					}
					else
					{
						ph.h.update((char const*)bufs[i].iov_base, bufs[i].iov_len);
						small_piece_size -= bufs[i].iov_len;
					}
					ph.offset += bufs[i].iov_len;
					m_storage->disk_pool()->free_buffer((char*)bufs[i].iov_base);
				}
			}
			else
			{
				file::iovec_t buf;
				disk_buffer_holder holder(*m_storage->disk_pool()
					, m_storage->disk_pool()->allocate_buffer("hash temp"));
				buf.iov_base = holder.get();
				for (int i = 0; i < num_blocks; ++i)
				{
					buf.iov_len = (std::min)(block_size, size);
					int ret = m_storage->readv(&buf, slot, ph.offset, 1);
					if (ret > 0) num_read += ret;

					if (small_hash && small_piece_size <= block_size)
					{
						if (small_piece_size > 0) ph.h.update((char const*)buf.iov_base, small_piece_size);
						*small_hash = hasher(ph.h).final();
						small_hash = 0; // avoid this case again
						if (buf.iov_len > small_piece_size)
							ph.h.update((char const*)buf.iov_base + small_piece_size
								, buf.iov_len - small_piece_size);
					}
					else
					{
						ph.h.update((char const*)buf.iov_base, buf.iov_len);
						small_piece_size -= buf.iov_len;
					}

					ph.offset += buf.iov_len;
					size -= buf.iov_len;
				}
			}
			if (error()) return 0;
		}
		return num_read;
	}

	bool storage::initialize(bool allocate_files)
	{
		m_allocate_files = allocate_files;
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
				fs::wpath wp = convert_to_wstring(last_path.string());
				if (!exists(wp))
					create_directories(wp);
#else
				fs::path p = convert_to_native(last_path.string());
				if (!exists(p))
					create_directories(p);
#endif
			}

			int file_index = file_iter - files().begin();

			// ignore files that have priority 0
			if (int(m_file_priority.size()) > file_index
				&& m_file_priority[file_index] == 0) continue;

			// ignore pad files
			if (file_iter->pad_file) continue;

#ifndef BOOST_NO_EXCEPTIONS
			try {
#endif

#if TORRENT_USE_WPATH
			fs::wpath file_path = convert_to_wstring((m_save_path / file_iter->path).string());
#else
			fs::path file_path = convert_to_native((m_save_path / file_iter->path).string());
#endif
			// if the file is empty, just create it either way.
			// if the file already exists, but is larger than what
			// it's supposed to be, also truncate it
			if (allocate_files
				|| file_iter->size == 0
				|| (exists(file_path) && file_size(file_path) > file_iter->size))
			{
				error_code ec;
				int mode = file::read_write;
				if (m_settings
					&& (settings().disk_io_read_mode == session_settings::disable_os_cache
					|| (settings().disk_io_read_mode == session_settings::disable_os_cache_for_aligned_files
					&& ((file_iter->offset + file_iter->file_base) & (m_page_size-1)) == 0)))
					mode |= file::no_buffer;
				if (!m_allocate_files) mode |= file::sparse;
				boost::shared_ptr<file> f = m_pool.open_file(this
					, m_save_path / file_iter->path, mode, ec);
				if (ec) set_error(m_save_path / file_iter->path, ec);
				else if (f)
				{
					f->set_size(file_iter->size, ec);
					if (ec) set_error(m_save_path / file_iter->path, ec);
				}
			}
#ifndef BOOST_NO_EXCEPTIONS
			}
#if BOOST_VERSION >= 103500
			catch (boost::system::system_error& e)
			{
				set_error(m_save_path / file_iter->path, e.code());
				return true;
			}
#else
			catch (boost::filesystem::filesystem_error& e)
			{
				set_error(m_save_path / file_iter->path
					, error_code(e.system_error(), get_system_category()));
				return true;
			}
#endif // BOOST_VERSION
#endif // BOOST_NO_EXCEPTIONS
		}
		std::vector<boost::uint8_t>().swap(m_file_priority);
		// close files that were opened in write mode
		m_pool.release(this);
		return false;
	}

	bool storage::has_any_file()
	{
		file_storage::iterator i = files().begin();
		file_storage::iterator end = files().end();

		for (; i != end; ++i)
		{
			bool file_exists = false;
#if TORRENT_USE_WPATH
			fs::wpath f = convert_to_wstring((m_save_path / i->path).string());
#else
			fs::path f = convert_to_native((m_save_path / i->path).string());
#endif
#ifndef BOOST_NO_EXCEPTIONS
			try
			{
#endif
				file_exists = exists(f);
#ifndef BOOST_NO_EXCEPTIONS
			}
#if BOOST_VERSION >= 103500
			catch (boost::system::system_error& e)
			{
				set_error(m_save_path / i->path, e.code());
				return false;
			}
#else
			catch (boost::filesystem::filesystem_error& e)
			{
				set_error(m_save_path / i->path, error_code(e.system_error(), get_system_category()));
				return false;
			}
#endif // BOOST_VERSION
#endif // BOOST_NO_EXCEPTIONS
			if (file_exists && i->size > 0)
				return true;
		}
		return false;
	}

	bool storage::rename_file(int index, std::string const& new_filename)
	{
		if (index < 0 || index >= m_files.num_files()) return true;
		fs::path old_name = m_save_path / files().at(index).path;
		m_pool.release(old_name);

#if TORRENT_USE_WPATH
		fs::wpath old_path = convert_to_wstring(old_name.string());
		fs::wpath new_path = convert_to_wstring((m_save_path / new_filename).string());
#else
		fs::path const& old_path = convert_to_native(old_name.string());
		fs::path new_path = convert_to_native((m_save_path / new_filename).string());
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
#else
		catch (boost::filesystem::filesystem_error& e)
		{
			set_error(old_name, error_code(e.system_error()
				, get_system_category()));
			return true;
		}
#endif // BOOST_VERSION
#endif
		return false;
	}

	bool storage::release_files()
	{
		m_pool.release(this);
		return false;
	}

	void storage::delete_one_file(std::string const& p)
	{
#if TORRENT_USE_WPATH
#ifndef BOOST_NO_EXCEPTIONS
		try
#endif
		{ fs::remove(convert_to_wstring(p)); }
#ifndef BOOST_NO_EXCEPTIONS
#if BOOST_VERSION >= 103500
		catch (boost::system::system_error& e)
		{
			// no such file or directory is not an error
			if (e.code() != make_error_code(boost::system::errc::no_such_file_or_directory))
				set_error(p, e.code());
		}
#else
		catch (boost::filesystem::filesystem_error& e)
		{
			set_error(p, error_code(e.system_error(), get_system_category()));
		}
#endif // BOOST_VERSION
#endif // BOOST_NO_EXCEPTIONS
#else
		// no such file or directory is not an error
		if (std::remove(convert_to_native(p).c_str()) != 0 && errno != ENOENT)
		{
			set_error(p, error_code(errno, get_posix_category()));
		}
#endif
	}

	bool storage::delete_files()
	{
		// make sure we don't have the files open
		m_pool.release(this);

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
				ret = directories.insert((m_save_path / bp).string());
				bp = bp.branch_path();
			}
			delete_one_file(p);
		}

		// remove the directories. Reverse order to delete
		// subdirectories first

		for (std::set<std::string>::reverse_iterator i = directories.rbegin()
			, end(directories.rend()); i != end; ++i)
		{
			delete_one_file(*i);
		}

		if (error()) return true;
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
		
		return false;
	}

	int storage::sparse_end(int slot) const
	{
		TORRENT_ASSERT(slot >= 0);
		TORRENT_ASSERT(slot < m_files.num_pieces());

		size_type file_offset = (size_type)slot * m_files.piece_length();
		std::vector<file_entry>::const_iterator file_iter;

		for (file_iter = files().begin();;)
		{
			if (file_offset < file_iter->size)
				break;

			file_offset -= file_iter->size;
			++file_iter;
			TORRENT_ASSERT(file_iter != files().end());
		}
	
		fs::path path = m_save_path / file_iter->path;
		error_code ec;
		int mode = file::read_only;

		boost::shared_ptr<file> file_handle;
		int cache_setting = m_settings ? settings().disk_io_write_mode : 0;
		if (cache_setting == session_settings::disable_os_cache
			|| (cache_setting == session_settings::disable_os_cache_for_aligned_files
			&& ((file_iter->offset + file_iter->file_base) & (m_page_size-1)) == 0))
			mode |= file::no_buffer;
		if (!m_allocate_files) mode |= file::sparse;

		file_handle = m_pool.open_file(const_cast<storage*>(this), path, mode, ec);
		if (!file_handle || ec) return slot;

		size_type data_start = file_handle->sparse_end(file_offset);
		return (data_start + m_files.piece_length() - 1) / m_files.piece_length();
	}

	bool storage::verify_resume_data(lazy_entry const& rd, error_code& error)
	{
		lazy_entry const* mapped_files = rd.dict_find_list("mapped_files");
		if (mapped_files && mapped_files->list_size() == m_files.num_files())
		{
			m_mapped_files.reset(new file_storage(m_files));
			for (int i = 0; i < m_files.num_files(); ++i)
			{
				std::string new_filename = mapped_files->list_string_value_at(i);
				if (new_filename.empty()) continue;
				m_mapped_files->rename_file(i, new_filename);
			}
		}
		
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
			error = errors::missing_file_sizes;
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
			error = errors::no_files_in_resume_data;
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
			error = errors::missing_pieces;
			return false;
		}

		bool full_allocation_mode = false;
		if (rd.dict_find_string_value("allocation") != "compact")
			full_allocation_mode = true;

		if (seed)
		{
			if (files().num_files() != (int)file_sizes.size())
			{
				error = errors::mismatching_number_of_files;
				return false;
			}

			std::vector<std::pair<size_type, std::time_t> >::iterator
				fs = file_sizes.begin();
			// the resume data says we have the entire torrent
			// make sure the file sizes are the right ones
			for (file_storage::iterator i = files().begin()
				, end(files().end()); i != end; ++i, ++fs)
			{
				if (!i->pad_file && i->size != fs->first)
				{
					error = errors::mismatching_file_size;
					return false;
				}
			}
		}
		return match_filesizes(files(), m_save_path, file_sizes
			, !full_allocation_mode, error);

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
		fs::wpath wp = convert_to_wstring(save_path.string());
		if (!exists(wp))
			create_directory(wp);
		else if (!is_directory(wp))
			return false;
#else
		fs::path p = convert_to_native(save_path.string());
		if (!exists(p))
			create_directory(p);
		else if (!is_directory(p))
			return false;
#endif

		m_pool.release(this);

		bool ret = true;
		std::set<std::string> to_move;
		file_storage const& f = files();

		for (file_storage::iterator i = f.begin()
			, end(f.end()); i != end; ++i)
		{
			to_move.insert(to_move.begin(), *i->path.begin());
		}

		for (std::set<std::string>::const_iterator i = to_move.begin()
			, end(to_move.end()); i != end; ++i)
		{
			
#if TORRENT_USE_WPATH
			old_path = convert_to_wstring((m_save_path / *i).string());
			new_path = convert_to_wstring((save_path / *i).string());
#else
			old_path = convert_to_native((m_save_path / *i).string());
			new_path = convert_to_native((save_path / *i).string());
#endif

#ifndef BOOST_NO_EXCEPTIONS
			try
			{
#endif

				if (exists(old_path))
					rename(old_path, new_path);
#ifndef BOOST_NO_EXCEPTIONS
			}
			catch (std::exception&)
			{
				error_code ec;
				recursive_copy(old_path, new_path, ec);
				if (ec)
				{
					set_error(m_save_path / files().name(), ec);
					ret = false;
				}
				else
				{
					recursive_remove(old_path);
				}
			}
#endif
		}

		if (ret) m_save_path = save_path;

		return ret;
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

#define TORRENT_ALLOCATE_BLOCKS(bufs, num_blocks, piece_size) \
	int num_blocks = (piece_size + disk_pool()->block_size() - 1) / disk_pool()->block_size(); \
	file::iovec_t* bufs = TORRENT_ALLOCA(file::iovec_t, num_blocks); \
	for (int i = 0, size = piece_size; i < num_blocks; ++i) \
	{ \
		bufs[i].iov_base = disk_pool()->allocate_buffer("move temp"); \
		bufs[i].iov_len = (std::min)(disk_pool()->block_size(), size); \
		size -= bufs[i].iov_len; \
	}

#define TORRENT_FREE_BLOCKS(bufs, num_blocks) \
	for (int i = 0; i < num_blocks; ++i) \
		disk_pool()->free_buffer((char*)bufs[i].iov_base);

#define TORRENT_SET_SIZE(bufs, size, num_bufs) \
	for (num_bufs = 0; size > 0; size -= disk_pool()->block_size(), ++num_bufs) \
		bufs[num_bufs].iov_len = (std::min)(disk_pool()->block_size(), size)
	

	bool storage::move_slot(int src_slot, int dst_slot)
	{
		bool r = true;
		int piece_size = m_files.piece_size(dst_slot);

		TORRENT_ALLOCATE_BLOCKS(bufs, num_blocks, piece_size);

		readv(bufs, src_slot, 0, num_blocks); if (error()) goto ret;
		writev(bufs, dst_slot, 0, num_blocks); if (error()) goto ret;

		r = false;
ret:
		TORRENT_FREE_BLOCKS(bufs, num_blocks)
		return r;
	}

	bool storage::swap_slots(int slot1, int slot2)
	{
		bool r = true;

		// the size of the target slot is the size of the piece
		int piece1_size = m_files.piece_size(slot2);
		int piece2_size = m_files.piece_size(slot1);

		TORRENT_ALLOCATE_BLOCKS(bufs1, num_blocks1, piece1_size);
		TORRENT_ALLOCATE_BLOCKS(bufs2, num_blocks2, piece2_size);

		readv(bufs1, slot1, 0, num_blocks1); if (error()) goto ret;
		readv(bufs2, slot2, 0, num_blocks2); if (error()) goto ret;
		writev(bufs1, slot2, 0, num_blocks1); if (error()) goto ret;
		writev(bufs2, slot1, 0, num_blocks2); if (error()) goto ret;

		r = false;
ret:
		TORRENT_FREE_BLOCKS(bufs1, num_blocks1)
		TORRENT_FREE_BLOCKS(bufs2, num_blocks2)
		return r;
	}

	bool storage::swap_slots3(int slot1, int slot2, int slot3)
	{
		bool r = true;

		// the size of the target slot is the size of the piece
		int piece_size = m_files.piece_length();
		int piece1_size = m_files.piece_size(slot2);
		int piece2_size = m_files.piece_size(slot3);
		int piece3_size = m_files.piece_size(slot1);

		TORRENT_ALLOCATE_BLOCKS(bufs1, num_blocks1, piece_size);
		TORRENT_ALLOCATE_BLOCKS(bufs2, num_blocks2, piece_size);

		int tmp1 = 0;
		int tmp2 = 0;
		TORRENT_SET_SIZE(bufs1, piece1_size, tmp1);
		readv(bufs1, slot1, 0, tmp1); if (error()) goto ret;
		TORRENT_SET_SIZE(bufs2, piece2_size, tmp2);
		readv(bufs2, slot2, 0, tmp2); if (error()) goto ret;
		writev(bufs1, slot2, 0, tmp1); if (error()) goto ret;
		TORRENT_SET_SIZE(bufs1, piece3_size, tmp1);
		readv(bufs1, slot3, 0, tmp1); if (error()) goto ret;
		writev(bufs2, slot3, 0, tmp2); if (error()) goto ret;
		writev(bufs1, slot1, 0, tmp1); if (error()) goto ret;
ret:
		TORRENT_FREE_BLOCKS(bufs1, num_blocks1)
		TORRENT_FREE_BLOCKS(bufs2, num_blocks2)
		return r;
	}

	int storage::writev(file::iovec_t const* bufs, int slot, int offset
		, int num_bufs)
	{
#ifdef TORRENT_DISK_STATS
		disk_buffer_pool* pool = disk_pool();
		if (pool)
		{
			pool->m_disk_access_log << log_time() << " write "
				<< physical_offset(slot, offset) << std::endl;
		}
#endif
		fileop op = { &file::writev, &storage::write_unaligned
			, m_settings ? settings().disk_io_write_mode : 0, file::read_write };
#ifdef TORRENT_DISK_STATS
		int ret = readwritev(bufs, slot, offset, num_bufs, op);
		if (pool)
		{
			pool->m_disk_access_log << log_time() << " write_end "
				<< (physical_offset(slot, offset) + ret) << std::endl;
		}
		return ret;
#else
	return readwritev(bufs, slot, offset, num_bufs, op);
#endif
	}

	size_type storage::physical_offset(int slot, int offset)
	{
		TORRENT_ASSERT(slot >= 0);
		TORRENT_ASSERT(slot < m_files.num_pieces());
		TORRENT_ASSERT(offset >= 0);

		// find the file and file
		size_type tor_off = size_type(slot)
			* files().piece_length() + offset;
		file_storage::iterator file_iter = files().file_at_offset(tor_off);

		size_type file_offset = tor_off - file_iter->offset;
		TORRENT_ASSERT(file_offset >= 0);

		fs::path p(m_save_path / file_iter->path);
		error_code ec;
	
		// open the file read only to avoid re-opening
		// it in case it's already opened in read-only mode
		boost::shared_ptr<file> f = m_pool.open_file(
			this, p, file::read_only, ec);

		size_type ret = 0;
		if (f && !ec) ret = f->phys_offset(file_offset);

		if (ret == 0)
		{
			// this means we don't support true physical offset
			// just make something up
			return size_type(slot) * files().piece_length() + offset;
		}
		return ret;
	}

	int storage::readv(file::iovec_t const* bufs, int slot, int offset
		, int num_bufs)
	{
#ifdef TORRENT_DISK_STATS
		disk_buffer_pool* pool = disk_pool();
		if (pool)
		{
			pool->m_disk_access_log << log_time() << " read "
				<< physical_offset(slot, offset) << std::endl;
		}
#endif
		fileop op = { &file::readv, &storage::read_unaligned
			, m_settings ? settings().disk_io_read_mode : 0, file::read_only };
#ifdef TORRENT_SIMULATE_SLOW_READ
		boost::thread::sleep(boost::get_system_time()
			+ boost::posix_time::milliseconds(1000));
#endif
#ifdef TORRENT_DISK_STATS
		int ret = readwritev(bufs, slot, offset, num_bufs, op);
		if (pool)
		{
			pool->m_disk_access_log << log_time() << " read_end "
				<< (physical_offset(slot, offset) + ret) << std::endl;
		}
		return ret;
#else
		return readwritev(bufs, slot, offset, num_bufs, op);
#endif
	}

	// much of what needs to be done when reading and writing 
	// is buffer management and piece to file mapping. Most
	// of that is the same for reading and writing. This function
	// is a template, and the fileop decides what to do with the
	// file and the buffers.
	int storage::readwritev(file::iovec_t const* bufs, int slot, int offset
		, int num_bufs, fileop const& op)
	{
		TORRENT_ASSERT(bufs != 0);
		TORRENT_ASSERT(slot >= 0);
		TORRENT_ASSERT(slot < m_files.num_pieces());
		TORRENT_ASSERT(offset >= 0);
		TORRENT_ASSERT(offset < m_files.piece_size(slot));
		TORRENT_ASSERT(num_bufs > 0);

		int size = bufs_size(bufs, num_bufs);
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
			TORRENT_ASSERT(file_iter != files().end());
		}

		int buf_pos = 0;
		error_code ec;

		boost::shared_ptr<file> file_handle;
		int bytes_left = size;
		int slot_size = static_cast<int>(m_files.piece_size(slot));

		if (offset + bytes_left > slot_size)
			bytes_left = slot_size - offset;

		TORRENT_ASSERT(bytes_left >= 0);

#ifdef TORRENT_DEBUG
		int counter = 0;
#endif

		file::iovec_t* tmp_bufs = TORRENT_ALLOCA(file::iovec_t, num_bufs);
		file::iovec_t* current_buf = TORRENT_ALLOCA(file::iovec_t, num_bufs);
		copy_bufs(bufs, size, current_buf);
		TORRENT_ASSERT(count_bufs(current_buf, size) == num_bufs);
		int file_bytes_left;
		for (;bytes_left > 0; ++file_iter, bytes_left -= file_bytes_left
			, buf_pos += file_bytes_left)
		{
			TORRENT_ASSERT(file_iter != files().end());
			TORRENT_ASSERT(buf_pos >= 0);

			file_bytes_left = bytes_left;
			if (file_offset + file_bytes_left > file_iter->size)
				file_bytes_left = (std::max)(static_cast<int>(file_iter->size - file_offset), 0);

			if (file_bytes_left == 0) continue;

#ifdef TORRENT_DEBUG
			TORRENT_ASSERT(int(slices.size()) > counter);
			size_type slice_size = slices[counter].size;
			TORRENT_ASSERT(slice_size == file_bytes_left);
			TORRENT_ASSERT(files().at(slices[counter].file_index).path
				== file_iter->path);
			++counter;
#endif

			if (file_iter->pad_file)
			{
				if (op.mode == file::read_only)
				{
					int num_tmp_bufs = copy_bufs(current_buf, file_bytes_left, tmp_bufs);
					TORRENT_ASSERT(count_bufs(tmp_bufs, file_bytes_left) == num_tmp_bufs);
					TORRENT_ASSERT(num_tmp_bufs <= num_bufs);
					clear_bufs(tmp_bufs, num_tmp_bufs);
				}
				advance_bufs(current_buf, file_bytes_left);
				TORRENT_ASSERT(count_bufs(current_buf, bytes_left - file_bytes_left) <= num_bufs);
				file_offset = 0;
				continue;
			}

			fs::path path = m_save_path / file_iter->path;

			error_code ec;
			int mode = op.mode;

			if (op.cache_setting == session_settings::disable_os_cache
				|| (op.cache_setting == session_settings::disable_os_cache_for_aligned_files
				&& ((file_iter->offset + file_iter->file_base) & (m_page_size-1)) == 0))
				mode |= file::no_buffer;
			if (!m_allocate_files) mode |= file::sparse;

			file_handle = m_pool.open_file(this, path, mode, ec);
			if (!file_handle || ec)
			{
				TORRENT_ASSERT(ec);
				set_error(path, ec);
				return -1;
			}

			int num_tmp_bufs = copy_bufs(current_buf, file_bytes_left, tmp_bufs);
			TORRENT_ASSERT(count_bufs(tmp_bufs, file_bytes_left) == num_tmp_bufs);
			TORRENT_ASSERT(num_tmp_bufs <= num_bufs);
			int bytes_transferred = 0;
			// if the file is opened in no_buffer mode, and the
			// read is unaligned, we need to fall back on a slow
			// special read that reads aligned buffers and copies
			// it into the one supplied
			if ((file_handle->open_mode() & file::no_buffer)
				&& (((file_iter->file_base + file_offset) & (file_handle->pos_alignment()-1)) != 0
				|| (uintptr_t(tmp_bufs->iov_base) & (file_handle->buf_alignment()-1)) != 0))
			{
				bytes_transferred = (this->*op.unaligned_op)(file_handle, file_iter->file_base
					+ file_offset, tmp_bufs, num_tmp_bufs, ec);
			}
			else
			{
				bytes_transferred = (int)((*file_handle).*op.regular_op)(file_iter->file_base
					+ file_offset, tmp_bufs, num_tmp_bufs, ec);
			}
			file_offset = 0;

			if (ec)
			{
				set_error(m_save_path / file_iter->path, ec);
				return -1;
			}

			if (file_bytes_left != bytes_transferred)
				return bytes_transferred;

			advance_bufs(current_buf, bytes_transferred);
			TORRENT_ASSERT(count_bufs(current_buf, bytes_left - file_bytes_left) <= num_bufs);
		}
		return size;
	}

	// these functions are inefficient, but should be fairly uncommon. The read
	// case happens if unaligned files are opened in no_buffer mode or if clients
	// makes unaligned requests (and the disk cache is disabled or fully utilized
	// for write cache).

	// they read an unaligned buffer from a file that requires aligned access

	size_type storage::read_unaligned(boost::shared_ptr<file> const& file_handle
		, size_type file_offset, file::iovec_t const* bufs, int num_bufs, error_code& ec)
	{
		const int pos_align = file_handle->pos_alignment()-1;
		const int size_align = file_handle->size_alignment()-1;
		const int block_size = disk_pool()->block_size();

		const int size = bufs_size(bufs, num_bufs);
		const int start_adjust = file_offset & pos_align;
		const int aligned_start = file_offset - start_adjust;
		const int aligned_size = ((size+start_adjust) & size_align)
			? ((size+start_adjust) & ~size_align) + size_align + 1 : size + start_adjust;
		const int num_blocks = (aligned_size + block_size - 1) / block_size;
		TORRENT_ASSERT((aligned_size & size_align) == 0);

		disk_buffer_holder tmp_buf(*disk_pool(), disk_pool()->allocate_buffers(num_blocks, "read scratch"), num_blocks);
		file::iovec_t b = {tmp_buf.get(), aligned_size};
		size_type ret = file_handle->readv(aligned_start, &b, 1, ec);
		if (ret < 0) return ret;
		char* read_buf = tmp_buf.get() + start_adjust;
		for (file::iovec_t const* i = bufs, *end(bufs + num_bufs); i != end; ++i)
		{
			memcpy(i->iov_base, read_buf, i->iov_len);
			read_buf += i->iov_len;
		}
		if (ret < size + start_adjust) return ret - start_adjust;
		return size;
	}

	size_type storage::write_unaligned(boost::shared_ptr<file> const& file_handle
		, size_type file_offset, file::iovec_t const* bufs, int num_bufs, error_code& ec)
	{
		TORRENT_ASSERT(false); // not implemented
		return 0;
	}

	int storage::write(
		const char* buf
		, int slot
		, int offset
		, int size)
	{
		file::iovec_t b = { (file::iovec_base_t)buf, size };
		return writev(&b, slot, offset, 1);
	}

	int storage::read(
		char* buf
		, int slot
		, int offset
		, int size)
	{
		file::iovec_t b = { (file::iovec_base_t)buf, size };
		return readv(&b, slot, offset, 1);
	}

	storage_interface* default_storage_constructor(file_storage const& fs
		, file_storage const* mapped, fs::path const& path, file_pool& fp)
	{
		return new storage(fs, mapped, path, fp);
	}

	// this storage implementation does not write anything to disk
	// and it pretends to read, and just leaves garbage in the buffers
	// this is useful when simulating many clients on the same machine
	// or when running stress tests and want to take the cost of the
	// disk I/O out of the picture. This cannot be used for any kind
	// of normal bittorrent operation, since it will just send garbage
	// to peers and throw away all the data it downloads. It would end
	// up being banned immediately
	class disabled_storage : public storage_interface, boost::noncopyable
	{
	public:
		disabled_storage(int piece_size) : m_piece_size(piece_size) {}
		bool has_any_file() { return false; }
		bool rename_file(int index, std::string const& new_filename) { return false; }
		bool release_files() { return false; }
		bool delete_files() { return false; }
		bool initialize(bool allocate_files) { return false; }
		bool move_storage(fs::path save_path) { return true; }
		int read(char* buf, int slot, int offset, int size) { return size; }
		int write(char const* buf, int slot, int offset, int size) { return size; }
		size_type physical_offset(int slot, int offset) { return 0; }
		int readv(file::iovec_t const* bufs, int slot, int offset, int num_bufs)
		{
#ifdef TORRENT_DISK_STATS
			disk_buffer_pool* pool = disk_pool();
			if (pool)
			{
				pool->m_disk_access_log << log_time() << " read "
					<< physical_offset(slot, offset) << std::endl;
			}
#endif
			int ret = 0;
			for (int i = 0; i < num_bufs; ++i)
				ret += bufs[i].iov_len;
#ifdef TORRENT_DISK_STATS
			if (pool)
			{
				pool->m_disk_access_log << log_time() << " read_end "
					<< (physical_offset(slot, offset) + ret) << std::endl;
			}
#endif
			return ret;
		}
		int writev(file::iovec_t const* bufs, int slot, int offset, int num_bufs)
		{
#ifdef TORRENT_DISK_STATS
			disk_buffer_pool* pool = disk_pool();
			if (pool)
			{
				pool->m_disk_access_log << log_time() << " write "
					<< physical_offset(slot, offset) << std::endl;
			}
#endif
			int ret = 0;
			for (int i = 0; i < num_bufs; ++i)
				ret += bufs[i].iov_len;
#ifdef TORRENT_DISK_STATS
			if (pool)
			{
				pool->m_disk_access_log << log_time() << " write_end "
					<< (physical_offset(slot, offset) + ret) << std::endl;
			}
#endif
			return ret;
		}
		bool move_slot(int src_slot, int dst_slot) { return false; }
		bool swap_slots(int slot1, int slot2) { return false; }
		bool swap_slots3(int slot1, int slot2, int slot3) { return false; }
		bool verify_resume_data(lazy_entry const& rd, error_code& error) { return false; }
		bool write_resume_data(entry& rd) const { return false; }

		int m_piece_size;
	};

	storage_interface* disabled_storage_constructor(file_storage const& fs
		, file_storage const* mapped, fs::path const& path, file_pool& fp)
	{
		return new disabled_storage(fs.piece_length());
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
		, m_storage(sc(m_info->orig_files(), &m_info->files() != &m_info->orig_files()
			? &m_info->files() : 0, save_path, fp))
		, m_storage_mode(sm)
		, m_save_path(complete(save_path))
		, m_state(state_none)
		, m_current_slot(0)
		, m_out_of_place(false)
		, m_scratch_buffer(io, 0)
		, m_scratch_buffer2(io, 0)
		, m_scratch_piece(-1)
		, m_last_piece(-1)
		, m_storage_constructor(sc)
		, m_io_thread(io)
		, m_torrent(torrent)
	{
		m_storage->m_disk_pool = &m_io_thread;
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

	void piece_manager::async_read_and_hash(
		peer_request const& r
		, boost::function<void(int, disk_io_job const&)> const& handler
		, int priority)
	{
		disk_io_job j;
		j.storage = this;
		j.action = disk_io_job::read_and_hash;
		j.piece = r.piece;
		j.offset = r.start;
		j.buffer_size = r.length;
		j.buffer = 0;
		j.priority = priority;
		TORRENT_ASSERT(r.length <= 16 * 1024);
		m_io_thread.add_job(j, handler);
#ifdef TORRENT_DEBUG
		boost::recursive_mutex::scoped_lock l(m_mutex);
		// if this assert is hit, it suggests
		// that check_files was not successful
		TORRENT_ASSERT(slot_for(r.piece) >= 0);
#endif
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

	int piece_manager::queued_bytes() const
	{
		return m_io_thread.queue_buffer_size();
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
		hash_for_slot(slot, ph, m_files.piece_size(piece));
		if (m_storage->error()) return sha1_hash(0);
		return ph.h.final();
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
		file::iovec_t* bufs
		, int piece_index
		, int offset
		, int num_bufs)
	{
		TORRENT_ASSERT(bufs);
		TORRENT_ASSERT(offset >= 0);
		TORRENT_ASSERT(num_bufs > 0);
		m_last_piece = piece_index;
		int slot = slot_for(piece_index);
		return m_storage->readv(bufs, slot, offset, num_bufs);
	}

	int piece_manager::write_impl(
		file::iovec_t* bufs
	  , int piece_index
	  , int offset
	  , int num_bufs)
	{
		TORRENT_ASSERT(bufs);
		TORRENT_ASSERT(offset >= 0);
		TORRENT_ASSERT(num_bufs > 0);
		TORRENT_ASSERT(piece_index >= 0 && piece_index < m_files.num_pieces());

		int size = bufs_size(bufs, num_bufs);

		file::iovec_t* iov = TORRENT_ALLOCA(file::iovec_t, num_bufs);
		std::copy(bufs, bufs + num_bufs, iov);
		m_last_piece = piece_index;
		int slot = allocate_slot_for_piece(piece_index);
		int ret = m_storage->writev(bufs, slot, offset, num_bufs);
		// only save the partial hash if the write succeeds
		if (ret != size) return ret;

#if defined TORRENT_PARTIAL_HASH_LOG && TORRENT_USE_IOSTREAM
		std::ofstream out("partial_hash.log", std::ios::app);
#endif

		if (offset == 0)
		{
			partial_hash& ph = m_piece_hasher[piece_index];
			TORRENT_ASSERT(ph.offset == 0);
			ph.offset = size;

			for (file::iovec_t* i = iov, *end(iov + num_bufs); i < end; ++i)
				ph.h.update((char const*)i->iov_base, i->iov_len);

#if defined TORRENT_PARTIAL_HASH_LOG && TORRENT_USE_IOSTREAM
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
					for (file::iovec_t* b = iov, *end(iov + num_bufs); b < end; ++b)
					{
						i->second.h.update((char const*)b->iov_base, b->iov_len);
						i->second.offset += b->iov_len;
					}
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

	size_type piece_manager::physical_offset(
		int piece_index
		, int offset)
	{
		TORRENT_ASSERT(offset >= 0);
		TORRENT_ASSERT(piece_index >= 0 && piece_index < m_files.num_pieces());

		int slot = slot_for(piece_index);
		// we may not have a slot for this piece yet.
		// assume there is no re-mapping of slots
		if (slot < 0) slot = piece_index;
		return m_storage->physical_offset(slot, offset);
	}

	int piece_manager::identify_data(
		sha1_hash const& large_hash
		, sha1_hash const& small_hash
		, int current_slot)
	{
//		INVARIANT_CHECK;
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

	int piece_manager::check_no_fastresume(error_code& error)
	{
		bool has_files = m_storage->has_any_file();

		if (m_storage->error())
			return fatal_disk_error;

		if (has_files)
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
	
	int piece_manager::check_init_storage(error_code& error)
	{
		if (m_storage->initialize(m_storage_mode == storage_mode_allocate))
		{
			error = m_storage->error();
			TORRENT_ASSERT(error);
			return fatal_disk_error;
		}
		m_state = state_finished;
		m_scratch_buffer.reset();
		m_scratch_buffer2.reset();
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
		lazy_entry const& rd, error_code& error)
	{
		boost::recursive_mutex::scoped_lock lock(m_mutex);

		INVARIANT_CHECK;

		TORRENT_ASSERT(m_files.piece_length() > 0);
		
		m_current_slot = 0;

		// if we don't have any resume data, return
		if (rd.type() == lazy_entry::none_t) return check_no_fastresume(error);

		if (rd.type() != lazy_entry::dict_t)
		{
			error = errors::not_a_dictionary;
			return check_no_fastresume(error);
		}

		int block_size = (std::min)(16 * 1024, m_files.piece_length());
		int blocks_per_piece = rd.dict_find_int_value("blocks per piece", -1);
		if (blocks_per_piece != -1
			&& blocks_per_piece != m_files.piece_length() / block_size)
		{
			error = errors::invalid_blocks_per_piece;
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
				error = errors::missing_slots;
				return check_no_fastresume(error);
			}

			if ((int)slots->list_size() > m_files.num_pieces())
			{
				error = errors::too_many_slots;
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
						error = errors::invalid_slot_list;
						return check_no_fastresume(error);
					}

					int index = int(e->int_value());
					if (index >= num_pieces || index < -2)
					{
						error = errors::invalid_piece_index;
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
						error = errors::invalid_slot_list;
						return check_no_fastresume(error);
					}

					int index = int(e->int_value());
					if (index != i && index >= 0)
					{
						error = errors::invalid_piece_index;
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
					error = errors::pieces_need_reorder;
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
				error = errors::missing_pieces;
				return check_no_fastresume(error);
			}

			if ((int)pieces->string_length() != m_files.num_pieces())
			{
				error = errors::too_many_slots;
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
	int piece_manager::check_files(int& current_slot, int& have_piece, error_code& error)
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
					if (!m_scratch_buffer2)
					{
						int blocks_per_piece = (std::max)(m_files.piece_length()
							/ m_io_thread.block_size(), 1);
						m_scratch_buffer2.reset(m_io_thread.allocate_buffers(
							blocks_per_piece, "check scratch"), blocks_per_piece);
					}

					int piece_size = m_files.piece_size(other_piece);
					if (m_storage->read(m_scratch_buffer2.get(), piece, 0, piece_size)
						!= piece_size)
					{
						error = m_storage->error();
						TORRENT_ASSERT(error);
						return fatal_disk_error;
					}
					m_scratch_piece = other_piece;
					m_piece_to_slot[other_piece] = unassigned;
				}
				
				// the slot where this piece belongs is
				// free. Just move the piece there.
				int piece_size = m_files.piece_size(piece);
				if (m_storage->write(m_scratch_buffer.get(), piece, 0, piece_size) != piece_size)
				{
					error = m_storage->error();
					TORRENT_ASSERT(error);
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
				if (!m_scratch_buffer)
				{
					int blocks_per_piece = (std::max)(m_files.piece_length() / m_io_thread.block_size(), 1);
					m_scratch_buffer.reset(m_io_thread.allocate_buffers(
						blocks_per_piece, "check scratch"), blocks_per_piece);
				}
			
				int piece_size = m_files.piece_size(other_piece);
				if (m_storage->read(m_scratch_buffer.get(), piece, 0, piece_size) != piece_size)
				{
					error = m_storage->error();
					TORRENT_ASSERT(error);
					return fatal_disk_error;
				}
				m_scratch_piece = other_piece;
				m_piece_to_slot[other_piece] = unassigned;
			}

			// the slot where this piece belongs is
			// free. Just move the piece there.
			m_last_piece = piece;
			m_storage->move_slot(m_current_slot, piece);
			if (m_storage->error()) return -1;

			m_piece_to_slot[piece] = piece;
			m_slot_to_piece[m_current_slot] = unassigned;
			m_slot_to_piece[piece] = piece;
		
			TORRENT_ASSERT(int(m_piece_to_slot.size()) == m_files.num_pieces());
			return need_full_check;
		}

		TORRENT_ASSERT(m_state == state_full_check);
		if (m_state == state_finished) return 0;

		int skip = check_one_piece(have_piece);
		TORRENT_ASSERT(m_current_slot <= m_files.num_pieces());

		if (skip == -1)
		{
			error = m_storage->error();
			TORRENT_ASSERT(error);
			return fatal_disk_error;
		}

		if (skip > 0)
		{
			clear_error();
			// skip means that the piece we checked failed to be read from disk
			// completely. This may be caused by the file not being there, or the
			// piece overlapping with a sparse region. We should skip 'skip' number
			// of pieces

			if (m_storage_mode == storage_mode_compact)
			{
				for (int i = m_current_slot; i < m_current_slot + skip - 1; ++i)
				{
					TORRENT_ASSERT(m_slot_to_piece[i] == unallocated);
					m_unallocated_slots.push_back(i);
				}
			}

			// current slot will increase by one below
			m_current_slot += skip - 1;
			TORRENT_ASSERT(m_current_slot <= m_files.num_pieces());
		}

		++m_current_slot;
		current_slot = m_current_slot;

		if (m_current_slot >= m_files.num_pieces())
		{
			TORRENT_ASSERT(m_current_slot == m_files.num_pieces());

			// clear the memory we've been using
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

	int piece_manager::skip_file() const
	{
		size_type file_offset = 0;
		size_type current_offset = size_type(m_current_slot) * m_files.piece_length();
		for (file_storage::iterator i = m_files.begin()
			, end(m_files.end()); i != end; ++i)
		{
			file_offset += i->size;
			if (file_offset > current_offset) break;
		}

		TORRENT_ASSERT(file_offset > current_offset);
		int ret = static_cast<int>(
			(file_offset - current_offset + m_files.piece_length() - 1)
			/ m_files.piece_length());
		TORRENT_ASSERT(ret >= 1);
		return ret;
	}

	// -1 = error, 0 = ok, >0 = skip this many pieces
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
				m_hash_to_piece.insert(std::pair<const sha1_hash, int>(m_info->hash_for_piece(i), i));
		}

		partial_hash ph;
		int num_read = 0;
		int piece_size = m_files.piece_size(m_current_slot);
		int small_piece_size = m_files.piece_size(m_files.num_pieces() - 1);
		bool read_short = true;
		sha1_hash small_hash;
		if (piece_size == small_piece_size)
		{
			num_read = hash_for_slot(m_current_slot, ph, piece_size, 0, 0);
		}
		else
		{
			num_read = hash_for_slot(m_current_slot, ph, piece_size
				, small_piece_size, &small_hash);
		}
		read_short = num_read != piece_size;

		if (read_short)
		{
			if (m_storage->error()
#ifdef TORRENT_WINDOWS
				&& m_storage->error() != error_code(ERROR_PATH_NOT_FOUND, get_system_category())
				&& m_storage->error() != error_code(ERROR_FILE_NOT_FOUND, get_system_category())
				&& m_storage->error() != error_code(ERROR_HANDLE_EOF, get_system_category())
				&& m_storage->error() != error_code(ERROR_INVALID_HANDLE, get_system_category()))
#else
				&& m_storage->error() != error_code(ENOENT, get_posix_category()))
#endif
			{
				return -1;
			}
			// if the file is incomplete, skip the rest of it
			return skip_file();
		}

		sha1_hash large_hash = ph.h.final();
		int piece_index = identify_data(large_hash, small_hash, m_current_slot);

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
			m_last_piece = piece_index;
			if (other_piece >= 0)
				ret |= m_storage->swap_slots(other_slot, m_current_slot);
			else
				ret |= m_storage->move_slot(m_current_slot, other_slot);

			if (ret) return skip_file();

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
			m_last_piece = other_piece;
			if (ret) return skip_file();

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

				m_last_piece = piece_index;
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

				m_last_piece = piece_index;
				if (ret) return skip_file();

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

		if (piece_index == unassigned)
		{
			// the data did not match any piece. Maybe we're reading
			// from a sparse region, see if we are and skip
			if (m_current_slot == m_files.num_pieces() -1) return 0;

			int next_slot = m_storage->sparse_end(m_current_slot + 1);
			if (next_slot > m_current_slot + 1) return next_slot - m_current_slot;
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

#if defined TORRENT_DEBUG && defined TORRENT_STORAGE_DEBUG && TORRENT_USE_IOSTREAM
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

			m_last_piece = piece_index;
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

#ifdef TORRENT_EXPENSIVE_INVARIANT_CHECKS
		INVARIANT_CHECK;
#endif

		TORRENT_ASSERT(!m_unallocated_slots.empty());
		TORRENT_ASSERT(m_storage_mode == storage_mode_compact);

		bool written = false;

		for (int i = 0; i < num_slots && !m_unallocated_slots.empty(); ++i)
		{
			int pos = m_unallocated_slots.front();
			TORRENT_ASSERT(m_slot_to_piece[pos] == unallocated);
			TORRENT_ASSERT(m_piece_to_slot[pos] != pos);

			int new_free_slot = pos;
			if (m_piece_to_slot[pos] != has_no_slot)
			{
				m_last_piece = pos;
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

#if defined(TORRENT_STORAGE_DEBUG) && TORRENT_USE_IOSTREAM
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

