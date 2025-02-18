/*

Copyright (c) 2003-2009, 2011, 2013-2022, Arvid Norberg
Copyright (c) 2003, Daniel Wallin
Copyright (c) 2016, Vladimir Golovnev
Copyright (c) 2017, 2020-2021, Alden Torres
Copyright (c) 2017, 2019, Steven Siloti
Copyright (c) 2018, d-komarov
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/config.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/storage_utils.hpp"
#include "libtorrent/hasher.hpp"

#include "try_signal.hpp"

#include <ctime>
#include <algorithm>
#include <numeric>
#include <set>
#include <functional>
#include <cstdio>
#include <optional>

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/aux_/mmap_storage.hpp"
#include "libtorrent/aux_/torrent.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/aux_/invariant_check.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/aux_/file_view_pool.hpp"
#include "libtorrent/aux_/drive_info.hpp"
#include "libtorrent/aux_/stat_cache.hpp"
#include "libtorrent/aux_/readwrite.hpp"
#include "libtorrent/hex.hpp" // to_hex
#include "libtorrent/aux_/scope_end.hpp"

#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE

namespace libtorrent::aux {

namespace {

error_code translate_error(std::error_code const& err, bool const write)
{
	// We don't really know why we failed to read or write. SIGBUS essentially
	// means I/O failure. We assume that if we were writing, the failure was
	// because of disk full
	if (write)
	{
#ifdef TORRENT_WINDOWS
		if (err == std::error_code(sig::seh_errors::in_page_error))
			return {boost::system::errc::no_space_on_device, generic_category()};
#else
		if (err == std::error_code(sig::errors::bus))
			return {boost::system::errc::no_space_on_device, generic_category()};
#endif
	}

#if BOOST_VERSION >= 107700

#ifdef TORRENT_WINDOWS
	if (err == std::error_code(sig::seh_errors::in_page_error))
		return {boost::system::errc::io_error, generic_category()};
#else
	if (err == std::error_code(sig::errors::bus))
		return {boost::system::errc::io_error, generic_category()};
#endif

	return err;
#else

	return {boost::system::errc::io_error, generic_category()};

#endif
}
} // namespace


	mmap_storage::mmap_storage(storage_params const& params
		, aux::file_view_pool& pool)
		: m_files(params.files)
		, m_renamed_files(params.renamed_files)
		, m_file_priority(params.priorities)
		, m_save_path(complete(params.path))
		, m_part_file_name("." + aux::to_hex(params.info_hash) + ".parts")
		, m_pool(pool)
		, m_allocate_files(params.mode == storage_mode_allocate)
	{
		TORRENT_ASSERT(files().num_files() > 0);

#if TORRENT_HAVE_MAP_VIEW_OF_FILE
		m_file_open_unmap_lock.reset(new std::mutex[files().num_files()]
			, [](std::mutex* o) { delete[] o; });
#endif
	}

	mmap_storage::~mmap_storage()
	{
		error_code ec;
		if (m_part_file) m_part_file->flush_metadata(ec);

		// this may be called from a different
		// thread than the disk thread
		m_pool.release(storage_index());
	}

	filenames mmap_storage::names() const
	{
		return {m_files, m_renamed_files};
	}

	void mmap_storage::need_partfile()
	{
		if (m_part_file) return;

		m_part_file = std::make_unique<part_file>(
			m_save_path, m_part_file_name
			, files().num_pieces(), files().piece_length());
	}

	void mmap_storage::set_file_priority(settings_interface const& sett
		, aux::vector<download_priority_t, file_index_t>& prio
		, storage_error& ec)
	{
		// extend our file priorities in case it's truncated
		// the default assumed priority is 4 (the default)
		if (prio.size() > m_file_priority.size())
			m_file_priority.resize(prio.size(), default_priority);

		filenames const fs = names();
		for (file_index_t i(0); i < prio.end_index(); ++i)
		{
			// pad files always have priority 0.
			if (fs.pad_file_at(i)) continue;

			download_priority_t const old_prio = m_file_priority[i];
			download_priority_t new_prio = prio[i];

			m_file_priority[i] = new_prio;

			// in case there's an error, we make sure m_file_priority is only
			// updated for the successful files. By leaving failed files as
			// priority 0, we allow re-trying them.
			auto restore_prio = aux::scope_end([&] {
				m_file_priority[i] = old_prio;
				prio = m_file_priority;
			});

			if (old_prio == dont_download && new_prio != dont_download)
			{
				// move stuff out of the part file
				std::shared_ptr<aux::file_mapping> f = open_file(sett, i, aux::open_mode::write, ec);
				if (ec) return;

				if (m_part_file && use_partfile(i))
				{
					try
					{
						m_part_file->export_file([&f](std::int64_t file_offset, span<char> buf) {
							if (!f->has_memory_map())
							{
								lt::error_code err;
								aux::pwrite_all(f->fd(), buf, file_offset, err);
								if (err) throw lt::system_error(err);
								return;
							}

							auto file_range = f->range().subspan(std::ptrdiff_t(file_offset));
							TORRENT_ASSERT(file_range.size() >= buf.size());
							sig::try_signal([&]{
								std::memcpy(const_cast<char*>(file_range.data()), buf.data()
									, static_cast<std::size_t>(buf.size()));
								});
						}, fs.file_offset(i), fs.file_size(i), ec.ec);

						if (ec)
						{
							ec.file(i);
							ec.operation = operation_t::partfile_write;
							return;
						}
					}
					catch (std::system_error const& err)
					{
						ec.file(i);
						ec.operation = operation_t::partfile_write;
						ec.ec = translate_error(err.code(), true);
						return;
					}
					catch (lt::system_error const& err)
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
					return;
				}
				use_partfile(i, !file_exists);
/*
				auto f = open_file(sett, i, aux::open_mode::read_only, ec);
				if (ec.ec != boost::system::errc::no_such_file_or_directory)
				{
					if (ec) return;

					need_partfile();

					m_part_file->import_file(*f, fs.file_offset(i), fs.file_size(i), ec.ec);
					if (ec)
					{
						ec.file(i);
						ec.operation = operation_t::partfile_read;
						return;
					}
					// remove the file
					std::string const p = fs.file_path(i, m_save_path);
					delete_one_file(p, ec.ec);
					if (ec)
					{
						ec.file(i);
						ec.operation = operation_t::file_remove;
						return;
					}
				}
*/
			}
			ec.ec.clear();
			restore_prio.disarm();

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

	bool mmap_storage::use_partfile(file_index_t const index) const
	{
		TORRENT_ASSERT_VAL(index >= file_index_t{}, index);
		if (index >= m_use_partfile.end_index()) return true;
		return m_use_partfile[index];
	}

	void mmap_storage::use_partfile(file_index_t const index, bool const b)
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

	status_t mmap_storage::initialize(settings_interface const& sett, storage_error& ec)
	{
		m_stat_cache.reserve(files().num_files());

		auto const di = aux::get_drive_info(m_save_path);
		if (di == aux::drive_info::remote)
		{
			// don't do full file allocations on network drives
			m_allocate_files = false;
		}

		switch (sett.get_int(settings_pack::disk_write_mode))
		{
			case settings_pack::always_pwrite: m_use_mmap_writes = false; break;
			case settings_pack::always_mmap_write: m_use_mmap_writes = true; break;
			case settings_pack::auto_mmap_write: m_use_mmap_writes = (di == aux::drive_info::ssd_dax); break;
		}

		{
			std::unique_lock<std::mutex> l(m_file_created_mutex);
			m_file_created.resize(files().num_files(), false);
		}

		filenames const fs = names();
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
					ret |= disk_status::oversized_file;
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

		aux::initialize_storage(fs, m_save_path, m_stat_cache, m_file_priority
			, [&sett, this](file_index_t const file_index, storage_error& e)
			{ open_file(sett, file_index, aux::open_mode::write, e); }
			, aux::create_symlink
			, [&ret](file_index_t, std::int64_t) { ret |= disk_status::oversized_file; }
			, ec);

		// close files that were opened in write mode
		m_pool.release(storage_index());
		return ret;
	}

	bool mmap_storage::has_any_file(storage_error& ec)
	{
		m_stat_cache.reserve(files().num_files());

		if (aux::has_any_file(names(), m_save_path, m_stat_cache, ec))
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

	void mmap_storage::rename_file(file_index_t const index, std::string const& new_filename
		, storage_error& ec)
	{
		if (index < file_index_t(0) || index >= files().end_file()) return;
		std::string const old_name = m_renamed_files.file_path(files(), index, m_save_path);
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
				ec.operation = operation_t::mkdir;
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
				aux::copy_file(old_name, new_path, ec);

				if (ec)
				{
					ec.file(index);
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
			ec.operation = operation_t::file_stat;
			return;
		}

		// if old path doesn't exist, just rename the file
		// in our file_storage, so that when it is created
		// it will get the new name
		m_renamed_files.rename_file(files(), index, new_filename);
	}

	void mmap_storage::release_files(storage_error&)
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

	void mmap_storage::delete_files(remove_flags_t const options, storage_error& ec)
	{
		// make sure we don't have the files open
		m_pool.release(storage_index());

		// if there's a part file open, make sure to destruct it to have it
		// release the underlying part file. Otherwise we may not be able to
		// delete it
		if (m_part_file) m_part_file.reset();

		aux::delete_files(names(), m_save_path, m_part_file_name, options, ec);
	}

	bool mmap_storage::verify_resume_data(add_torrent_params const& rd
		, aux::vector<std::string, file_index_t> const& links
		, storage_error& ec)
	{
		return aux::verify_resume_data(rd, links, names()
			, m_file_priority, m_stat_cache, m_save_path, ec);
	}

	std::pair<status_t, std::string> mmap_storage::move_storage(std::string save_path
		, move_flags_t const flags, storage_error& ec)
	{
		m_pool.release(storage_index());

		status_t ret;
		auto move_partfile = [&](std::string const& new_save_path, error_code& e)
		{
			if (!m_part_file) return;
			m_part_file->move_partfile(new_save_path, e);
		};
		std::tie(ret, m_save_path) = aux::move_storage(names(), m_save_path, std::move(save_path)
			, std::move(move_partfile), flags, ec);

		// clear the stat cache in case the new location has new files
		m_stat_cache.clear();

		return { ret, m_save_path };
	}

	int mmap_storage::read(settings_interface const& sett
		, span<char> buffer
		, piece_index_t const piece, int const offset
		, aux::open_mode_t const mode
		, disk_job_flags_t const flags
		, storage_error& error)
	{
#ifdef TORRENT_SIMULATE_SLOW_READ
		std::this_thread::sleep_for(milliseconds(rand() % 2000));
#endif
		return readwrite(files(), buffer, piece, offset, error
			, [this, mode, flags, &sett](file_index_t const file_index
				, std::int64_t const file_offset
				, span<char> buf, storage_error& ec)
		{
			// reading from a pad file yields zeroes
			if (files().pad_file_at(file_index)) return aux::read_zeroes(buf);

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
					ec.operation = operation_t::partfile_read;
					return -1;
				}
				return ret;
			}

			auto handle = open_file(sett, file_index, mode, ec);
			if (ec) return -1;
			TORRENT_ASSERT(handle);

			if (!handle->has_memory_map())
				return aux::pread_all(handle->fd(), buf, file_offset, ec.ec);

			int ret = 0;
			span<byte const> file_range = handle->range();

			// set this unconditionally in case the upper layer would like to treat
			// short reads as errors
			ec.operation = operation_t::file_read;

			if (file_range.size() <= file_offset)
			{
				ec.ec = boost::asio::error::eof;
				return -1;
			}

			try
			{
				file_range = file_range.subspan(static_cast<std::ptrdiff_t>(file_offset));
				if (!file_range.empty())
				{
					if (file_range.size() < buf.size()) buf = buf.first(file_range.size());

					sig::try_signal([&]{
						std::memcpy(buf.data(), const_cast<char*>(file_range.data())
							, static_cast<std::size_t>(buf.size()));
						});

					if (flags & disk_interface::volatile_read)
						handle->dont_need(file_range.first(buf.size()));
					if (flags & disk_interface::flush_piece)
						handle->page_out(file_range.first(buf.size()));

					file_range = file_range.subspan(buf.size());
					ret += static_cast<int>(buf.size());
				}
			}
			catch (std::system_error const& err)
			{
				ec.ec = translate_error(err.code(), false);
				return -1;
			}

			return static_cast<int>(ret);
		});
	}

	int mmap_storage::write(settings_interface const& sett
		, span<char const> buffer
		, piece_index_t const piece, int const offset
		, aux::open_mode_t const mode
		, disk_job_flags_t const flags
		, storage_error& error)
	{
#ifdef TORRENT_SIMULATE_SLOW_WRITE
		std::this_thread::sleep_for(milliseconds(rand() % 800));
#endif
		return readwrite(files(), buffer, piece, offset, error
			, [this, mode, flags, &sett](file_index_t const file_index
				, std::int64_t const file_offset
				, span<char const> buf, storage_error& ec)
		{
			if (files().pad_file_at(file_index))
			{
				// writing to a pad-file is a no-op
				return int(buf.size());
			}

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
					ec.operation = operation_t::partfile_write;
					return -1;
				}
				return ret;
			}

			// invalidate our stat cache for this file, since
			// we're writing to it
			m_stat_cache.set_dirty(file_index);

			TORRENT_ASSERT(file_index < m_files.end_file());

			auto handle = open_file(sett, file_index
				, aux::open_mode::write | mode, ec);
			if (ec) return -1;

			// set this unconditionally in case the upper layer would like to treat
			// short reads as errors
			ec.operation = operation_t::file_write;

			if (!m_use_mmap_writes || !handle->has_memory_map())
				return aux::pwrite_all(handle->fd(), buf, file_offset, ec.ec);

			int ret = 0;
			span<byte> file_range = handle->range().subspan(static_cast<std::ptrdiff_t>(file_offset));

			try
			{
				TORRENT_ASSERT(file_range.size() >= buf.size());

				sig::try_signal([&]{
					std::memcpy(const_cast<char*>(file_range.data()), buf.data(), static_cast<std::size_t>(buf.size()));
					});

				file_range = file_range.subspan(buf.size());
				ret += static_cast<int>(buf.size());

				if (flags & disk_interface::volatile_read)
					handle->dont_need(file_range.first(buf.size()));
				if (flags & disk_interface::flush_piece)
					handle->page_out(file_range.first(buf.size()));
			}
			catch (std::system_error const& err)
			{
				ec.ec = translate_error(err.code(), true);
				return -1;
			}

			return ret;
		});
	}

	int mmap_storage::hash(settings_interface const& sett
		, hasher& ph, std::ptrdiff_t const len
		, piece_index_t const piece, int const offset
		, aux::open_mode_t const mode
		, disk_job_flags_t const flags
		, storage_error& error)
	{
#ifdef TORRENT_SIMULATE_SLOW_READ
		std::this_thread::sleep_for(milliseconds(rand() % 2000));
#endif

		char dummy = 0;
		std::vector<char> scratch;

		return readwrite(files(), span<char const>{&dummy, len}, piece, offset, error
			, [this, mode, flags, &ph, &sett, &scratch](file_index_t const file_index
				, std::int64_t const file_offset
				, span<char const> const buf, storage_error& ec)
		{
			if (files().pad_file_at(file_index))
				return aux::hash_zeroes(ph, buf.size());

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
					ec.operation = operation_t::partfile_read;
					return -1;
				}
				return ret;
			}

			auto handle = open_file(sett, file_index, mode, ec);
			if (ec) return -1;

			if (!handle->has_memory_map())
			{
				scratch.resize(std::size_t(buf.size()));
				int const ret = aux::pread_all(handle->fd(), scratch, file_offset, ec.ec);
				if (ec) return -1;
				ph.update(scratch);
				return ret;
			}

			int ret = 0;
			span<byte const> file_range = handle->range();
			if (file_range.size() > file_offset)
			{
				file_range = file_range.subspan(std::ptrdiff_t(file_offset)
					, std::min(buf.size(), std::ptrdiff_t(file_range.size() - file_offset)));

				sig::try_signal([&]{
					ph.update({const_cast<char const*>(file_range.data()), file_range.size()});
				});
				ret += static_cast<int>(file_range.size());
				if (flags & disk_interface::volatile_read)
					handle->dont_need(file_range);
				if (flags & disk_interface::flush_piece)
					handle->page_out(file_range);
			}

			return ret;
		});
	}

	int mmap_storage::hash2(settings_interface const& sett
		, hasher256& ph, std::ptrdiff_t const len
		, piece_index_t const piece, int const offset
		, aux::open_mode_t const mode
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
			int const ret = m_part_file->hash2(ph, len
				, map.piece, map.start, e);

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

		if (!handle->has_memory_map())
		{
			std::vector<char> scratch(static_cast<std::size_t>(len));
			int const ret = aux::pread_all(handle->fd(), scratch, file_offset, error.ec);
			if (error)
			{
				error.file(file_index);
				error.operation = operation_t::file_read;
				return -1;
			}
			ph.update(scratch);
			return ret;
		}

		span<byte const> file_range = handle->range();
		if (std::int64_t(file_range.size()) <= file_offset)
		{
			error.ec = boost::asio::error::eof;
			error.file(file_index);
			error.operation = operation_t::file_read;
			return -1;
		}
		file_range = file_range.subspan(std::ptrdiff_t(file_offset));
		file_range = file_range.first(std::min(std::ptrdiff_t(len), file_range.size()));
		ph.update(file_range);
		if (flags & disk_interface::volatile_read)
			handle->dont_need(file_range);
		if (flags & disk_interface::flush_piece)
			handle->page_out(file_range);

		return static_cast<int>(file_range.size());
	}

	// a wrapper around open_file_impl that, if it fails, makes sure the
	// directories have been created and retries
	std::shared_ptr<aux::file_mapping> mmap_storage::open_file(settings_interface const& sett
		, file_index_t const file
		, aux::open_mode_t mode, storage_error& ec) const
	{
		if (mode & aux::open_mode::write
			&& !(mode & aux::open_mode::truncate))
		{
			std::unique_lock<std::mutex> l(m_file_created_mutex);
			if (m_file_created.size() != files().num_files())
				m_file_created.resize(files().num_files(), false);

			// if we haven't created this file already, make sure to truncate it to
			// its final size
			mode |= (m_file_created[file] == false) ? aux::open_mode::truncate : aux::open_mode::read_only;
		}

		if (files().file_flags(file) & file_storage::flag_executable)
			mode |= aux::open_mode::executable;

		if (files().file_flags(file) & file_storage::flag_hidden)
			mode |= aux::open_mode::hidden;

#ifdef _WIN32
		if (sett.get_bool(settings_pack::enable_set_file_valid_data))
		{
			mode |= aux::open_mode::allow_set_file_valid_data;
		}
#endif

		std::shared_ptr<aux::file_mapping> h = open_file_impl(sett, file, mode, ec);
		if (ec.ec)
		{
			ec.file(file);
			return {};
		}
		TORRENT_ASSERT(h);

		if (mode & aux::open_mode::truncate)
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

	std::shared_ptr<aux::file_mapping> mmap_storage::open_file_impl(settings_interface const& sett
		, file_index_t file
		, aux::open_mode_t mode
		, storage_error& ec) const
	{
		TORRENT_ASSERT(!files().pad_file_at(file));
		if (!m_allocate_files) mode |= aux::open_mode::sparse;

		// files with priority 0 should always be sparse
		if (m_file_priority.end_index() > file && m_file_priority[file] == dont_download)
			mode |= aux::open_mode::sparse;

		if (sett.get_bool(settings_pack::no_atime_storage))
			mode |= aux::open_mode::no_atime;

		if (files().file_size(file) / default_block_size
			<= sett.get_int(settings_pack::mmap_file_size_cutoff))
			mode |= aux::open_mode::no_mmap;

		// if we have a cache already, don't store the data twice by leaving it in the OS cache as well
		auto const write_mode = sett.get_int(settings_pack::disk_io_write_mode);
		if (write_mode == settings_pack::disable_os_cache
			|| write_mode == settings_pack::write_through)
		{
			mode |= aux::open_mode::no_cache;
		}

		try {
			return m_pool.open_file(storage_index(), m_save_path, file
				, names(), mode
#if TORRENT_HAVE_MAP_VIEW_OF_FILE
				, std::shared_ptr<std::mutex>(m_file_open_unmap_lock
					, &m_file_open_unmap_lock.get()[int(file)])
#endif
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

	bool mmap_storage::tick()
	{
		error_code ec;
		if (m_part_file) m_part_file->flush_metadata(ec);

		return false;
	}
} // namespace libtorrent::aux

#endif // TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE
