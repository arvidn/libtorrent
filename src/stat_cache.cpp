/*

Copyright (c) 2012-2018, Arvid Norberg, Daniel Wallin
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

#include "libtorrent/stat_cache.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/path.hpp"

namespace libtorrent {

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

	std::int64_t stat_cache::get_filesize(file_index_t const i, file_storage const& fs
		, std::string const& save_path, error_code& ec)
	{
		// always pretend symlinks don't exist, to trigger special logic for
		// creating and possibly validating them. There's a risk we'll and up in a
		// cycle of references here otherwise.
		// Should stat_file() be changed to use lstat()?
		if (fs.file_flags(i) & file_storage::flag_symlink)
		{
			ec.assign(boost::system::errc::no_such_file_or_directory, boost::system::system_category());
			return 0;
		}

		std::lock_guard<std::mutex> l(m_mutex);
		TORRENT_ASSERT(i < fs.end_file());
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
			std::string const file_path = fs.file_path(i, save_path);
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
