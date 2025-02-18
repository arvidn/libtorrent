/*

Copyright (c) 2010, 2013-2022, Arvid Norberg
Copyright (c) 2016-2017, 2020, Alden Torres
Copyright (c) 2016, Andrei Kurushin
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/stat_cache.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/path.hpp"

namespace libtorrent::aux {

	stat_cache::stat_cache() = default;
	stat_cache::~stat_cache() = default;

	void stat_cache::set_cache(file_index_t const i, std::int64_t const size)
	{
		std::lock_guard<std::mutex> l(m_mutex);
		set_cache_impl(i, size);
	}

	void stat_cache::set_cache_impl(file_index_t const i, std::int64_t const size)
	{
		if (i >= m_stat_cache.end_index())
			m_stat_cache.resize(static_cast<int>(i) + 1, stat_cache_t{not_in_cache});
		m_stat_cache[i].file_size = size;
	}

	void stat_cache::set_error(file_index_t const i, error_code const& ec)
	{
		std::lock_guard<std::mutex> l(m_mutex);
		set_error_impl(i, ec);
	}

	void stat_cache::set_error_impl(file_index_t const i, error_code const& ec)
	{
		if (i >= m_stat_cache.end_index())
			m_stat_cache.resize(static_cast<int>(i) + 1, stat_cache_t{not_in_cache});

		int const error_index = add_error(ec);
		m_stat_cache[i].file_size = file_error - error_index;
	}

	void stat_cache::set_dirty(file_index_t const i)
	{
		std::lock_guard<std::mutex> l(m_mutex);
		if (i >= m_stat_cache.end_index()) return;
		m_stat_cache[i].file_size = not_in_cache;
	}

	std::int64_t stat_cache::get_filesize(file_index_t const i, filenames const& fn
		, std::string const& save_path, error_code& ec)
	{
		// always pretend symlinks don't exist, to trigger special logic for
		// creating and possibly validating them. There's a risk we'll and up in a
		// cycle of references here otherwise.
		// Should stat_file() be changed to use lstat()?
		if (fn.file_flags(i) & file_storage::flag_symlink)
		{
			ec.assign(boost::system::errc::no_such_file_or_directory, boost::system::system_category());
			return -1;
		}

		std::lock_guard<std::mutex> l(m_mutex);
		TORRENT_ASSERT(i < fn.end_file());
		if (i >= m_stat_cache.end_index()) m_stat_cache.resize(static_cast<int>(i) + 1
			, stat_cache_t{not_in_cache});
		std::int64_t sz = m_stat_cache[i].file_size;
		if (sz < not_in_cache)
		{
			ec = m_errors[std::size_t(-sz + file_error)];
			return file_error;
		}
		else if (sz == not_in_cache)
		{
			// query the filesystem
			file_status s;
			std::string const file_path = fn.file_path(i, save_path);
			stat_file(file_path, &s, ec);
			if (ec)
			{
				set_error_impl(i, ec);
				sz = file_error;
			}
			else
			{
				set_cache_impl(i, s.file_size);
				sz = s.file_size;
			}
		}
		return sz;
	}

	void stat_cache::reserve(int num_files)
	{
		std::lock_guard<std::mutex> l(m_mutex);
		m_stat_cache.resize(num_files, stat_cache_t{not_in_cache});
	}

	void stat_cache::clear()
	{
		std::lock_guard<std::mutex> l(m_mutex);
		m_stat_cache.clear();
		m_stat_cache.shrink_to_fit();
		m_errors.clear();
		m_errors.shrink_to_fit();
	}

	int stat_cache::add_error(error_code const& ec)
	{
		auto const i = std::find(m_errors.begin(), m_errors.end(), ec);
		if (i != m_errors.end()) return int(i - m_errors.begin());
		m_errors.push_back(ec);
		return int(m_errors.size()) - 1;
	}
}
