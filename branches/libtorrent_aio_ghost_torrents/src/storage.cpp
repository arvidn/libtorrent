/*

Copyright (c) 2003, Arvid Norberg, Daniel Wallin
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

#include "libtorrent/pch.hpp"

#include <ctime>
#include <algorithm>
#include <set>
#include <functional>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/ref.hpp>
#include <boost/bind.hpp>
#include <boost/version.hpp>
#include <boost/scoped_array.hpp>
#if BOOST_VERSION >= 103500
#include <boost/system/system_error.hpp>
#endif

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/config.hpp"
#include "libtorrent/storage.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/file.hpp"
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/file_pool.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/disk_buffer_holder.hpp"
#include "libtorrent/alloca.hpp"
#include "libtorrent/stat_cache.hpp"

#include <cstdio>

//#define TORRENT_PARTIAL_HASH_LOG

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

// for convert_to_wstring and convert_to_native
#include "libtorrent/escape_string.hpp"

#define DEBUG_STORAGE 0

#define DLOG if (DEBUG_STORAGE) fprintf

namespace libtorrent
{
	int copy_bufs(file::iovec_t const* bufs, int bytes, file::iovec_t* target)
	{
		int size = 0;
		int ret = 1;
		for (;;)
		{
			*target = *bufs;
			size += bufs->iov_len;
			if (size >= bytes)
			{
				target->iov_len -= size - bytes;
				return ret;
			}
			++bufs;
			++target;
			++ret;
		}
	}

	void advance_bufs(file::iovec_t*& bufs, int bytes)
	{
		int size = 0;
		for (;;)
		{
			size += bufs->iov_len;
			if (size >= bytes)
			{
				((char*&)bufs->iov_base) += bufs->iov_len - (size - bytes);
				bufs->iov_len = size - bytes;
				return;
			}
			++bufs;
		}
	}

	TORRENT_EXPORT int bufs_size(file::iovec_t const* bufs, int num_bufs)
	{
		int size = 0;
		for (file::iovec_t const* i = bufs, *end(bufs + num_bufs); i < end; ++i)
			size += i->iov_len;
		return size;
	}
	
	void clear_bufs(file::iovec_t const* bufs, int num_bufs)
	{
		for (file::iovec_t const* i = bufs, *end(bufs + num_bufs); i < end; ++i)
			std::memset(i->iov_base, 0, i->iov_len);
	}

#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
	int count_bufs(file::iovec_t const* bufs, int bytes)
	{
		int size = 0;
		int count = 1;
		if (bytes == 0) return 0;
		for (file::iovec_t const* i = bufs;; ++i, ++count)
		{
			size += i->iov_len;
			TORRENT_ASSERT(size <= bytes);
			if (size >= bytes) return count;
		}
	}
#endif

	// TODO: use the info-hash as part of the partfile name
	default_storage::default_storage(file_storage const& fs, file_storage const* mapped
		, std::string const& path, file_pool& fp, storage_mode_t mode
		, std::vector<boost::uint8_t> const& file_prio)
		: m_files(fs)
		, m_file_priority(file_prio)
		, m_pool(fp)
		, m_part_file(path, "." + fs.name() + ".parts", fs.num_pieces(), fs.piece_length())
		, m_allocate_files(mode == storage_mode_allocate)
	{
		if (mapped) m_mapped_files.reset(new file_storage(*mapped));

		TORRENT_ASSERT(m_files.num_files() > 0);
		m_save_path = complete(path);
	}

	default_storage::~default_storage()
	{
		// this may be called from a different
		// thread than the disk thread
		m_pool.release(this);
	}

	void default_storage::set_file_priority(std::vector<boost::uint8_t> const& prio, storage_error& ec)
	{
		// extend our file priorities in case it's truncated
		// the default assumed priority is 1
		if (prio.size() > m_file_priority.size())
			m_file_priority.resize(prio.size(), 1);

		file_storage::iterator file_iter = files().begin();
		for (int i = 0; i < int(prio.size()); ++i, ++file_iter)
		{
			int old_prio = m_file_priority[i];
			int new_prio = prio[i];
			if (old_prio == 0 && new_prio != 0)
			{
				// move stuff out of the part file
				boost::intrusive_ptr<file> f = open_file(file_iter, file::read_write, 0, ec.ec);
				if (ec || !f)
				{
					ec.file = i;
					ec.operation = storage_error::open;
					return;
				}
				m_part_file.export_file(*f, file_iter->offset, file_iter->size, ec.ec);
				if (ec)
				{
					ec.file = i;
					ec.operation = storage_error::write;
					return;
				}
			}
			else if (old_prio != 0 && new_prio == 0)
			{
				// move stuff into the part file
				// this is not implemented yet.
				// pretend that we didn't set the priority to 0.

				std::string fp = files().file_path(*file_iter);
				if (exists(combine_path(m_save_path, fp)))
					new_prio = 1;
/*
				boost::intrusive_ptr<file> f = open_file(file_iter, file::read_only, 0, ec.ec);
				if (ec.ec != boost::system::errc::no_such_file_or_directory)
				{
					if (ec || !f)
					{
						ec.file = i;
						ec.operation = storage_error::open;
						return;
					}
					m_part_file.import_file(*f, file_iter->offset, file_iter->size, ec.ec);
					if (ec)
					{
						ec.file = i;
						ec.operation = storage_error::read;
						return;
					}
					// remove the file
					std::string fp = files().file_path(*file_iter);
					std::string p = combine_path(m_save_path, fp);
					delete_one_file(p, ec.ec);
					if (ec)
					{
						ec.file = i;
						ec.operation = storage_error::remove;
					}
				}
*/
			}
			ec.ec.clear();
			m_file_priority[i] = new_prio;
		}
		m_part_file.flush_metadata(ec.ec);
		if (ec)
		{
			ec.file = -1;
			ec.operation = storage_error::partfile;
		}
	}

	void default_storage::initialize(storage_error& ec)
	{
		m_stat_cache.init(files().num_files());

		// first, create all missing directories
		std::string last_path;
		int file_index = 0;
		std::string file_path;
		for (file_storage::iterator file_iter = files().begin(),
			end_iter = files().end(); file_iter != end_iter; ++file_iter, ++file_index)
		{
			// ignore files that have priority 0
			if (int(m_file_priority.size()) > file_index
				&& m_file_priority[file_index] == 0)
			{
				continue;
			}

			// ignore pad files
			if (file_iter->pad_file)
			{
				continue;
			}

			if (m_stat_cache.get_filesize(file_index) == stat_cache::not_in_cache)
			{
				file_path = combine_path(m_save_path, files().file_path(*file_iter));

				file_status s;
				stat_file(file_path, &s, ec.ec);
				if (ec && ec.ec != boost::system::errc::no_such_file_or_directory
					&& ec.ec != boost::system::errc::not_a_directory)
				{
					m_stat_cache.set_error(file_index);
					ec.file = file_index;
					ec.operation = storage_error::stat;
					break;
				}
				m_stat_cache.set_cache(file_index, s.file_size, s.mtime);
			}

			// ec is either ENOENT or the file existed and s is valid
			// allocate file only if it is not exist and (m_allocate_files == true)
			// if the file already exists, but is larger than what
			// it's supposed to be, also truncate it
			// if the file is empty, just create it either way.
			if ((ec && m_allocate_files)
				|| (!ec && m_stat_cache.get_filesize(file_index) > file_iter->size)
				|| file_iter->size == 0)
			{
				std::string dir = parent_path(file_path);

				if (dir != last_path)
				{
					last_path = dir;

					create_directories(last_path, ec.ec);
					if (ec.ec)
					{
						ec.file = file_index;
						ec.operation = storage_error::mkdir;
						break;
					}
				}
				ec.ec.clear();
				boost::intrusive_ptr<file> f = open_file(file_iter, file::read_write, 0, ec.ec);
				if (ec || !f)
				{
					ec.file = file_index;
					ec.operation = storage_error::open;
					return;
				}
				f->set_size(file_iter->size, ec.ec);
				if (ec)
				{
					ec.file = file_index;
					ec.operation = storage_error::fallocate;
					break;
				}
			}
			ec.ec.clear();
		}

		// close files that were opened in write mode
		m_pool.release(this);
	}

	void default_storage::finalize_file(int index, storage_error& ec)
	{
		TORRENT_ASSERT(index >= 0 && index < files().num_files());
		if (index < 0 || index >= files().num_files()) return;
	
		boost::intrusive_ptr<file> f = open_file(files().begin() + index, file::read_write, 0, ec.ec);
		if (ec || !f)
		{
			ec.file = index;
			ec.operation = storage_error::open;
			return;
		}

		f->finalize();
	}

	bool default_storage::has_any_file(storage_error& ec)
	{
		file_storage::iterator i = files().begin();
		file_storage::iterator end = files().end();

		m_stat_cache.init(files().num_files());

		std::string file_path;
		int index = 0;
		for (; i != end; ++i, ++index)
		{
			file_status s;
			size_type cache_status = m_stat_cache.get_filesize(index);
			if (cache_status < 0 && cache_status != stat_cache::no_exist)
			{
				file_path = combine_path(m_save_path, files().file_path(*i));
				stat_file(file_path, &s, ec.ec);
				size_type r = s.file_size;
				if (ec.ec || !(s.mode & file_status::regular_file)) r = -1;

				if (ec && ec.ec == boost::system::errc::no_such_file_or_directory)
				{
					ec.ec.clear();
					r = -3;
				}
				m_stat_cache.set_cache(index, r, s.mtime);

				if (ec)
				{
					ec.file = index;
					ec.operation = storage_error::stat;
					return false;
				}
			}
	
			// if we didn't find the file, check the next one
			if (m_stat_cache.get_filesize(index) == stat_cache::no_exist) continue;

			if (m_stat_cache.get_filesize(index) > 0)
				return true;
		}
		return false;
	}

	void default_storage::rename_file(int index, std::string const& new_filename, storage_error& ec)
	{
		if (index < 0 || index >= files().num_files()) return;
		std::string old_name = combine_path(m_save_path, files().file_path(files().at(index)));
		m_pool.release(this, index);

		rename(old_name, combine_path(m_save_path, new_filename), ec.ec);
		
		if (ec.ec == boost::system::errc::no_such_file_or_directory)
			ec.ec.clear();

		if (ec)
		{
			ec.file = index;
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

	void default_storage::release_files(storage_error& ec)
	{
		m_pool.release(this);
	}

	void default_storage::delete_one_file(std::string const& p, error_code& ec)
	{
		remove(p, ec);
		
		if (ec == boost::system::errc::no_such_file_or_directory)
			ec.clear();
	}

	void default_storage::delete_files(storage_error& ec)
	{
		// make sure we don't have the files open
		m_pool.release(this);

		// delete the files from disk
		std::set<std::string> directories;
		typedef std::set<std::string>::iterator iter_t;
		for (file_storage::iterator i = files().begin()
			, end(files().end()); i != end; ++i)
		{
			std::string fp = files().file_path(*i);
			std::string p = combine_path(m_save_path, fp);
			std::string bp = parent_path(fp);
			std::pair<iter_t, bool> ret;
			ret.second = true;
			while (ret.second && !bp.empty())
			{
				ret = directories.insert(combine_path(m_save_path, bp));
				bp = parent_path(bp);
			}
			delete_one_file(p, ec.ec);
			if (ec) { ec.file = i - files().begin(); ec.operation = storage_error::remove; }
		}

		// remove the directories. Reverse order to delete
		// subdirectories first

		for (std::set<std::string>::reverse_iterator i = directories.rbegin()
			, end(directories.rend()); i != end; ++i)
		{
			delete_one_file(*i, ec.ec);
			if (ec) { ec.file = -1; ec.operation = storage_error::remove; }
		}
	}

	void default_storage::write_resume_data(entry& rd, storage_error& ec) const
	{
		TORRENT_ASSERT(rd.type() == entry::dictionary_t);

		entry::list_type& fl = rd["file sizes"].list();

		file_storage const& fs = files();
		int index = 0;
		for (file_storage::iterator i = fs.begin(); i != fs.end(); ++i, ++index)
		{
			size_type file_size = 0;
			time_t file_time = 0;
			size_type cache_state = m_stat_cache.get_filesize(index);
			if (cache_state != stat_cache::not_in_cache)
			{
				if (cache_state >= 0)
				{
					file_size = cache_state;
					file_time = m_stat_cache.get_filetime(index);
				}
			}
			else
			{
				file_status s;
				error_code ec;
				stat_file(combine_path(m_save_path, fs.file_path(*i)), &s, ec);
				if (!ec)
				{
					file_size = s.file_size;
					file_time = s.mtime;
				}
				else
				{
					if (ec == boost::system::errc::no_such_file_or_directory)
					{
						m_stat_cache.set_noexist(index);
					}
					else
					{
						m_stat_cache.set_error(index);
					}
				}
			}

			fl.push_back(entry(entry::list_t));
			entry::list_type& p = fl.back().list();
			p.push_back(entry(file_size));
			p.push_back(entry(file_time));
		}
	}

	int default_storage::sparse_end(int slot) const
	{
		TORRENT_ASSERT(slot >= 0);
		TORRENT_ASSERT(slot < m_files.num_pieces());

		size_type file_offset = (size_type)slot * m_files.piece_length();
		file_storage::iterator file_iter;

		for (file_iter = files().begin();;)
		{
			if (file_offset < file_iter->size)
				break;

			file_offset -= file_iter->size;
			++file_iter;
			TORRENT_ASSERT(file_iter != files().end());
		}
	
		error_code ec;
		boost::intrusive_ptr<file> file_handle = open_file(file_iter, file::read_only, 0, ec);
		if (!file_handle || ec) return slot;

		size_type data_start = file_handle->sparse_end(file_offset);
		return int((data_start + m_files.piece_length() - 1) / m_files.piece_length());
	}

	bool default_storage::verify_resume_data(lazy_entry const& rd, storage_error& ec)
	{
		// TODO: make this more generic to not just work if files have been
		// renamed, but also if they have been merged into a single file for instance
		// maybe use the same format as .torrent files and reuse some code from torrent_info
		lazy_entry const* mapped_files = rd.dict_find_list("mapped_files");
		if (mapped_files && mapped_files->list_size() == m_files.num_files())
		{
			m_mapped_files.reset(new file_storage(m_files));
			for (int i = 0; i < m_files.num_files(); ++i)
			{
				std::string new_filename = mapped_files->list_string_value_at(i);
				if (new_filename.empty()) continue;
				m_mapped_files->rename_file(i, new_filename);
			}
		}
		
		lazy_entry const* file_priority = rd.dict_find_list("file_priority");
		if (file_priority && file_priority->list_size()
			== files().num_files())
		{
			m_file_priority.resize(file_priority->list_size());
			for (int i = 0; i < file_priority->list_size(); ++i)
				m_file_priority[i] = boost::uint8_t(file_priority->list_int_value_at(i, 1));
		}

		lazy_entry const* file_sizes_ent = rd.dict_find_list("file sizes");
		if (file_sizes_ent == 0)
		{
			ec.ec = errors::missing_file_sizes;
			return false;
		}
		
		if (file_sizes_ent->list_size() == 0)
		{
			ec.ec = errors::no_files_in_resume_data;
			return false;
		}
		
		file_storage const& fs = files();
		if (file_sizes_ent->list_size() != fs.num_files())
		{
			ec.ec = errors::mismatching_number_of_files;
			ec.file = -1;
			ec.operation = storage_error::none;
			return false;
		}

		bool seed = false;
		lazy_entry const* slots = rd.dict_find_list("slots");
		if (slots)
		{
			if (int(slots->list_size()) == m_files.num_pieces())
			{
				seed = true;
				for (int i = 0; i < slots->list_size(); ++i)
				{
					if (slots->list_int_value_at(i, -1) >= 0) continue;
					seed = false;
					break;
				}
			}
		}
		else if (lazy_entry const* pieces = rd.dict_find_string("pieces"))
		{
			if (int(pieces->string_length()) == m_files.num_pieces())
			{
				seed = true;
				char const* p = pieces->string_ptr();
				for (int i = 0; i < pieces->string_length(); ++i)
				{
					if ((p[i] & 1) == 1) continue;
					seed = false;
					break;
				}
			}
		}
		else
		{
			ec.ec = errors::missing_pieces;
			return false;
		}

		file_storage::iterator file_iter = fs.begin();
		for (int i = 0; i < file_sizes_ent->list_size(); ++i, ++file_iter)
		{
			if (file_iter->pad_file) continue;
			lazy_entry const* e = file_sizes_ent->list_at(i);
			if (e->type() != lazy_entry::list_t
				|| e->list_size() < 2
				|| e->list_at(0)->type() != lazy_entry::int_t
				|| e->list_at(1)->type() != lazy_entry::int_t)
			{
				ec.ec = errors::missing_file_sizes;
				ec.file = i;
				ec.operation = storage_error::none;
				return false;
			}

			size_type expected_size = e->list_int_value_at(0);
			time_t expected_time = e->list_int_value_at(1);

			// if we're a seed, the expected size should match
			// the actual full size according to the torrent
			if (seed && expected_size < file_iter->size)
			{
				ec.ec = errors::mismatching_file_size;
				ec.file = i;
				ec.operation = storage_error::none;
				return false;
			}

			size_type file_size = m_stat_cache.get_filesize(i);
			time_t file_time;
			if (file_size >= 0)
			{
				file_time = m_stat_cache.get_filetime(i);
			}
			else
			{
				file_status s;
				error_code error;
				std::string file_path = combine_path(m_save_path, fs.file_path(*file_iter));
				stat_file(file_path, &s, error);
				file_size = s.file_size;
				file_time = s.mtime;
				if (error)
				{
					if (error != boost::system::errc::no_such_file_or_directory)
					{
						m_stat_cache.set_error(i);
						ec.ec = error;
						ec.file = i;
						ec.operation = storage_error::stat;
						return false;
					}
					m_stat_cache.set_noexist(i);
					if (expected_size != 0)
					{
						ec.ec = errors::mismatching_file_size;
						ec.file = i;
						ec.operation = storage_error::none;
						return false;
					}
				}
			}

			if (expected_size > file_size)
			{
				ec.ec = errors::mismatching_file_size;
				ec.file = i;
				ec.operation = storage_error::none;
				return false;
			}

			if (settings().get_bool(settings_pack::ignore_resume_timestamps)) continue;

			// allow some slack, because of FAT volumes
			if (file_time > expected_time + 5 * 60 || file_time < expected_time - 5)
			{
				ec.ec = errors::mismatching_file_timestamp;
				ec.file = i;
				ec.operation = storage_error::stat;
				return false;
			}
		}

		bool full_allocation_mode = false;
		if (rd.dict_find_string_value("allocation") != "compact")
			full_allocation_mode = true;

		return true;
	}

	void default_storage::move_storage(std::string const& sp, storage_error& ec)
	{
		std::string save_path = complete(sp);

		file_status s;
		stat_file(save_path, &s, ec.ec);
		if (ec.ec == boost::system::errc::no_such_file_or_directory)
		{
			create_directories(save_path, ec.ec);
			if (ec)
			{
				ec.file = -1;
				ec.operation = storage_error::mkdir;
				return;
			}
		}
		else if (ec)
		{
			ec.file = -1;
			ec.operation = storage_error::mkdir;
			return;
		}
		ec.ec.clear();

		m_pool.release(this);

		std::map<std::string, int> to_move;
		file_storage const& f = files();

		for (file_storage::iterator i = f.begin()
			, end(f.end()); i != end; ++i)
		{
			std::string split = split_path(f.file_path(*i));
			to_move.insert(to_move.begin(), std::make_pair(split, int(i - f.begin())));
		}

		for (std::map<std::string, int>::const_iterator i = to_move.begin()
			, end(to_move.end()); i != end; ++i)
		{
			std::string old_path = combine_path(m_save_path, i->first);
			std::string new_path = combine_path(save_path, i->first);

			rename(old_path, new_path, ec.ec);
			if (ec.ec == boost::system::errc::no_such_file_or_directory)
				ec.ec.clear();

			if (ec)
			{
				ec.ec.clear();
				recursive_copy(old_path, new_path, ec.ec);
				if (!ec)
				{
					// ignore errors when removing
					error_code e;
					remove_all(old_path, e);
				}
				else
				{
					ec.file = i->second;
					ec.operation = storage_error::copy;
				}
				break;
			}
		}

		if (!ec)
		{
			m_part_file.move_partfile(save_path, ec.ec);
			if (ec)
			{
				ec.file = -1;
				ec.operation = storage_error::partfile;
				return;
			}

			m_save_path = save_path;
		}
	}

	int default_storage::readv(file::iovec_t const* bufs, int num_bufs
		, int slot, int offset, int flags, storage_error& ec)
	{
		fileop op = { &file::readv
			, m_settings ? settings().get_int(settings_pack::disk_io_read_mode) : 0, file::read_only };
#ifdef TORRENT_SIMULATE_SLOW_READ
		boost::thread::sleep(boost::get_system_time()
			+ boost::posix_time::milliseconds(1000));
#endif
		return readwritev(bufs, slot, offset, num_bufs, op, ec);
	}

	int default_storage::writev(file::iovec_t const* bufs, int num_bufs
		, int slot, int offset, int flags, storage_error& ec)
	{
		fileop op = { &file::writev
			, m_settings ? settings().get_int(settings_pack::disk_io_write_mode) : 0, file::read_write };
		return readwritev(bufs, slot, offset, num_bufs, op, ec);
	}

	// much of what needs to be done when reading and writing 
	// is buffer management and piece to file mapping. Most
	// of that is the same for reading and writing. This function
	// is a template, and the fileop decides what to do with the
	// file and the buffers.
	int default_storage::readwritev(file::iovec_t const* bufs, int slot, int offset
		, int num_bufs, fileop const& op, storage_error& ec)
	{
		TORRENT_ASSERT(bufs != 0);
		TORRENT_ASSERT(slot >= 0);
		TORRENT_ASSERT(slot < m_files.num_pieces());
		TORRENT_ASSERT(offset >= 0);
		TORRENT_ASSERT(offset < m_files.piece_size(slot));
		TORRENT_ASSERT(num_bufs > 0);

		int size = bufs_size(bufs, num_bufs);
		TORRENT_ASSERT(size > 0);

#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		std::vector<file_slice> slices
			= files().map_block(slot, offset, size);
		TORRENT_ASSERT(!slices.empty());
#endif

		size_type start = slot * (size_type)m_files.piece_length() + offset;
		TORRENT_ASSERT(start + size <= m_files.total_size());

		// find the file iterator and file offset
		size_type file_offset = start;
		file_storage::iterator file_iter;

		// TODO: use binary search!
		int file_index = 0;
		for (file_iter = files().begin();;)
		{
			if (file_offset < file_iter->size)
				break;

			++file_index;
			file_offset -= file_iter->size;
			++file_iter;
			TORRENT_ASSERT(file_iter != files().end());
		}

		int buf_pos = 0;

		boost::intrusive_ptr<file> file_handle;
		int bytes_left = size;
		int slot_size = static_cast<int>(m_files.piece_size(slot));

		if (offset + bytes_left > slot_size)
			bytes_left = slot_size - offset;

		TORRENT_ASSERT(bytes_left >= 0);

#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		int counter = 0;
#endif

		file::iovec_t* tmp_bufs = TORRENT_ALLOCA(file::iovec_t, num_bufs);
		file::iovec_t* current_buf = TORRENT_ALLOCA(file::iovec_t, num_bufs);
		copy_bufs(bufs, size, current_buf);
		TORRENT_ASSERT(count_bufs(current_buf, size) == num_bufs);
		int file_bytes_left;
		for (;bytes_left > 0; ++file_iter, bytes_left -= file_bytes_left
			, buf_pos += file_bytes_left, ++file_index)
		{
			TORRENT_ASSERT(file_iter != files().end());
			TORRENT_ASSERT(buf_pos >= 0);

			file_bytes_left = bytes_left;
			if (file_offset + file_bytes_left > file_iter->size)
				file_bytes_left = (std::max)(static_cast<int>(file_iter->size - file_offset), 0);

			if (file_bytes_left == 0) continue;

#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
			TORRENT_ASSERT(int(slices.size()) > counter);
			size_type slice_size = slices[counter].size;
			TORRENT_ASSERT(slice_size == file_bytes_left);
			TORRENT_ASSERT((files().begin() + slices[counter].file_index)
				== file_iter);
			++counter;
#endif

			if (file_iter->pad_file)
			{
				if (op.mode == file::read_only)
				{
					int num_tmp_bufs = copy_bufs(current_buf, file_bytes_left, tmp_bufs);
					TORRENT_ASSERT(count_bufs(tmp_bufs, file_bytes_left) == num_tmp_bufs);
					TORRENT_ASSERT(num_tmp_bufs <= num_bufs);
					clear_bufs(tmp_bufs, num_tmp_bufs);
				}
				advance_bufs(current_buf, file_bytes_left);
				TORRENT_ASSERT(count_bufs(current_buf, bytes_left - file_bytes_left) <= num_bufs);
				file_offset = 0;
				continue;
			}

			int num_tmp_bufs = copy_bufs(current_buf, file_bytes_left, tmp_bufs);
			TORRENT_ASSERT(count_bufs(tmp_bufs, file_bytes_left) == num_tmp_bufs);
			TORRENT_ASSERT(num_tmp_bufs <= num_bufs);
			int bytes_transferred = 0;
			error_code e;

			if (file_index < int(m_file_priority.size())
				&& m_file_priority[file_index] == 0)
			{
				if (op.mode == file::read_write)
				{
					// write
					bytes_transferred = m_part_file.writev(tmp_bufs, num_tmp_bufs, slot, offset, e);
				}
				else
				{
					// read
					bytes_transferred = m_part_file.readv(tmp_bufs, num_tmp_bufs, slot, offset, e);
				}
			}
			else
			{
				file_handle = open_file(file_iter, op.mode, op.flags, e);
				if ((op.mode == file::read_write) && e == boost::system::errc::no_such_file_or_directory)
				{
					// this means the directory the file is in doesn't exist.
					// so create it
					e.clear();
					std::string path = combine_path(m_save_path, files().file_path(*file_iter));
					create_directories(parent_path(path), e);
					// if the directory creation failed, don't try to open the file again
					// but actually just fail
					if (!e) file_handle = open_file(file_iter, op.mode, op.flags, e);
				}

				if (!file_handle || e)
				{
					ec.ec = e;
					ec.file = file_iter - files().begin();
					ec.operation = storage_error::open;
					return -1;
				}

				size_type adjusted_offset = files().file_base(*file_iter) + file_offset;
				bytes_transferred = (int)((*file_handle).*op.op)(adjusted_offset
					, tmp_bufs, num_tmp_bufs, e, op.flags);
				TORRENT_ASSERT(bytes_transferred <= bufs_size(tmp_bufs, num_tmp_bufs));
			}
			file_offset = 0;

			if (e)
			{
				ec.ec = e;
				ec.file = file_iter - files().begin();
				ec.operation = op.mode == file::read_only ? storage_error::read : storage_error::write;
				return -1;
			}

			if (file_bytes_left != bytes_transferred)
				return bytes_transferred;

			advance_bufs(current_buf, bytes_transferred);
			TORRENT_ASSERT(count_bufs(current_buf, bytes_left - file_bytes_left) <= num_bufs);
		}
		return size;
	}


	boost::intrusive_ptr<file> default_storage::open_file(file_storage::iterator fe, int mode
		, int flags, error_code& ec) const
	{
		if (!(flags & file::sequential_access))
			mode |= file::random_access;

		bool lock_files = m_settings ? settings().get_bool(settings_pack::lock_files) : false;
		if (lock_files) mode |= file::lock_file;
		if (!m_allocate_files) mode |= file::sparse;
		if (m_settings && settings().get_bool(settings_pack::no_atime_storage)) mode |= file::no_atime;

		// if we have a cache already, don't store the data twice by leaving it in the OS cache as well
		if (m_settings && settings().get_bool(settings_pack::use_read_cache)) mode |= file::no_cache;

		return m_pool.open_file(const_cast<default_storage*>(this), m_save_path, fe, files(), mode, ec);
	}

	storage_interface* default_storage_constructor(file_storage const& fs
		, file_storage const* mapped, std::string const& path, file_pool& fp
		, storage_mode_t mode, std::vector<boost::uint8_t> const& file_prio)
	{
		return new default_storage(fs, mapped, path, fp, mode, file_prio);
	}

	int disabled_storage::readv(file::iovec_t const* bufs
		, int num_bufs, int slot, int offset, int flags, storage_error& ec)
	{
		return 0;
	}

	int disabled_storage::writev(file::iovec_t const* bufs
		, int num_bufs, int slot, int offset, int flags, storage_error& ec)
	{
		return 0;
	}

	storage_interface* disabled_storage_constructor(file_storage const& fs
		, file_storage const* mapped, std::string const& path, file_pool& fp
		, storage_mode_t mode, std::vector<boost::uint8_t> const&)
	{
		return new disabled_storage(fs.piece_length());
	}

	// -- zero_storage ------------------------------------------------------

	int zero_storage::readv(file::iovec_t const* bufs, int num_bufs
		, int piece, int offset, int flags, storage_error& ec)
	{
		int ret = 0;
		for (int i = 0; i < num_bufs; ++i)
		{
			memset(bufs[i].iov_base, 0, bufs[i].iov_len);
			ret += bufs[i].iov_len;
		}
		return 0;
	}

	int zero_storage::writev(file::iovec_t const* bufs, int num_bufs
		, int piece, int offset, int flags, storage_error& ec)
	{
		int ret = 0;
		for (int i = 0; i < num_bufs; ++i)
			ret += bufs[i].iov_len;
		return 0;
	}

	storage_interface* zero_storage_constructor(file_storage const& fs
		, file_storage const* mapped, std::string const& path, file_pool& fp
		, storage_mode_t mode, std::vector<boost::uint8_t> const&)
	{
		return new zero_storage;
	}

	// -- piece_manager -----------------------------------------------------

	piece_manager::piece_manager(
		storage_interface* storage_impl
		, boost::shared_ptr<void> const& torrent
		, file_storage* files)
		: m_files(*files)
		, m_storage(storage_impl)
		, m_torrent(torrent)
	{
	}

	piece_manager::~piece_manager()
	{}

	void piece_manager::add_piece(cached_piece_entry* p)
	{
		TORRENT_ASSERT(m_cached_pieces.count(p) == 0);
		m_cached_pieces.insert(p);
	}

	bool piece_manager::has_piece(cached_piece_entry* p) const
	{
		return m_cached_pieces.count(p) > 0;
	}

	void piece_manager::remove_piece(cached_piece_entry* p)
	{
		TORRENT_ASSERT(m_cached_pieces.count(p) == 1);
		m_cached_pieces.erase(p);
	}

	// used in torrent_handle.cpp
	void piece_manager::write_resume_data(entry& rd, storage_error& ec) const
	{
		INVARIANT_CHECK;
		m_storage->write_resume_data(rd, ec);
	}

	int piece_manager::check_no_fastresume(storage_error& ec)
	{
		bool has_files = false;
		if (!m_storage->settings().get_bool(settings_pack::no_recheck_incomplete_resume))
		{
			storage_error se;
			has_files = m_storage->has_any_file(se);

			if (se)
			{
				ec = se;
				return fatal_disk_error; 
			}

			if (has_files) return need_full_check;
		}

		return check_init_storage(ec);
	}
	
	int piece_manager::check_init_storage(storage_error& ec)
	{
		storage_error se;
		// TODO: change the initialize signature and let the
		// storage_impl be responsible for which storage mode
		// it's using
		m_storage->initialize(se);
		if (se)
		{
			ec = se;
			return fatal_disk_error; 
		}

		return no_error;
	}

	// check if the fastresume data is up to date
	// if it is, use it and return true. If it 
	// isn't return false and the full check
	// will be run
	int piece_manager::check_fastresume(
		lazy_entry const& rd, storage_error& ec)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(m_files.piece_length() > 0);
		
		// if we don't have any resume data, return
		if (rd.type() == lazy_entry::none_t) return check_no_fastresume(ec);

		if (rd.type() != lazy_entry::dict_t)
		{
			ec.ec = errors::not_a_dictionary;
			return check_no_fastresume(ec);
		}

		int block_size = (std::min)(16 * 1024, m_files.piece_length());
		int blocks_per_piece = int(rd.dict_find_int_value("blocks per piece", -1));
		if (blocks_per_piece != -1
			&& blocks_per_piece != m_files.piece_length() / block_size)
		{
			ec.ec = errors::invalid_blocks_per_piece;
			return check_no_fastresume(ec);
		}

		if (!m_storage->verify_resume_data(rd, ec))
			return check_no_fastresume(ec);

		return check_init_storage(ec);
	}

	// ====== disk_job_fence implementation ========

	disk_job_fence::disk_job_fence()
		: m_has_fence(0)
		, m_outstanding_jobs(0)
	{}

	int disk_job_fence::job_complete(disk_io_job* j, tailqueue& jobs)
	{
		mutex::scoped_lock l(m_mutex);

		TORRENT_ASSERT(j->flags & disk_io_job::in_progress);
		j->flags &= ~disk_io_job::in_progress;

		TORRENT_ASSERT(m_outstanding_jobs > 0);
		--m_outstanding_jobs;
		if (j->flags & disk_io_job::fence)
		{
			// a fence job just completed. Make sure the fence logic
			// works by asserting m_outstanding_jobs is in fact 0 now
			TORRENT_ASSERT(m_outstanding_jobs == 0);

			// the fence can now be lowered
			--m_has_fence;

			// now we need to post all jobs that have been queued up
			// while this fence was up. However, if there's another fence
			// in the queue, stop there and raise the fence again
			int ret = 0;
			while (m_blocked_jobs.size())
			{
				disk_io_job *bj = (disk_io_job*)m_blocked_jobs.pop_front();
				if (bj->flags & disk_io_job::fence)
				{
					// we encountered another fence. We cannot post anymore
					// jobs from the blocked jobs queue. We have to go back
					// into a raised fence mode and wait for all current jobs
					// to complete. The exception is that if there are no jobs
					// executing currently, we should add the fence job.
					if (m_outstanding_jobs == 0 && jobs.empty())
					{
						TORRENT_ASSERT((bj->flags & disk_io_job::in_progress) == 0);
						bj->flags |= disk_io_job::in_progress;
						++m_outstanding_jobs;
						++ret;
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
						TORRENT_ASSERT(bj->blocked);
						bj->blocked = false;
#endif
						jobs.push_back(bj);
					}
					else
					{
						// put the fence job back in the blocked queue
						m_blocked_jobs.push_front(bj);
					}
					return ret;
				}
				TORRENT_ASSERT((bj->flags & disk_io_job::in_progress) == 0);
				bj->flags |= disk_io_job::in_progress;

				++m_outstanding_jobs;
				++ret;
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
				TORRENT_ASSERT(bj->blocked);
				bj->blocked = false;
#endif
				jobs.push_back(bj);
			}
			return ret;
		}

		// there are still outstanding jobs, even if we have a
		// fence, it's not time to lower it yet
		// also, if we don't have a fence, we're done
		if (m_outstanding_jobs > 0 || m_has_fence == 0) return 0;

		// there's a fence raised, and no outstanding operations.
		// it means we can execute the fence job right now.
		TORRENT_ASSERT(m_blocked_jobs.size() > 0);

		// this is the fence job
		disk_io_job *bj = (disk_io_job*)m_blocked_jobs.pop_front();
		TORRENT_ASSERT(bj->flags & disk_io_job::fence);

		TORRENT_ASSERT((bj->flags & disk_io_job::in_progress) == 0);
		bj->flags |= disk_io_job::in_progress;

		++m_outstanding_jobs;
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		TORRENT_ASSERT(bj->blocked);
		bj->blocked = false;
#endif
		jobs.push_back(bj);
		return 1;
	}

	bool disk_job_fence::is_blocked(disk_io_job* j, bool ignore_fence)
	{
		mutex::scoped_lock l(m_mutex);
		DLOG(stderr, "[%p] is_blocked: fence: %d num_outstanding: %d\n"
			, this, m_has_fence, int(m_outstanding_jobs));

		// if this is the job that raised the fence, don't block it
		// ignore fence can only ignore one fence. If there are several,
		// this job still needs to get queued up
		if ((ignore_fence && m_has_fence <= 1) || m_has_fence == 0)
		{
			TORRENT_ASSERT((j->flags & disk_io_job::in_progress) == 0);
			j->flags |= disk_io_job::in_progress;
			++m_outstanding_jobs;
			return false;
		}
		m_blocked_jobs.push_back(j);

#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		TORRENT_ASSERT(j->blocked == false);
		j->blocked = true;
#endif

		return true;
	}

	bool disk_job_fence::has_fence() const
	{
		mutex::scoped_lock l(m_mutex);
		return m_has_fence;
	}

	int disk_job_fence::num_blocked() const
	{
		mutex::scoped_lock l(m_mutex);
		return m_blocked_jobs.size();
	}

	// j is the fence job. It must have exclusive access to the storage
	// fj is the flush job. If the job j is queued, we need to issue
	// this job
	int disk_job_fence::raise_fence(disk_io_job* j, disk_io_job* fj, atomic_count* blocked_counter)
	{
		TORRENT_ASSERT((j->flags & disk_io_job::fence) == 0);
		j->flags |= disk_io_job::fence;

		mutex::scoped_lock l(m_mutex);

		DLOG(stderr, "[%p] raise_fence: fence: %d num_outstanding: %d\n"
			, this, m_has_fence, int(m_outstanding_jobs));

		if (m_has_fence == 0 && m_outstanding_jobs == 0)
		{
			++m_has_fence;
			DLOG(stderr, "[%p] raise_fence: need posting\n", this);

			// the job j is expected to be put on the job queue
			// after this, without being passed through is_blocked()
			// that's why we're accounting for it here

			// fj is expected to be discarded by the caller
			j->flags |= disk_io_job::in_progress;
			++m_outstanding_jobs;
			return fence_post_fence;
		}

		++m_has_fence;
		if (m_has_fence > 1)
		{
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
			TORRENT_ASSERT(fj->blocked == false);
			fj->blocked = true;
#endif
			m_blocked_jobs.push_back(fj);
			++*blocked_counter;
		}
		else
		{
			// in this case, fj is expected to be put on the job queue
			fj->flags |= disk_io_job::in_progress;
			++m_outstanding_jobs;
		}
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		TORRENT_ASSERT(j->blocked == false);
		j->blocked = true;
#endif
		m_blocked_jobs.push_back(j);
		++*blocked_counter;

		return m_has_fence > 1 ? fence_post_none : fence_post_flush;
	}

#ifdef TORRENT_DEBUG
	void piece_manager::check_invariant() const {}
#endif
} // namespace libtorrent

