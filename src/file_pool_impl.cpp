/*

Copyright (c) 2016, 2019-2021, Arvid Norberg
Copyright (c) 2019, Steven Siloti
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#define TRACE_FILE_POOL 0

#include "libtorrent/assert.hpp"
#include "libtorrent/aux_/file_pool_impl.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/units.hpp"
#include "libtorrent/disk_interface.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/aux_/throw.hpp"
#ifdef TORRENT_WINDOWS
#include "libtorrent/aux_/win_util.hpp"
#endif

#include <limits>

#if TRACE_FILE_POOL
#include <iostream>
#include <thread>
#endif


using namespace libtorrent::flags;

namespace libtorrent::aux {

	template <typename FileEntry>
	typename file_pool_impl<FileEntry>::FileHandle
	file_pool_impl<FileEntry>::open_file(storage_index_t st, std::string const& p
		, file_index_t const file_index, filenames const& fn
		, open_mode_t const m
#if TORRENT_HAVE_MAP_VIEW_OF_FILE
		, typename FileEntry::mutex_type open_unmap_lock
#endif
		)
	{
		// potentially used to hold a reference to a file object that's
		// about to be destructed. If we have such object we assign it to
		// this member to be destructed after we release the std::mutex. On some
		// operating systems (such as OSX) closing a file may take a long
		// time. We don't want to hold the std::mutex for that.
		FileHandle defer_destruction1;
		FileHandle defer_destruction2;

		std::unique_lock<std::mutex> l(m_mutex);

		TORRENT_ASSERT(is_complete(p));
		auto& key_view = m_files.template get<0>();
		file_id const file_key{st, file_index};
		auto i = key_view.find(file_key);

		if (i == key_view.end())
		{
			auto opening = std::find_if(m_opening_files.begin(), m_opening_files.end()
				, [&file_key, m](opening_file_entry const& oe) {
					return oe.file_key == file_key
						&& (!(m & open_mode::write) || (oe.mode & open_mode::write));
				});
			if (opening != m_opening_files.end())
			{
				wait_open_entry woe;
				opening->waiters.push_back(woe);

#if TRACE_FILE_POOL
				std::cout << std::this_thread::get_id() << " waiting for: ("
					<< file_key.first << ", " << file_key.second << ")\n";
#endif
				do {
					woe.cond.wait(l);
				} while (!woe.mapping && !woe.error);
				if (woe.error)
				{
#if TRACE_FILE_POOL
					std::cout << std::this_thread::get_id() << " open failed: ("
						<< file_key.first << ", " << file_key.second
						<< "): " << woe.error.ec << std::endl;
#endif
					throw_ex<storage_error>(woe.error);
				}

#if TRACE_FILE_POOL
				std::cout << std::this_thread::get_id() << " file opened: ("
					<< file_key.first << ", " << file_key.second << ")\n";
#endif
				return woe.mapping;
			}
		}

		// make sure the write bit is set if we asked for it
		// it's OK to use a read-write file if we just asked for read. But if
		// we asked for write, the file we serve back must be opened in write
		// mode
		if (i != key_view.end()
			&& (!(m & open_mode::write) || (i->mode & open_mode::write)))
		{
			key_view.modify(i, [&](FileEntry& e)
			{
				e.last_use = aux::time_now();
			});

			auto& lru_view = m_files. template get<1>();
			lru_view.relocate(lru_view.end(), m_files. template project<1>(i));

#if TORRENT_USE_ASSERTS
			// ensure the LRU index is maintained as we would expect it to be
			TORRENT_ASSERT(lru_view.back().key == i->key);
#endif
			return i->mapping;
		}

		if (int(m_files.size()) >= m_size - 1)
		{
			// the file cache is at its maximum size, close
			// the least recently used file
			defer_destruction1 = remove_oldest(l);
		}

		opening_file_entry ofe;
		ofe.file_key = file_key;
		ofe.mode = m;
		m_opening_files.push_back(ofe);

#if TRACE_FILE_POOL
		std::cout << std::this_thread::get_id() << " opening file: ("
			<< file_key.first << ", " << file_key.second << ")\n";
#endif

		l.unlock();

		try
		{
			FileEntry e = open_file_impl(p, file_index, fn, m, file_key
#if TORRENT_HAVE_MAP_VIEW_OF_FILE
				, std::move(open_unmap_lock)
#endif
				);

			l.lock();

			// there's an edge case where two threads are racing to insert a newly
			// opened file, one thread is opening a file for writing and the other
			// for reading. If the reading thread wins, it's important that the
			// thread opening for writing still overwrites the file in the pool,
			// since a file opened for reading and writing can be used for both.
			// So, we can't move e in here, because we may need it again of the
			// insertion failed.
			// if the insertion failed, check to see if we can use the existing
			// entry. If not, overwrite it with the newly opened file ``e``.
			bool added;
			std::tie(i, added) = key_view.insert(e);
			if (added == false)
			{
				// this is the case where this file was already in the pool. Make
				// sure we can use it. If we asked for write mode, it must have been
				// opened in write mode too.
				TORRENT_ASSERT(i != key_view.end());

				if ((m & open_mode::write) && !(i->mode & open_mode::write))
				{
					key_view.modify(i, [&](FileEntry& fe)
					{
						defer_destruction2 = std::move(fe.mapping);
						fe = std::move(e);
					});
				}

				auto& lru_view = m_files.template get<1>();
				lru_view.relocate(lru_view.end(), m_files. template project<1>(i));
			}
#if TORRENT_USE_ASSERTS
			// ensure the LRU index is maintained as we would expect it to be
			auto& lru_view = m_files. template get<1>();
			TORRENT_ASSERT(lru_view.back().key == e.key);
#endif
			notify_file_open(ofe, i->mapping, storage_error());
			return i->mapping;
		}
		catch (storage_error const& se)
		{
			if (!l.owns_lock()) l.lock();
			notify_file_open(ofe, {}, se);
			throw;
		}
		catch (std::bad_alloc const&)
		{
			if (!l.owns_lock()) l.lock();
			notify_file_open(ofe, {}, storage_error(
				errors::no_memory, file_index, operation_t::file_open));
			throw;
		}
		catch (boost::system::system_error const& se)
		{
			if (!l.owns_lock()) l.lock();
			notify_file_open(ofe, {}, storage_error(
				se.code(), file_index, operation_t::file_open));
			throw;
		}
		catch (...)
		{
			if (!l.owns_lock()) l.lock();
			notify_file_open(ofe, {}, storage_error(
				errors::no_memory, file_index, operation_t::file_open));
			throw;
		}
	}

	template <typename FileEntry>
	void file_pool_impl<FileEntry>::notify_file_open(opening_file_entry& ofe
		, typename file_pool_impl<FileEntry>::FileHandle mapping
		, lt::storage_error const& se)
	{
#if TRACE_FILE_POOL
		if (!ofe.waiters.empty())
		{
			std::cout << std::this_thread::get_id() << " notify_file_open: ("
				<< ofe.file_key.first << ", " << ofe.file_key.second << ")\n";
		}
#endif

		m_opening_files.erase(m_opening_files.s_iterator_to(ofe));
		for (auto& woe : ofe.waiters)
		{
			woe.mapping = mapping;
			woe.error = se;
			woe.cond.notify_all();
		}
	}

	template <typename FileEntry>
	FileEntry file_pool_impl<FileEntry>::open_file_impl(std::string const& p
		, file_index_t const file_index, filenames const& fn
		, open_mode_t const m, file_id const file_key
#if TORRENT_HAVE_MAP_VIEW_OF_FILE
		, typename FileEntry::mutex_type open_unmap_lock
#endif
		)
	{
		std::string const file_path = fn.file_path(file_index, p);
#if TORRENT_HAVE_MAP_VIEW_OF_FILE
		typename FileEntry::lock_type lou(*open_unmap_lock);
		TORRENT_UNUSED(lou);
#endif
		try
		{
			return FileEntry(file_key, file_path, m, fn.file_size(file_index)
#if TORRENT_HAVE_MAP_VIEW_OF_FILE
				, open_unmap_lock
#endif
				);
		}
		catch (storage_error& se)
		{
			// opening the file failed. If it was because the directory was
			// missing, create it and try again. Otherwise, propagate the
			// error
			if (!(m & open_mode::write)
				|| (se.ec != boost::system::errc::no_such_file_or_directory
#ifdef TORRENT_WINDOWS
					// this is a workaround for improper handling of files on windows shared drives.
					// if the directory on a shared drive does not exist,
					// windows returns ERROR_IO_DEVICE instead of ERROR_FILE_NOT_FOUND
					&& se.ec != error_code(ERROR_IO_DEVICE, system_category())
#endif
				   ))
			{
				throw;
			}

			// create directory and try again
			// this means the directory the file is in doesn't exist.
			// so create it
			se.ec.clear();
			create_directories(parent_path(fn.file_path(file_index, p)), se.ec);

			if (se.ec)
			{
				// if the directory creation failed, don't try to open the file again
				// but actually just fail
				throw_ex<storage_error>(se);
			}

			return FileEntry(file_key, file_path, m, fn.file_size(file_index)
#if TORRENT_HAVE_MAP_VIEW_OF_FILE
				, open_unmap_lock
#endif
				);
		}
	}

	file_open_mode_t to_file_open_mode(open_mode_t const mode, bool const has_mapping)
	{
		return ((mode & open_mode::write)
				? file_open_mode::read_write : file_open_mode::read_only)
			| ((mode & open_mode::no_atime)
				? file_open_mode::no_atime : file_open_mode::read_only)
			| (has_mapping ? file_open_mode::mmapped : file_open_mode_t{})
			;
	}

	template <typename FileEntry>
	std::vector<open_file_state> file_pool_impl<FileEntry>::get_status(storage_index_t const st) const
	{
		std::vector<open_file_state> ret;
		{
			std::unique_lock<std::mutex> l(m_mutex);

			auto const& key_view = m_files. template get<0>();
			auto const start = key_view.lower_bound(file_id{st, file_index_t(0)});
			auto const end = key_view.upper_bound(file_id{st, std::numeric_limits<file_index_t>::max()});

			for (auto i = start; i != end; ++i)
			{
				ret.push_back({i->key.second
					, to_file_open_mode(i->mode, i->mapping->has_memory_map())
					, i->last_use});
			}
		}
		return ret;
	}

	template <typename FileEntry>
	typename file_pool_impl<FileEntry>::FileHandle
	file_pool_impl<FileEntry>::remove_oldest(std::unique_lock<std::mutex>&)
	{
		auto& lru_view = m_files. template get<1>();
		if (lru_view.size() == 0) return {};

#if TRACE_FILE_POOL
		std::cout << std::this_thread::get_id() << " removing: ("
			<< lru_view.front().key.first << ", " << lru_view.front().key.second << ")\n";
#endif

		FileHandle mapping = std::move(lru_view.front().mapping);
		lru_view.pop_front();

		// closing a file may be long running operation (mac os x)
		// let the caller destruct it once it has released the mutex
		return mapping;
	}

	template <typename FileEntry>
	void file_pool_impl<FileEntry>::release(storage_index_t const st, file_index_t file_index)
	{
		std::unique_lock<std::mutex> l(m_mutex);

		auto& key_view = m_files. template get<0>();
		auto const i = key_view.find(file_id{st, file_index});
		if (i == key_view.end()) return;

		auto mapping = std::move(i->mapping);
		key_view.erase(i);

		// closing a file may take a long time (mac os x), so make sure
		// we're not holding the mutex
		l.unlock();
	}

	// closes files belonging to the specified
	// storage, or all if none is specified.
	template <typename FileEntry>
	void file_pool_impl<FileEntry>::release()
	{
		std::unique_lock<std::mutex> l(m_mutex);
		std::unique_lock<std::mutex> l2(m_destruction_mutex);
		m_deferred_destruction = std::move(m_files);
		l.unlock();

		// the files and mappings will be destructed here, not holding the main
		// mutex
		m_deferred_destruction.clear();
	}

	template <typename FileEntry>
	void file_pool_impl<FileEntry>::release(storage_index_t const st)
	{
		std::vector<FileHandle> defer_destruction;

		std::unique_lock<std::mutex> l(m_mutex);

		auto& key_view = m_files. template get<0>();
		auto const begin = key_view.lower_bound(file_id{st, file_index_t(0)});
		auto const end = key_view.upper_bound(file_id{st, std::numeric_limits<file_index_t>::max()});

		for (auto it = begin; it != end; ++it)
			defer_destruction.emplace_back(std::move(it->mapping));

		if (begin != end) key_view.erase(begin, end);
		l.unlock();
		// the files are closed here while the lock is not held
	}

	template <typename FileEntry>
	void file_pool_impl<FileEntry>::resize(int const size)
	{
		// these are destructed _after_ the mutex is released
		std::vector<FileHandle> defer_destruction;

		std::unique_lock<std::mutex> l(m_mutex);

		TORRENT_ASSERT(size > 0);

		if (size == m_size) return;
		m_size = size;
		if (int(m_files.size()) <= m_size) return;

		// close the least recently used files
		while (int(m_files.size()) > m_size)
			defer_destruction.emplace_back(remove_oldest(l));
	}

	template <typename FileEntry>
	void file_pool_impl<FileEntry>::close_oldest()
	{
		// closing a file may be long running operation (mac os x)
		// destruct it after the mutex is released
		FileHandle deferred_destruction;

		std::unique_lock<std::mutex> l(m_mutex);
		deferred_destruction = remove_oldest(l);
	}
}


#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE
#include "libtorrent/aux_/file_view_pool.hpp" // for file_view_entry
#endif
#include "libtorrent/aux_/file_pool.hpp" // for file_pool_entry

namespace libtorrent::aux {

#if defined _MSC_VER || defined __clang__
#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE
	template struct TORRENT_EXTRA_EXPORT file_pool_impl<file_view_entry>;
#endif
	template struct TORRENT_EXTRA_EXPORT file_pool_impl<file_pool_entry>;
#else
#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE
	template struct file_pool_impl<file_view_entry>;
#endif
	template struct file_pool_impl<file_pool_entry>;
#endif
}

