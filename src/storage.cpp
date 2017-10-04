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
#include "libtorrent/hasher.hpp"

#include "try_signal.hpp"

#include <ctime>
#include <algorithm>
#include <set>
#include <functional>
#include <cstdio>

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/optional.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/storage.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/disk_buffer_holder.hpp"
#include "libtorrent/stat_cache.hpp"
#include "libtorrent/hex.hpp" // to_hex
// for convert_to_wstring and convert_to_native
#include "libtorrent/aux_/escape_string.hpp"

namespace libtorrent {

	default_storage::default_storage(storage_params const& params
		, aux::file_view_pool& pool)
		: m_files(params.files)
		, m_file_priority(params.priorities)
		, m_save_path(complete(params.path))
		, m_part_file_name("." + aux::to_hex(params.info_hash) + ".parts")
		, m_pool(pool)
		, m_allocate_files(params.mode == storage_mode_allocate)
	{
		if (params.mapped_files) m_mapped_files.reset(new file_storage(*params.mapped_files));

		TORRENT_ASSERT(files().num_files() > 0);
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

	void default_storage::set_file_priority(aux::session_settings const& sett
		, aux::vector<std::uint8_t, file_index_t> const& prio
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
				boost::optional<aux::file_view> f = open_file(sett, i, aux::open_mode::write, ec);
				if (ec) return;

				need_partfile();

				m_part_file->export_file([&f, &ec](std::int64_t file_offset, span<char> buf)
				{
					auto file_range = f->range().subspan(std::size_t(file_offset));
					TORRENT_ASSERT(file_range.size() >= buf.size());
					sig::try_signal([&]{
						std::memcpy(const_cast<char*>(file_range.data()), buf.data(), buf.size());
					});
				}, fs.file_offset(i), fs.file_size(i), ec.ec);

				if (ec)
				{
					ec.file(i);
					ec.operation = operation_t::partfile_write;
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
					std::string p = fs.file_path(i, m_save_path);
					delete_one_file(p, ec.ec);
					if (ec)
					{
						ec.file(i);
						ec.operation = operation_t::file_remove;
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
			ec.operation = operation_t::partfile_write;
		}
	}

	void default_storage::initialize(aux::session_settings const& sett, storage_error& ec)
	{
		m_stat_cache.reserve(files().num_files());

#ifdef TORRENT_WINDOWS
		// don't do full file allocations on network drives
		std::wstring file_name = convert_to_wstring(m_save_path);
		int const drive_type = GetDriveTypeW(file_name.c_str());

		if (drive_type == DRIVE_REMOTE)
			m_allocate_files = false;
#endif
		{
			std::unique_lock<std::mutex> l(m_file_created_mutex);
			m_file_created.resize(files().num_files(), false);
		}

		// first, create zero-sized files
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
				ec.operation = operation_t::file_stat;
				ec.ec = err;
				break;
			}

			// if the file already exists, but is larger than what
			// it's supposed to be, truncate it
			// if the file is empty, just create it either way.
			if ((!err && size > files().file_size(file_index))
				|| (files().file_size(file_index) == 0 && err == boost::system::errc::no_such_file_or_directory))
			{
				auto f = open_file(sett, file_index, aux::open_mode::write
					| aux::open_mode::random_access | aux::open_mode::truncate, ec);
				if (ec)
				{
					ec.file(file_index);
					ec.operation = operation_t::file_fallocate;
					return;
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
			ec.file(file_index_t(-1));
			ec.operation = operation_t::file_stat;
			return false;
		}
		return false;
	}

	void default_storage::rename_file(file_index_t const index, std::string const& new_filename
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
				ec.file(index);
				ec.operation = operation_t::file_rename;
				return;
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
			m_part_file.reset();
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

	int default_storage::readv(aux::session_settings const& sett
		, span<iovec_t const> bufs
		, piece_index_t const piece, int const offset
		, aux::open_mode_t const flags, storage_error& error)
	{
#ifdef TORRENT_SIMULATE_SLOW_READ
		std::this_thread::sleep_for(seconds(1));
#endif
		return readwritev(files(), bufs, piece, offset, error
			, [this, flags, &sett](file_index_t const file_index
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
				&& m_file_priority[file_index] == 0)
			{
				need_partfile();

				error_code e;
				peer_request map = files().map_file(file_index, file_offset, 0);
				int const ret = m_part_file->readv(vec, map.piece, map.start, e);

				if (e)
				{
					ec.ec = e;
					ec.file(file_index);
					ec.operation = operation_t::partfile_read;
					return -1;
				}
				return ret;
			}

			auto handle = open_file(sett, file_index, flags, ec);
			if (ec) return -1;

			std::size_t ret = 0;
			error_code e;
			span<byte const volatile> file_range = handle->range();
			if (file_range.size() > std::size_t(file_offset))
			{
				file_range = file_range.subspan(std::size_t(file_offset));
				for (auto buf : vec)
				{
					if (file_range.empty()) break;
					if (file_range.size() < buf.size()) buf = buf.first(file_range.size());

					sig::try_signal([&]{
						std::memcpy(buf.data(), const_cast<char*>(file_range.data()), buf.size());
					});

					file_range = file_range.subspan(buf.size());
					ret += buf.size();
				}
			}

			// set this unconditionally in case the upper layer would like to treat
			// short reads as errors
			ec.operation = operation_t::file_read;

			// we either get an error or 0 or more bytes read
			TORRENT_ASSERT(e || ret > 0);
			TORRENT_ASSERT(int(ret) <= bufs_size(vec));

			if (e)
			{
				ec.ec = e;
				ec.file(file_index);
				return -1;
			}

			return static_cast<int>(ret);
		});
	}

	int default_storage::writev(aux::session_settings const& sett
		, span<iovec_t const> bufs
		, piece_index_t const piece, int const offset
		, aux::open_mode_t const flags, storage_error& error)
	{
		return readwritev(files(), bufs, piece, offset, error
			, [this, flags, &sett](file_index_t const file_index
				, std::int64_t const file_offset
				, span<iovec_t const> vec, storage_error& ec)
		{
			if (files().pad_file_at(file_index))
			{
				// writing to a pad-file is a no-op
				return bufs_size(vec);
			}

			if (file_index < m_file_priority.end_index()
				&& m_file_priority[file_index] == 0)
			{
				need_partfile();

				error_code e;
				peer_request map = files().map_file(file_index
					, file_offset, 0);
				int const ret = m_part_file->writev(vec, map.piece, map.start, e);

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

			auto handle = open_file(sett, file_index
				, aux::open_mode::write | flags, ec);
			if (ec) return -1;

			std::size_t ret = 0;
			error_code e;
			span<byte volatile> file_range = handle->range().subspan(std::size_t(file_offset));
			for (auto buf : vec)
			{
				TORRENT_ASSERT(file_range.size() >= buf.size());

				sig::try_signal([&]{
					std::memcpy(const_cast<char*>(file_range.data()), buf.data(), buf.size());
				});

				file_range = file_range.subspan(buf.size());
				ret += buf.size();
			}

			// set this unconditionally in case the upper layer would like to treat
			// short reads as errors
			ec.operation = operation_t::file_write;

			if (e)
			{
				ec.ec = e;
				ec.file(file_index);
				return -1;
			}

			return static_cast<int>(ret);
		});
	}

	int default_storage::hashv(aux::session_settings const& sett
		, hasher& ph, std::size_t const len
		, piece_index_t const piece, int const offset
		, aux::open_mode_t const flags, storage_error& error)
	{
#ifdef TORRENT_SIMULATE_SLOW_READ
		std::this_thread::sleep_for(seconds(1));
#endif
		char dummy = 0;
		iovec_t dummy1 = {&dummy, len};
		span<iovec_t> dummy2(&dummy1, 1);

		return readwritev(files(), dummy2, piece, offset, error
			, [this, flags, &ph, &sett](file_index_t const file_index
				, std::int64_t const file_offset
				, span<iovec_t const> vec, storage_error& ec)
		{
			std::size_t const read_size = std::size_t(bufs_size(vec));

			if (files().pad_file_at(file_index))
			{
				std::array<char, 64> zeroes;
				zeroes.fill(0);
				for (int left = int(read_size); left > 0; left -= int(zeroes.size()))
				{
					ph.update({zeroes.data(), std::min(zeroes.size(), std::size_t(left))});
				}
				return int(read_size);
			}

			if (file_index < m_file_priority.end_index()
				&& m_file_priority[file_index] == 0)
			{
				need_partfile();

				error_code e;
				peer_request map = files().map_file(file_index, file_offset, 0);
				int const ret = m_part_file->hashv(ph, read_size
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

			auto handle = open_file(sett, file_index, flags, ec);
			if (ec) return -1;

			std::size_t ret = 0;
			error_code e;
			span<byte const volatile> file_range = handle->range();
			if (file_range.size() > std::size_t(file_offset))
			{
				file_range = file_range.subspan(std::size_t(file_offset)
					, std::min(read_size, file_range.size() - std::size_t(file_offset)));

				sig::try_signal([&]{
					ph.update({const_cast<char const*>(file_range.data()), file_range.size()});
				});
				ret += file_range.size();
			}

			if (ret == 0)
			{
				ec.operation = operation_t::file_read;
				ec.ec = boost::asio::error::eof;
				ec.file(file_index);
				return -1;
			}

			return static_cast<int>(ret);
		});
	}

	// a wrapper around open_file_impl that, if it fails, makes sure the
	// directories have been created and retries
	boost::optional<aux::file_view> default_storage::open_file(aux::session_settings const& sett
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

		boost::optional<aux::file_view> h = open_file_impl(sett, file, mode, ec.ec);
		if ((mode & aux::open_mode::write)
			&& ec.ec == boost::system::errc::no_such_file_or_directory)
		{
			// this means the directory the file is in doesn't exist.
			// so create it
			ec.ec.clear();
			std::string path = files().file_path(file, m_save_path);
			create_directories(parent_path(path), ec.ec);

			if (ec.ec)
			{
				// if the directory creation failed, don't try to open the file again
				// but actually just fail
				ec.file(file);
				ec.operation = operation_t::mkdir;
				return {};
			}

			h = open_file_impl(sett, file, mode, ec.ec);
		}
		if (ec.ec)
		{
			ec.file(file);
			ec.operation = operation_t::file_open;
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

	boost::optional<aux::file_view> default_storage::open_file_impl(aux::session_settings const& sett
		, file_index_t file
		, aux::open_mode_t mode
		, error_code& ec) const
	{
		if (!m_allocate_files) mode |= aux::open_mode::sparse;

		// files with priority 0 should always be sparse
		if (m_file_priority.end_index() > file && m_file_priority[file] == 0)
			mode |= aux::open_mode::sparse;

		if (sett.get_bool(settings_pack::no_atime_storage))
		{
			mode |= aux::open_mode::no_atime;
		}

		// if we have a cache already, don't store the data twice by leaving it in the OS cache as well
		if (sett.get_int(settings_pack::disk_io_write_mode)
			== settings_pack::disable_os_cache)
		{
			mode |= aux::open_mode::no_cache;
		}

		try {
			return m_pool.open_file(storage_index(), m_save_path, file
				, files(), mode);
		}
		catch (system_error const& ex)
		{
			ec = ex.code();
			TORRENT_ASSERT(ec);
			return {};
		}
	}

	bool default_storage::tick()
	{
		error_code ec;
		if (m_part_file) m_part_file->flush_metadata(ec);

		return false;
	}

} // namespace libtorrent
