/*

Copyright (c) 2006, 2009, 2013-2021, Arvid Norberg
Copyright (c) 2016, Alden Torres
Copyright (c) 2019, Steven Siloti
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_FILE_VIEW_POOL_HPP
#define TORRENT_FILE_VIEW_POOL_HPP

#include "libtorrent/config.hpp"

#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE

#include "libtorrent/aux_/file_pool_impl.hpp"
#include "libtorrent/aux_/mmap.hpp"

namespace libtorrent::aux {

	struct file_view_entry
	{
		using mutex_type = std::shared_ptr<std::mutex>;
		using lock_type = std::unique_lock<std::mutex>;

		file_view_entry(file_id k
			, string_view name
			, open_mode_t const m
			, std::int64_t const size
#if TORRENT_HAVE_MAP_VIEW_OF_FILE
			, mutex_type open_unmap_lock
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

	using file_view_pool = file_pool_impl<file_view_entry>;

#if defined _MSC_VER && defined TORRENT_BUILDING_SHARED
	// msvc doesn't like __declspec(dllexport) on an extern template
	extern template struct file_pool_impl<aux::file_view_entry>;
#else
	extern template struct TORRENT_EXTRA_EXPORT file_pool_impl<aux::file_view_entry>;
#endif
}

#endif // HAVE_MMAP || HAVE_MAP_VIEW_OF_FILE

#endif
