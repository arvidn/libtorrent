/*

Copyright (c) 2016-2022, Arvid Norberg
Copyright (c) 2017-2018, Alden Torres
Copyright (c) 2017-2018, Steven Siloti
Copyright (c) 2018, Pavel Pimenov
Copyright (c) 2019, Mike Tzou
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

#include "libtorrent/aux_/storage_utils.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/aux_/alloca.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/session.hpp" // for session::delete_files
#include "libtorrent/stat_cache.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/torrent_status.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/string_view.hpp"
#include "libtorrent/hasher.hpp"

#if TORRENT_HAS_SYMLINK
#include <unistd.h> // for symlink()
#endif

#include <set>

namespace libtorrent { namespace aux {

	// much of what needs to be done when reading and writing is buffer
	// management and piece to file mapping. Most of that is the same for reading
	// and writing. This function is a template, and the fileop decides what to
	// do with the file and the buffers.
	int readwrite(file_storage const& files, span<char> buf
		, piece_index_t const piece, const int offset
		, storage_error& ec, fileop op)
	{
		TORRENT_ASSERT(piece >= piece_index_t(0));
		TORRENT_ASSERT(piece < files.end_piece());
		TORRENT_ASSERT(offset >= 0);
		TORRENT_ASSERT(buf.size() > 0);

		TORRENT_ASSERT(buf.size() > 0);
		TORRENT_ASSERT(static_cast<int>(piece) * static_cast<std::int64_t>(files.piece_length())
			+ offset + buf.size() <= files.total_size());

		// find the file iterator and file offset
		std::int64_t const torrent_offset = static_cast<int>(piece) * std::int64_t(files.piece_length()) + offset;
		file_index_t file_index = files.file_index_at_offset(torrent_offset);
		TORRENT_ASSERT(torrent_offset >= files.file_offset(file_index));
		TORRENT_ASSERT(torrent_offset < files.file_offset(file_index) + files.file_size(file_index));
		std::int64_t file_offset = torrent_offset - files.file_offset(file_index);

		int ret = 0;

		while (buf.size() > 0)
		{
			// the number of bytes left to read in the current file (specified by
			// file_index). This is the minimum of (file_size - file_offset) and
			// buf.size().
			int file_bytes_left = int(buf.size());
			if (file_offset + file_bytes_left > files.file_size(file_index))
				file_bytes_left = std::max(static_cast<int>(files.file_size(file_index) - file_offset), 0);

			// there are no bytes left in this file, move to the next one
			// this loop skips over empty files
			if (file_bytes_left == 0)
			{
				do
				{
					++file_index;
					file_offset = 0;
					TORRENT_ASSERT(file_index < files.end_file());

					// this should not happen. buf.size() should be clamped by the total
					// size of the torrent, so we should never run off the end of it
					if (file_index >= files.end_file()) return ret;

					// skip empty files
				}
				while (files.file_size(file_index) == 0);

				file_bytes_left = int(buf.size());
				if (file_offset + file_bytes_left > files.file_size(file_index))
					file_bytes_left = std::max(static_cast<int>(files.file_size(file_index) - file_offset), 0);
				TORRENT_ASSERT(file_bytes_left > 0);
			}

			int const bytes_transferred = op(file_index, file_offset
				, buf.first(file_bytes_left), ec);
			TORRENT_ASSERT(bytes_transferred <= file_bytes_left);
			if (ec)
			{
				ec.file(file_index);
				return ret;
			}

			buf = buf.subspan(bytes_transferred);
			file_offset += bytes_transferred;
			ret += bytes_transferred;

			// if the file operation returned 0, we've hit end-of-file. We're done
			if (bytes_transferred == 0 && file_bytes_left > 0 )
			{
				// fill in this information in case the caller wants to treat
				// a short-read as an error
				ec.operation = operation_t::file_read;
				ec.ec = boost::asio::error::eof;
				ec.file(file_index);
			}
		}
		return ret;
	}

	std::pair<status_t, std::string> move_storage(file_storage const& f
		, std::string save_path
		, std::string const& destination_save_path
		, std::function<void(std::string const&, error_code&)> const& move_partfile
		, move_flags_t const flags, storage_error& ec)
	{
		status_t ret = status_t::no_error;
		std::string const new_save_path = complete(destination_save_path);

		// check to see if any of the files exist
		if (flags == move_flags_t::fail_if_exist)
		{
			file_status s;
			error_code err;
			stat_file(new_save_path, &s, err);
			if (err != boost::system::errc::no_such_file_or_directory)
			{
				// the directory exists, check all the files
				for (auto const i : f.file_range())
				{
					// files moved out to absolute paths are ignored
					if (f.file_absolute_path(i)) continue;

					stat_file(f.file_path(i, new_save_path), &s, err);
					if (err != boost::system::errc::no_such_file_or_directory)
					{
						ec.ec = err;
						ec.file(i);
						ec.operation = operation_t::file_stat;
						return { status_t::file_exist, save_path };
					}
				}
			}
		}

		{
			file_status s;
			error_code err;
			stat_file(new_save_path, &s, err);
			if (err == boost::system::errc::no_such_file_or_directory)
			{
				err.clear();
				create_directories(new_save_path, err);
				if (err)
				{
					ec.ec = err;
					ec.file(file_index_t(-1));
					ec.operation = operation_t::mkdir;
					return { status_t::fatal_disk_error, save_path };
				}
			}
			else if (err)
			{
				ec.ec = err;
				ec.file(file_index_t(-1));
				ec.operation = operation_t::file_stat;
				return { status_t::fatal_disk_error, save_path };
			}
		}

		// indices of all files we ended up copying. These need to be deleted
		// later
		aux::vector<bool, file_index_t> copied_files(std::size_t(f.num_files()), false);

		// track how far we got in case of an error
		file_index_t file_index{};
		for (auto const i : f.file_range())
		{
			// files moved out to absolute paths are not moved
			if (f.file_absolute_path(i)) continue;

			std::string const old_path = combine_path(save_path, f.file_path(i));
			std::string const new_path = combine_path(new_save_path, f.file_path(i));

			error_code ignore;
			if (flags == move_flags_t::dont_replace && exists(new_path, ignore))
			{
				if (ret == status_t::no_error) ret = status_t::need_full_check;
				continue;
			}

			// TODO: ideally, if we end up copying files because of a move across
			// volumes, the source should not be deleted until they've all been
			// copied. That would let us rollback with higher confidence.
			move_file(old_path, new_path, ec);

			// if the source file doesn't exist. That's not a problem
			// we just ignore that file
			if (ec.ec == boost::system::errc::no_such_file_or_directory)
				ec.ec.clear();
			else if (ec
				&& ec.ec != boost::system::errc::invalid_argument
				&& ec.ec != boost::system::errc::permission_denied)
			{
				// moving the file failed
				// on OSX, the error when trying to rename a file across different
				// volumes is EXDEV, which will make it fall back to copying.
				ec.ec.clear();
				copy_file(old_path, new_path, ec);
				if (!ec) copied_files[i] = true;
			}

			if (ec)
			{
				ec.file(i);
				file_index = i;
				break;
			}
		}

		if (!ec && move_partfile)
		{
			error_code e;
			move_partfile(new_save_path, e);
			if (e)
			{
				ec.ec = e;
				ec.file(torrent_status::error_file_partfile);
				ec.operation = operation_t::partfile_move;
			}
		}

		if (ec)
		{
			// rollback
			while (--file_index >= file_index_t(0))
			{
				// files moved out to absolute paths are not moved
				if (f.file_absolute_path(file_index)) continue;

				// if we ended up copying the file, don't do anything during
				// roll-back
				if (copied_files[file_index]) continue;

				std::string const old_path = combine_path(save_path, f.file_path(file_index));
				std::string const new_path = combine_path(new_save_path, f.file_path(file_index));

				// ignore errors when rolling back
				storage_error ignore;
				move_file(new_path, old_path, ignore);
			}

			return { status_t::fatal_disk_error, save_path };
		}

		// TODO: 2 technically, this is where the transaction of moving the files
		// is completed. This is where the new save_path should be committed. If
		// there is an error in the code below, that should not prevent the new
		// save path to be set. Maybe it would make sense to make the save_path
		// an in-out parameter

		std::set<std::string> subdirs;
		for (auto const i : f.file_range())
		{
			// files moved out to absolute paths are not moved
			if (f.file_absolute_path(i)) continue;

			if (has_parent_path(f.file_path(i)))
				subdirs.insert(parent_path(f.file_path(i)));

			// if we ended up renaming the file instead of moving it, there's no
			// need to delete the source.
			if (copied_files[i] == false) continue;

			std::string const old_path = combine_path(save_path, f.file_path(i));

			// we may still have some files in old save_path
			// eg. if (flags == dont_replace && exists(new_path))
			// ignore errors when removing
			error_code ignore;
			remove(old_path, ignore);
		}

		for (std::string const& s : subdirs)
		{
			error_code err;
			std::string subdir = combine_path(save_path, s);

			while (!path_equal(subdir, save_path) && !err)
			{
				remove(subdir, err);
				subdir = parent_path(subdir);
			}
		}

		return { ret, new_save_path };
	}

	namespace {

	void delete_one_file(std::string const& p, error_code& ec)
	{
		remove(p, ec);

		if (ec == boost::system::errc::no_such_file_or_directory)
			ec.clear();
	}

	}

	void delete_files(file_storage const& fs, std::string const& save_path
		, std::string const& part_file_name, remove_flags_t const options, storage_error& ec)
	{
		if (options & session::delete_files)
		{
			// delete the files from disk
			std::set<std::string> directories;
			using iter_t = std::set<std::string>::iterator;
			for (auto const i : fs.file_range())
			{
				std::string const fp = fs.file_path(i);
				bool const complete = fs.file_absolute_path(i);
				std::string const p = complete ? fp : combine_path(save_path, fp);
				if (!complete)
				{
					std::string bp = parent_path(fp);
					std::pair<iter_t, bool> ret;
					ret.second = true;
					while (ret.second && !bp.empty())
					{
						ret = directories.insert(combine_path(save_path, bp));
						bp = parent_path(bp);
					}
				}
				delete_one_file(p, ec.ec);
				if (ec) { ec.file(i); ec.operation = operation_t::file_remove; }
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
					ec.operation = operation_t::file_remove;
				}
			}
		}

		// when we're deleting "files", we also delete the part file
		if ((options & session::delete_partfile)
			|| (options & session::delete_files))
		{
			error_code error;
			remove(combine_path(save_path, part_file_name), error);
			if (error && error != boost::system::errc::no_such_file_or_directory)
			{
				ec.file(file_index_t(-1));
				ec.ec = error;
				ec.operation = operation_t::file_remove;
			}
		}
	}

namespace {

std::int64_t get_filesize(stat_cache& stat, file_index_t const file_index
	, file_storage const& fs, std::string const& save_path, storage_error& ec)
{
	error_code error;
	std::int64_t const size = stat.get_filesize(file_index, fs, save_path, error);

	if (size >= 0) return size;
	if (error != boost::system::errc::no_such_file_or_directory)
	{
		ec.ec = error;
		ec.file(file_index);
		ec.operation = operation_t::file_stat;
	}
	else
	{
		ec.ec = errors::mismatching_file_size;
		ec.file(file_index);
		ec.operation = operation_t::file_stat;
	}
	return -1;
}

}

	bool verify_resume_data(add_torrent_params const& rd
		, aux::vector<std::string, file_index_t> const& links
		, file_storage const& fs
		, aux::vector<download_priority_t, file_index_t> const& file_priority
		, stat_cache& stat
		, std::string const& save_path
		, storage_error& ec)
	{
#ifdef TORRENT_DISABLE_MUTABLE_TORRENTS
		TORRENT_UNUSED(links);
#else
		bool added_files = false;
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
			for (auto const idx : fs.file_range())
			{
				std::string const& s = links[idx];
				if (s.empty()) continue;

				error_code err;
				std::string file_path = fs.file_path(idx, save_path);
				hard_link(s, file_path, err);
				if (err == boost::system::errc::no_such_file_or_directory)
				{
					// we create directories lazily, so it's possible it hasn't
					// been created yet. Create the directories now and try
					// again
					create_directories(parent_path(file_path), err);

					if (err)
					{
						ec.file(idx);
						ec.operation = operation_t::mkdir;
						return false;
					}

					hard_link(s, file_path, err);
				}

				// if the file already exists, that's not an error
				if (err == boost::system::errc::file_exists)
					continue;

				// TODO: 2 is this risky? The upper layer will assume we have the
				// whole file. Perhaps we should verify that at least the size
				// of the file is correct
				if (err)
				{
					ec.ec = err;
					ec.file(idx);
					ec.operation = operation_t::file_hard_link;
					return false;
				}
				added_files = true;
				stat.set_dirty(idx);
			}
		}
#endif // TORRENT_DISABLE_MUTABLE_TORRENTS

		bool const seed = (rd.have_pieces.size() >= fs.num_pieces()
			&& rd.have_pieces.all_set())
			|| (rd.flags & torrent_flags::seed_mode);

		if (seed)
		{
			for (file_index_t const file_index : fs.file_range())
			{
				if (fs.pad_file_at(file_index)) continue;

				// files with priority zero may not have been saved to disk at their
				// expected location, but is likely to be in a partfile. Just exempt it
				// from checking
				if (file_index < file_priority.end_index()
					&& file_priority[file_index] == dont_download
					&& !(rd.flags & torrent_flags::seed_mode))
					continue;

				std::int64_t const size = get_filesize(stat, file_index, fs
					, save_path, ec);
				if (size < 0) return false;

				if (size < fs.file_size(file_index))
				{
					ec.ec = errors::mismatching_file_size;
					ec.file(file_index);
					ec.operation = operation_t::check_resume;
					return false;
				}
			}
			return true;
		}

#ifndef TORRENT_DISABLE_MUTABLE_TORRENTS
		// always trigger a full recheck when we pull in files from other
		// torrents, via hard links
		// TODO: it would seem reasonable to, instead, set the have_pieces bits
		// for the pieces representing these files, and resume with the normal
		// logic
		if (added_files) return false;
#endif

		// parse have bitmask. Verify that the files we expect to have
		// actually do exist
		piece_index_t const end_piece = std::min(rd.have_pieces.end_index(), fs.end_piece());
		for (piece_index_t i(0); i < end_piece; ++i)
		{
			if (rd.have_pieces.get_bit(i) == false) continue;

			std::vector<file_slice> f = fs.map_block(i, 0, 1);
			TORRENT_ASSERT(!f.empty());

			file_index_t const file_index = f[0].file_index;

			// files with priority zero may not have been saved to disk at their
			// expected location, but is likely to be in a partfile. Just exempt it
			// from checking
			if (file_index < file_priority.end_index()
				&& file_priority[file_index] == dont_download)
				continue;

			if (fs.pad_file_at(file_index)) continue;

			if (get_filesize(stat, file_index, fs, save_path, ec) < 0)
				return false;

			// OK, this file existed, good. Now, skip all remaining pieces in
			// this file. We're just sanity-checking whether the files exist
			// or not.
			peer_request const pr = fs.map_file(file_index
				, fs.file_size(file_index) + 1, 0);
			i = std::max(next(i), pr.piece);
		}
		return true;
	}

	bool has_any_file(
		file_storage const& fs
		, std::string const& save_path
		, stat_cache& cache
		, storage_error& ec)
	{
		for (auto const i : fs.file_range())
		{
			std::int64_t const sz = cache.get_filesize(
				i, fs, save_path, ec.ec);

			if (sz < 0)
			{
				if (ec && ec.ec != boost::system::errc::no_such_file_or_directory)
				{
					ec.file(i);
					ec.operation = operation_t::file_stat;
					cache.clear();
					return false;
				}
				// some files not existing is expected and not an error
				ec.ec.clear();
			}

			if (sz > 0) return true;
		}
		return false;
	}

	int read_zeroes(span<char> buf)
	{
		std::fill(buf.begin(), buf.end(), '\0');
		return int(buf.size());
	}

	int hash_zeroes(hasher& ph, std::int64_t const size)
	{
		std::array<char, 64> zeroes;
		zeroes.fill(0);
		for (std::ptrdiff_t left = std::ptrdiff_t(size); left > 0; left -= zeroes.size())
			ph.update({zeroes.data(), std::min(std::ptrdiff_t(zeroes.size()), left)});
		return int(size);
	}

	void initialize_storage(file_storage const& fs
		, std::string const& save_path
		, stat_cache& sc
		, aux::vector<download_priority_t, file_index_t> const& file_priority
		, std::function<void(file_index_t, storage_error&)> create_file
		, std::function<void(std::string const&, std::string const&, storage_error&)> create_link
		, std::function<void(file_index_t, std::int64_t)> oversized_file
		, storage_error& ec)
	{
		// create zero-sized files
		for (auto const file_index : fs.file_range())
		{
			// ignore files that have priority 0
			if (file_priority.end_index() > file_index
				&& file_priority[file_index] == dont_download)
			{
				continue;
			}

			// ignore pad files
			if (fs.pad_file_at(file_index)) continue;

			// this is just to see if the file exists
			error_code err;
			auto const sz = sc.get_filesize(file_index, fs, save_path, err);

			if (err && err != boost::system::errc::no_such_file_or_directory)
			{
				ec.file(file_index);
				ec.operation = operation_t::file_stat;
				ec.ec = err;
				break;
			}

			auto const fs_file_size = fs.file_size(file_index);
			if (!err && sz > fs_file_size)
			{
				// this file is oversized, alert the client
				oversized_file(file_index, sz);
			}

			// if the file is empty and doesn't already exist, create it
			// deliberately don't truncate files that already exist
			// if a file is supposed to have size 0, but already exists, we will
			// never truncate it to 0.
			if (fs_file_size == 0)
			{
				// create symlinks
				if (fs.file_flags(file_index) & file_storage::flag_symlink)
				{
#if TORRENT_HAS_SYMLINK
					std::string const path = fs.file_path(file_index, save_path);
					// we make the symlink target relative to the link itself
					std::string const target = lexically_relative(
						parent_path(fs.file_path(file_index)), fs.symlink(file_index));
					create_link(target, path, ec);
					if (ec.ec)
					{
						ec.file(file_index);
						return;
					}
#else
					TORRENT_UNUSED(create_link);
#endif
				}
				else if (err == boost::system::errc::no_such_file_or_directory)
				{
					// just creating the file is enough to make it zero-sized. If
					// there's a race here and some other process truncates the file,
					// it's not a problem, we won't access empty files ever again
					ec.ec.clear();
					create_file(file_index, ec);
					if (ec) return;
				}
			}
			ec.ec.clear();
		}
	}

	void create_symlink(std::string const& target, std::string const& link, storage_error& ec)
	{
#if TORRENT_HAS_SYMLINK
		create_directories(parent_path(link), ec.ec);
		if (ec)
		{
			ec.ec = error_code(errno, generic_category());
			ec.operation = operation_t::mkdir;
			return;
		}
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
					ec.operation = operation_t::symlink;
					return;
				}
			}
			else
			{
				ec.ec = error_code(error, generic_category());
				ec.operation = operation_t::symlink;
				return;
			}
		}
#else
		TORRENT_UNUSED(target);
		TORRENT_UNUSED(link);
		TORRENT_UNUSED(ec);
#endif
	}

	void move_file(std::string const& inf, std::string const& newf, storage_error& se)
	{
		se.ec.clear();

		file_status s;
		stat_file(inf, &s, se.ec);
		if (se)
		{
			se.operation = operation_t::file_stat;
			return;
		}

		if (has_parent_path(newf))
		{
			create_directories(parent_path(newf), se.ec);
			if (se)
			{
				se.operation = operation_t::mkdir;
				return;
			}
		}

		rename(inf, newf, se.ec);
		if (se)
			se.operation = operation_t::file_rename;
	}

}}
