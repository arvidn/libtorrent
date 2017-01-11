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
#include "libtorrent/aux_/storage_utils.hpp"

#include <ctime>
#include <algorithm>
#include <set>
#include <functional>
#include <cstdio>

#include "libtorrent/aux_/disable_warnings_push.hpp"

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
#include "libtorrent/session.hpp"
#include "libtorrent/file.hpp"
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/file_pool.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/disk_buffer_holder.hpp"
#include "libtorrent/stat_cache.hpp"
#include "libtorrent/hex.hpp" // to_hex
// for convert_to_wstring and convert_to_native
#include "libtorrent/aux_/escape_string.hpp"

//#define TORRENT_PARTIAL_HASH_LOG

#define DEBUG_STORAGE 0
#define DEBUG_DELETE_FILES 0

#if __cplusplus >= 201103L || defined __clang__

#if DEBUG_STORAGE
#define DLOG(...) std::fprintf(__VA_ARGS__)
#else
#define DLOG(...) do {} while (false)
#endif

#if DEBUG_DELETE_FILES
#define DFLOG(...) std::fprintf(__VA_ARGS__)
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
	void clear_bufs(span<iovec_t const> bufs)
	{
		for (auto buf : bufs)
			std::memset(buf.iov_base, 0, buf.iov_len);
	}

	struct write_fileop final : fileop
	{
		write_fileop(default_storage& st, int flags)
			: m_storage(st)
			, m_flags(flags)
		{}

		int file_op(file_index_t const file_index
			, std::int64_t const file_offset
			, span<iovec_t const> bufs, storage_error& ec)
			final
		{
			if (m_storage.files().pad_file_at(file_index))
			{
				// writing to a pad-file is a no-op
				return bufs_size(bufs);
			}

			if (file_index < m_storage.m_file_priority.end_index()
				&& m_storage.m_file_priority[file_index] == 0)
			{
				m_storage.need_partfile();

				error_code e;
				peer_request map = m_storage.files().map_file(file_index
					, file_offset, 0);
				int ret = m_storage.m_part_file->writev(bufs
					, map.piece, map.start, e);

				if (e)
				{
					ec.ec = e;
					ec.file(file_index);
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
			std::int64_t adjusted_offset =
#ifndef TORRENT_NO_DEPRECATE
				m_storage.files().file_base_deprecated(file_index) +
#endif
				file_offset;

			error_code e;
			int const ret = int(handle->writev(adjusted_offset
				, bufs, e, m_flags));

			// set this unconditionally in case the upper layer would like to treat
			// short reads as errors
			ec.operation = storage_error::write;

				// we either get an error or 0 or more bytes read
			TORRENT_ASSERT(e || ret >= 0);
			TORRENT_ASSERT(ret <= bufs_size(bufs));

			if (e)
			{
				ec.ec = e;
				ec.file(file_index);
				return -1;
			}

			return ret;
		}
	private:
		default_storage& m_storage;
		int m_flags;
	};

	struct read_fileop final : fileop
	{
		read_fileop(default_storage& st, int const flags)
			: m_storage(st)
			, m_flags(flags)
		{}

		int file_op(file_index_t const file_index
			, std::int64_t const file_offset
			, span<iovec_t const> bufs, storage_error& ec)
			final
		{
			if (m_storage.files().pad_file_at(file_index))
			{
				// reading from a pad file yields zeroes
				clear_bufs(bufs);
				return bufs_size(bufs);
			}

			if (file_index < m_storage.m_file_priority.end_index()
				&& m_storage.m_file_priority[file_index] == 0)
			{
				m_storage.need_partfile();

				error_code e;
				peer_request map = m_storage.files().map_file(file_index
					, file_offset, 0);
				int ret = m_storage.m_part_file->readv(bufs
					, map.piece, map.start, e);

				if (e)
				{
					ec.ec = e;
					ec.file(file_index);
					ec.operation = storage_error::partfile_read;
					return -1;
				}
				return ret;
			}

			file_handle handle = m_storage.open_file(file_index
				, file::read_only | m_flags, ec);
			if (ec) return -1;

			// please ignore the adjusted_offset. It's just file_offset.
			std::int64_t adjusted_offset =
#ifndef TORRENT_NO_DEPRECATE
				m_storage.files().file_base_deprecated(file_index) +
#endif
				file_offset;

			error_code e;
			int const ret = int(handle->readv(adjusted_offset
				, bufs, e, m_flags));

			// set this unconditionally in case the upper layer would like to treat
			// short reads as errors
			ec.operation = storage_error::read;

				// we either get an error or 0 or more bytes read
			TORRENT_ASSERT(e || ret >= 0);
			TORRENT_ASSERT(ret <= bufs_size(bufs));

			if (e)
			{
				ec.ec = e;
				ec.file(file_index);
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
			? aux::to_hex(params.info->info_hash())
			: params.files->name()) + ".parts";
	}

	default_storage::~default_storage()
	{
		error_code ec;
		if (m_part_file) m_part_file->flush_metadata(ec);

		// this may be called from a different
		// thread than the disk thread
		m_pool.release(storage_index());
	}

	void default_storage::need_partfile()
	{
		if (m_part_file) return;

		m_part_file.reset(new part_file(
			m_save_path, m_part_file_name
			, m_files.num_pieces(), m_files.piece_length()));
	}

	void default_storage::set_file_priority(
		aux::vector<std::uint8_t, file_index_t> const& prio
		, storage_error& ec)
	{
		// extend our file priorities in case it's truncated
		// the default assumed priority is 1
		if (prio.size() > m_file_priority.size())
			m_file_priority.resize(prio.size(), 1);

		file_storage const& fs = files();
		for (file_index_t i(0); i < prio.end_index(); ++i)
		{
			int const old_prio = m_file_priority[i];
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
					ec.file(i);
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
						ec.file(i);
						ec.operation = storage_error::partfile_read;
						return;
					}
					// remove the file
					std::string p = fs.file_path(i, m_save_path);
					delete_one_file(p, ec.ec);
					if (ec)
					{
						ec.file(i);
						ec.operation = storage_error::remove;
					}
				}
*/
			}
			ec.ec.clear();
			m_file_priority[i] = std::uint8_t(new_prio);
		}
		if (m_part_file) m_part_file->flush_metadata(ec.ec);
		if (ec)
		{
			ec.file(file_index_t(-1));
			ec.operation = storage_error::partfile_write;
		}
	}

	void default_storage::initialize(storage_error& ec)
	{
		m_stat_cache.reserve(files().num_files());

#ifdef TORRENT_WINDOWS
		// don't do full file allocations on network drives
#if TORRENT_USE_WSTRING
		std::wstring file_name = convert_to_wstring(m_save_path);
		int const drive_type = GetDriveTypeW(file_name.c_str());
#else
		int const drive_type = GetDriveTypeA(m_save_path.c_str());
#endif

		if (drive_type == DRIVE_REMOTE)
			m_allocate_files = false;
#endif

		m_file_created.resize(files().num_files(), false);

		// first, create all missing directories
		std::string last_path;
		file_storage const& fs = files();
		for (file_index_t file_index(0); file_index < fs.end_file(); ++file_index)
		{
			// ignore files that have priority 0
			if (m_file_priority.end_index() > file_index
				&& m_file_priority[file_index] == 0)
			{
				continue;
			}

			// ignore pad files
			if (files().pad_file_at(file_index)) continue;

			error_code err;
			std::int64_t size = m_stat_cache.get_filesize(file_index, files()
				, m_save_path, err);

			if (err && err != boost::system::errc::no_such_file_or_directory)
			{
				ec.file(file_index);
				ec.operation = storage_error::stat;
				ec.ec = err;
				break;
			}

			// if the file already exists, but is larger than what
			// it's supposed to be, truncate it
			// if the file is empty, just create it either way.
			if ((!err && size > files().file_size(file_index))
				|| files().file_size(file_index) == 0)
			{
				std::string file_path = files().file_path(file_index, m_save_path);
				std::string dir = parent_path(file_path);

				if (dir != last_path)
				{
					last_path = dir;

					create_directories(last_path, ec.ec);
					if (ec.ec)
					{
						ec.file(file_index);
						ec.operation = storage_error::mkdir;
						break;
					}
				}
				ec.ec.clear();
				file_handle f = open_file(file_index, file::read_write
					| file::random_access, ec);
				if (ec)
				{
					ec.file(file_index);
					ec.operation = storage_error::fallocate;
					return;
				}

				size = files().file_size(file_index);
				f->set_size(size, ec.ec);
				if (ec)
				{
					ec.file(file_index);
					ec.operation = storage_error::fallocate;
					break;
				}
			}
			ec.ec.clear();
		}

		// close files that were opened in write mode
		m_pool.release(storage_index());
	}

	bool default_storage::has_any_file(storage_error& ec)
	{
		m_stat_cache.reserve(files().num_files());

		file_storage const& fs = files();
		for (file_index_t i(0); i < fs.end_file(); ++i)
		{
			std::int64_t const sz = m_stat_cache.get_filesize(
				i, files(), m_save_path, ec.ec);

			if (sz < 0)
			{
				if (ec && ec.ec != boost::system::errc::no_such_file_or_directory)
				{
					ec.file(i);
					ec.operation = storage_error::stat;
					m_stat_cache.clear();
					return false;
				}
				// some files not existing is expected and not an error
				ec.ec.clear();
			}

			if (sz > 0) return true;
		}
		file_status s;
		stat_file(combine_path(m_save_path, m_part_file_name), &s, ec.ec);
		if (!ec) return true;

		// the part file not existing is expected
		if (ec && ec.ec == boost::system::errc::no_such_file_or_directory)
			ec.ec.clear();

		if (ec)
		{
			ec.file(file_index_t(-1));
			ec.operation = storage_error::stat;
			return false;
		}
		return false;
	}

	void default_storage::rename_file(file_index_t const index, std::string const& new_filename
		, storage_error& ec)
	{
		if (index < file_index_t(0) || index >= files().end_file()) return;
		std::string old_name = files().file_path(index, m_save_path);
		m_pool.release(storage_index(), index);

		// if the old file doesn't exist, just succeed and change the filename
		// that will be created. This shortcut is important because the
		// destination directory may not exist yet, which would cause a failure
		// even though we're not moving a file (yet). It's better for it to
		// fail later when we try to write to the file the first time, because
		// the user then will have had a chance to make the destination directory
		// valid.
		if (exists(old_name, ec.ec))
		{
			std::string new_path;
			if (is_complete(new_filename)) new_path = new_filename;
			else new_path = combine_path(m_save_path, new_filename);
			std::string new_dir = parent_path(new_path);

			// create any missing directories that the new filename
			// lands in
			create_directories(new_dir, ec.ec);
			if (ec.ec)
			{
				ec.file(index);
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
				ec.file(index);
				ec.operation = storage_error::rename;
				return;
			}
		}
		else if (ec.ec)
		{
			// if exists fails, report that error
			ec.file(index);
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
			m_part_file.reset();
		}

		// make sure we don't have the files open
		m_pool.release(storage_index());
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
		if (!m_pool.assert_idle_files(storage_index()))
		{
			TORRENT_ASSERT_FAIL();
		}
#endif

		// make sure we don't have the files open
		m_pool.release(storage_index());

		// if there's a part file open, make sure to destruct it to have it
		// release the underlying part file. Otherwise we may not be able to
		// delete it
		if (m_part_file) m_part_file.reset();

		if (options == session::delete_files)
		{
#if TORRENT_USE_ASSERTS
			m_pool.mark_deleted(m_files);
#endif
			// delete the files from disk
			std::set<std::string> directories;
			using iter_t = std::set<std::string>::iterator;
			file_storage const& fs = files();
			for (file_index_t i(0); i < fs.end_file(); ++i)
			{
				std::string const fp = files().file_path(i);
				bool const complete = files().file_absolute_path(i);
				std::string const p = complete ? fp : combine_path(m_save_path, fp);
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
				if (ec) { ec.file(i); ec.operation = storage_error::remove; }
			}

			// remove the directories. Reverse order to delete
			// subdirectories first

			for (auto i = directories.rbegin()
				, end(directories.rend()); i != end; ++i)
			{
				error_code error;
				delete_one_file(*i, error);
				if (error && !ec)
				{
					ec.file(file_index_t(-1));
					ec.ec = error;
					ec.operation = storage_error::remove;
				}
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
				ec.file(file_index_t(-1));
				ec.ec = error;
				ec.operation = storage_error::remove;
			}
		}

		DFLOG(stderr, "[%p] delete_files result: %s\n", static_cast<void*>(this)
			, ec.ec.message().c_str());
	}

	bool default_storage::verify_resume_data(add_torrent_params const& rd
		, aux::vector<std::string, file_index_t> const& links
		, storage_error& ec)
	{
		file_storage const& fs = files();
#ifdef TORRENT_DISABLE_MUTABLE_TORRENTS
		TORRENT_UNUSED(links);
#else
		if (!links.empty())
		{
			TORRENT_ASSERT(int(links.size()) == fs.num_files());
			// if this is a mutable torrent, and we need to pick up some files
			// from other torrents, do that now. Note that there is an inherent
			// race condition here. We checked if the files existed on a different
			// thread a while ago. These files may no longer exist or may have been
			// moved. If so, we just fail. The user is responsible to not touch
			// other torrents until a new mutable torrent has been completely
			// added.
			for (file_index_t idx(0); idx < fs.end_file(); ++idx)
			{
				std::string const& s = links[idx];
				if (s.empty()) continue;

				error_code err;
				std::string file_path = fs.file_path(idx, m_save_path);
				hard_link(s, file_path, err);

				// if the file already exists, that's not an error
				// TODO: 2 is this risky? The upper layer will assume we have the
				// whole file. Perhaps we should verify that at least the size
				// of the file is correct
				if (!err || err == boost::system::errc::file_exists)
					continue;

				ec.ec = err;
				ec.file(idx);
				ec.operation = storage_error::hard_link;
				return false;
			}
		}
#endif // TORRENT_DISABLE_MUTABLE_TORRENTS

		bool const seed = rd.have_pieces.all_set();

		// parse have bitmask. Verify that the files we expect to have
		// actually do exist
		for (piece_index_t i(0); i < piece_index_t(rd.have_pieces.size()); ++i)
		{
			if (rd.have_pieces.get_bit(i) == false) continue;

			std::vector<file_slice> f = fs.map_block(i, 0, 1);
			TORRENT_ASSERT(!f.empty());

			file_index_t const file_index = f[0].file_index;

			// files with priority zero may not have been saved to disk at their
			// expected location, but is likely to be in a partfile. Just exempt it
			// from checking
			if (file_index < m_file_priority.end_index()
				&& m_file_priority[file_index] == 0)
				continue;

			error_code error;
			std::int64_t const size = m_stat_cache.get_filesize(f[0].file_index
				, fs, m_save_path, error);

			if (size < 0)
			{
				if (error != boost::system::errc::no_such_file_or_directory)
				{
					ec.ec = error;
					ec.file(file_index);
					ec.operation = storage_error::stat;
					return false;
				}
				else
				{
					ec.ec = errors::mismatching_file_size;
					ec.file(file_index);
					ec.operation = storage_error::stat;
					return false;
				}
			}

			if (seed && size != fs.file_size(file_index))
			{
				// the resume data indicates we're a seed, but this file has
				// the wrong size. Reject the resume data
				ec.ec = errors::mismatching_file_size;
				ec.file(file_index);
				ec.operation = storage_error::check_resume;
				return false;
			}

			// OK, this file existed, good. Now, skip all remaining pieces in
			// this file. We're just sanity-checking whether the files exist
			// or not.
			peer_request const pr = fs.map_file(file_index
				, fs.file_size(file_index) + 1, 0);
			i = std::max(next(i), pr.piece);
		}
		return true;
	}

	status_t default_storage::move_storage(std::string const& sp, int const flags
		, storage_error& ec)
	{
		status_t ret = status_t::no_error;
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
				for (file_index_t i(0); i < f.end_file(); ++i)
				{
					// files moved out to absolute paths are ignored
					if (f.file_absolute_path(i)) continue;

					stat_file(f.file_path(i, save_path), &s, err);
					if (err != boost::system::errc::no_such_file_or_directory)
					{
						ec.ec = err;
						ec.file(i);
						ec.operation = storage_error::stat;
						return status_t::file_exist;
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
					ec.file(file_index_t(-1));
					ec.operation = storage_error::mkdir;
					return status_t::fatal_disk_error;
				}
			}
			else if (err)
			{
				ec.ec = err;
				ec.file(file_index_t(-1));
				ec.operation = storage_error::stat;
				return status_t::fatal_disk_error;
			}
		}

		m_pool.release(storage_index());

		// indices of all files we ended up copying. These need to be deleted
		// later
		aux::vector<bool, file_index_t> copied_files(f.num_files(), false);

		file_index_t i;
		error_code e;
		for (i = file_index_t(0); i < f.end_file(); ++i)
		{
			// files moved out to absolute paths are not moved
			if (f.file_absolute_path(i)) continue;

			std::string const old_path = combine_path(m_save_path, f.file_path(i));
			std::string const new_path = combine_path(save_path, f.file_path(i));

			if (flags == dont_replace && exists(new_path))
			{
				if (ret == status_t::no_error) ret = status_t::need_full_check;
				continue;
			}

			// TODO: ideally, if we end up copying files because of a move across
			// volumes, the source should not be deleted until they've all been
			// copied. That would let us rollback with higher confidence.
			move_file(old_path, new_path, e);

			// if the source file doesn't exist. That's not a problem
			// we just ignore that file
			if (e == boost::system::errc::no_such_file_or_directory)
				e.clear();
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
				ec.file(i);
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
				ec.file(file_index_t(-1));
				ec.operation = storage_error::partfile_move;
			}
		}

		if (e)
		{
			// rollback
			while (--i >= file_index_t(0))
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

			return status_t::fatal_disk_error;
		}

		std::string const old_save_path = m_save_path;
		m_save_path = save_path;

		std::set<std::string> subdirs;
		for (i = file_index_t(0); i < f.end_file(); ++i)
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

		for (std::string const& s : subdirs)
		{
			error_code err;
			std::string subdir = combine_path(old_save_path, s);

			while (subdir != old_save_path && !err)
			{
				remove(subdir, err);
				subdir = parent_path(subdir);
			}
		}

		return ret;
	}

	int default_storage::readv(span<iovec_t const> bufs
		, piece_index_t const piece, int offset, int flags, storage_error& ec)
	{
		read_fileop op(*this, flags);

#ifdef TORRENT_SIMULATE_SLOW_READ
		std::this_thread::sleep_for(seconds(1));
#endif
		return readwritev(files(), bufs, piece, offset, op, ec);
	}

	int default_storage::writev(span<iovec_t const> bufs
		, piece_index_t const piece, int offset, int flags, storage_error& ec)
	{
		write_fileop op(*this, flags);
		return readwritev(files(), bufs, piece, offset, op, ec);
	}

	file_handle default_storage::open_file(file_index_t const file, int mode
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
				ec.file(file);
				ec.operation = storage_error::mkdir;
				return file_handle();
			}

			// if the directory creation failed, don't try to open the file again
			// but actually just fail
			h = open_file_impl(file, mode, ec.ec);
		}
		if (ec.ec)
		{
			ec.file(file);
			ec.operation = storage_error::open;
			return file_handle();
		}
		TORRENT_ASSERT(h);

		if (m_allocate_files && (mode & file::rw_mask) != file::read_only)
		{
			if (m_file_created.size() != files().num_files())
				m_file_created.resize(files().num_files(), false);

			TORRENT_ASSERT(int(m_file_created.size()) == files().num_files());
			TORRENT_ASSERT(file < m_file_created.end_index());
			// if this is the first time we open this file for writing,
			// and we have m_allocate_files enabled, set the final size of
			// the file right away, to allocate it on the filesystem.
			if (m_file_created[file] == false)
			{
				error_code e;
				std::int64_t const size = files().file_size(file);
				h->set_size(size, e);
				m_file_created.set_bit(file);
				if (e)
				{
					ec.ec = e;
					ec.file(file);
					ec.operation = storage_error::fallocate;
					return h;
				}
				m_stat_cache.set_dirty(file);
			}
		}
		return h;
	}

	file_handle default_storage::open_file_impl(file_index_t file, int mode
		, error_code& ec) const
	{
		bool lock_files = m_settings ? settings().get_bool(settings_pack::lock_files) : false;
		if (lock_files) mode |= file::lock_file;

		if (!m_allocate_files) mode |= file::sparse;

		// files with priority 0 should always be sparse
		if (m_file_priority.end_index() > file && m_file_priority[file] == 0)
			mode |= file::sparse;

		if (m_settings && settings().get_bool(settings_pack::no_atime_storage)) mode |= file::no_atime;

		// if we have a cache already, don't store the data twice by leaving it in the OS cache as well
		if (m_settings
			&& settings().get_int(settings_pack::disk_io_write_mode)
			== settings_pack::disable_os_cache)
		{
			mode |= file::no_cache;
		}

		file_handle ret = m_pool.open_file(storage_index(), m_save_path, file
			, files(), mode, ec);
		if (ec && (mode & file::lock_file))
		{
			// we failed to open the file and we're trying to lock it. It's
			// possible we're failing because we have another handle to this
			// file in use (but waiting to be closed). Just retry to open it
			// without locking.
			mode &= ~file::lock_file;
			ret = m_pool.open_file(storage_index(), m_save_path, file, files()
				, mode, ec);
		}
		return ret;
	}

	bool default_storage::tick()
	{
		error_code ec;
		if (m_part_file) m_part_file->flush_metadata(ec);

		return false;
	}

	storage_interface* default_storage_constructor(storage_params const& params)
	{
		return new default_storage(params);
	}

	// -- disabled_storage --------------------------------------------------

	namespace
	{
		// this storage implementation does not write anything to disk
		// and it pretends to read, and just leaves garbage in the buffers
		// this is useful when simulating many clients on the same machine
		// or when running stress tests and want to take the cost of the
		// disk I/O out of the picture. This cannot be used for any kind
		// of normal bittorrent operation, since it will just send garbage
		// to peers and throw away all the data it downloads. It would end
		// up being banned immediately
		class disabled_storage final : public storage_interface
		{
		public:
			bool has_any_file(storage_error&) override { return false; }
			void set_file_priority(aux::vector<std::uint8_t, file_index_t> const&
				, storage_error&) override {}
			void rename_file(file_index_t, std::string const&, storage_error&) override {}
			void release_files(storage_error&) override {}
			void delete_files(int, storage_error&) override {}
			void initialize(storage_error&) override {}
			status_t move_storage(std::string const&, int, storage_error&) override { return status_t::no_error; }

			int readv(span<iovec_t const> bufs
				, piece_index_t, int, int, storage_error&) override
			{
				return bufs_size(bufs);
			}
			int writev(span<iovec_t const> bufs
				, piece_index_t, int, int, storage_error&) override
			{
				return bufs_size(bufs);
			}

			bool verify_resume_data(add_torrent_params const&
				, aux::vector<std::string, file_index_t> const&
				, storage_error&) override { return false; }
		};
	}

	storage_interface* disabled_storage_constructor(storage_params const& params)
	{
		TORRENT_UNUSED(params);
		return new disabled_storage;
	}

	// -- zero_storage ------------------------------------------------------

	namespace
	{
		// this storage implementation always reads zeroes, and always discards
		// anything written to it
		struct zero_storage final : storage_interface
		{
			void initialize(storage_error&) override {}

			int readv(span<iovec_t const> bufs
				, piece_index_t, int, int, storage_error&) override
			{
				int ret = 0;
				for (int i = 0; i < int(bufs.size()); ++i)
				{
					memset(bufs[i].iov_base, 0, bufs[i].iov_len);
					ret += int(bufs[i].iov_len);
				}
				return 0;
			}
			int writev(span<iovec_t const> bufs
				, piece_index_t, int, int, storage_error&) override
			{
				int ret = 0;
				for (int i = 0; i < int(bufs.size()); ++i)
					ret += int(bufs[i].iov_len);
				return 0;
			}

			bool has_any_file(storage_error&) override { return false; }
			void set_file_priority(aux::vector<std::uint8_t, file_index_t> const& /* prio */
				, storage_error&) override {}
			status_t move_storage(std::string const& /* save_path */
				, int /* flags */, storage_error&) override { return status_t::no_error; }
			bool verify_resume_data(add_torrent_params const& /* rd */
				, aux::vector<std::string, file_index_t> const& /* links */
				, storage_error&) override
			{ return false; }
			void release_files(storage_error&) override {}
			void rename_file(file_index_t
				, std::string const& /* new_filename */, storage_error&) override {}
			void delete_files(int, storage_error&) override {}
		};
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

	// ====== disk_job_fence implementation ========

	disk_job_fence::disk_job_fence() {}

	int disk_job_fence::job_complete(disk_io_job* j, tailqueue<disk_io_job>& jobs)
	{
		std::lock_guard<std::mutex> l(m_mutex);

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
		std::lock_guard<std::mutex> l(m_mutex);
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
		std::lock_guard<std::mutex> l(m_mutex);
		return m_has_fence != 0;
	}

	int disk_job_fence::num_blocked() const
	{
		std::lock_guard<std::mutex> l(m_mutex);
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

		std::lock_guard<std::mutex> l(m_mutex);

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
