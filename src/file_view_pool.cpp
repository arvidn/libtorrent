/*

Copyright (c) 2016, 2019-2020, Arvid Norberg
Copyright (c) 2019, Steven Siloti
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/config.hpp"

#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE

#include "libtorrent/assert.hpp"
#include "libtorrent/aux_/file_view_pool.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/units.hpp"
#include "libtorrent/disk_interface.hpp"
#include "libtorrent/aux_/path.hpp"
#ifdef TORRENT_WINDOWS
#include "libtorrent/aux_/win_util.hpp"
#endif

#include <limits>

using namespace lt::flags;

namespace lt::aux {

	file_view_pool::file_view_pool(int size) : m_size(size) {}
	file_view_pool::~file_view_pool() = default;

	file_view file_view_pool::open_file(storage_index_t st, std::string const& p
		, file_index_t const file_index, file_storage const& fs
		, open_mode_t const m
#if TORRENT_HAVE_MAP_VIEW_OF_FILE
		, std::shared_ptr<std::mutex> open_unmap_lock
#endif
		)
	{
		// potentially used to hold a reference to a file object that's
		// about to be destructed. If we have such object we assign it to
		// this member to be destructed after we release the std::mutex. On some
		// operating systems (such as OSX) closing a file may take a long
		// time. We don't want to hold the std::mutex for that.
		std::shared_ptr<file_mapping> defer_destruction1;
		std::shared_ptr<file_mapping> defer_destruction2;

		std::unique_lock<std::mutex> l(m_mutex);

		TORRENT_ASSERT(is_complete(p));
		auto& key_view = m_files.get<0>();
		auto i = key_view.find(file_id{st, file_index});

		// make sure the write bit is set if we asked for it
		// it's OK to use a read-write file if we just asked for read. But if
		// we asked for write, the file we serve back must be opened in write
		// mode
		if (i != key_view.end()
			&& (!(m & open_mode::write) || (i->mode & open_mode::write)))
		{
			key_view.modify(i, [&](file_entry& e)
			{
				e.last_use = aux::time_now();
			});

			auto& lru_view = m_files.get<1>();
			lru_view.relocate(m_files.project<1>(i), lru_view.begin());

			return i->mapping->view();
		}

		if (int(m_files.size()) >= m_size - 1)
		{
			// the file cache is at its maximum size, close
			// the least recently used file
			defer_destruction1 = remove_oldest(l);
		}

		l.unlock();

#if TORRENT_HAVE_MAP_VIEW_OF_FILE
		std::unique_lock<std::mutex> lou(*open_unmap_lock);
#endif
		file_entry e({st, file_index}, fs.file_path(file_index, p), m
			, fs.file_size(file_index)
#if TORRENT_HAVE_MAP_VIEW_OF_FILE
			, open_unmap_lock
#endif
			);
#if TORRENT_HAVE_MAP_VIEW_OF_FILE
		lou.unlock();
#endif
		l.lock();

		// there's an edge case where two threads are racing to insert a newly
		// opened file, one thread is opening a file for writing and the other
		// fore reading. If the reading thread wins, it's important that the
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
				key_view.modify(i, [&](file_entry& fe)
				{
					defer_destruction2 = std::move(fe.mapping);
					fe = std::move(e);
				});
			}

			auto& lru_view = m_files.get<1>();
			lru_view.relocate(m_files.project<1>(i), lru_view.begin());
		}

		return i->mapping->view();
	}

	file_open_mode_t to_file_open_mode(open_mode_t const mode)
	{
		return ((mode & open_mode::write)
				? file_open_mode::read_write : file_open_mode::read_only)
			| ((mode & open_mode::no_atime)
				? file_open_mode::no_atime : file_open_mode::read_only)
			;
	}

	std::vector<open_file_state> file_view_pool::get_status(storage_index_t const st) const
	{
		std::vector<open_file_state> ret;
		{
			std::unique_lock<std::mutex> l(m_mutex);

			auto const& key_view = m_files.get<0>();
			auto const start = key_view.lower_bound(file_id{st, file_index_t(0)});
			auto const end = key_view.upper_bound(file_id{st, std::numeric_limits<file_index_t>::max()});

			for (auto i = start; i != end; ++i)
			{
				ret.push_back({i->key.second
					, to_file_open_mode(i->mode)
					, i->last_use});
			}
		}
		return ret;
	}

	std::shared_ptr<file_mapping> file_view_pool::remove_oldest(std::unique_lock<std::mutex>&)
	{
		auto& lru_view = m_files.get<1>();
		if (lru_view.size() == 0) return {};

		auto mapping = std::move(lru_view.back().mapping);
		lru_view.pop_back();

		// closing a file may be long running operation (mac os x)
		// let the caller destruct it once it has released the mutex
		return mapping;
	}

	void file_view_pool::release(storage_index_t const st, file_index_t file_index)
	{
		std::unique_lock<std::mutex> l(m_mutex);

		auto& key_view = m_files.get<0>();
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
	void file_view_pool::release()
	{
		std::unique_lock<std::mutex> l(m_mutex);
		std::unique_lock<std::mutex> l2(m_destruction_mutex);
		m_deferred_destruction = std::move(m_files);
		l.unlock();

		// the files and mappings will be destructed here, not holding the main
		// mutex
		m_deferred_destruction.clear();
	}

	void file_view_pool::release(storage_index_t const st)
	{
		std::vector<std::shared_ptr<file_mapping>> defer_destruction;

		std::unique_lock<std::mutex> l(m_mutex);

		auto& key_view = m_files.get<0>();
		auto const begin = key_view.lower_bound(file_id{st, file_index_t(0)});
		auto const end = key_view.upper_bound(file_id{st, std::numeric_limits<file_index_t>::max()});

		for (auto it = begin; it != end; ++it)
			defer_destruction.emplace_back(std::move(it->mapping));

		if (begin != end) key_view.erase(begin, end);
		l.unlock();
		// the files are closed here while the lock is not held
	}

	void file_view_pool::resize(int const size)
	{
		// these are destructed _after_ the mutex is released
		std::vector<std::shared_ptr<file_mapping>> defer_destruction;

		std::unique_lock<std::mutex> l(m_mutex);

		TORRENT_ASSERT(size > 0);

		if (size == m_size) return;
		m_size = size;
		if (int(m_files.size()) <= m_size) return;

		// close the least recently used files
		while (int(m_files.size()) > m_size)
			defer_destruction.emplace_back(remove_oldest(l));
	}

	void file_view_pool::close_oldest()
	{
		// closing a file may be long running operation (mac os x)
		// destruct it after the mutex is released
		std::shared_ptr<file_mapping> deferred_destruction;

		std::unique_lock<std::mutex> l(m_mutex);
		deferred_destruction = remove_oldest(l);
	}
}

#endif // HAVE_MMAP || HAVE_MAP_VIEW_OF_FILE

