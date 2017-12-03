/*

Copyright (c) 2003-2016, Arvid Norberg, Daniel Wallin
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

#include "libtorrent/config.hpp"
#include "libtorrent/error_code.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <ctime>
#include <algorithm>
#include <set>
#include <functional>

#include <boost/ref.hpp>
#include <boost/bind.hpp>
#include <boost/version.hpp>
#include <boost/scoped_array.hpp>
#include <boost/system/error_code.hpp>

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

#include "libtorrent/aux_/disable_warnings_pop.hpp"

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
#include "libtorrent/stat_cache.hpp"

#include <cstdio>

//#define TORRENT_PARTIAL_HASH_LOG

// for convert_to_wstring and convert_to_native
#include "libtorrent/aux_/escape_string.hpp"

#define DEBUG_STORAGE 0
#define DEBUG_DELETE_FILES 0

#if __cplusplus >= 201103L || defined __clang__

#if DEBUG_STORAGE
#define DLOG(...) fprintf(__VA_ARGS__)
#else
#define DLOG(...) do {} while (false)
#endif

#if DEBUG_DELETE_FILES
#define DFLOG(...) fprintf(__VA_ARGS__)
#else
#define DFLOG(...) do {} while (false)
#endif

#else

#if DEBUG_STORAGE
#define DLOG fprintf
#else
#define DLOG TORRENT_WHILE_0 fprintf
#endif

#if DEBUG_DELETE_FILES
#define DFLOG fprintf
#else
#define DFLOG TORRENT_WHILE_0 fprintf
#endif

#endif // cplusplus

namespace libtorrent
{
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
				bufs->iov_base = reinterpret_cast<char*>(bufs->iov_base)
					+ bufs->iov_len - (size - bytes);
				bufs->iov_len = size - bytes;
				return;
			}
			++bufs;
		}
	}

	void clear_bufs(file::iovec_t const* bufs, int num_bufs)
	{
		for (file::iovec_t const* i = bufs, *end(bufs + num_bufs); i < end; ++i)
			std::memset(i->iov_base, 0, i->iov_len);
	}

	namespace {

	int count_bufs(file::iovec_t const* bufs, int bytes)
	{
		int size = 0;
		int count = 1;
		if (bytes == 0) return 0;
		for (file::iovec_t const* i = bufs;; ++i, ++count)
		{
			size += i->iov_len;
			if (size >= bytes) return count;
		}
	}

#ifdef TORRENT_DISK_STATS
	static boost::atomic<int> event_id;
	static mutex disk_access_mutex;

	// this is opened and closed by the disk_io_thread class
	FILE* g_access_log = NULL;

	enum access_log_flags_t
	{
		op_read = 0,
		op_write = 1,
		op_start = 0,
		op_end = 2
	};

	void write_access_log(boost::uint64_t offset, boost::uint32_t fileid, int flags, time_point timestamp)
	{
		if (g_access_log == NULL) return;

		// the event format in the log is:
		// uint64_t timestamp (microseconds)
		// uint64_t file offset
		// uint32_t file-id
		// uint8_t  event (0: start read, 1: start write, 2: complete read, 4: complete write)
		char event[29];
		char* ptr = event;
		detail::write_uint64(timestamp.time_since_epoch().count(), ptr);
		detail::write_uint64(offset, ptr);
		detail::write_uint64(static_cast<boost::uint64_t>(event_id++), ptr);
		detail::write_uint32(fileid, ptr);
		detail::write_uint8(flags, ptr);

		mutex::scoped_lock l(disk_access_mutex);
		int ret = fwrite(event, 1, sizeof(event), g_access_log);
		l.unlock();
		if (ret != sizeof(event))
		{
			fprintf(stderr, "ERROR writing to disk access log: (%d) %s\n"
				, errno, strerror(errno));
		}
	}
#endif

	} // anonymous namespace

	struct write_fileop : fileop
	{
		write_fileop(default_storage& st, int flags)
			: m_storage(st)
			, m_flags(flags)
		{}

		int file_op(int file_index, boost::int64_t file_offset, int size
			, file::iovec_t const* bufs, storage_error& ec)
			TORRENT_OVERRIDE TORRENT_FINAL
		{
			if (m_storage.files().pad_file_at(file_index))
			{
				// writing to a pad-file is a no-op
				return size;
			}

			int num_bufs = count_bufs(bufs, size);

			if (file_index < int(m_storage.m_file_priority.size())
				&& m_storage.m_file_priority[file_index] == 0)
			{
				TORRENT_ASSERT(m_storage.m_part_file);

				error_code e;
				peer_request map = m_storage.files().map_file(file_index
					, file_offset, 0);
				int ret = m_storage.m_part_file->writev(bufs, num_bufs
					, map.piece, map.start, e);

				if (e)
				{
					ec.ec = e;
					ec.file = file_index;
					ec.operation = storage_error::partfile_write;
					return -1;
				}
				return ret;
			}

			// invalidate our stat cache for this file, since
			// we're writing to it
			m_storage.m_stat_cache.set_dirty(file_index);

			file_handle handle = m_storage.open_file(file_index
				, file::read_write, ec);
			if (ec) return -1;

			// please ignore the adjusted_offset. It's just file_offset.
			boost::int64_t adjusted_offset =
#ifndef TORRENT_NO_DEPRECATE
				m_storage.files().file_base_deprecated(file_index) +
#endif
				file_offset;

#ifdef TORRENT_DISK_STATS
			write_access_log(adjusted_offset, handle->file_id(), op_start | op_write, clock_type::now());
#endif

			error_code e;
			int ret = handle->writev(adjusted_offset
				, bufs, num_bufs, e, m_flags);

			// set this unconditionally in case the upper layer would like to treat
			// short reads as errors
			ec.operation = storage_error::write;

				// we either get an error or 0 or more bytes read
			TORRENT_ASSERT(e || ret >= 0);

#ifdef TORRENT_DISK_STATS
			write_access_log(adjusted_offset + ret , handle->file_id(), op_end | op_write, clock_type::now());
#endif
			TORRENT_ASSERT(ret <= bufs_size(bufs, num_bufs));

			if (e)
			{
				ec.ec = e;
				ec.file = file_index;
				return -1;
			}

			return ret;
		}
	private:
		default_storage& m_storage;
		int m_flags;
	};

	struct read_fileop : fileop
	{
		read_fileop(default_storage& st, int const flags)
			: m_storage(st)
			, m_flags(flags)
		{}

		int file_op(int file_index, boost::int64_t file_offset, int size
			, file::iovec_t const* bufs, storage_error& ec)
			TORRENT_OVERRIDE TORRENT_FINAL
		{
			int num_bufs = count_bufs(bufs, size);

			if (m_storage.files().pad_file_at(file_index))
			{
				// reading from a pad file yields zeroes
				clear_bufs(bufs, num_bufs);
				return size;
			}

			if (file_index < int(m_storage.m_file_priority.size())
				&& m_storage.m_file_priority[file_index] == 0)
			{
				TORRENT_ASSERT(m_storage.m_part_file);

				error_code e;
				peer_request map = m_storage.files().map_file(file_index
					, file_offset, 0);
				int ret = m_storage.m_part_file->readv(bufs, num_bufs
					, map.piece, map.start, e);

				if (e)
				{
					ec.ec = e;
					ec.file = file_index;
					ec.operation = storage_error::partfile_read;
					return -1;
				}
				return ret;
			}

			file_handle handle = m_storage.open_file(file_index
				, file::read_only | m_flags, ec);
			if (ec) return -1;

			// please ignore the adjusted_offset. It's just file_offset.
			boost::int64_t adjusted_offset =
#ifndef TORRENT_NO_DEPRECATE
				m_storage.files().file_base_deprecated(file_index) +
#endif
				file_offset;

#ifdef TORRENT_DISK_STATS
			write_access_log(adjusted_offset, handle->file_id(), op_start | op_read, clock_type::now());
#endif

			error_code e;
			int ret = handle->readv(adjusted_offset
				, bufs, num_bufs, e, m_flags);

			// set this unconditionally in case the upper layer would like to treat
			// short reads as errors
			ec.operation = storage_error::read;

				// we either get an error or 0 or more bytes read
			TORRENT_ASSERT(e || ret >= 0);

#ifdef TORRENT_DISK_STATS
			write_access_log(adjusted_offset + ret , handle->file_id(), op_end | op_read, clock_type::now());
#endif
			TORRENT_ASSERT(ret <= bufs_size(bufs, num_bufs));

			if (e)
			{
				ec.ec = e;
				ec.file = file_index;
				return -1;
			}

			return ret;
		}

	private:
		default_storage& m_storage;
		int const m_flags;
	};

	default_storage::default_storage(storage_params const& params)
		: m_files(*params.files)
		, m_pool(*params.pool)
		, m_allocate_files(params.mode == storage_mode_allocate)
	{
		if (params.mapped_files) m_mapped_files.reset(new file_storage(*params.mapped_files));
		if (params.priorities) m_file_priority = *params.priorities;

		TORRENT_ASSERT(m_files.num_files() > 0);
		m_save_path = complete(params.path);
		m_part_file_name = "." + (params.info
			? to_hex(params.info->info_hash().to_string())
			: params.files->name()) + ".parts";

		file_storage const& fs = files();
		for (int i = 0; i < m_file_priority.size(); ++i)
		{
			if (m_file_priority[i] == 0 && !fs.pad_file_at(i))
			{
				need_partfile();
				break;
			}
		}
	}

	default_storage::~default_storage()
	{
		error_code ec;
		if (m_part_file) m_part_file->flush_metadata(ec);

		// this may be called from a different
		// thread than the disk thread
		m_pool.release(this);
	}

	void default_storage::need_partfile()
	{
		if (m_part_file) return;

		m_part_file.reset(new part_file(
			m_save_path, m_part_file_name
			, m_files.num_pieces(), m_files.piece_length()));
	}

	void default_storage::set_file_priority(std::vector<boost::uint8_t> const& prio, storage_error& ec)
	{
		// extend our file priorities in case it's truncated
		// the default assumed priority is 4 (the default)
		if (prio.size() > m_file_priority.size())
			m_file_priority.resize(prio.size(), 4);

		file_storage const& fs = files();
		for (int i = 0; i < int(prio.size()); ++i)
		{
			int old_prio = m_file_priority[i];
			int new_prio = prio[i];
			if (old_prio == 0 && new_prio != 0)
			{
				// move stuff out of the part file
				file_handle f = open_file(i, file::read_write, ec);
				if (ec) return;

				need_partfile();

				m_part_file->export_file(*f, fs.file_offset(i), fs.file_size(i), ec.ec);
				if (ec)
				{
					ec.file = i;
					ec.operation = storage_error::partfile_write;
					return;
				}
			}
			else if (old_prio != 0 && new_prio == 0)
			{
				// move stuff into the part file
				// this is not implemented yet.
				// pretend that we didn't set the priority to 0.

				std::string fp = fs.file_path(i, m_save_path);
				if (exists(fp))
					new_prio = 1;
/*
				file_handle f = open_file(i, file::read_only, ec);
				if (ec.ec != boost::system::errc::no_such_file_or_directory)
				{
					if (ec) return;

					need_partfile();

					m_part_file->import_file(*f, fs.file_offset(i), fs.file_size(i), ec.ec);
					if (ec)
					{
						ec.file = i;
						ec.operation = storage_error::partfile_read;
						return;
					}
					// remove the file
					std::string p = fs.file_path(i, m_save_path);
					delete_one_file(p, ec.ec);
					if (ec)
					{
						ec.file = i;
						ec.operation = storage_error::remove;
					}
				}
*/
			}
			ec.ec.clear();
			m_file_priority[i] = new_prio;

			if (m_file_priority[i] == 0 && !fs.pad_file_at(i))
				need_partfile();
		}
		if (m_part_file) m_part_file->flush_metadata(ec.ec);
		if (ec)
		{
			ec.file = -1;
			ec.operation = storage_error::partfile_write;
		}
	}

	void default_storage::initialize(storage_error& ec)
	{
		m_stat_cache.init(files().num_files());

#ifdef TORRENT_WINDOWS
		// don't do full file allocations on network drives
#if TORRENT_USE_WSTRING
		std::wstring f = convert_to_wstring(m_save_path);
		int const drive_type = GetDriveTypeW(f.c_str());
#else
		int const drive_type = GetDriveTypeA(m_save_path.c_str());
#endif

		if (drive_type == DRIVE_REMOTE)
			m_allocate_files = false;
#endif

		{
			mutex::scoped_lock l(m_file_created_mutex);
			m_file_created.resize(files().num_files(), false);
		}

		// first, create all missing directories
		std::string last_path;
		for (int file_index = 0; file_index < files().num_files(); ++file_index)
		{
			// ignore files that have priority 0
			if (int(m_file_priority.size()) > file_index
				&& m_file_priority[file_index] == 0)
			{
				continue;
			}

			// ignore pad files
			if (files().pad_file_at(file_index)) continue;

			boost::int64_t cached_size = m_stat_cache.get_filesize(file_index);
			if (cached_size == stat_cache::not_in_cache)
			{
				file_status s;
				std::string file_path = files().file_path(file_index, m_save_path);
				stat_file(file_path, &s, ec.ec);
				if (!ec)
				{
					m_stat_cache.set_cache(file_index, s.file_size, s.mtime);
					cached_size = s.file_size;
				}
				else if (ec.ec != boost::system::errc::no_such_file_or_directory)
				{
					m_stat_cache.set_error(file_index);
					ec.file = file_index;
					ec.operation = storage_error::stat;
					break;
				}
				else
				{
					cached_size = stat_cache::no_exist;
				}
			}

			// if the file already exists, but is larger than what
			// it's supposed to be, truncate it
			// if the file is empty and doesn't already exist, create it
			if ((!ec && cached_size > files().file_size(file_index))
				|| (files().file_size(file_index) == 0 && cached_size == stat_cache::no_exist))
			{
				std::string file_path = files().file_path(file_index, m_save_path);
				std::string dir = parent_path(file_path);

				if (dir != last_path)
				{
					last_path = dir;

					create_directories(last_path, ec.ec);
					if (ec.ec)
					{
						ec.file = file_index;
						ec.operation = storage_error::mkdir;
						break;
					}
				}
				ec.ec.clear();
				file_handle f = open_file(file_index, file::read_write
					| file::random_access, ec);
				if (ec) return;

				boost::int64_t const size = files().file_size(file_index);
				f->set_size(size, ec.ec);
				if (ec)
				{
					ec.file = file_index;
					ec.operation = storage_error::fallocate;
					break;
				}
				size_t const mtime = m_stat_cache.get_filetime(file_index);
				m_stat_cache.set_cache(file_index, size, mtime);
			}
			ec.ec.clear();
		}

		// close files that were opened in write mode
		m_pool.release(this);

#if defined TORRENT_DEBUG_FILE_LEAKS
		print_open_files("release files", m_files.name().c_str());
#endif
	}

#ifndef TORRENT_NO_DEPRECATE
	void default_storage::finalize_file(int, storage_error&) {}
#endif

	bool default_storage::has_any_file(storage_error& ec)
	{
		m_stat_cache.init(files().num_files());

		std::string file_path;
		for (int i = 0; i < files().num_files(); ++i)
		{
			file_status s;
			boost::int64_t cache_status = m_stat_cache.get_filesize(i);
			if (cache_status < 0 && cache_status != stat_cache::no_exist)
			{
				file_path = files().file_path(i, m_save_path);
				stat_file(file_path, &s, ec.ec);
				boost::int64_t r = s.file_size;
				if (ec.ec || !(s.mode & file_status::regular_file))
				{
					r = stat_cache::cache_error;
				}

				if (ec && ec.ec == boost::system::errc::no_such_file_or_directory)
				{
					ec.ec.clear();
					r = stat_cache::no_exist;
				}
				m_stat_cache.set_cache(i, r, s.mtime);

				if (ec)
				{
					ec.file = i;
					ec.operation = storage_error::stat;
					m_stat_cache.clear();
					return false;
				}
			}

			// if we didn't find the file, check the next one
			if (m_stat_cache.get_filesize(i) == stat_cache::no_exist) continue;

			if (m_stat_cache.get_filesize(i) > 0)
				return true;
		}
		file_status s;
		stat_file(combine_path(m_save_path, m_part_file_name), &s, ec.ec);
		if (!ec) return true;

		if (ec && ec.ec == boost::system::errc::no_such_file_or_directory)
			ec.ec.clear();
		if (ec)
		{
			ec.file = -1;
			ec.operation = storage_error::stat;
			return false;
		}
		return false;
	}

	void default_storage::rename_file(int index, std::string const& new_filename
		, storage_error& ec)
	{
		if (index < 0 || index >= files().num_files()) return;
		std::string old_name = files().file_path(index, m_save_path);
		m_pool.release(this, index);

		// if the old file doesn't exist, just succeed and change the filename
		// that will be created. This shortcut is important because the
		// destination directory may not exist yet, which would cause a failure
		// even though we're not moving a file (yet). It's better for it to
		// fail later when we try to write to the file the first time, because
		// the user then will have had a chance to make the destination directory
		// valid.
		if (exists(old_name, ec.ec))
		{
#if defined TORRENT_DEBUG_FILE_LEAKS
			print_open_files("release files", m_files.name().c_str());
#endif

			std::string new_path;
			if (is_complete(new_filename)) new_path = new_filename;
			else new_path = combine_path(m_save_path, new_filename);
			std::string new_dir = parent_path(new_path);

			// create any missing directories that the new filename
			// lands in
			create_directories(new_dir, ec.ec);
			if (ec.ec)
			{
				ec.file = index;
				ec.operation = storage_error::rename;
				return;
			}

			rename(old_name, new_path, ec.ec);

			// if old_name doesn't exist, that's not an error
			// here. Once we start writing to the file, it will
			// be written to the new filename
			if (ec.ec == boost::system::errc::no_such_file_or_directory)
				ec.ec.clear();

			if (ec)
			{
				ec.file = index;
				ec.operation = storage_error::rename;
				return;
			}
		}
		else if (ec.ec)
		{
			// if exists fails, report that error
			ec.file = index;
			ec.operation = storage_error::rename;
			return;
		}

		// if old path doesn't exist, just rename the file
		// in our file_storage, so that when it is created
		// it will get the new name
		if (!m_mapped_files)
		{ m_mapped_files.reset(new file_storage(m_files)); }
		m_mapped_files->rename_file(index, new_filename);
	}

	void default_storage::release_files(storage_error&)
	{
		if (m_part_file)
		{
			error_code ignore;
			m_part_file->flush_metadata(ignore);
		}

		// make sure we don't have the files open
		m_pool.release(this);

		m_stat_cache.clear();

#if defined TORRENT_DEBUG_FILE_LEAKS
		print_open_files("release files", m_files.name().c_str());
#endif
	}

	void default_storage::delete_one_file(std::string const& p, error_code& ec)
	{
		remove(p, ec);

		DFLOG(stderr, "[%p] delete_one_file: %s [%s]\n", static_cast<void*>(this)
			, p.c_str(), ec.message().c_str());

		if (ec == boost::system::errc::no_such_file_or_directory)
			ec.clear();
	}

	void default_storage::delete_files(int const options, storage_error& ec)
	{
		DFLOG(stderr, "[%p] delete_files [%x]\n", static_cast<void*>(this)
			, options);

#if TORRENT_USE_ASSERTS
		// this is a fence job, we expect no other
		// threads to hold any references to any files
		// in this file storage. Assert that that's the
		// case
		if (!m_pool.assert_idle_files(this))
		{
#if defined TORRENT_DEBUG_FILE_LEAKS
			print_open_files("delete-files idle assert failed", m_files.name().c_str());
#endif
			TORRENT_ASSERT(false);
		}
#endif

		// make sure we don't have the files open
		m_pool.release(this);

		// if there's a part file open, make sure to destruct it to have it
		// release the underlying part file. Otherwise we may not be able to
		// delete it
		if (m_part_file) m_part_file.reset();

#if defined TORRENT_DEBUG_FILE_LEAKS
		print_open_files("release files", m_files.name().c_str());
#endif

		if (options == session::delete_files)
		{
#if TORRENT_USE_ASSERTS
			m_pool.mark_deleted(m_files);
#endif
			// delete the files from disk
			std::set<std::string> directories;
			typedef std::set<std::string>::iterator iter_t;
			for (int i = 0; i < files().num_files(); ++i)
			{
				std::string const fp = files().file_path(i);
				bool const complete = files().file_absolute_path(i);
				std::string p = complete ? fp : combine_path(m_save_path, fp);
				if (!complete)
				{
					std::string bp = parent_path(fp);
					std::pair<iter_t, bool> ret;
					ret.second = true;
					while (ret.second && !bp.empty())
					{
						ret = directories.insert(combine_path(m_save_path, bp));
						bp = parent_path(bp);
					}
				}
				delete_one_file(p, ec.ec);
				if (ec) { ec.file = i; ec.operation = storage_error::remove; }
			}

			// remove the directories. Reverse order to delete
			// subdirectories first

			for (std::set<std::string>::reverse_iterator i = directories.rbegin()
				, end(directories.rend()); i != end; ++i)
			{
				error_code error;
				delete_one_file(*i, error);
				if (error && !ec) { ec.file = -1; ec.ec = error; ec.operation = storage_error::remove; }
			}
		}

		if (options == session::delete_files
			|| options == session::delete_partfile)
		{
			error_code error;
			remove(combine_path(m_save_path, m_part_file_name), error);
			DFLOG(stderr, "[%p] delete partfile %s/%s [%s]\n", static_cast<void*>(this)
				, m_save_path.c_str(), m_part_file_name.c_str(), error.message().c_str());
			if (error && error != boost::system::errc::no_such_file_or_directory)
			{
				ec.file = -1;
				ec.ec = error;
				ec.operation = storage_error::remove;
			}
		}

		DFLOG(stderr, "[%p] delete_files result: %s\n", static_cast<void*>(this)
			, ec.ec.message().c_str());

#if defined TORRENT_DEBUG_FILE_LEAKS
		print_open_files("delete-files done", m_files.name().c_str());
#endif
	}

	void default_storage::write_resume_data(entry& rd, storage_error& ec) const
	{
		TORRENT_ASSERT(rd.type() == entry::dictionary_t);

		entry::list_type& fl = rd["file sizes"].list();

		if (m_part_file)
		{
			error_code ignore;
			const_cast<part_file&>(*m_part_file).flush_metadata(ignore);
		}

		file_storage const& fs = files();
		for (int i = 0; i < fs.num_files(); ++i)
		{
			boost::int64_t file_size = 0;
			time_t file_time = 0;
			boost::int64_t const cache_state = m_stat_cache.get_filesize(i);
			if (cache_state != stat_cache::not_in_cache)
			{
				if (cache_state >= 0)
				{
					file_size = cache_state;
					file_time = m_stat_cache.get_filetime(i);
				}
			}
			else
			{
				file_status s;
				error_code error;
				stat_file(fs.file_path(i, m_save_path), &s, error);
				if (!error)
				{
					file_size = s.file_size;
					file_time = s.mtime;
					m_stat_cache.set_cache(i, file_size, file_time);
				}
				else if (error == error_code(boost::system::errc::no_such_file_or_directory
					, generic_category()))
				{
					m_stat_cache.set_noexist(i);
				}
				else
				{
					ec.ec = error;
					ec.file = i;
					ec.operation = storage_error::stat;
					m_stat_cache.set_error(i);
				}
			}
#if TORRENT_USE_INVARIANT_CHECKS
			{
				file_status s;
				error_code error;
				stat_file(fs.file_path(i, m_save_path), &s, error);
				if (s.file_size >= 0 && !error)
				{
					TORRENT_ASSERT_VAL(s.file_size == file_size, file_size);
				}
				else
				{
					TORRENT_ASSERT_VAL(file_size == 0, file_size);
				}
			}
#endif

			fl.push_back(entry(entry::list_t));
			entry::list_type& p = fl.back().list();
			p.push_back(entry(file_size));
			p.push_back(entry(file_time));
		}
	}

	bool default_storage::verify_resume_data(bdecode_node const& rd
		, std::vector<std::string> const* links
		, storage_error& ec)
	{
		// TODO: make this more generic to not just work if files have been
		// renamed, but also if they have been merged into a single file for instance
		// maybe use the same format as .torrent files and reuse some code from torrent_info
		bdecode_node mapped_files = rd.dict_find_list("mapped_files");
		if (mapped_files && mapped_files.list_size() == m_files.num_files())
		{
			m_mapped_files.reset(new file_storage(m_files));
			for (int i = 0; i < m_files.num_files(); ++i)
			{
				std::string new_filename = mapped_files.list_string_value_at(i);
				if (new_filename.empty()) continue;
				m_mapped_files->rename_file(i, new_filename);
			}
		}

		bdecode_node file_priority = rd.dict_find_list("file_priority");
		if (file_priority && file_priority.list_size()
			== files().num_files())
		{
			m_file_priority.resize(file_priority.list_size());
			for (int i = 0; i < file_priority.list_size(); ++i)
				m_file_priority[i] = boost::uint8_t(file_priority.list_int_value_at(i, 1));
		}

		bdecode_node file_sizes_ent = rd.dict_find_list("file sizes");
		if (file_sizes_ent == 0)
		{
			ec.ec = errors::missing_file_sizes;
			ec.file = -1;
			ec.operation = storage_error::check_resume;
			return false;
		}

		if (file_sizes_ent.list_size() == 0)
		{
			ec.ec = errors::no_files_in_resume_data;
			return false;
		}

		file_storage const& fs = files();
		if (file_sizes_ent.list_size() != fs.num_files())
		{
			ec.ec = errors::mismatching_number_of_files;
			ec.file = -1;
			ec.operation = storage_error::check_resume;
			return false;
		}

		bool seed = false;
		bdecode_node slots = rd.dict_find_list("slots");
		if (slots)
		{
			if (slots.list_size() == m_files.num_pieces())
			{
				seed = true;
				for (int i = 0; i < slots.list_size(); ++i)
				{
					if (slots.list_int_value_at(i, -1) >= 0) continue;
					seed = false;
					break;
				}
			}
		}
		else if (bdecode_node pieces = rd.dict_find_string("pieces"))
		{
			if (pieces.string_length() == m_files.num_pieces())
			{
				seed = true;
				char const* p = pieces.string_ptr();
				for (int i = 0; i < pieces.string_length(); ++i)
				{
					if ((p[i] & 1) == 1) continue;
					seed = false;
					break;
				}
			}
		}
		else
		{
			ec.ec = errors::missing_pieces;
			ec.file = -1;
			ec.operation = storage_error::check_resume;
			return false;
		}

		for (int i = 0; i < file_sizes_ent.list_size(); ++i)
		{
			if (fs.pad_file_at(i)) continue;

			// files with priority zero may not have been saved to disk at their
			// expected location, but is likely to be in a partfile. Just exempt it
			// from checking
			if (i < int(m_file_priority.size())
				&& m_file_priority[i] == 0)
				continue;

			bdecode_node e = file_sizes_ent.list_at(i);
			if (e.type() != bdecode_node::list_t
				|| e.list_size() < 2
				|| e.list_at(0).type() != bdecode_node::int_t
				|| e.list_at(1).type() != bdecode_node::int_t)
			{
				ec.ec = errors::missing_file_sizes;
				ec.file = i;
				ec.operation = storage_error::check_resume;
				return false;
			}

			boost::int64_t expected_size = e.list_int_value_at(0);
			time_t expected_time = e.list_int_value_at(1);

			// if we're a seed, the expected size should match
			// the actual full size according to the torrent
			if (seed && expected_size < fs.file_size(i))
			{
				ec.ec = errors::mismatching_file_size;
				ec.file = i;
				ec.operation = storage_error::check_resume;
				return false;
			}

			boost::int64_t file_size = m_stat_cache.get_filesize(i);
			time_t file_time;
			if (file_size >= 0)
			{
				file_time = m_stat_cache.get_filetime(i);
			}
			else
			{
				file_status s;
				error_code error;
				std::string file_path = fs.file_path(i, m_save_path);
				stat_file(file_path, &s, error);
				if (error)
				{
					if (error != boost::system::errc::no_such_file_or_directory)
					{
						m_stat_cache.set_error(i);
						ec.ec = error;
						ec.file = i;
						ec.operation = storage_error::stat;
						return false;
					}
					m_stat_cache.set_noexist(i);
					if (expected_size != 0)
					{
						ec.ec = errors::mismatching_file_size;
						ec.file = i;
						ec.operation = storage_error::none;
						return false;
					}
					file_size = 0;
					file_time = 0;
				}
				else
				{
					file_size = s.file_size;
					file_time = s.mtime;
				}
			}

			if (expected_size > file_size)
			{
				ec.ec = errors::mismatching_file_size;
				ec.file = i;
				ec.operation = storage_error::none;
				return false;
			}

			if (settings().get_bool(settings_pack::ignore_resume_timestamps)) continue;

			// allow some slack, because of FAT volumes
			if (expected_time != 0 &&
				(file_time > expected_time + 5 * 60 || file_time < expected_time - 5))
			{
				ec.ec = errors::mismatching_file_timestamp;
				ec.file = i;
				ec.operation = storage_error::stat;
				return false;
			}
		}

		// TODO: 2 we probably need to do this unconditionally in this function.
		// Even if the resume data file appears stale, we need to create these
		// hard links, right?
#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS
		if (links)
		{
			// if this is a mutable torrent, and we need to pick up some files
			// from other torrents, do that now. Note that there is an inherent
			// race condition here. We checked if the files existed on a different
			// thread a while ago. These files may no longer exist or may have been
			// moved. If so, we just fail. The user is responsible to not touch
			// other torrents until a new mutable torrent has been completely
			// added.
			int idx = 0;
			for (std::vector<std::string>::const_iterator i = links->begin();
				i != links->end(); ++i, ++idx)
			{
				if (i->empty()) continue;

				error_code err;
				std::string file_path = fs.file_path(idx, m_save_path);
				hard_link(*i, file_path, err);

				// if the file already exists, that's not an error
				// TODO: 2 is this risky? The upper layer will assume we have the
				// whole file. Perhaps we should verify that at least the size
				// of the file is correct
				if (!err || err == boost::system::errc::file_exists)
					continue;

				ec.ec = err;
				ec.file = idx;
				ec.operation = storage_error::hard_link;
				return false;
			}
		}
#endif // TORRENT_DISABLE_MUTABLE_TORRENTS

		return true;
	}

	int default_storage::move_storage(std::string const& sp, int const flags
		, storage_error& ec)
	{
		int ret = piece_manager::no_error;
		std::string const save_path = complete(sp);

		// check to see if any of the files exist
		file_storage const& f = files();

		if (flags == fail_if_exist)
		{
			file_status s;
			error_code err;
			stat_file(save_path, &s, err);
			if (err != boost::system::errc::no_such_file_or_directory)
			{
				// the directory exists, check all the files
				for (int i = 0; i < f.num_files(); ++i)
				{
					// files moved out to absolute paths are ignored
					if (f.file_absolute_path(i)) continue;

					stat_file(f.file_path(i, save_path), &s, err);
					if (err != boost::system::errc::no_such_file_or_directory)
					{
						ec.ec = err;
						ec.file = i;
						ec.operation = storage_error::stat;
						return piece_manager::file_exist;
					}
				}
			}
		}

		{
			file_status s;
			error_code err;
			stat_file(save_path, &s, err);
			if (err == boost::system::errc::no_such_file_or_directory)
			{
				err.clear();
				create_directories(save_path, err);
				if (err)
				{
					ec.ec = err;
					ec.file = -1;
					ec.operation = storage_error::mkdir;
					return piece_manager::fatal_disk_error;
				}
			}
			else if (err)
			{
				ec.ec = err;
				ec.file = -1;
				ec.operation = storage_error::stat;
				return piece_manager::fatal_disk_error;
			}
		}

		m_pool.release(this);

#if defined TORRENT_DEBUG_FILE_LEAKS
		print_open_files("release files", m_files.name().c_str());
#endif

		// indices of all files we ended up copying. These need to be deleted
		// later
		std::vector<bool> copied_files(f.num_files(), false);

		int i;
		error_code e;
		for (i = 0; i < f.num_files(); ++i)
		{
			// files moved out to absolute paths are not moved
			if (f.file_absolute_path(i)) continue;

			std::string const old_path = combine_path(m_save_path, f.file_path(i));
			std::string const new_path = combine_path(save_path, f.file_path(i));

			if (flags == dont_replace && exists(new_path))
			{
				if (ret == piece_manager::no_error) ret = piece_manager::need_full_check;
				// this is a new file, clear our cached version
				m_stat_cache.set_dirty(i);
				continue;
			}

			// TODO: ideally, if we end up copying files because of a move across
			// volumes, the source should not be deleted until they've all been
			// copied. That would let us rollback with higher confidence.
			move_file(old_path, new_path, e);

			// if the source file doesn't exist. That's not a problem
			// we just ignore that file
			if (e == boost::system::errc::no_such_file_or_directory)
			{
				e.clear();
				// the source file doesn't exist, but it may exist at the
				// destination, we don't know.
				m_stat_cache.set_dirty(i);
			}
			else if (e
				&& e != boost::system::errc::invalid_argument
				&& e != boost::system::errc::permission_denied)
			{
				// moving the file failed
				// on OSX, the error when trying to rename a file across different
				// volumes is EXDEV, which will make it fall back to copying.
				e.clear();
				copy_file(old_path, new_path, e);
				if (!e) copied_files[i] = true;
			}

			if (e)
			{
				ec.ec = e;
				ec.file = i;
				ec.operation = storage_error::rename;
				break;
			}
		}

		if (!e && m_part_file)
		{
			m_part_file->move_partfile(save_path, e);
			if (e)
			{
				ec.ec = e;
				ec.file = -1;
				ec.operation = storage_error::partfile_move;
			}
		}

		if (e)
		{
			// rollback
			while (--i >= 0)
			{
				// files moved out to absolute paths are not moved
				if (f.file_absolute_path(i)) continue;

				// if we ended up copying the file, don't do anything during
				// roll-back
				if (copied_files[i]) continue;

				std::string const old_path = combine_path(m_save_path, f.file_path(i));
				std::string const new_path = combine_path(save_path, f.file_path(i));

				// ignore errors when rolling back
				error_code ignore;
				move_file(new_path, old_path, ignore);
			}

			return piece_manager::fatal_disk_error;
		}

		std::string const old_save_path = m_save_path;
		m_save_path = save_path;

		std::set<std::string> subdirs;
		for (i = 0; i < f.num_files(); ++i)
		{
			// files moved out to absolute paths are not moved
			if (f.file_absolute_path(i)) continue;

			if (has_parent_path(f.file_path(i)))
				subdirs.insert(parent_path(f.file_path(i)));

			// if we ended up renaming the file instead of moving it, there's no
			// need to delete the source.
			if (copied_files[i] == false) continue;

			std::string const old_path = combine_path(old_save_path, f.file_path(i));

			// we may still have some files in old old_save_path
			// eg. if (flags == dont_replace && exists(new_path))
			// ignore errors when removing
			error_code ignore;
			remove(old_path, ignore);
		}

		for (std::set<std::string>::iterator it(subdirs.begin())
			, end(subdirs.end()); it != end; ++it)
		{
			error_code err;
			std::string subdir = combine_path(old_save_path, *it);
			while (subdir != old_save_path && !err)
			{
				remove(subdir, err);
				subdir = parent_path(subdir);
			}
		}

		return ret;
	}

	int default_storage::readv(file::iovec_t const* bufs, int num_bufs
		, int piece, int offset, int flags, storage_error& ec)
	{
		read_fileop op(*this, flags);

#ifdef TORRENT_SIMULATE_SLOW_READ
		boost::thread::sleep(boost::get_system_time()
			+ boost::posix_time::milliseconds(1000));
#endif
		return readwritev(files(), bufs, piece, offset, num_bufs, op, ec);
	}

	int default_storage::writev(file::iovec_t const* bufs, int num_bufs
		, int piece, int offset, int flags, storage_error& ec)
	{
		write_fileop op(*this, flags);
		return readwritev(files(), bufs, piece, offset, num_bufs, op, ec);
	}

	// much of what needs to be done when reading and writing is buffer
	// management and piece to file mapping. Most of that is the same for reading
	// and writing. This function is a template, and the fileop decides what to
	// do with the file and the buffers.
	int readwritev(file_storage const& files, file::iovec_t const* const bufs
		, const int piece, const int offset, const int num_bufs, fileop& op
		, storage_error& ec)
	{
		TORRENT_ASSERT(bufs != 0);
		TORRENT_ASSERT(piece >= 0);
		TORRENT_ASSERT(piece < files.num_pieces());
		TORRENT_ASSERT(offset >= 0);
		TORRENT_ASSERT(num_bufs > 0);

		const int size = bufs_size(bufs, num_bufs);
		TORRENT_ASSERT(size > 0);
		TORRENT_ASSERT(files.is_loaded());

		// find the file iterator and file offset
		boost::uint64_t torrent_offset = piece * boost::uint64_t(files.piece_length()) + offset;
		int file_index = files.file_index_at_offset(torrent_offset);
		TORRENT_ASSERT(torrent_offset >= files.file_offset(file_index));
		TORRENT_ASSERT(torrent_offset < files.file_offset(file_index) + files.file_size(file_index));
		boost::int64_t file_offset = torrent_offset - files.file_offset(file_index);

		// the number of bytes left before this read or write operation is
		// completely satisfied.
		int bytes_left = size;

		TORRENT_ASSERT(bytes_left >= 0);

		// copy the iovec array so we can use it to keep track of our current
		// location by updating the head base pointer and size. (see
		// advance_bufs())
		file::iovec_t* current_buf = TORRENT_ALLOCA(file::iovec_t, num_bufs);
		copy_bufs(bufs, size, current_buf);
		TORRENT_ASSERT(count_bufs(current_buf, size) == num_bufs);

		file::iovec_t* tmp_buf = TORRENT_ALLOCA(file::iovec_t, num_bufs);

		// the number of bytes left to read in the current file (specified by
		// file_index). This is the minimum of (file_size - file_offset) and
		// bytes_left.
		int file_bytes_left;

		while (bytes_left > 0)
		{
			file_bytes_left = bytes_left;
			if (file_offset + file_bytes_left > files.file_size(file_index))
				file_bytes_left = (std::max)(static_cast<int>(files.file_size(file_index) - file_offset), 0);

			// there are no bytes left in this file, move to the next one
			// this loop skips over empty files
			while (file_bytes_left == 0)
			{
				++file_index;
				file_offset = 0;
				TORRENT_ASSERT(file_index < files.num_files());

				// this should not happen. bytes_left should be clamped by the total
				// size of the torrent, so we should never run off the end of it
				if (file_index >= files.num_files()) return size;

				file_bytes_left = bytes_left;
				if (file_offset + file_bytes_left > files.file_size(file_index))
					file_bytes_left = (std::max)(static_cast<int>(files.file_size(file_index) - file_offset), 0);
			}

			// make a copy of the iovec array that _just_ covers the next
			// file_bytes_left bytes, i.e. just this one operation
			copy_bufs(current_buf, file_bytes_left, tmp_buf);

			int bytes_transferred = op.file_op(file_index, file_offset,
				file_bytes_left, tmp_buf, ec);
			if (ec) return -1;

			// advance our position in the iovec array and the file offset.
			advance_bufs(current_buf, bytes_transferred);
			bytes_left -= bytes_transferred;
			file_offset += bytes_transferred;

			TORRENT_ASSERT(count_bufs(current_buf, bytes_left) <= num_bufs);

			// if the file operation returned 0, we've hit end-of-file. We're done
			if (bytes_transferred == 0)
			{
				if (file_bytes_left > 0 )
				{
					// fill in this information in case the caller wants to treat
					// a short-read as an error
					ec.file = file_index;
				}
				return size - bytes_left;
			}
		}
		return size;
	}

	file_handle default_storage::open_file(int file, int mode
		, storage_error& ec) const
	{
		file_handle h = open_file_impl(file, mode, ec.ec);
		if (((mode & file::rw_mask) != file::read_only)
			&& ec.ec == boost::system::errc::no_such_file_or_directory)
		{
			// this means the directory the file is in doesn't exist.
			// so create it
			ec.ec.clear();
			std::string path = files().file_path(file, m_save_path);
			create_directories(parent_path(path), ec.ec);

			if (ec.ec)
			{
				ec.file = file;
				ec.operation = storage_error::mkdir;
				return file_handle();
			}

			// if the directory creation failed, don't try to open the file again
			// but actually just fail
			h = open_file_impl(file, mode, ec.ec);
		}
		if (ec.ec)
		{
			ec.file = file;
			ec.operation = storage_error::open;
			return file_handle();
		}
		TORRENT_ASSERT(h);

		if (m_allocate_files && (mode & file::rw_mask) != file::read_only)
		{
			mutex::scoped_lock l(m_file_created_mutex);
			if (m_file_created.size() != files().num_files())
				m_file_created.resize(files().num_files(), false);

			TORRENT_ASSERT(int(m_file_created.size()) == files().num_files());
			TORRENT_ASSERT(file < m_file_created.size());
			// if this is the first time we open this file for writing,
			// and we have m_allocate_files enabled, set the final size of
			// the file right away, to allocate it on the filesystem.
			if (m_file_created[file] == false)
			{
				m_file_created.set_bit(file);
				l.unlock();
				error_code e;
				boost::int64_t const size = files().file_size(file);
				h->set_size(size, e);
				if (e)
				{
					ec.ec = e;
					ec.file = file;
					ec.operation = storage_error::fallocate;
					return h;
				}
				m_stat_cache.set_dirty(file);
			}
		}
		return h;
	}

	file_handle default_storage::open_file_impl(int file, int mode
		, error_code& ec) const
	{
		bool lock_files = m_settings ? settings().get_bool(settings_pack::lock_files) : false;
		if (lock_files) mode |= file::lock_file;

		if (!m_allocate_files) mode |= file::sparse;

		// files with priority 0 should always be sparse
		if (int(m_file_priority.size()) > file && m_file_priority[file] == 0)
			mode |= file::sparse;

		if (m_settings && settings().get_bool(settings_pack::no_atime_storage)) mode |= file::no_atime;

		// if we have a cache already, don't store the data twice by leaving it in the OS cache as well
		if (m_settings
			&& settings().get_int(settings_pack::disk_io_write_mode)
			== settings_pack::disable_os_cache)
		{
			mode |= file::no_cache;
		}

		file_handle ret = m_pool.open_file(const_cast<default_storage*>(this)
			, m_save_path, file, files(), mode, ec);
		if (ec && (mode & file::lock_file))
		{
			// we failed to open the file and we're trying to lock it. It's
			// possible we're failing because we have another handle to this
			// file in use (but waiting to be closed). Just retry to open it
			// without locking.
			mode &= ~file::lock_file;
			ret = m_pool.open_file(const_cast<default_storage*>(this)
				, m_save_path, file, files(), mode, ec);
		}
		return ret;
	}

	bool default_storage::tick()
	{
		error_code ec;
		if (m_part_file) m_part_file->flush_metadata(ec);

		return false;
	}

#ifdef TORRENT_DISK_STATS
	bool default_storage::disk_write_access_log() {
		return g_access_log != NULL;
	}

	void default_storage::disk_write_access_log(bool enable) {
		if (enable)
		{
			if (g_access_log == NULL)
			{
				g_access_log = fopen("file_access.log", "a+");
			}
		}
		else
		{
			if (g_access_log != NULL)
			{
				FILE* f = g_access_log;
				g_access_log = NULL;
				fclose(f);
			}
		}
	}
#endif

	storage_interface* default_storage_constructor(storage_params const& params)
	{
		return new default_storage(params);
	}

	int disabled_storage::readv(file::iovec_t const* bufs
		, int num_bufs, int, int, int, storage_error&)
	{
		return bufs_size(bufs, num_bufs);
	}

	int disabled_storage::writev(file::iovec_t const* bufs
		, int num_bufs, int, int, int, storage_error&)
	{
		return bufs_size(bufs, num_bufs);
	}

	storage_interface* disabled_storage_constructor(storage_params const& params)
	{
		TORRENT_UNUSED(params);
		return new disabled_storage;
	}

	// -- zero_storage ------------------------------------------------------

	int zero_storage::readv(file::iovec_t const* bufs, int num_bufs
		, int /* piece */, int /* offset */, int /* flags */, storage_error&)
	{
		int ret = 0;
		for (int i = 0; i < num_bufs; ++i)
		{
			memset(bufs[i].iov_base, 0, bufs[i].iov_len);
			ret += bufs[i].iov_len;
		}
		return 0;
	}

	int zero_storage::writev(file::iovec_t const* bufs, int num_bufs
		, int /* piece */, int /* offset */, int /* flags */, storage_error&)
	{
		int ret = 0;
		for (int i = 0; i < num_bufs; ++i)
			ret += bufs[i].iov_len;
		return 0;
	}

	storage_interface* zero_storage_constructor(storage_params const&)
	{
		return new zero_storage;
	}

	void storage_piece_set::add_piece(cached_piece_entry* p)
	{
		TORRENT_ASSERT(p->in_storage == false);
		TORRENT_ASSERT(p->storage.get() == this);
		TORRENT_ASSERT(m_cached_pieces.count(p) == 0);
		m_cached_pieces.insert(p);
#if TORRENT_USE_ASSERTS
		p->in_storage = true;
#endif
	}

	bool storage_piece_set::has_piece(cached_piece_entry const* p) const
	{
		return m_cached_pieces.count(const_cast<cached_piece_entry*>(p)) > 0;
	}

	void storage_piece_set::remove_piece(cached_piece_entry* p)
	{
		TORRENT_ASSERT(p->in_storage == true);
		TORRENT_ASSERT(m_cached_pieces.count(p) == 1);
		m_cached_pieces.erase(p);
#if TORRENT_USE_ASSERTS
		p->in_storage = false;
#endif
	}

	// -- piece_manager -----------------------------------------------------

	piece_manager::piece_manager(
		storage_interface* storage_impl
		, boost::shared_ptr<void> const& torrent
		, file_storage* files)
		: m_files(*files)
		, m_storage(storage_impl)
		, m_torrent(torrent)
	{
	}

	piece_manager::~piece_manager()
	{}

#ifdef TORRENT_DEBUG
	void piece_manager::assert_torrent_refcount() const
	{
		if (!m_torrent) return;
		// sorry about this layer violation, but it's
		// quite convenient to make sure the torrent won't
		// get unloaded under our feet later
		TORRENT_ASSERT(static_cast<torrent*>(m_torrent.get())->refcount() > 0);
	}
#endif

	// used in torrent_handle.cpp
	void piece_manager::write_resume_data(entry& rd, storage_error& ec) const
	{
		m_storage->write_resume_data(rd, ec);
	}

	int piece_manager::check_no_fastresume(storage_error& ec)
	{
		bool has_files = false;
		if (!m_storage->settings().get_bool(settings_pack::no_recheck_incomplete_resume))
		{
			storage_error se;
			has_files = m_storage->has_any_file(se);

			if (se)
			{
				ec = se;
				return fatal_disk_error;
			}

			if (has_files)
			{
				// always initialize the storage
				int ret = check_init_storage(ec);
				return ret != no_error ? ret : need_full_check;
			}
		}

		return check_init_storage(ec);
	}

	int piece_manager::check_init_storage(storage_error& ec)
	{
		storage_error se;
		m_storage->initialize(se);
		if (se)
		{
			ec = se;
			return fatal_disk_error;
		}

		return no_error;
	}

	// check if the fastresume data is up to date
	// if it is, use it and return true. If it
	// isn't return false and the full check
	// will be run. If the links pointer is non-null, it has the same number
	// of elements as there are files. Each element is either empty or contains
	// the absolute path to a file identical to the corresponding file in this
	// torrent. The storage must create hard links (or copy) those files. If
	// any file does not exist or is inaccessible, the disk job must fail.
	int piece_manager::check_fastresume(
		bdecode_node const& rd
		, std::vector<std::string> const* links
		, storage_error& ec)
	{
		TORRENT_ASSERT(m_files.piece_length() > 0);

		// if we don't have any resume data, return
		if (rd.type() == bdecode_node::none_t) return check_no_fastresume(ec);

		if (rd.type() != bdecode_node::dict_t)
		{
			ec.ec = errors::not_a_dictionary;
			return check_no_fastresume(ec);
		}

		int block_size = (std::min)(16 * 1024, m_files.piece_length());
		int blocks_per_piece = int(rd.dict_find_int_value("blocks per piece", -1));
		if (blocks_per_piece != -1
			&& blocks_per_piece != m_files.piece_length() / block_size)
		{
			ec.ec = errors::invalid_blocks_per_piece;
			return check_no_fastresume(ec);
		}

		if (!m_storage->verify_resume_data(rd, links, ec))
			return check_no_fastresume(ec);

		return check_init_storage(ec);
	}

	// ====== disk_job_fence implementation ========

	disk_job_fence::disk_job_fence()
		: m_has_fence(0)
		, m_outstanding_jobs(0)
	{}

	int disk_job_fence::job_complete(disk_io_job* j, tailqueue<disk_io_job>& jobs)
	{
		mutex::scoped_lock l(m_mutex);

		TORRENT_ASSERT(j->flags & disk_io_job::in_progress);
		j->flags &= ~disk_io_job::in_progress;

		TORRENT_ASSERT(m_outstanding_jobs > 0);
		--m_outstanding_jobs;
		if (j->flags & disk_io_job::fence)
		{
			// a fence job just completed. Make sure the fence logic
			// works by asserting m_outstanding_jobs is in fact 0 now
			TORRENT_ASSERT(m_outstanding_jobs == 0);

			// the fence can now be lowered
			--m_has_fence;

			// now we need to post all jobs that have been queued up
			// while this fence was up. However, if there's another fence
			// in the queue, stop there and raise the fence again
			int ret = 0;
			while (m_blocked_jobs.size())
			{
				disk_io_job *bj = static_cast<disk_io_job*>(m_blocked_jobs.pop_front());
				if (bj->flags & disk_io_job::fence)
				{
					// we encountered another fence. We cannot post anymore
					// jobs from the blocked jobs queue. We have to go back
					// into a raised fence mode and wait for all current jobs
					// to complete. The exception is that if there are no jobs
					// executing currently, we should add the fence job.
					if (m_outstanding_jobs == 0 && jobs.empty())
					{
						TORRENT_ASSERT((bj->flags & disk_io_job::in_progress) == 0);
						bj->flags |= disk_io_job::in_progress;
						++m_outstanding_jobs;
						++ret;
#if TORRENT_USE_ASSERTS
						TORRENT_ASSERT(bj->blocked);
						bj->blocked = false;
#endif
						jobs.push_back(bj);
					}
					else
					{
						// put the fence job back in the blocked queue
						m_blocked_jobs.push_front(bj);
					}
					return ret;
				}
				TORRENT_ASSERT((bj->flags & disk_io_job::in_progress) == 0);
				bj->flags |= disk_io_job::in_progress;

				++m_outstanding_jobs;
				++ret;
#if TORRENT_USE_ASSERTS
				TORRENT_ASSERT(bj->blocked);
				bj->blocked = false;
#endif
				jobs.push_back(bj);
			}
			return ret;
		}

		// there are still outstanding jobs, even if we have a
		// fence, it's not time to lower it yet
		// also, if we don't have a fence, we're done
		if (m_outstanding_jobs > 0 || m_has_fence == 0) return 0;

		// there's a fence raised, and no outstanding operations.
		// it means we can execute the fence job right now.
		TORRENT_ASSERT(m_blocked_jobs.size() > 0);

		// this is the fence job
		disk_io_job *bj = static_cast<disk_io_job*>(m_blocked_jobs.pop_front());
		TORRENT_ASSERT(bj->flags & disk_io_job::fence);

		TORRENT_ASSERT((bj->flags & disk_io_job::in_progress) == 0);
		bj->flags |= disk_io_job::in_progress;

		++m_outstanding_jobs;
#if TORRENT_USE_ASSERTS
		TORRENT_ASSERT(bj->blocked);
		bj->blocked = false;
#endif
		// prioritize fence jobs since they're blocking other jobs
		jobs.push_front(bj);
		return 1;
	}

	bool disk_job_fence::is_blocked(disk_io_job* j)
	{
		mutex::scoped_lock l(m_mutex);
		DLOG(stderr, "[%p] is_blocked: fence: %d num_outstanding: %d\n"
			, static_cast<void*>(this), m_has_fence, int(m_outstanding_jobs));

		// if this is the job that raised the fence, don't block it
		// ignore fence can only ignore one fence. If there are several,
		// this job still needs to get queued up
		if (m_has_fence == 0)
		{
			TORRENT_ASSERT((j->flags & disk_io_job::in_progress) == 0);
			j->flags |= disk_io_job::in_progress;
			++m_outstanding_jobs;
			return false;
		}

		m_blocked_jobs.push_back(j);

#if TORRENT_USE_ASSERTS
		TORRENT_ASSERT(j->blocked == false);
		j->blocked = true;
#endif

		return true;
	}

	bool disk_job_fence::has_fence() const
	{
		mutex::scoped_lock l(m_mutex);
		return m_has_fence;
	}

	int disk_job_fence::num_blocked() const
	{
		mutex::scoped_lock l(m_mutex);
		return m_blocked_jobs.size();
	}

	// j is the fence job. It must have exclusive access to the storage
	// fj is the flush job. If the job j is queued, we need to issue
	// this job
	int disk_job_fence::raise_fence(disk_io_job* j, disk_io_job* fj
		, counters& cnt)
	{
		TORRENT_ASSERT((j->flags & disk_io_job::fence) == 0);
		j->flags |= disk_io_job::fence;

		mutex::scoped_lock l(m_mutex);

		DLOG(stderr, "[%p] raise_fence: fence: %d num_outstanding: %d\n"
			, static_cast<void*>(this), m_has_fence, int(m_outstanding_jobs));

		if (m_has_fence == 0 && m_outstanding_jobs == 0)
		{
			++m_has_fence;
			DLOG(stderr, "[%p] raise_fence: need posting\n"
				, static_cast<void*>(this));

			// the job j is expected to be put on the job queue
			// after this, without being passed through is_blocked()
			// that's why we're accounting for it here

			// fj is expected to be discarded by the caller
			j->flags |= disk_io_job::in_progress;
			++m_outstanding_jobs;
			return fence_post_fence;
		}

		++m_has_fence;
		if (m_has_fence > 1)
		{
#if TORRENT_USE_ASSERTS
			TORRENT_ASSERT(fj->blocked == false);
			fj->blocked = true;
#endif
			m_blocked_jobs.push_back(fj);
			cnt.inc_stats_counter(counters::blocked_disk_jobs);
		}
		else
		{
			// in this case, fj is expected to be put on the job queue
			fj->flags |= disk_io_job::in_progress;
			++m_outstanding_jobs;
		}
#if TORRENT_USE_ASSERTS
		TORRENT_ASSERT(j->blocked == false);
		j->blocked = true;
#endif
		m_blocked_jobs.push_back(j);
		cnt.inc_stats_counter(counters::blocked_disk_jobs);

		return m_has_fence > 1 ? fence_post_none : fence_post_flush;
	}
} // namespace libtorrent

