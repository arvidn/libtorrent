/*

Copyright (c) 2016, 2019-2022, Arvid Norberg
Copyright (c) 2020, Rosen Penev
Copyright (c) 2020, pavel-pimenov
Copyright (c) 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/config.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/aux_/posix_storage.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/aux_/open_mode.hpp"
#include "libtorrent/aux_/file_pointer.hpp"
#include "libtorrent/torrent_status.hpp"
#include "libtorrent/aux_/storage_utils.hpp" // for read_zeroes, move_storage
#include "libtorrent/aux_/readwrite.hpp"

using namespace libtorrent::flags; // for flag operators

#ifndef TORRENT_WINDOWS
// make sure the _FILE_OFFSET_BITS define worked
// on this platform. It's supposed to make file
// related functions support 64-bit offsets.
#if TORRENT_HAS_FTELLO
static_assert(sizeof(ftello(std::declval<FILE*>())) >= 8, "64 bit file operations are required");
#endif
static_assert(sizeof(off_t) >= 8, "64 bit file operations are required");
#endif

namespace libtorrent {
namespace aux {

	posix_storage::posix_storage(storage_params const& p)
		: m_files(p.files)
		, m_renamed_files(std::move(p.renamed_files))
		, m_save_path(p.path)
		, m_file_priority(p.priorities)
		, m_part_file_name("." + to_hex(p.info_hash) + ".parts")
	{}

	filenames posix_storage::names() const
	{
		return {m_files, m_renamed_files};
	}

	posix_storage::~posix_storage()
	{
		error_code ec;
		if (m_part_file) m_part_file->flush_metadata(ec);
	}

	void posix_storage::need_partfile()
	{
		if (m_part_file) return;

		m_part_file = std::make_unique<posix_part_file>(
			m_save_path, m_part_file_name
			, files().num_pieces(), files().piece_length());
	}

	void posix_storage::set_file_priority(settings_interface const&
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
			if (old_prio == dont_download && new_prio != dont_download)
			{
				if (m_part_file && use_partfile(i))
				{
					m_part_file->export_file([this, i, &ec](std::int64_t file_offset, span<char> buf)
					{
						// move stuff out of the part file
						file_pointer const f = open_file(i, open_mode::write, file_offset, ec);
						if (ec) return;
						int const r = static_cast<int>(std::fwrite(buf.data(), 1
							, static_cast<std::size_t>(buf.size()), f.file()));
						if (r != buf.size())
						{
							if (std::ferror(f.file())) ec.ec.assign(errno, generic_category());
							else ec.ec.assign(errors::file_too_short, libtorrent_category());
							return;
						}
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

	int posix_storage::read(settings_interface const&
		, span<char> buffer
		, piece_index_t const piece, int const offset
		, storage_error& error)
	{
#ifdef TORRENT_SIMULATE_SLOW_READ
		std::this_thread::sleep_for(milliseconds(rand() % 2000));
#endif
		return readwrite(files(), buffer, piece, offset, error
			, [this](file_index_t const file_index
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

			file_pointer const f = open_file(file_index, open_mode::read_only
				, file_offset, ec);
			if (ec.ec) return -1;

			// set this unconditionally in case the upper layer would like to treat
			// short reads as errors
			ec.operation = operation_t::file_read;

			int ret = 0;
			int const r = static_cast<int>(std::fread(buf.data(), 1
				, static_cast<std::size_t>(buf.size()), f.file()));
			if (r == 0)
			{
				if (std::ferror(f.file())) ec.ec.assign(errno, generic_category());
				else ec.ec.assign(errors::file_too_short, libtorrent_category());
			}
			ret += r;

			// we either get an error or 0 or more bytes read
			TORRENT_ASSERT(ec.ec || ret > 0);
			TORRENT_ASSERT(ret <= buf.size());
			return ret;
		});
	}

	int posix_storage::write(settings_interface const&
		, span<char const> buffer
		, piece_index_t const piece, int const offset
		, storage_error& error)
	{
#ifdef TORRENT_SIMULATE_SLOW_WRITE
		std::this_thread::sleep_for(milliseconds(rand() % 800));
#endif
		return readwrite(files(), buffer, piece, offset, error
			, [this](file_index_t const file_index
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
				}
				return ret;
			}

			file_pointer const f = open_file(file_index, open_mode::write
				, file_offset, ec);
			if (ec.ec) return -1;

			// set this unconditionally in case the upper layer would like to treat
			// short reads as errors
			ec.operation = operation_t::file_write;

			int ret = 0;
			auto const r = static_cast<int>(std::fwrite(buf.data(), 1
				, static_cast<std::size_t>(buf.size()), f.file()));
			if (r != buf.size())
			{
				if (std::ferror(f.file())) ec.ec.assign(errno, generic_category());
				else ec.ec.assign(errors::file_too_short, libtorrent_category());
			}
			ret += r;

			// invalidate our stat cache for this file, since
			// we're writing to it
			m_stat_cache.set_dirty(file_index);
			return ret;
		});
	}

	bool posix_storage::has_any_file(storage_error& error)
	{
		m_stat_cache.reserve(files().num_files());
		return aux::has_any_file(names(), m_save_path, m_stat_cache, error);
	}

	bool posix_storage::verify_resume_data(add_torrent_params const& rd
		, vector<std::string, file_index_t> const& links
		, storage_error& ec)
	{
		return aux::verify_resume_data(rd, links, names()
			, m_file_priority, m_stat_cache, m_save_path, ec);
	}

	void posix_storage::release_files()
	{
		m_stat_cache.clear();
		if (m_part_file)
		{
			error_code ignore;
			m_part_file->flush_metadata(ignore);
		}
	}

	void posix_storage::delete_files(remove_flags_t const options, storage_error& error)
	{
		// if there's a part file open, make sure to destruct it to have it
		// release the underlying part file. Otherwise we may not be able to
		// delete it
		if (m_part_file) m_part_file.reset();
		aux::delete_files(names(), m_save_path, m_part_file_name, options, error);
	}

	std::pair<status_t, std::string> posix_storage::move_storage(std::string const& sp
		, move_flags_t const flags, storage_error& ec)
	{
		lt::status_t ret{};
		auto move_partfile = [&](std::string const& new_save_path, error_code& e)
		{
			if (!m_part_file) return;
			m_part_file->move_partfile(new_save_path, e);
		};
		std::tie(ret, m_save_path) = aux::move_storage(names(), m_save_path, sp
			, std::move(move_partfile), flags, ec);

		// clear the stat cache in case the new location has new files
		m_stat_cache.clear();

		return { ret, m_save_path };
	}

	void posix_storage::rename_file(file_index_t const index, std::string const& new_filename, storage_error& ec)
	{
		if (index < file_index_t(0) || index >= files().end_file()) return;
		std::string const old_name = m_renamed_files.file_path(m_files, index, m_save_path);

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

		m_renamed_files.rename_file(files(), index, new_filename);
	}

	status_t posix_storage::initialize(settings_interface const&, storage_error& ec)
	{
		m_stat_cache.reserve(files().num_files());

		filenames const fs = names();
		// if some files have priority 0, we need to check if they exist on the
		// filesystem, in which case we won't use a partfile for them.
		// this is to be backwards compatible with previous versions of
		// libtorrent, when part files were not supported.
		status_t ret{};
		for (file_index_t i(0); i < m_file_priority.end_index(); ++i)
		{
			if (m_file_priority[i] != dont_download || fs.pad_file_at(i))
				continue;

			file_status s;
			std::string const file_path = fs.file_path(i, m_save_path);
			error_code err;
			stat_file(file_path, &s, err);

			if (s.file_size > fs.file_size(i))
				ret |= disk_status::oversized_file;

			if (!err)
			{
				use_partfile(i, false);
			}
			else
			{
				need_partfile();
			}
		}

		aux::initialize_storage(fs, m_save_path, m_stat_cache, m_file_priority
			, [this](file_index_t const file_index, storage_error& e)
			{ open_file(file_index, aux::open_mode::write, 0, e); }
			, aux::create_symlink
			, [&ret](file_index_t, std::int64_t) { ret |= disk_status::oversized_file; }
			, ec);
		return ret;
	}

	file_pointer posix_storage::open_file(file_index_t idx, open_mode_t const mode
		, std::int64_t const offset, storage_error& ec)
	{
		std::string const fn = m_renamed_files.file_path(m_files, idx, m_save_path);

		auto const* mode_str = (mode & open_mode::write)
#ifdef TORRENT_WINDOWS
			? L"rb+" : L"rb";
#else
			? "rb+" : "rb";
#endif

#ifdef TORRENT_WINDOWS
		FILE* f = ::_wfopen(convert_to_native_path_string(fn).c_str(), mode_str);
#else
		FILE* f = std::fopen(fn.c_str(), mode_str);
#endif
		if (f == nullptr)
		{
			ec.ec.assign(errno, generic_category());

			// if we fail to open a file for writing, and the error is ENOENT,
			// it is likely because the directory we're creating the file in
			// does not exist. Create the directory and try again.
			if ((mode & aux::open_mode::write)
				&& (ec.ec == boost::system::errc::no_such_file_or_directory
#ifdef TORRENT_WINDOWS
					// this is a workaround for improper handling of files on windows shared drives.
					// if the directory on a shared drive does not exist,
					// windows returns ERROR_IO_DEVICE instead of ERROR_FILE_NOT_FOUND
					|| ec.ec == error_code(ERROR_IO_DEVICE, system_category())
#endif
			))
			{
				// this means the directory the file is in doesn't exist.
				// so create it
				ec.ec.clear();
				create_directories(parent_path(fn), ec.ec);

				if (ec.ec)
				{
					ec.file(idx);
					ec.operation = operation_t::mkdir;
					return file_pointer{};
				}

				// now that we've created the directories, try again
				// and make sure we create the file this time ("r+") opens for
				// reading and writing, but doesn't create the file. "w+" creates
				// the file and truncates it
#ifdef TORRENT_WINDOWS
				f = ::_wfopen(convert_to_native_path_string(fn).c_str(), L"wb+");
#else
				f = std::fopen(fn.c_str(), "wb+");
#endif
				if (f == nullptr)
				{
					ec.ec.assign(errno, generic_category());
					ec.file(idx);
					ec.operation = operation_t::file_open;
					return file_pointer{};
				}
			}
			else
			{
				ec.file(idx);
				ec.operation = operation_t::file_open;
				return file_pointer{};
			}
		}

		if (offset != 0)
		{
			if (portable_fseeko(f, offset, SEEK_SET) != 0)
			{
				ec.ec.assign(errno, generic_category());
				ec.file(idx);
				ec.operation = operation_t::file_seek;
				return file_pointer{};
			}
		}

		return file_pointer{f};
	}

	bool posix_storage::use_partfile(file_index_t const index) const
	{
		TORRENT_ASSERT_VAL(index >= file_index_t{}, index);
		if (index >= m_use_partfile.end_index()) return true;
		return m_use_partfile[index];
	}

	void posix_storage::use_partfile(file_index_t const index, bool const b)
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

}
}
