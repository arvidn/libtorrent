/*

Copyright (c) 2017, Arvid Norberg
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
#include "libtorrent/aux_/posix_storage.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/aux_/path.hpp" // for bufs_size
#include "libtorrent/aux_/open_mode.hpp"

using namespace libtorrent::flags; // for flag operators

namespace libtorrent {
namespace aux {

	posix_storage::posix_storage(storage_params p)
		: m_files(p.files)
		, m_save_path(std::move(p.path))
		, m_part_file_name("." + to_hex(p.info_hash) + ".parts")
	{
		if (p.mapped_files) m_mapped_files.reset(new file_storage(*p.mapped_files));
	}

	file_storage const& posix_storage::files() const { return m_mapped_files ? *m_mapped_files.get() : m_files; }

	int posix_storage::readv(session_settings const& sett
		, span<iovec_t const> bufs
		, piece_index_t const piece, int const offset
		, storage_error& error)
	{
		return readwritev(files(), bufs, piece, offset, error
			, [this, &sett](file_index_t const file_index
				, std::int64_t const file_offset
				, span<iovec_t const> vec, storage_error& ec)
		{
			if (files().pad_file_at(file_index))
			{
				// reading from a pad file yields zeroes
				clear_bufs(vec);
				return bufs_size(vec);
			}

			FILE* const f = open_file(file_index, open_mode::read_only
				, file_offset, ec);
			if (ec.ec) return -1;

			// set this unconditionally in case the upper layer would like to treat
			// short reads as errors
			ec.operation = operation_t::file_read;

			std::size_t ret = 0;
			for (auto buf : vec)
			{
				std::size_t const r = fread(buf.data(), 1, buf.size(), f);
				if (r == 0)
				{
					ec.ec.assign(errno, generic_category());
					break;
				}
				ret += r;

				// the file may be shorter than we think
				if (r < buf.size()) break;
			}

			fclose(f);

			// we either get an error or 0 or more bytes read
			TORRENT_ASSERT(ec.ec || ret > 0);
			TORRENT_ASSERT(int(ret) <= bufs_size(vec));

			if (ec.ec)
			{
				ec.file(file_index);
				return -1;
			}

			return static_cast<int>(ret);
		});
	}

	int posix_storage::writev(session_settings const& sett
		, span<iovec_t const> bufs
		, piece_index_t const piece, int const offset
		, storage_error& error)
	{
		return readwritev(files(), bufs, piece, offset, error
			, [this, &sett](file_index_t const file_index
				, std::int64_t const file_offset
				, span<iovec_t const> vec, storage_error& ec)
		{
			if (files().pad_file_at(file_index))
			{
				// writing to a pad-file is a no-op
				return bufs_size(vec);
			}

			FILE* const f = open_file(file_index, open_mode::write
				, file_offset, ec);
			if (ec.ec) return -1;

			// set this unconditionally in case the upper layer would like to treat
			// short reads as errors
			ec.operation = operation_t::file_write;

			std::size_t ret = 0;
			for (auto buf : vec)
			{
				std::size_t const r = fwrite(buf.data(), 1, buf.size(), f);
				if (r != buf.size())
				{
					ec.ec.assign(errno, generic_category());
					break;
				}
				ret += r;
			}

			fclose(f);

			// invalidate our stat cache for this file, since
			// we're writing to it
			m_stat_cache.set_dirty(file_index);

			if (ec)
			{
				ec.file(file_index);
				return -1;
			}

			return static_cast<int>(ret);
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

	void posix_storage::delete_files(remove_flags_t const options, storage_error& error)
	{
		aux::delete_files(files(), m_save_path, m_part_file_name, options, error);
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

	void posix_storage::initialize(aux::session_settings const&, storage_error& ec)
	{
		m_stat_cache.reserve(files().num_files());

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
			std::int64_t const file_size = files().file_size(file_index);
			if ((!err && size > file_size)
				|| files().file_size(file_index) == 0)
			{
				FILE* f = open_file(file_index, aux::open_mode::write, 0, ec);
				if (ec) return;
#ifndef TORRENT_WINDOWS
				if (file_size > 0)
				{
					int ret = ftruncate(fileno(f), file_size);
					if (ret < 0)
					{
						fclose(f);
						ec.file(file_index);
						ec.operation = operation_t::file_fallocate;
						return;
					}
				}
#endif
				fclose(f);
			}
			ec.ec.clear();
		}
	}

	FILE* posix_storage::open_file(file_index_t idx, open_mode_t const mode
		, std::int64_t const offset, storage_error& ec)
	{
		std::string const fn = files().file_path(idx, m_save_path);

		char const* mode_str = (mode & open_mode::write)
			? "rb+" : "rb";

		FILE* f = fopen(fn.c_str(), mode_str);
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
					return nullptr;
				}

				// now that we've created the directories, try again
				// and make sure we create the file this time ("r+") opens for
				// reading and writing, but doesn't create the file. "w+" creates
				// the file and truncates it
				f = fopen(fn.c_str(), "wb+");
				if (f == nullptr)
				{
					ec.ec.assign(errno, generic_category());
					ec.file(idx);
					ec.operation = operation_t::file_open;
					return nullptr;
				}
			}
			else
			{
				ec.file(idx);
				ec.operation = operation_t::file_open;
				return nullptr;
			}
		}

#ifdef TORRENT_WINDOWS
#define fseek _fseeki64
#endif

		if (offset != 0)
		{
			if (fseek(f, offset, SEEK_SET) != 0)
			{
				ec.ec.assign(errno, generic_category());
				ec.file(idx);
				ec.operation = operation_t::file_seek;
				fclose(f);
				return nullptr;
			}
		}

		return f;
	}
}
}

