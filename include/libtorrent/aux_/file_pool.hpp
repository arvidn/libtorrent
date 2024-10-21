/*

Copyright (c) 2022, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_FILE_POOL_HPP
#define TORRENT_FILE_POOL_HPP

#include "libtorrent/config.hpp"

#include "libtorrent/aux_/file_pool_impl.hpp"
#include "libtorrent/aux_/file.hpp" // for file_handle

namespace libtorrent::aux {

	struct file_pool_entry
	{
#if TORRENT_HAVE_MAP_VIEW_OF_FILE
		using mutex_type = int*;
		using lock_type = int;
#endif

		file_pool_entry(file_id k
			, string_view name
			, open_mode_t const m
			, std::int64_t const size
#if TORRENT_HAVE_MAP_VIEW_OF_FILE
			, mutex_type
#endif
			)
			: key(k)
			, mapping(std::make_shared<file_handle>(name, size, m))
			, mode(m)
		{}

		file_id key;
		std::shared_ptr<file_handle> mapping;
		time_point last_use{aux::time_now()};
		open_mode_t mode{};
	};

	using file_pool = file_pool_impl<file_pool_entry>;

#if defined _MSC_VER && defined TORRENT_BUILDING_SHARED
	// msvc doesn't like __declspec(dllexport) on an extern template
	extern template struct file_pool_impl<aux::file_pool_entry>;
#else
	extern template struct TORRENT_EXTRA_EXPORT file_pool_impl<aux::file_pool_entry>;
#endif
}

#endif

