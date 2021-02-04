/*

Copyright (c) 2003-2009, 2011, 2013-2020, Arvid Norberg
Copyright (c) 2003, Daniel Wallin
Copyright (c) 2016, Vladimir Golovnev
Copyright (c) 2017-2018, Alden Torres
Copyright (c) 2017, 2019, Steven Siloti
Copyright (c) 2018, d-komarov
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
#include <numeric>
#include <set>
#include <functional>
#include <cstdio>

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/optional.hpp>

#if TORRENT_HAS_SYMLINK
#include <unistd.h> // for symlink()
#endif

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/mmap_storage.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/aux_/invariant_check.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/aux_/file_view_pool.hpp"
#include "libtorrent/disk_buffer_holder.hpp"
#include "libtorrent/stat_cache.hpp"
#include "libtorrent/hex.hpp" // to_hex

#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE

namespace libtorrent {

	mmap_storage::mmap_storage(storage_params const& params
		, aux::file_view_pool& pool)
		: m_files(params.files)
		, m_file_priority(params.priorities)
		, m_save_path(complete(params.path))
		, m_part_file_name("." + aux::to_hex(params.info_hash) + ".parts")
		, m_pool(pool)
		, m_allocate_files(params.mode == storage_mode_allocate)
	{
		if (params.mapped_files) m_mapped_files = std::make_unique<file_storage>(*params.mapped_files);

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
				boost::optional<aux::file_view> f = open_file(sett, i, aux::open_mode::write, ec);
				if (ec)
				{
					prio = m_file_priority;
					return;
				}

				if (m_part_file && use_partfile(i))
				{
					m_part_file->export_file([&f](std::int64_t file_offset, span<char> buf)
					{
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
				if (exists(fp, ec.ec)) use_partfile(i, false);
				if (ec.ec)
				{
					ec.file(i);
					ec.operation = operation_t::file_stat;
					prio = m_file_priority;
					return;
				}
/*
				auto f = open_file(sett, i, aux::open_mode::read_only, ec);
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

	bool mmap_storage::use_partfile(file_index_t const index) const
	{
		TORRENT_ASSERT_VAL(index >= file_index_t{}, index);
		if (index >= m_use_partfile.end_index()) return true;
		return m_use_partfile[index];
	}

	void mmap_storage::use_partfile(file_index_t const index, bool const b)
	{
		if (index >= m_use_partfile.end_index()) m_use_partfile.resize(static_cast<int>(index) + 1, true);
		m_use_partfile[index] = b;
	}

	void mmap_storage::initialize(settings_interface const& sett, storage_error& ec)
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
		// first, create zero-sized files
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
			if (fs.file_size(file_index) == 0)
			{
#if TORRENT_HAS_SYMLINK
				// create symlinks
				if (fs.file_flags(file_index) & file_storage::flag_symlink)
				{
					std::string path = fs.file_path(file_index, m_save_path);
					create_directories(parent_path(path), ec.ec);
					if (ec)
					{
						ec.ec = error_code(errno, generic_category());
						ec.file(file_index);
						ec.operation = operation_t::mkdir;
						break;
					}
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
				if (err == boost::system::errc::no_such_file_or_directory)
				{
					// just creating the file is enough to make it zero-sized. If
					// there's a race here and some other process truncates the file,
					// it's not a problem, we won't access empty files ever again
					ec.ec.clear();
					auto f = open_file(sett, file_index, aux::open_mode::write
						| aux::open_mode::random_access | aux::open_mode::truncate, ec);
					if (ec)
					{
						ec.file(file_index);
						ec.operation = operation_t::file_fallocate;
						return;
					}
				}
			}
			ec.ec.clear();
		}

		// close files that were opened in write mode
		m_pool.release(storage_index());
	}

	bool mmap_storage::has_any_file(storage_error& ec)
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

	void mmap_storage::rename_file(file_index_t const index, std::string const& new_filename
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

		aux::delete_files(files(), m_save_path, m_part_file_name, options, ec);
	}

	bool mmap_storage::verify_resume_data(add_torrent_params const& rd
		, aux::vector<std::string, file_index_t> const& links
		, storage_error& ec)
	{
		return aux::verify_resume_data(rd, links, files()
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
		std::tie(ret, m_save_path) = aux::move_storage(files(), m_save_path, std::move(save_path)
			, std::move(move_partfile), flags, ec);

		// clear the stat cache in case the new location has new files
		m_stat_cache.clear();

		return { ret, m_save_path };
	}

	int mmap_storage::readv(settings_interface const& sett
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
			// reading from a pad file yields zeroes
			if (files().pad_file_at(file_index)) return aux::read_zeroes(vec);

			if (file_index < m_file_priority.end_index()
				&& m_file_priority[file_index] == dont_download
				&& use_partfile(file_index))
			{
				TORRENT_ASSERT(m_part_file);

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

			int ret = 0;
			error_code e;
			span<byte const> file_range = handle->range();
			if (file_range.size() > file_offset)
			{
				file_range = file_range.subspan(static_cast<std::ptrdiff_t>(file_offset));
				for (auto buf : vec)
				{
					if (file_range.empty()) break;
					if (file_range.size() < buf.size()) buf = buf.first(file_range.size());

					sig::try_signal([&]{
						std::memcpy(buf.data(), const_cast<char*>(file_range.data())
							, static_cast<std::size_t>(buf.size()));
					});

					file_range = file_range.subspan(buf.size());
					ret += static_cast<int>(buf.size());
				}
			}

			// set this unconditionally in case the upper layer would like to treat
			// short reads as errors
			ec.operation = operation_t::file_read;

			// we either get an error or 0 or more bytes read
			TORRENT_ASSERT(e || ret > 0);
			TORRENT_ASSERT(ret <= bufs_size(vec));

			if (e)
			{
				ec.ec = e;
				ec.file(file_index);
				return -1;
			}

			return static_cast<int>(ret);
		});
	}

	int mmap_storage::writev(settings_interface const& sett
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
				&& m_file_priority[file_index] == dont_download
				&& use_partfile(file_index))
			{
				TORRENT_ASSERT(m_part_file);

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

			int ret = 0;
			error_code e;
			span<byte> file_range = handle->range().subspan(static_cast<std::ptrdiff_t>(file_offset));
			for (auto buf : vec)
			{
				TORRENT_ASSERT(file_range.size() >= buf.size());

				sig::try_signal([&]{
					std::memcpy(const_cast<char*>(file_range.data()), buf.data(), static_cast<std::size_t>(buf.size()));
				});

				file_range = file_range.subspan(buf.size());
				ret += static_cast<int>(buf.size());
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

			return ret;
		});
	}

	int mmap_storage::hashv(settings_interface const& sett
		, hasher& ph, std::ptrdiff_t const len
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
			auto const read_size = bufs_size(vec);

			if (files().pad_file_at(file_index))
			{
				std::array<char, 64> zero_buf;
				zero_buf.fill(0);
				span<char> zeroes = zero_buf;
				for (std::ptrdiff_t left = read_size; left > 0; left -= zeroes.size())
				{
					ph.update({zeroes.data(), std::min(zeroes.size(), left)});
				}
				return read_size;
			}

			if (file_index < m_file_priority.end_index()
				&& m_file_priority[file_index] == dont_download
				&& use_partfile(file_index))
			{
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

			int ret = 0;
			span<byte const> file_range = handle->range();
			if (file_range.size() > file_offset)
			{
				file_range = file_range.subspan(std::ptrdiff_t(file_offset)
					, std::min(std::ptrdiff_t(read_size), std::ptrdiff_t(file_range.size() - file_offset)));

				sig::try_signal([&]{
					ph.update({const_cast<char const*>(file_range.data()), file_range.size()});
				});
				ret += static_cast<int>(file_range.size());
			}

			return ret;
		});
	}

	int mmap_storage::hashv2(settings_interface const& sett
		, hasher256& ph, std::ptrdiff_t const len
		, piece_index_t const piece, int const offset
		, aux::open_mode_t const flags, storage_error& error)
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
			int const ret = m_part_file->hashv2(ph, len
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

		auto handle = open_file(sett, file_index, flags, error);
		if (error) return -1;

		span<byte const> file_range = handle->range();
		if (std::int64_t(file_range.size()) <= file_offset)
			return 0;
		file_range = file_range.subspan(std::ptrdiff_t(file_offset));
		file_range = file_range.first(std::min(std::ptrdiff_t(len), file_range.size()));
		ph.update(file_range);

		return static_cast<int>(file_range.size());
	}

	// a wrapper around open_file_impl that, if it fails, makes sure the
	// directories have been created and retries
	boost::optional<aux::file_view> mmap_storage::open_file(settings_interface const& sett
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

#ifdef _WIN32
		if (sett.get_bool(settings_pack::enable_set_file_valid_data))
		{
			mode |= aux::open_mode::allow_set_file_valid_data;
		}
#endif

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

	boost::optional<aux::file_view> mmap_storage::open_file_impl(settings_interface const& sett
		, file_index_t file
		, aux::open_mode_t mode
		, error_code& ec) const
	{
		TORRENT_ASSERT(!files().pad_file_at(file));
		if (!m_allocate_files) mode |= aux::open_mode::sparse;

		// files with priority 0 should always be sparse
		if (m_file_priority.end_index() > file && m_file_priority[file] == dont_download)
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
				, files(), mode
#if TORRENT_HAVE_MAP_VIEW_OF_FILE
				, std::shared_ptr<std::mutex>(m_file_open_unmap_lock
					, &m_file_open_unmap_lock.get()[int(file)])
#endif
				);
		}
		catch (system_error const& ex)
		{
			ec = ex.code();
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
} // namespace libtorrent

#endif // TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE
