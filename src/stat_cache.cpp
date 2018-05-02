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

namespace libtorrent
{
	stat_cache::stat_cache() {}
	stat_cache::~stat_cache() {}

	void stat_cache::set_cache(int i, boost::int64_t size, time_t time)
	{
		TORRENT_ASSERT(i >= 0);
		if (i >= int(m_stat_cache.size()))
			m_stat_cache.resize(i + 1, not_in_cache);
		m_stat_cache[i].file_size = size;
		m_stat_cache[i].file_time = time;
	}

	void stat_cache::set_dirty(int i)
	{
		TORRENT_ASSERT(i >= 0);
		if (i >= int(m_stat_cache.size())) return;
		m_stat_cache[i].file_size = not_in_cache;
	}

	void stat_cache::set_noexist(int i)
	{
		TORRENT_ASSERT(i >= 0);
		if (i >= int(m_stat_cache.size()))
			m_stat_cache.resize(i + 1, not_in_cache);
		m_stat_cache[i].file_size = no_exist;
	}

	void stat_cache::set_error(int i)
	{
		TORRENT_ASSERT(i >= 0);
		if (i >= int(m_stat_cache.size()))
			m_stat_cache.resize(i + 1, not_in_cache);
		m_stat_cache[i].file_size = cache_error;
	}

	boost::int64_t stat_cache::get_filesize(int i) const
	{
		if (i >= int(m_stat_cache.size())) return not_in_cache;
		return m_stat_cache[i].file_size;
	}

	time_t stat_cache::get_filetime(int i) const
	{
		if (i >= int(m_stat_cache.size())) return not_in_cache;
		if (m_stat_cache[i].file_size < 0) return m_stat_cache[i].file_size;
		return m_stat_cache[i].file_time;
	}

	void stat_cache::init(int num_files)
	{
		m_stat_cache.resize(num_files, not_in_cache);
	}

	void stat_cache::clear()
	{
		std::vector<stat_cache_t>().swap(m_stat_cache);
	}

}

