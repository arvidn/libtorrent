/*

Copyright (c) 2006, 2009, 2013-2021, Arvid Norberg
Copyright (c) 2016, Alden Torres
Copyright (c) 2019, Steven Siloti
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_FILE_POOL_IMPL_HPP
#define TORRENT_FILE_POOL_IMPL_HPP

#include "libtorrent/config.hpp"

#include <map>
#include <mutex>
#include <vector>
#include <memory>
#include <condition_variable>

#include "libtorrent/aux_/time.hpp"
#include "libtorrent/units.hpp"
#include "libtorrent/storage_defs.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/open_mode.hpp"
#include "libtorrent/disk_interface.hpp" // for file_open_mode_t
#include "libtorrent/fwd.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

#define BOOST_BIND_NO_PLACEHOLDERS

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index/member.hpp>

#include <boost/intrusive/list.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent::aux {

	namespace mi = boost::multi_index;

	TORRENT_EXTRA_EXPORT file_open_mode_t to_file_open_mode(open_mode_t, bool const has_mapping);

	using file_id = std::pair<storage_index_t, file_index_t>;

	// this is an internal cache of open file mappings.
	template <typename FileEntry>
#if defined _MSC_VER
	struct file_pool_impl
#else
	struct TORRENT_EXTRA_EXPORT file_pool_impl
#endif
	{
		// ``size`` specifies the number of allowed files handles
		// to hold open at any given time.
		explicit file_pool_impl(int size = 40) : m_size(size) {}
		~file_pool_impl() = default;

		file_pool_impl(file_pool_impl const&) = delete;
		file_pool_impl& operator=(file_pool_impl const&) = delete;

		using FileHandle = decltype(FileEntry::mapping);

		// return an open file handle to file at ``file_index`` in the
		// filenames ``fn`` opened at save path ``p``. ``m`` is the
		// file open mode (see file::open_mode_t).
		FileHandle open_file(storage_index_t st, std::string const& p
			, file_index_t file_index, filenames const& fn, open_mode_t m
#if TORRENT_HAVE_MAP_VIEW_OF_FILE
			, typename FileEntry::mutex_type open_unmap_lock
#endif
			);

		// release all file views belonging to the specified storage_interface
		// (``st``) the overload that takes ``file_index`` releases only the file
		// with that index in storage ``st``.
		void release();
		void release(storage_index_t st);
		void release(storage_index_t st, file_index_t file_index);

		// update the allowed number of open file handles to ``size``.
		void resize(int size);

		// returns the current limit of number of allowed open file views held
		// by the file_pool_impl.
		int size_limit() const { return m_size; }

		std::vector<open_file_state> get_status(storage_index_t st) const;

		void close_oldest();

	private:

		FileHandle remove_oldest(std::unique_lock<std::mutex>&);

		int m_size;

		using files_container = mi::multi_index_container<
			FileEntry,
			mi::indexed_by<
			// look up files by (torrent, file) key
			mi::ordered_unique<mi::member<FileEntry, file_id, &FileEntry::key>>,
			// look up files by least recently used. New items are added to the
			// back, and old items are removed from the front.
			mi::sequenced<>
			>
		>;

		struct wait_open_entry
		{
			boost::intrusive::list_member_hook<> list_hook;

			std::condition_variable cond;

			// the open file is passed back to the waiting threads, just in case
			// the pool size is so small that it's otherwise evicted between
			// being notified and waking up to look for it.
			FileHandle mapping;

			// if opening the file fails, waiters are also notified but there
			// won't be a mapping. Then this error code is set.
			lt::storage_error error = {};
		};

		struct opening_file_entry
		{
			boost::intrusive::list_member_hook<> list_hook;

			file_id file_key;

			// the open mode for the file the thread is opening. A thread
			// needing a file opened in read-write mode should not wait for a
			// thread opening the file in read mode
			open_mode_t mode{};

			boost::intrusive::list<wait_open_entry
				, boost::intrusive::member_hook<wait_open_entry
				, boost::intrusive::list_member_hook<>
				, &wait_open_entry::list_hook>
			> waiters;
		};

		void notify_file_open(opening_file_entry& ofe, FileHandle, lt::storage_error const&);

		FileEntry open_file_impl(std::string const& p
			, file_index_t file_index, filenames const& fn
			, open_mode_t m, file_id file_key
#if TORRENT_HAVE_MAP_VIEW_OF_FILE
			, typename FileEntry::mutex_type open_unmap_lock
#endif
			);

		// In order to avoid multiple threads opening the same file in parallel,
		// just to race to add it to the pool. This list, also protected by
		// m_mutex, contains files that one thread is currently opening. If
		// another thread also need this file, it can add itself to the waiters
		// list. The condition variable will then be notified when the file has
		// been opened.
		boost::intrusive::list<opening_file_entry
			, boost::intrusive::member_hook<opening_file_entry
			, boost::intrusive::list_member_hook<>
			, &opening_file_entry::list_hook>
			> m_opening_files;

		// maps storage pointer, file index pairs to the lru entry for the file
		files_container m_files;
		mutable std::mutex m_mutex;

		// the boost.multi-index container is not no-throw move constructable. In
		// order to destruct m_files without holding the mutex, we need this
		// separate pre-allocated container to move it into before releasing the
		// mutex and clearing it.
		files_container m_deferred_destruction;
		mutable std::mutex m_destruction_mutex;
	};

}

#endif
