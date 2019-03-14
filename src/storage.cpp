/*

Copyright (c) 2003-2018, Arvid Norberg, Daniel Wallin
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
#include <numeric>
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

#if TORRENT_HAS_SYMLINK
#include <unistd.h> // for symlink()
#endif

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/storage.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/file_pool.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/disk_buffer_holder.hpp"
#include "libtorrent/stat_cache.hpp"
#include "libtorrent/hex.hpp" // to_hex
//#include "libtorrent/aux_/escape_string.hpp"

namespace libtorrent {

	default_storage::default_storage(storage_params const& params
		, file_pool& pool)
		: storage_interface(params.files)
		, m_file_priority(params.priorities)
		, m_pool(pool)
		, m_allocate_files(params.mode == storage_mode_allocate)
	{
		if (params.mapped_files) m_mapped_files.reset(new file_storage(*params.mapped_files));

		TORRENT_ASSERT(files().num_files() > 0);
		m_save_path = complete(params.path);
		m_part_file_name = "." + aux::to_hex(params.info_hash) + ".parts";
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
			, files().num_pieces(), files().piece_length()));
	}

	void default_storage::set_file_priority(
		aux::vector<download_priority_t, file_index_t>& prio
		, storage_error& ec)
	{
		// extend our file priorities in case it's truncated
		// the default assumed priority is 4 (the default)
		if (prio.size() > m_file_priority.size())
			m_file_priority.resize(prio.size(), default_priority);

		file_storage const& fs = files();
		for (file_index_t i(0); i < prio.end_index(); ++i)
		{
			// pad files always have priority 0.
			if (fs.pad_file_at(i)) continue;

			download_priority_t const old_prio = m_file_priority[i];
			download_priority_t new_prio = prio[i];
			if (old_prio == dont_download && new_prio != dont_download)
			{
				// move stuff out of the part file
				file_handle f = open_file(i, open_mode::read_write, ec);
				if (ec)
				{
					prio = m_file_priority;
					return;
				}

				if (m_part_file && use_partfile(i))
				{
					m_part_file->export_file([&f, &ec](std::int64_t file_offset, span<char> buf)
					{
						iovec_t const v = {buf.data(), buf.size()};
						std::int64_t const ret = f->writev(file_offset, v, ec.ec);
						TORRENT_UNUSED(ret);
						TORRENT_ASSERT(ec || ret == std::int64_t(v.size()));
					}, fs.file_offset(i), fs.file_size(i), ec.ec);

					if (ec)
					{
						ec.file(i);
						ec.operation = operation_t::partfile_write;
						prio = m_file_priority;
						return;
					}
				}
			}
			else if (old_prio != dont_download && new_prio == dont_download)
			{
				// move stuff into the part file
				// this is not implemented yet.
				// so we just don't use a partfile for this file

				std::string const fp = fs.file_path(i, m_save_path);
				if (exists(fp)) use_partfile(i, false);
/*
				file_handle f = open_file(i, open_mode::read_only, ec);
				if (ec.ec != boost::system::errc::no_such_file_or_directory)
				{
					if (ec)
					{
						prio = m_file_priority;
						return;
					}

					need_partfile();

					m_part_file->import_file(*f, fs.file_offset(i), fs.file_size(i), ec.ec);
					if (ec)
					{
						ec.file(i);
						ec.operation = operation_t::partfile_read;
						prio = m_file_priority;
						return;
					}
					// remove the file
					std::string p = fs.file_path(i, m_save_path);
					delete_one_file(p, ec.ec);
					if (ec)
					{
						ec.file(i);
						ec.operation = operation_t::file_remove;
						prio = m_file_priority;
						return;
					}
				}
*/
			}
			ec.ec.clear();
			m_file_priority[i] = new_prio;

			if (m_file_priority[i] == dont_download && use_partfile(i))
			{
				need_partfile();
			}
		}
		if (m_part_file) m_part_file->flush_metadata(ec.ec);
		if (ec)
		{
			ec.file(torrent_status::error_file_partfile);
			ec.operation = operation_t::partfile_write;
		}
	}

	bool default_storage::use_partfile(file_index_t const index) const
	{
		TORRENT_ASSERT_VAL(index >= file_index_t{}, index);
		if (index >= m_use_partfile.end_index()) return true;
		return m_use_partfile[index];
	}

	void default_storage::use_partfile(file_index_t const index, bool const b)
	{
		if (index >= m_use_partfile.end_index()) m_use_partfile.resize(static_cast<int>(index) + 1, true);
		m_use_partfile[index] = b;
	}

	void default_storage::initialize(storage_error& ec)
	{
		m_stat_cache.reserve(files().num_files());

#ifdef TORRENT_WINDOWS
		// don't do full file allocations on network drives
		auto const file_name = convert_to_native_path_string(m_save_path);
		int const drive_type = GetDriveTypeW(file_name.c_str());

		if (drive_type == DRIVE_REMOTE)
			m_allocate_files = false;
#endif

		{
			std::unique_lock<std::mutex> l(m_file_created_mutex);
			m_file_created.resize(files().num_files(), false);
		}

		file_storage const& fs = files();
		// if some files have priority 0, we need to check if they exist on the
		// filesystem, in which case we won't use a partfile for them.
		// this is to be backwards compatible with previous versions of
		// libtorrent, when part files were not supported.
		for (file_index_t i(0); i < m_file_priority.end_index(); ++i)
		{
			if (m_file_priority[i] != dont_download || fs.pad_file_at(i))
				continue;

			file_status s;
			std::string const file_path = fs.file_path(i, m_save_path);
			error_code err;
			stat_file(file_path, &s, err);
			if (!err)
			{
				use_partfile(i, false);
			}
			else
			{
				need_partfile();
			}
		}

		// first, create all missing directories
		std::string last_path;
		for (auto const file_index : fs.file_range())
		{
			// ignore files that have priority 0
			if (m_file_priority.end_index() > file_index
				&& m_file_priority[file_index] == dont_download)
			{
				continue;
			}

			// ignore pad files
			if (fs.pad_file_at(file_index)) continue;

			// this is just to see if the file exists
			error_code err;
			m_stat_cache.get_filesize(file_index, fs, m_save_path, err);

			if (err && err != boost::system::errc::no_such_file_or_directory)
			{
				ec.file(file_index);
				ec.operation = operation_t::file_stat;
				ec.ec = err;
				break;
			}

			// if the file is empty and doesn't already exist, create it
			// deliberately don't truncate files that already exist
			// if a file is supposed to have size 0, but already exists, we will
			// never truncate it to 0.
			if (fs.file_size(file_index) == 0
				&& err == boost::system::errc::no_such_file_or_directory)
			{
				std::string dir = parent_path(fs.file_path(file_index, m_save_path));

				if (dir != last_path)
				{
					last_path = dir;

					create_directories(last_path, ec.ec);
					if (ec.ec)
					{
						ec.file(file_index);
						ec.operation = operation_t::mkdir;
						break;
					}
				}
				ec.ec.clear();

#if TORRENT_HAS_SYMLINK
				// create symlinks
				if (fs.file_flags(file_index) & file_storage::flag_symlink)
				{
					// we make the symlink target relative to the link itself
					std::string const target = lexically_relative(
						parent_path(fs.file_path(file_index)), fs.symlink(file_index));
					std::string const link = fs.file_path(file_index, m_save_path);
					if (::symlink(target.c_str(), link.c_str()) != 0)
					{
						int const error = errno;
						if (error == EEXIST)
						{
							// if the file exist, it may be a symlink already. if so,
							// just verify the link target is what it's supposed to be
							// note that readlink() does not null terminate the buffer
							char buffer[512];
							auto const ret = ::readlink(link.c_str(), buffer, sizeof(buffer));
							if (ret <= 0 || target != string_view(buffer, std::size_t(ret)))
							{
								ec.ec = error_code(error, generic_category());
								ec.file(file_index);
								ec.operation = operation_t::symlink;
								return;
							}
						}
						else
						{
							ec.ec = error_code(error, generic_category());
							ec.file(file_index);
							ec.operation = operation_t::symlink;
							return;
						}
					}
				}
				else
#endif
				{
					// just creating the file is enough to make it zero-sized. If
					// there's a race here and some other process truncates the file,
					// it's not a problem, we won't access empty files ever again
					file_handle f = open_file(file_index, open_mode::read_write
						| open_mode::random_access, ec);
					if (ec) return;
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

		if (aux::has_any_file(files(), m_save_path, m_stat_cache, ec))
			return true;

		if (ec) return false;

		file_status s;
		stat_file(combine_path(m_save_path, m_part_file_name), &s, ec.ec);
		if (!ec) return true;

		// the part file not existing is expected
		if (ec && ec.ec == boost::system::errc::no_such_file_or_directory)
			ec.ec.clear();

		if (ec)
		{
			ec.file(torrent_status::error_file_partfile);
			ec.operation = operation_t::file_stat;
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
				ec.operation = operation_t::file_rename;
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
				ec.ec.clear();
				copy_file(old_name, new_path, ec.ec);

				if (ec)
				{
					ec.file(index);
					ec.operation = operation_t::file_rename;
					return;
				}

				error_code ignore;
				remove(old_name, ignore);
			}
		}
		else if (ec.ec)
		{
			// if exists fails, report that error
			ec.file(index);
			ec.operation = operation_t::file_rename;
			return;
		}

		// if old path doesn't exist, just rename the file
		// in our file_storage, so that when it is created
		// it will get the new name
		if (!m_mapped_files)
		{ m_mapped_files.reset(new file_storage(files())); }
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
		m_pool.release(storage_index());

		// make sure we can pick up new files added to the download directory when
		// we start the torrent again
		m_stat_cache.clear();
	}

	void default_storage::delete_files(remove_flags_t const options, storage_error& ec)
	{
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

	status_t default_storage::move_storage(std::string const& sp
		, move_flags_t const flags, storage_error& ec)
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
		, piece_index_t const piece, int const offset
		, open_mode_t const flags, storage_error& error)
	{
#ifdef TORRENT_SIMULATE_SLOW_READ
		std::this_thread::sleep_for(seconds(1));
#endif
		return readwritev(files(), bufs, piece, offset, error
			, [this, flags](file_index_t const file_index
				, std::int64_t const file_offset
				, span<iovec_t const> vec, storage_error& ec)
		{
			if (files().pad_file_at(file_index))
			{
				// reading from a pad file yields zeroes
				aux::clear_bufs(vec);
				return bufs_size(vec);
			}

			if (file_index < m_file_priority.end_index()
				&& m_file_priority[file_index] == dont_download
				&& use_partfile(file_index))
			{
				TORRENT_ASSERT(m_part_file);

				error_code e;
				peer_request map = files().map_file(file_index
					, file_offset, 0);
				int const ret = m_part_file->readv(vec
					, map.piece, map.start, e);

				if (e)
				{
					ec.ec = e;
					ec.file(file_index);
					ec.operation = operation_t::partfile_read;
					return -1;
				}
				return ret;
			}

			file_handle handle = open_file(file_index
				, open_mode::read_only | flags, ec);
			if (ec) return -1;

			error_code e;
			int const ret = int(handle->readv(file_offset
				, vec, e, flags));

			// set this unconditionally in case the upper layer would like to treat
			// short reads as errors
			ec.operation = operation_t::file_read;

			// we either get an error or 0 or more bytes read
			TORRENT_ASSERT(e || ret >= 0);
			TORRENT_ASSERT(ret <= bufs_size(vec));

			if (e)
			{
				ec.ec = e;
				ec.file(file_index);
				return -1;
			}

			return ret;
		});
	}

	int default_storage::writev(span<iovec_t const> bufs
		, piece_index_t const piece, int const offset
		, open_mode_t const flags, storage_error& error)
	{
		return readwritev(files(), bufs, piece, offset, error
			, [this, flags](file_index_t const file_index
				, std::int64_t const file_offset
				, span<iovec_t const> vec, storage_error& ec)
		{
			if (files().pad_file_at(file_index))
			{
				// writing to a pad-file is a no-op
				return bufs_size(vec);
			}

			if (file_index < m_file_priority.end_index()
				&& m_file_priority[file_index] == dont_download
				&& use_partfile(file_index))
			{
				TORRENT_ASSERT(m_part_file);

				error_code e;
				peer_request map = files().map_file(file_index
					, file_offset, 0);
				int const ret = m_part_file->writev(vec
					, map.piece, map.start, e);

				if (e)
				{
					ec.ec = e;
					ec.file(file_index);
					ec.operation = operation_t::partfile_write;
					return -1;
				}
				return ret;
			}

			// invalidate our stat cache for this file, since
			// we're writing to it
			m_stat_cache.set_dirty(file_index);

			file_handle handle = open_file(file_index
				, open_mode::read_write, ec);
			if (ec) return -1;

			error_code e;
			int const ret = int(handle->writev(file_offset
				, vec, e, flags));

			// set this unconditionally in case the upper layer would like to treat
			// short reads as errors
			ec.operation = operation_t::file_write;

			// we either get an error or 0 or more bytes read
			TORRENT_ASSERT(e || ret >= 0);
			TORRENT_ASSERT(ret <= bufs_size(vec));

			if (e)
			{
				ec.ec = e;
				ec.file(file_index);
				return -1;
			}

			return ret;
		});
	}

	file_handle default_storage::open_file(file_index_t const file
		, open_mode_t mode, storage_error& ec) const
	{
		file_handle h = open_file_impl(file, mode, ec.ec);
		if (((mode & open_mode::rw_mask) != open_mode::read_only)
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
				ec.operation = operation_t::mkdir;
				return file_handle();
			}

			// if the directory creation failed, don't try to open the file again
			// but actually just fail
			h = open_file_impl(file, mode, ec.ec);
		}
		if (ec.ec)
		{
			ec.file(file);
			ec.operation = operation_t::file_open;
			return file_handle();
		}
		TORRENT_ASSERT(h);

		if ((mode & open_mode::rw_mask) != open_mode::read_only)
		{
			std::unique_lock<std::mutex> l(m_file_created_mutex);
			if (m_file_created.size() != files().num_files())
				m_file_created.resize(files().num_files(), false);

			TORRENT_ASSERT(int(m_file_created.size()) == files().num_files());
			TORRENT_ASSERT(file < m_file_created.end_index());
			// if this is the first time we open this file for writing,
			// and we have m_allocate_files enabled, set the final size of
			// the file right away, to allocate it on the filesystem.
			if (m_file_created[file] == false)
			{
				m_file_created.set_bit(file);
				l.unlock();

				// if we're allocating files or if the file exists and is greater
				// than what it's supposed to be, truncate it to its correct size
				std::int64_t const size = files().file_size(file);
				error_code e;
				bool const need_truncate = h->get_size(e) > size;
				if (e)
				{
					ec.ec = e;
					ec.file(file);
					ec.operation = operation_t::file_stat;
					return h;
				}

				if (m_allocate_files || need_truncate)
				{
					h->set_size(size, e);
					if (e)
					{
						ec.ec = e;
						ec.file(file);
						ec.operation = operation_t::file_fallocate;
						return h;
					}
					m_stat_cache.set_dirty(file);
				}
			}
		}
		return h;
	}

	file_handle default_storage::open_file_impl(file_index_t file, open_mode_t mode
		, error_code& ec) const
	{
		if (!m_allocate_files) mode |= open_mode::sparse;

		// files with priority 0 should always be sparse
		if (m_file_priority.end_index() > file
			&& m_file_priority[file] == dont_download)
		{
			mode |= open_mode::sparse;
		}

		if (m_settings && settings().get_bool(settings_pack::no_atime_storage)) mode |= open_mode::no_atime;

		// if we have a cache already, don't store the data twice by leaving it in the OS cache as well
		if (m_settings
			&& settings().get_int(settings_pack::disk_io_write_mode)
			== settings_pack::disable_os_cache)
		{
			mode |= open_mode::no_cache;
		}

		file_handle ret = m_pool.open_file(storage_index(), m_save_path, file
			, files(), mode, ec);
		return ret;
	}

	bool default_storage::tick()
	{
		error_code ec;
		if (m_part_file) m_part_file->flush_metadata(ec);

		return false;
	}

	storage_interface* default_storage_constructor(storage_params const& params
		, file_pool& pool)
	{
		return new default_storage(params, pool);
	}

	// -- disabled_storage --------------------------------------------------

namespace {

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
			explicit disabled_storage(file_storage const& fs) : storage_interface(fs) {}

			bool has_any_file(storage_error&) override { return false; }
			void set_file_priority(aux::vector<download_priority_t, file_index_t>&
				, storage_error&) override {}
			void rename_file(file_index_t, std::string const&, storage_error&) override {}
			void release_files(storage_error&) override {}
			void delete_files(remove_flags_t, storage_error&) override {}
			void initialize(storage_error&) override {}
			status_t move_storage(std::string const&, move_flags_t, storage_error&) override { return status_t::no_error; }

			int readv(span<iovec_t const> bufs
				, piece_index_t, int, open_mode_t, storage_error&) override
			{
				return bufs_size(bufs);
			}
			int writev(span<iovec_t const> bufs
				, piece_index_t, int, open_mode_t, storage_error&) override
			{
				return bufs_size(bufs);
			}

			bool verify_resume_data(add_torrent_params const&
				, aux::vector<std::string, file_index_t> const&
				, storage_error&) override { return false; }
		};
	}

	storage_interface* disabled_storage_constructor(storage_params const& params, file_pool&)
	{
		return new disabled_storage(params.files);
	}

	// -- zero_storage ------------------------------------------------------

namespace {

		// this storage implementation always reads zeroes, and always discards
		// anything written to it
		struct zero_storage final : storage_interface
		{
			explicit zero_storage(file_storage const& fs) : storage_interface(fs) {}
			void initialize(storage_error&) override {}

			int readv(span<iovec_t const> bufs
				, piece_index_t, int, open_mode_t, storage_error&) override
			{
				int ret = 0;
				for (auto const& b : bufs)
				{
					std::memset(b.data(), 0, std::size_t(b.size()));
					ret += int(b.size());
				}
				return ret;
			}
			int writev(span<iovec_t const> bufs
				, piece_index_t, int, open_mode_t, storage_error&) override
			{
				return std::accumulate(bufs.begin(), bufs.end(), 0
					, [](int const acc, iovec_t const& b) { return acc + int(b.size()); });
			}

			bool has_any_file(storage_error&) override { return false; }
			void set_file_priority(aux::vector<download_priority_t, file_index_t>& /* prio */
				, storage_error&) override {}
			status_t move_storage(std::string const& /* save_path */
				, move_flags_t, storage_error&) override { return status_t::no_error; }
			bool verify_resume_data(add_torrent_params const& /* rd */
				, aux::vector<std::string, file_index_t> const& /* links */
				, storage_error&) override
			{ return false; }
			void release_files(storage_error&) override {}
			void rename_file(file_index_t
				, std::string const& /* new_filename */, storage_error&) override {}
			void delete_files(remove_flags_t, storage_error&) override {}
		};
	}

	storage_interface* zero_storage_constructor(storage_params const& params, file_pool&)
	{
		return new zero_storage(params.files);
	}

} // namespace libtorrent
