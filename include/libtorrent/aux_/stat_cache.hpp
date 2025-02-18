/*

Copyright (c) 2010, 2013-2018, 2020-2021, Arvid Norberg
Copyright (c) 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_STAT_CACHE_HPP
#define TORRENT_STAT_CACHE_HPP

#include <vector>
#include <string>
#include <cstdint>
#include <mutex>

#include "libtorrent/config.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/units.hpp"
#include "libtorrent/aux_/vector.hpp"

namespace libtorrent::aux {

	struct TORRENT_EXTRA_EXPORT stat_cache
	{
		stat_cache();
		~stat_cache();

		void reserve(int num_files);

		// returns the size of the file unless an error occurs, in which case ec
		// is set to indicate the error.
		std::int64_t get_filesize(file_index_t i, filenames const& fn
			, std::string const& save_path, error_code& ec);

		void set_dirty(file_index_t i);

		void clear();

		// internal
		enum
		{
			not_in_cache = -1,
			file_error = -2 // (first index in m_errors)
		};

		// internal
		void set_cache(file_index_t i, std::int64_t size);
		void set_error(file_index_t i, error_code const& ec);

	private:

		void set_cache_impl(file_index_t i, std::int64_t size);
		void set_error_impl(file_index_t i, error_code const& ec);

		// returns the index to the specified error. Either an existing one or a
		// newly added entry
		int add_error(error_code const& ec);

		struct stat_cache_t
		{
			explicit stat_cache_t(std::int64_t s): file_size(s) {}

			// the size of the file. Negative values have special meaning. -1 means
			// not-in-cache (i.e. there's no data for this file in the cache).
			// lower values (larger negative values) indicate that an error
			// occurred while stat()ing the file. The positive value is an index
			// into m_errors, that recorded the actual error.
			std::int64_t file_size;
		};

		mutable std::mutex m_mutex;

		// one entry per file
		aux::vector<stat_cache_t, file_index_t> m_stat_cache;

		// These are the errors that have happened when stating files. Each entry
		// that had an error, refers to an index into this vector.
		std::vector<error_code> m_errors;
	};
}

#endif // TORRENT_STAT_CACHE_HPP
