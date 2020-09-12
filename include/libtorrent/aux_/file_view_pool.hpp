/*

Copyright (c) 2006, 2009, 2013-2020, Arvid Norberg
Copyright (c) 2016, Alden Torres
Copyright (c) 2019, Steven Siloti
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

#ifndef TORRENT_FILE_VIEW_POOL_HPP
#define TORRENT_FILE_VIEW_POOL_HPP

#include "libtorrent/config.hpp"

#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE

#include <map>
#include <mutex>
#include <vector>
#include <memory>

#include "libtorrent/aux_/time.hpp"
#include "libtorrent/units.hpp"
#include "libtorrent/storage_defs.hpp"
#include "libtorrent/aux_/mmap.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

#define BOOST_BIND_NO_PLACEHOLDERS

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index/member.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent {

class file_storage;
struct open_file_state;

namespace aux {

	namespace mi = boost::multi_index;

	TORRENT_EXTRA_EXPORT file_open_mode_t to_file_open_mode(open_mode_t const);

	// this is an internal cache of open file mappings.
	struct TORRENT_EXTRA_EXPORT file_view_pool
	{
		// ``size`` specifies the number of allowed files handles
		// to hold open at any given time.
		explicit file_view_pool(int size = 40);
		~file_view_pool();

		file_view_pool(file_view_pool const&) = delete;
		file_view_pool& operator=(file_view_pool const&) = delete;

		// return an open file handle to file at ``file_index`` in the
		// file_storage ``fs`` opened at save path ``p``. ``m`` is the
		// file open mode (see file::open_mode_t).
		file_view open_file(storage_index_t st, std::string const& p
			, file_index_t file_index, file_storage const& fs, open_mode_t m
#if TORRENT_HAVE_MAP_VIEW_OF_FILE
			, std::shared_ptr<std::mutex> open_unmap_lock
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
		// by the file_view_pool.
		int size_limit() const { return m_size; }

		std::vector<open_file_state> get_status(storage_index_t st) const;

		void close_oldest();

	private:

		std::shared_ptr<file_mapping> remove_oldest(std::unique_lock<std::mutex>&);

		int m_size;

		using file_id = std::pair<storage_index_t, file_index_t>;

		struct file_entry
		{
			file_entry(file_id k
				, string_view name
				, open_mode_t const m
				, std::int64_t const size
#if TORRENT_HAVE_MAP_VIEW_OF_FILE
				, std::shared_ptr<std::mutex> open_unmap_lock
#endif
				)
				: key(k)
				, mapping(std::make_shared<file_mapping>(file_handle(name, size, m), m, size
#if TORRENT_HAVE_MAP_VIEW_OF_FILE
					, open_unmap_lock
#endif
					))
				, mode(m)
			{}

			file_id key;
			std::shared_ptr<file_mapping> mapping;
			time_point last_use{aux::time_now()};
			open_mode_t mode{};
		};

		using files_container = mi::multi_index_container<
			file_entry,
			mi::indexed_by<
			// look up files by (torrent, file) key
			mi::ordered_unique<mi::member<file_entry, file_id, &file_entry::key>>,
			// look up files by least recently used
			mi::sequenced<>
			>
		>;

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
}

#endif // HAVE_MMAP || HAVE_MAP_VIEW_OF_FILE

#endif
