/*

Copyright (c) 2006, Arvid Norberg
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

#ifndef TORRENT_FILE_POOL_HPP
#define TORRENT_FILE_POOL_HPP

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/filesystem/path.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/thread.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/file.hpp"
#include "libtorrent/time.hpp"

namespace libtorrent
{

	using boost::multi_index::multi_index_container;
	using boost::multi_index::ordered_non_unique;
	using boost::multi_index::ordered_unique;
	using boost::multi_index::indexed_by;
	using boost::multi_index::member;
	namespace fs = boost::filesystem;

	struct TORRENT_EXPORT file_pool : boost::noncopyable
	{
		file_pool(int size = 40): m_size(size) {}

		boost::shared_ptr<file> open_file(void* st, fs::path const& p
			, file::open_mode m, error_code& ec);
		void release(void* st);
		void release(fs::path const& p);
		void resize(int size);

	private:
		int m_size;

		struct lru_file_entry
		{
			lru_file_entry(): last_use(time_now()) {}
			mutable boost::shared_ptr<file> file_ptr;
			fs::path file_path;
			void* key;
			ptime last_use;
			file::open_mode mode;
		};

		typedef multi_index_container<
			lru_file_entry, indexed_by<
				ordered_unique<member<lru_file_entry, fs::path
					, &lru_file_entry::file_path> >
				, ordered_non_unique<member<lru_file_entry, ptime
					, &lru_file_entry::last_use> >
				, ordered_non_unique<member<lru_file_entry, void*
					, &lru_file_entry::key> >
				> 
			> file_set;
		
		file_set m_files;
		boost::mutex m_mutex;
	};
}

#endif
