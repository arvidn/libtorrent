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

#ifndef TORRENT_STAT_CACHE_HPP
#define TORRENT_STAT_CACHE_HPP

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <time.h>
#include <vector>
#include <boost/cstdint.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/config.hpp"

namespace libtorrent
{
	struct TORRENT_EXTRA_EXPORT stat_cache
	{
		stat_cache();
		~stat_cache();

		void init(int num_files);
		
		enum
		{
			cache_error = -1,
			not_in_cache = -2,
			no_exist = -3
		};

		// returns the size of the file or one
		// of the enums, noent or not_in_cache
		boost::int64_t get_filesize(int i) const;
		time_t get_filetime(int i) const;

		void set_cache(int i, boost::int64_t size, time_t time);
		void set_noexist(int i);
		void set_error(int i);
		void set_dirty(int i);

		void clear();

	private:

		struct stat_cache_t
		{
			stat_cache_t(boost::int64_t s, time_t t = 0): file_size(s), file_time(t) {}
			boost::int64_t file_size;
			time_t file_time;
		};
		std::vector<stat_cache_t> m_stat_cache;
	};
}

#endif // TORRENT_STAT_CACHE_HPP

