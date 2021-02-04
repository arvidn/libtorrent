/*

Copyright (c) 2016, 2019-2020, Arvid Norberg
Copyright (c) 2020, pavel-pimenov
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
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/aux_/posix_storage.hpp"
#include "libtorrent/aux_/path.hpp" // for bufs_size
#include "libtorrent/aux_/open_mode.hpp"
#include "libtorrent/aux_/file_pointer.hpp"
#include "libtorrent/torrent_status.hpp"
#ifdef TORRENT_WINDOWS
#include "libtorrent/utf8.hpp"
#endif

#if TORRENT_HAS_SYMLINK
#include <unistd.h> // for symlink()
#endif

using namespace libtorrent::flags; // for flag operators

#ifndef TORRENT_WINDOWS
// make sure the _FILE_OFFSET_BITS define worked
// on this platform. It's supposed to make file
// related functions support 64-bit offsets.
static_assert(sizeof(ftello(nullptr)) >= 8, "64 bit file operations are required");
static_assert(sizeof(off_t) >= 8, "64 bit file operations are required");
#endif

namespace libtorrent {
namespace aux {

	posix_storage::posix_storage(storage_params const& p)
		: m_files(p.files)
		, m_save_path(p.path)
		, m_part_file_name("." + to_hex(p.info_hash) + ".parts")
	{
		if (p.mapped_files) m_mapped_files.reset(new file_storage(*p.mapped_files));
	}

	file_storage const& posix_storage::files() const { return m_mapped_files ? *m_mapped_files.get() : m_files; }

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

	void posix_storage::set_file_priority(aux::vector<download_priority_t, file_index_t>& prio
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
				if (m_part_file && use_partfile(i))
				{
					m_part_file->export_file([this, i, &ec](std::int64_t file_offset, span<char> buf)
					{
						// move stuff out of the part file
						file_pointer const f = open_file(i, open_mode::write, file_offset, ec);
						if (ec) return;
						int const r = static_cast<int>(fwrite(buf.data(), 1
							, static_cast<std::size_t>(buf.size()), f.file()));
						if (r != buf.size())
						{
							if (ferror(f.file())) ec.ec.assign(errno, generic_category());
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
				if (exists(fp, ec.ec)) use_partfile(i, false);
				if (ec.ec)
				{
					ec.file(i);
					ec.operation = operation_t::file_stat;
					prio = m_file_priority;
					return;
				}
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

	int posix_storage::readv(settings_interface const&
		, span<iovec_t const> bufs
		, piece_index_t const piece, int const offset
		, storage_error& error)
	{
		return readwritev(files(), bufs, piece, offset, error
			, [this](file_index_t const file_index
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

			file_pointer const f = open_file(file_index, open_mode::read_only
				, file_offset, ec);
			if (ec.ec) return -1;

			// set this unconditionally in case the upper layer would like to treat
			// short reads as errors
			ec.operation = operation_t::file_read;

			int ret = 0;
			for (auto buf : vec)
			{
				int const r = static_cast<int>(fread(buf.data(), 1
					, static_cast<std::size_t>(buf.size()), f.file()));
				if (r == 0)
				{
					if (ferror(f.file())) ec.ec.assign(errno, generic_category());
					else ec.ec.assign(errors::file_too_short, libtorrent_category());
					break;
				}
				ret += r;

				// the file may be shorter than we think
				if (r < buf.size()) break;
			}

			// we either get an error or 0 or more bytes read
			TORRENT_ASSERT(ec.ec || ret > 0);
			TORRENT_ASSERT(ret <= bufs_size(vec));

			if (ec.ec)
			{
				ec.file(file_index);
				return -1;
			}

			return ret;
		});
	}

	int posix_storage::writev(settings_interface const&
		, span<iovec_t const> bufs
		, piece_index_t const piece, int const offset
		, storage_error& error)
	{
		return readwritev(files(), bufs, piece, offset, error
			, [this](file_index_t const file_index
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

			file_pointer const f = open_file(file_index, open_mode::write
				, file_offset, ec);
			if (ec.ec) return -1;

			// set this unconditionally in case the upper layer would like to treat
			// short reads as errors
			ec.operation = operation_t::file_write;

			int ret = 0;
			for (auto buf : vec)
			{
				auto const r = static_cast<int>(fwrite(buf.data(), 1
					, static_cast<std::size_t>(buf.size()), f.file()));
				if (r != buf.size())
				{
					if (ferror(f.file())) ec.ec.assign(errno, generic_category());
					else ec.ec.assign(errors::file_too_short, libtorrent_category());
					break;
				}
				ret += r;
			}

			// invalidate our stat cache for this file, since
			// we're writing to it
			m_stat_cache.set_dirty(file_index);

			if (ec)
			{
				ec.file(file_index);
				return -1;
			}

			return ret;
		});
	}

	bool posix_storage::has_any_file(storage_error& error)
	{
		m_stat_cache.reserve(files().num_files());
		return aux::has_any_file(files(), m_save_path, m_stat_cache, error);
	}

	bool posix_storage::verify_resume_data(add_torrent_params const& rd
		, vector<std::string, file_index_t> const& links
		, storage_error& ec)
	{
		return aux::verify_resume_data(rd, links, files()
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
		aux::delete_files(files(), m_save_path, m_part_file_name, options, error);
	}

	std::pair<status_t, std::string> posix_storage::move_storage(std::string const& sp
		, move_flags_t const flags, storage_error& ec)
	{
		lt::status_t ret;
		auto move_partfile = [&](std::string const& new_save_path, error_code& e)
		{
			if (!m_part_file) return;
			m_part_file->move_partfile(new_save_path, e);
		};
		std::tie(ret, m_save_path) = aux::move_storage(files(), m_save_path, sp
			, std::move(move_partfile), flags, ec);

		// clear the stat cache in case the new location has new files
		m_stat_cache.clear();

		return { ret, m_save_path };
	}

	void posix_storage::rename_file(file_index_t const index, std::string const& new_filename, storage_error& ec)
	{
		if (index < file_index_t(0) || index >= files().end_file()) return;
		std::string const old_name = files().file_path(index, m_save_path);

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

		if (!m_mapped_files)
		{
			m_mapped_files.reset(new file_storage(files()));
		}
		m_mapped_files->rename_file(index, new_filename);
	}

	void posix_storage::initialize(settings_interface const&, storage_error& ec)
	{
		m_stat_cache.reserve(files().num_files());

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
		for (auto file_index : fs.file_range())
		{
			// ignore files that have priority 0
			if (m_file_priority.end_index() > file_index
				&& m_file_priority[file_index] == dont_download)
			{
				continue;
			}

			// ignore pad files
			if (fs.pad_file_at(file_index)) continue;

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
							ec.ec = error_code(errno, generic_category());
							ec.file(file_index);
							ec.operation = operation_t::symlink;
							break;
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
					file_pointer f = open_file(file_index, aux::open_mode::write, 0, ec);
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
	}

	file_pointer posix_storage::open_file(file_index_t idx, open_mode_t const mode
		, std::int64_t const offset, storage_error& ec)
	{
		std::string const fn = files().file_path(idx, m_save_path);

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
			if ((mode & open_mode::write)
				&& ec.ec == boost::system::errc::no_such_file_or_directory)
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

#ifdef _MSC_VER
#define fseeko _fseeki64
#endif

		if (offset != 0)
		{
			if (fseeko(f, offset, SEEK_SET) != 0)
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
		if (index >= m_use_partfile.end_index()) m_use_partfile.resize(static_cast<int>(index) + 1, true);
		m_use_partfile[index] = b;
	}

}
}

