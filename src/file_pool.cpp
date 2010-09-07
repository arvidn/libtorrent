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

#include <boost/version.hpp>
#include "libtorrent/pch.hpp"
#include "libtorrent/file_pool.hpp"
#include "libtorrent/error_code.hpp"

#include <iostream>

namespace libtorrent
{
	using boost::multi_index::nth_index;
	using boost::multi_index::get;

	boost::shared_ptr<file> file_pool::open_file(void* st, fs::path const& p
		, file::open_mode m, error_code& ec)
	{
		TORRENT_ASSERT(st != 0);
		TORRENT_ASSERT(p.is_complete());
		TORRENT_ASSERT(m == file::in || m == (file::in | file::out));
		boost::mutex::scoped_lock l(m_mutex);
		typedef nth_index<file_set, 0>::type path_view;
		path_view& pt = get<0>(m_files);
		path_view::iterator i = pt.find(p);
		if (i != pt.end())
		{
			lru_file_entry e = *i;
			e.last_use = time_now();

			if (e.key != st && (e.mode != file::in
				|| m != file::in))
			{
				// this means that another instance of the storage
				// is using the exact same file.
#if BOOST_VERSION >= 103500
				ec = error_code(errors::file_collision, libtorrent_category);
#endif
				return boost::shared_ptr<file>();
			}

			e.key = st;
			if ((e.mode & m) != m)
			{
				// close the file before we open it with
				// the new read/write privilages
				i->file_ptr.reset();
				TORRENT_ASSERT(e.file_ptr.unique());
				e.file_ptr->close();
				if (!e.file_ptr->open(p, m, ec))
				{
					m_files.erase(i);
					return boost::shared_ptr<file>();
				}
				TORRENT_ASSERT(e.file_ptr->is_open());
				e.mode = m;
			}
			pt.replace(i, e);
			return e.file_ptr;
		}
		// the file is not in our cache
		if ((int)m_files.size() >= m_size)
		{
			// the file cache is at its maximum size, close
			// the least recently used (lru) file from it
			typedef nth_index<file_set, 1>::type lru_view;
			lru_view& lt = get<1>(m_files);
			lru_view::iterator i = lt.begin();
			// the first entry in this view is the least recently used
			TORRENT_ASSERT(lt.size() == 1 || (i->last_use <= boost::next(i)->last_use));
			lt.erase(i);
		}
		lru_file_entry e;
		e.file_ptr.reset(new (std::nothrow)file);
		if (!e.file_ptr)
		{
			ec = error_code(ENOMEM, get_posix_category());
			return e.file_ptr;
		}
		if (!e.file_ptr->open(p, m, ec))
			return boost::shared_ptr<file>();
		e.mode = m;
		e.key = st;
		e.file_path = p;
		pt.insert(e);
		TORRENT_ASSERT(e.file_ptr->is_open());
		return e.file_ptr;
	}

	void file_pool::release(fs::path const& p)
	{
		boost::mutex::scoped_lock l(m_mutex);

		typedef nth_index<file_set, 0>::type path_view;
		path_view& pt = get<0>(m_files);
		path_view::iterator i = pt.find(p);
		if (i != pt.end()) pt.erase(i);
	}

	void file_pool::release(void* st)
	{
		boost::mutex::scoped_lock l(m_mutex);
		TORRENT_ASSERT(st != 0);
		using boost::tie;

		typedef nth_index<file_set, 2>::type key_view;
		key_view& kt = get<2>(m_files);

		key_view::iterator start, end;
		tie(start, end) = kt.equal_range(st);
		kt.erase(start, end);
	}

	void file_pool::resize(int size)
	{
		TORRENT_ASSERT(size > 0);
		if (size == m_size) return;
		boost::mutex::scoped_lock l(m_mutex);
		m_size = size;
		if (int(m_files.size()) <= m_size) return;

		// close the least recently used files
		typedef nth_index<file_set, 1>::type lru_view;
		lru_view& lt = get<1>(m_files);
		lru_view::iterator i = lt.begin();
		while (int(m_files.size()) > m_size)
		{
			// the first entry in this view is the least recently used
			TORRENT_ASSERT(lt.size() == 1 || (i->last_use <= boost::next(i)->last_use));
			lt.erase(i++);
		}
	}

}
