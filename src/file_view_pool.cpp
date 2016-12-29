/*

Copyright (c) 2006-2016, Arvid Norberg
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

using namespace libtorrent::flags;

namespace libtorrent { namespace aux {

	file_view_pool::file_view_pool(int size) : m_size(size) {}
	file_view_pool::~file_view_pool() = default;

	file_view file_view_pool::open_file(storage_index_t st, std::string const& p
		, file_index_t const file_index, file_storage const& fs
		, open_mode_t const m)
	{
		// potentially used to hold a reference to a file object that's
		// about to be destructed. If we have such object we assign it to
		// this member to be destructed after we release the std::mutex. On some
		// operating systems (such as OSX) closing a file may take a long
		// time. We don't want to hold the std::mutex for that.
		std::shared_ptr<file_mapping> defer_destruction;

		std::unique_lock<std::mutex> l(m_mutex);

		TORRENT_ASSERT(is_complete(p));
		auto& key_view = m_files.get<0>();
		auto const i = key_view.find(file_id{st, file_index});
		if (i != key_view.end())
		{
			key_view.modify(i, [&](file_entry& e)
			{
				e.last_use = aux::time_now();

				// make sure the write bit is set if we asked for it
				// it's OK to use a read-write file if we just asked for read. But if
				// we asked for write, the file we serve back must be opened in write
				// mode
				if (!(e.mode & open_mode::write) && (m & open_mode::write))
				{
					defer_destruction = std::move(e.mapping);
					e.mapping = std::make_shared<file_mapping>(
						file_handle(fs.file_path(file_index, p)
							, fs.file_size(file_index), m), m
						, fs.file_size(file_index));
					e.mode = m;
				}
			});

			auto& lru_view = m_files.get<1>();
			lru_view.relocate(m_files.project<1>(i), lru_view.begin());

			return i->mapping->view();
		}

		if (int(m_files.size()) >= m_size - 1)
		{
			// the file cache is at its maximum size, close
			// the least recently used file
			remove_oldest(l);
		}

		l.unlock();
		file_entry e({st, file_index}, fs.file_path(file_index, p), m
			, fs.file_size(file_index));
		auto ret = e.mapping->view();

		l.lock();
		auto& key_view2 = m_files.get<0>();
		key_view2.insert(std::move(e));
		return ret;
	}

namespace {

	file_open_mode_t to_file_open_mode(open_mode_t const mode)
	{
		return ((mode & open_mode::write)
				? file_open_mode::read_write : file_open_mode::read_write)
			| ((mode & open_mode::no_atime)
				? file_open_mode::no_atime : file_open_mode::read_only)
			;
	}

}

	std::vector<open_file_state> file_view_pool::get_status(storage_index_t const st) const
	{
		std::vector<open_file_state> ret;
		{
			std::unique_lock<std::mutex> l(m_mutex);

			auto& key_view = m_files.get<0>();
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
		files_container defer_destruction;
		std::unique_lock<std::mutex> l(m_mutex);
		m_files.swap(defer_destruction);
		l.unlock();

		// the files and mappings will be destructed here, not holding the mutex
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
		std::unique_lock<std::mutex> l(m_mutex);

		TORRENT_ASSERT(size > 0);

		if (size == m_size) return;
		m_size = size;
		if (int(m_files.size()) <= m_size) return;

		// close the least recently used files
		while (int(m_files.size()) > m_size)
			remove_oldest(l);
	}
}
}

#endif // HAVE_MMAP || HAVE_MAP_VIEW_OF_FILE

