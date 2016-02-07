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

#ifndef TORRENT_STAT_CACHE_HPP
#define TORRENT_STAT_CACHE_HPP

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <vector>
#include <string>
#include <boost/cstdint.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/config.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/file_storage.hpp"

namespace libtorrent
{
	struct TORRENT_EXTRA_EXPORT stat_cache
	{
		stat_cache();
		~stat_cache();

		void reserve(int num_files);

		// returns the size of the file unless an error occurs, in which case ec
		// is set to indicate the error
		boost::int64_t get_filesize(int i, file_storage const& fs
			, std::string const& save_path, error_code& ec);

		void set_dirty(int i);

		void clear();

		// internal
		enum
		{
			not_in_cache = -1,
			file_error = -2 // (first index in m_errors)
		};

		// internal
		void set_cache(int i, boost::int64_t size);
		void set_error(int i, error_code const& ec);

	private:

		// returns the index to the specified error. Either an existing one or a
		// newly added entry
		int add_error(error_code const& ec);

		struct stat_cache_t
		{
			stat_cache_t(boost::int64_t s): file_size(s) {}

			// the size of the file. Negative values have special meaning. -1 means
			// not-in-cache (i.e. there's no data for this file in the cache).
			// lower values (larger negative values) indicate that an error
			// occurred while stat()ing the file. The positive value is an index
			// into m_errors, that recorded the actual error.
			boost::int64_t file_size;
		};

		// one entry per file
		std::vector<stat_cache_t> m_stat_cache;

		// These are the errors that have happened when stating files. Each entry
		// that had an error, refers to an index into this vector.
		std::vector<error_code> m_errors;
	};
}

#endif // TORRENT_STAT_CACHE_HPP

