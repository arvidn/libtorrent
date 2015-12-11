/*

Copyright (c) 2012-2016, Arvid Norberg, Daniel Wallin
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
#include "libtorrent/file.hpp"

#include <string>

namespace libtorrent
{
	class file_storage;

	stat_cache::stat_cache() {}
	stat_cache::~stat_cache() {}

	void stat_cache::set_cache(int i, boost::int64_t size)
	{
		TORRENT_ASSERT(i >= 0);
		if (i >= int(m_stat_cache.size()))
			m_stat_cache.resize(i + 1, not_in_cache);
		m_stat_cache[i].file_size = size;
	}

	void stat_cache::set_error(int i, error_code const& ec)
	{
		TORRENT_ASSERT(i >= 0);
		if (i >= int(m_stat_cache.size()))
			m_stat_cache.resize(i + 1, not_in_cache);

		int error_index = add_error(ec);
		m_stat_cache[i].file_size = file_error - error_index;
	}

	void stat_cache::set_dirty(int i)
	{
		TORRENT_ASSERT(i >= 0);
		if (i >= int(m_stat_cache.size())) return;
		m_stat_cache[i].file_size = not_in_cache;
	}

	boost::int64_t stat_cache::get_filesize(int i, file_storage const& fs
		, std::string const& save_path, error_code& ec)
	{
		TORRENT_ASSERT(i < int(fs.num_files()));
		if (i >= int(m_stat_cache.size())) m_stat_cache.resize(i + 1, not_in_cache);
		boost::int64_t sz = m_stat_cache[i].file_size;
		if (sz < not_in_cache)
		{
			ec = m_errors[-sz + file_error];
			return file_error;
		}
		else if (sz == not_in_cache)
		{
			// query the filesystem
			file_status s;
			std::string file_path = fs.file_path(i, save_path);
			stat_file(file_path, &s, ec);
			if (ec)
			{
				set_error(i, ec);
				sz = file_error;
			}
			else
			{
				set_cache(i, s.file_size);
				sz = s.file_size;
			}
		}
		return sz;
	}

	void stat_cache::reserve(int num_files)
	{
		m_stat_cache.resize(num_files, not_in_cache);
	}

	void stat_cache::clear()
	{
		std::vector<stat_cache_t>().swap(m_stat_cache);
		std::vector<error_code>().swap(m_errors);
	}

	int stat_cache::add_error(error_code const& ec)
	{
		std::vector<error_code>::iterator i = std::find(m_errors.begin(), m_errors.end(), ec);
		if (i != m_errors.end()) return i - m_errors.begin();
		m_errors.push_back(ec);
		return m_errors.size() - 1;
	}
}

