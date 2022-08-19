/*

Copyright (c) 2022, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/config.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/storage_utils.hpp"
#include "libtorrent/hasher.hpp"

#include <ctime>
#include <algorithm>
#include <numeric>
#include <set>
#include <functional>
#include <cstdio>

#include "libtorrent/aux_/pread_storage.hpp"
#include "libtorrent/aux_/torrent.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/aux_/invariant_check.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/aux_/file_pool.hpp"
#include "libtorrent/aux_/file.hpp" // for file_handle, pread_all, pwrite_all
#include "libtorrent/disk_buffer_holder.hpp"
#include "libtorrent/aux_/stat_cache.hpp"
#include "libtorrent/hex.hpp" // to_hex

#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>


namespace libtorrent::aux {

namespace {

	void advise_dont_need(handle_type handle, std::int64_t offset, std::int64_t len)
	{
#if (TORRENT_HAS_FADVISE && defined POSIX_FADV_DONTNEED)
		::posix_fadvise(handle, offset, len, POSIX_FADV_DONTNEED);
#else
		TORRENT_UNUSED(handle);
		TORRENT_UNUSED(offset);
		TORRENT_UNUSED(len);
#endif
	}

	void sync_file(handle_type handle, std::int64_t offset, std::int64_t len)
	{
#if defined TORRENT_LINUX
		::sync_file_range(handle, offset, len, SYNC_FILE_RANGE_WRITE);
#elif defined TORRENT_BSD && ! defined __APPLE__
		::fsync_range(handle, FFILESYNC, offset, len);
#else
		::fsync(handle);
		TORRENT_UNUSED(offset);
		TORRENT_UNUSED(len);
#endif
	}
}

	pread_storage::pread_storage(storage_params const& params
		, file_pool& pool)
		: m_files(params.files)
		, m_file_priority(params.priorities)
		, m_save_path(complete(params.path))
		, m_part_file_name("." + to_hex(params.info_hash) + ".parts")
		, m_pool(pool)
		, m_allocate_files(params.mode == storage_mode_allocate)
	{
		if (params.mapped_files) m_mapped_files = std::make_unique<file_storage>(*params.mapped_files);

		TORRENT_ASSERT(files().num_files() > 0);
	}

	pread_storage::~pread_storage()
	{
		error_code ec;
		if (m_part_file) m_part_file->flush_metadata(ec);

		// this may be called from a different
		// thread than the disk thread
		m_pool.release(storage_index());
	}

	void pread_storage::need_partfile()
	{
		if (m_part_file) return;

		m_part_file = std::make_unique<part_file>(
			m_save_path, m_part_file_name
			, files().num_pieces(), files().piece_length());
	}

	void pread_storage::set_file_priority(settings_interface const& sett
		, vector<download_priority_t, file_index_t>& prio
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
				auto f = open_file(sett, i, open_mode::write, ec);
				if (ec)
				{
					prio = m_file_priority;
					return;
				}
				TORRENT_ASSERT(f);

				if (m_part_file && use_partfile(i))
				{
					try
					{
						m_part_file->export_file([&f](std::int64_t file_offset, span<char> buf)
						{
							do {
								error_code err;
								int const r = pwrite_all(f->fd(), buf, file_offset, err);
								if (err)
									throw_ex<std::system_error>(err);
								buf = buf.subspan(r);
								file_offset += r;
							} while (buf.size() > 0);
						}, fs.file_offset(i), fs.file_size(i), ec.ec);
						if (ec)
						{
							ec.file(i);
							ec.operation = operation_t::partfile_write;
							prio = m_file_priority;
							return;
						}
					}
					catch (std::system_error const& err)
					{
						ec.file(i);
						ec.operation = operation_t::partfile_write;
						ec.ec = err.code();
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
				bool const file_exists = exists(fp, ec.ec);
				if (ec.ec)
				{
					ec.file(i);
					ec.operation = operation_t::file_stat;
					prio = m_file_priority;
					return;
				}
				use_partfile(i, !file_exists);
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

	bool pread_storage::use_partfile(file_index_t const index) const
	{
		TORRENT_ASSERT_VAL(index >= file_index_t{}, index);
		if (index >= m_use_partfile.end_index()) return true;
		return m_use_partfile[index];
	}

	void pread_storage::use_partfile(file_index_t const index, bool const b)
	{
		if (index >= m_use_partfile.end_index())
		{
			// no need to extend this array if we're just setting it to "true",
			// that's default already
			if (b) return;
			m_use_partfile.resize(static_cast<int>(index) + 1, true);
		}
		m_use_partfile[index] = b;
	}

	status_t pread_storage::initialize(settings_interface const& sett, storage_error& ec)
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
		status_t ret{};
		// if some files have priority 0, we need to check if they exist on the
		// filesystem, in which case we won't use a partfile for them.
		// this is to be backwards compatible with previous versions of
		// libtorrent, when part files were not supported.
		for (file_index_t i(0); i < m_file_priority.end_index(); ++i)
		{
			if (m_file_priority[i] != dont_download || fs.pad_file_at(i))
				continue;

			error_code err;
			auto const size = m_stat_cache.get_filesize(i, fs, m_save_path, err);
			if (!err && size > 0)
			{
				use_partfile(i, false);
				if (size > fs.file_size(i))
					ret = ret | status_t::oversized_file;
			}
			else
			{
				// we may have earlier determined we *can't* use a partfile for
				// this file, we need to be able to change our mind in case the
				// file disappeared
				use_partfile(i, true);
				need_partfile();
			}
		}

		initialize_storage(fs, m_save_path, m_stat_cache, m_file_priority
			, [&sett, this](file_index_t const file_index, storage_error& e)
			{ open_file(sett, file_index, open_mode::write, e); }
			, create_symlink
			, [&ret](file_index_t, std::int64_t) { ret = ret | status_t::oversized_file; }
			, ec);

		// close files that were opened in write mode
		m_pool.release(storage_index());
		return ret;
	}

	bool pread_storage::has_any_file(storage_error& ec)
	{
		m_stat_cache.reserve(files().num_files());

		if (aux::has_any_file(files(), m_save_path, m_stat_cache, ec))
			return true;

		if (ec) return false;

		file_status s;
		stat_file(combine_path(m_save_path, m_part_file_name), &s, ec.ec);
		if (!ec) return true;

		// the part file not existing is expected
		if (ec.ec == boost::system::errc::no_such_file_or_directory)
			ec.ec.clear();

		if (ec)
		{
			ec.file(torrent_status::error_file_partfile);
			ec.operation = operation_t::file_stat;
		}
		return false;
	}

	void pread_storage::rename_file(file_index_t const index, std::string const& new_filename
		, storage_error& ec)
	{
		if (index < file_index_t(0) || index >= files().end_file()) return;
		std::string const old_name = files().file_path(index, m_save_path);
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
		{ m_mapped_files = std::make_unique<file_storage>(files()); }
		m_mapped_files->rename_file(index, new_filename);
	}

	void pread_storage::release_files(storage_error&)
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

	void pread_storage::delete_files(remove_flags_t const options, storage_error& ec)
	{
		// make sure we don't have the files open
		m_pool.release(storage_index());

		// if there's a part file open, make sure to destruct it to have it
		// release the underlying part file. Otherwise we may not be able to
		// delete it
		if (m_part_file) m_part_file.reset();

		aux::delete_files(files(), m_save_path, m_part_file_name, options, ec);
	}

	bool pread_storage::verify_resume_data(add_torrent_params const& rd
		, aux::vector<std::string, file_index_t> const& links
		, storage_error& ec)
	{
		return aux::verify_resume_data(rd, links, files()
			, m_file_priority, m_stat_cache, m_save_path, ec);
	}

	std::pair<status_t, std::string> pread_storage::move_storage(std::string save_path
		, move_flags_t const flags, storage_error& ec)
	{
		m_pool.release(storage_index());

		status_t ret;
		auto move_partfile = [&](std::string const& new_save_path, error_code& e)
		{
			if (!m_part_file) return;
			m_part_file->move_partfile(new_save_path, e);
		};
		std::tie(ret, m_save_path) = aux::move_storage(files(), m_save_path, std::move(save_path)
			, std::move(move_partfile), flags, ec);

		// clear the stat cache in case the new location has new files
		m_stat_cache.clear();

		return { ret, m_save_path };
	}

	int pread_storage::read(settings_interface const& sett
		, span<char> buffer
		, piece_index_t const piece, int const offset
		, open_mode_t const mode
		, disk_job_flags_t const flags
		, storage_error& error)
	{
#ifdef TORRENT_SIMULATE_SLOW_READ
		std::this_thread::sleep_for(seconds(1));
#endif
		return readwrite(files(), buffer, piece, offset, error
			, [this, mode, flags, &sett](file_index_t const file_index
				, std::int64_t const file_offset
				, span<char> buf, storage_error& ec)
		{
			// reading from a pad file yields zeroes
			if (files().pad_file_at(file_index)) return read_zeroes(buf);

			if (file_index < m_file_priority.end_index()
				&& m_file_priority[file_index] == dont_download
				&& use_partfile(file_index))
			{
				TORRENT_ASSERT(m_part_file);

				error_code e;
				peer_request map = files().map_file(file_index, file_offset, 0);
				int const ret = m_part_file->read(buf, map.piece, map.start, e);

				if (e)
				{
					ec.ec = e;
					ec.file(file_index);
					ec.operation = operation_t::partfile_read;
					return -1;
				}
				return ret;
			}

			auto handle = open_file(sett, file_index, mode, ec);
			if (ec) return -1;

			// set this unconditionally in case the upper layer would like to treat
			// short reads as errors
			ec.operation = operation_t::file_read;

			int const ret = pread_all(handle->fd(), buf, file_offset, ec.ec);
			if (flags & disk_interface::volatile_read)
				advise_dont_need(handle->fd(), file_offset, buf.size());

			return ret;
		});
	}

	int pread_storage::write(settings_interface const& sett
		, span<char> buffer
		, piece_index_t const piece, int const offset
		, open_mode_t const mode
		, disk_job_flags_t const
		, storage_error& error)
	{
		auto const write_mode = sett.get_int(settings_pack::disk_io_write_mode);
		return readwrite(files(), buffer, piece, offset, error
			, [this, mode, &sett, write_mode](file_index_t const file_index
				, std::int64_t const file_offset
				, span<char> buf, storage_error& ec)
		{
			// writing to a pad-file is a no-op
			if (files().pad_file_at(file_index))
				return int(buf.size());

			if (file_index < m_file_priority.end_index()
				&& m_file_priority[file_index] == dont_download
				&& use_partfile(file_index))
			{
				TORRENT_ASSERT(m_part_file);

				error_code e;
				peer_request map = files().map_file(file_index
					, file_offset, 0);
				int const ret = m_part_file->write(buf, map.piece, map.start, e);

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

			auto handle = open_file(sett, file_index, open_mode::write | mode, ec);
			if (ec) return -1;
			TORRENT_ASSERT(handle);

			// set this unconditionally in case the upper layer would like to treat
			// short reads as errors
			ec.operation = operation_t::file_write;

			int const ret = pwrite_all(handle->fd(), buf, file_offset, ec.ec);
			if (write_mode == settings_pack::write_through)
				sync_file(handle->fd(), file_offset, buf.size());
			return ret;
		});
	}

	int pread_storage::hash(settings_interface const& sett
		, hasher& ph, std::ptrdiff_t const len
		, piece_index_t const piece, int const offset
		, open_mode_t const mode
		, disk_job_flags_t const flags
		, storage_error& error)
	{
#ifdef TORRENT_SIMULATE_SLOW_READ
		std::this_thread::sleep_for(seconds(1));
#endif
		char dummy = 0;

		std::vector<char> scratch_buffer;

		return readwrite(files(), {&dummy, len}, piece, offset, error
			, [this, mode, flags, &ph, &sett, &scratch_buffer](
				file_index_t const file_index
				, std::int64_t const file_offset
				, span<char> buf, storage_error& ec)
		{
			if (files().pad_file_at(file_index))
				return hash_zeroes(ph, buf.size());

			if (file_index < m_file_priority.end_index()
				&& m_file_priority[file_index] == dont_download
				&& use_partfile(file_index))
			{
				error_code e;
				peer_request map = files().map_file(file_index, file_offset, 0);
				int const ret = m_part_file->hash(ph, buf.size()
					, map.piece, map.start, e);

				if (e)
				{
					ec.ec = e;
					ec.file(file_index);
					ec.operation = operation_t::partfile_read;
				}
				return ret;
			}

			auto handle = open_file(sett, file_index, mode, ec);
			if (ec) return -1;

			scratch_buffer.resize(std::size_t(buf.size()));
			int ret = pread_all(handle->fd(), scratch_buffer, file_offset, ec.ec);
			if (ret >= 0)
			{
				ph.update(scratch_buffer);
				if (flags & disk_interface::volatile_read)
					advise_dont_need(handle->fd(), file_offset, buf.size());
				if (flags & disk_interface::flush_piece)
					sync_file(handle->fd(), file_offset, buf.size());
			}

			return ret;
		});
	}

	int pread_storage::hash2(settings_interface const& sett
		, hasher256& ph, std::ptrdiff_t const len
		, piece_index_t const piece, int const offset
		, open_mode_t const mode
		, disk_job_flags_t const flags
		, storage_error& error)
	{
		std::int64_t const start_offset = static_cast<int>(piece) * std::int64_t(files().piece_length()) + offset;
		file_index_t const file_index = files().file_index_at_offset(start_offset);
		std::int64_t const file_offset = start_offset - files().file_offset(file_index);
		TORRENT_ASSERT(file_offset >= 0);
		TORRENT_ASSERT(!files().pad_file_at(file_index));

		if (file_index < m_file_priority.end_index()
			&& m_file_priority[file_index] == dont_download
			&& use_partfile(file_index))
		{
			error_code e;
			peer_request map = files().map_file(file_index, file_offset, 0);
			int const ret = m_part_file->hash2(ph, len, map.piece, map.start, e);

			if (e)
			{
				error.ec = e;
				error.file(file_index);
				error.operation = operation_t::partfile_read;
				return -1;
			}
			return ret;
		}

		auto handle = open_file(sett, file_index, mode, error);
		if (error) return -1;

		std::unique_ptr<char[]> scratch_buffer(new char[std::size_t(len)]);
		span<char> b = {scratch_buffer.get(), len};
		int const ret = pread_all(handle->fd(), b, file_offset, error.ec);
		if (error.ec)
		{
			error.operation = operation_t::file_read;
			error.file(file_index);
			return ret;
		}
		ph.update(b);
		if (flags & disk_interface::volatile_read)
			advise_dont_need(handle->fd(), file_offset, len);
		if (flags & disk_interface::flush_piece)
			sync_file(handle->fd(), file_offset, len);

		return static_cast<int>(len);
	}

	// a wrapper around open_file_impl that, if it fails, makes sure the
	// directories have been created and retries
	std::shared_ptr<file_handle> pread_storage::open_file(settings_interface const& sett
		, file_index_t const file
		, open_mode_t mode, storage_error& ec) const
	{
		if (mode & open_mode::write
			&& !(mode & open_mode::truncate))
		{
			std::unique_lock<std::mutex> l(m_file_created_mutex);
			if (m_file_created.size() != files().num_files())
				m_file_created.resize(files().num_files(), false);

			// if we haven't created this file already, make sure to truncate it to
			// its final size
			mode |= (m_file_created[file] == false) ? open_mode::truncate : open_mode::read_only;
		}

		if (files().file_flags(file) & file_storage::flag_executable)
			mode |= open_mode::executable;

		if (files().file_flags(file) & file_storage::flag_hidden)
			mode |= open_mode::hidden;

#ifdef _WIN32
		if (sett.get_bool(settings_pack::enable_set_file_valid_data))
		{
			mode |= open_mode::allow_set_file_valid_data;
		}
#endif

		auto h = open_file_impl(sett, file, mode, ec);
		if (ec.ec)
		{
			ec.file(file);
			return {};
		}
		TORRENT_ASSERT(h);

		if (mode & open_mode::truncate)
		{
			// remember that we've truncated this file, so we don't have to do it
			// again
			std::unique_lock<std::mutex> l(m_file_created_mutex);
			m_file_created.set_bit(file);
		}

		// the optional should be set here
		TORRENT_ASSERT(static_cast<bool>(h));
		return h;
	}

	std::shared_ptr<file_handle> pread_storage::open_file_impl(settings_interface const& sett
		, file_index_t file
		, open_mode_t mode
		, storage_error& ec) const
	{
		TORRENT_ASSERT(!files().pad_file_at(file));
		if (!m_allocate_files) mode |= open_mode::sparse;

		// files with priority 0 should always be sparse
		if (m_file_priority.end_index() > file && m_file_priority[file] == dont_download)
			mode |= open_mode::sparse;

		if (sett.get_bool(settings_pack::no_atime_storage))
		{
			mode |= open_mode::no_atime;
		}

		// if we have a cache already, don't store the data twice by leaving it in the OS cache as well
		auto const write_mode = sett.get_int(settings_pack::disk_io_write_mode);
		if (write_mode == settings_pack::disable_os_cache
			|| write_mode == settings_pack::write_through)
		{
			mode |= open_mode::no_cache;
		}

		try {
			return m_pool.open_file(storage_index(), m_save_path, file
				, files(), mode
				);
		}
		catch (storage_error const& se)
		{
			ec = se;
			ec.file(file);
			TORRENT_ASSERT(ec);
			return {};
		}
	}

	bool pread_storage::tick()
	{
		error_code ec;
		if (m_part_file) m_part_file->flush_metadata(ec);

		return false;
	}
} // namespace libtorrent::aux
