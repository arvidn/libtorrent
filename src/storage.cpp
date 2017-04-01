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
#include "libtorrent/file.hpp"
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/file_pool.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/disk_buffer_holder.hpp"
#include "libtorrent/stat_cache.hpp"
#include "libtorrent/hex.hpp" // to_hex
// for convert_to_wstring and convert_to_native
#include "libtorrent/aux_/escape_string.hpp"

namespace lt {
LIBTORRENT_VERSION_NAMESPACE {
	void clear_bufs(span<iovec_t const> bufs)
	{
		for (auto buf : bufs)
			std::memset(buf.iov_base, 0, buf.iov_len);
	}

	struct write_fileop final : aux::fileop
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

	struct read_fileop final : aux::fileop
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
		// the default assumed priority is 4 (the default)
		if (prio.size() > m_file_priority.size())
			m_file_priority.resize(prio.size(), default_piece_priority);

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

		// make sure we can pick up new files added to the download directory when
		// we start the torrent again
		m_stat_cache.clear();
	}

	void default_storage::delete_files(int const options, storage_error& ec)
	{
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

		aux::delete_files(files(), m_save_path, m_part_file_name, options, ec);
	}

	bool default_storage::verify_resume_data(add_torrent_params const& rd
		, aux::vector<std::string, file_index_t> const& links
		, storage_error& ec)
	{
		return aux::verify_resume_data(rd, links, files()
			, m_file_priority, m_stat_cache, m_save_path, ec);
	}

	status_t default_storage::move_storage(std::string const& sp, int const flags
		, storage_error& ec)
	{
		m_pool.release(storage_index());

		status_t ret;
		std::tie(ret, m_save_path) = aux::move_storage(files(), m_save_path, sp
			, m_part_file.get(), flags, ec);

		// clear the stat cache in case the new location has new files
		m_stat_cache.clear();

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
				for (auto const& b : bufs)
				{
					std::memset(b.iov_base, 0, b.iov_len);
					ret += int(b.iov_len);
				}
				return 0;
			}
			int writev(span<iovec_t const> bufs
				, piece_index_t, int, int, storage_error&) override
			{
				int ret = 0;
				for (auto const& b : bufs)
					ret += int(b.iov_len);
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

}} // namespace lt
