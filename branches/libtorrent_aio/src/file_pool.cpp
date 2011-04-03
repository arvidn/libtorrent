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
#include <boost/bind.hpp>
#include "libtorrent/pch.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/file_pool.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/file_storage.hpp" // for file_entry

namespace libtorrent
{
	file_pool::file_pool(int size)
		: m_size(size), m_low_prio_io(true)
	{
#if TORRENT_USE_OVERLAPPED
		m_iocp = INVALID_HANDLE_VALUE;
#endif

#if defined TORRENT_DEBUG && defined BOOST_HAS_PTHREADS
		m_owning_thread = pthread_self();
#endif
	}

#if defined TORRENT_DEBUG && defined BOOST_HAS_PTHREADS
	void file_pool::set_thread_owner()
	{
		m_owning_thread = pthread_self();
	}
	void file_pool::clear_thread_owner()
	{
		m_owning_thread = NULL;
	}
#endif

	boost::intrusive_ptr<file> file_pool::open_file(void* st, std::string const& p
		, file_storage::iterator fe, file_storage const& fs, int m, error_code& ec)
	{
		TORRENT_ASSERT(st != 0);
		TORRENT_ASSERT(is_complete(p));
		TORRENT_ASSERT((m & file::rw_mask) == file::read_only
			|| (m & file::rw_mask) == file::read_write);
#if defined TORRENT_DEBUG && defined BOOST_HAS_PTHREADS
		TORRENT_ASSERT(m_owning_thread == NULL || m_owning_thread == pthread_self());
#endif
		file_set::iterator i = m_files.find(std::make_pair(st, fs.file_index(*fe)));
		if (i != m_files.end())
		{
			lru_file_entry& e = i->second;
			e.last_use = time_now();

			if (e.key != st && ((e.mode & file::rw_mask) != file::read_only
				|| (m & file::rw_mask) != file::read_only))
			{
				// this means that another instance of the storage
				// is using the exact same file.
#if BOOST_VERSION >= 103500
				ec = errors::file_collision;
#endif
				return boost::intrusive_ptr<file>();
			}

			e.key = st;
			// if we asked for a file in write mode,
			// and the cached file is is not opened in
			// write mode, re-open it
			if ((((e.mode & file::rw_mask) != file::read_write)
				&& ((m & file::rw_mask) == file::read_write))
				|| (e.mode & file::no_buffer) != (m & file::no_buffer))
			{
				// close the file before we open it with
				// the new read/write privilages, since windows may
				// file opening a file twice. However, since there may
				// be outstanding operations on it, we can't close the
				// file, we can only delete our reference to it.
				// if this is the only reference to the file, it will be closed
				e.file_ptr.reset(new (std::nothrow)file);
				std::string full_path = combine_path(p, fs.file_path(*fe));
				if (!e.file_ptr->open(full_path, m, ec))
				{
					m_files.erase(i);
					return boost::intrusive_ptr<file>();
				}
#ifdef TORRENT_WINDOWS
// file prio is supported on vista and up
#if _WIN32_WINNT >= 0x0600
				if (m_low_prio_io)
				{
					// TODO: load this function dynamically from Kernel32.dll
					FILE_IO_PRIORITY_HINT_INFO priorityHint;
					priorityHint.PriorityHint = IoPriorityHintLow;
					SetFileInformationByHandle(e.file_ptr->native_handle(),
						FileIoPriorityHintInfo, &priorityHint, sizeof(PriorityHint));
				}
#endif
#endif

#if TORRENT_USE_OVERLAPPED
				// when we're using overlapped I/O, register this file with
				// the I/O completion port
				if (m_iocp != INVALID_HANDLE_VALUE)
					CreateIoCompletionPort(e.file_ptr->native_handle(), m_iocp, 0, 1);
#endif

				TORRENT_ASSERT(e.file_ptr->is_open());
				e.mode = m;
			}
			TORRENT_ASSERT((e.mode & file::no_buffer) == (m & file::no_buffer));
			return e.file_ptr;
		}
		// the file is not in our cache
		if ((int)m_files.size() >= m_size)
		{
			// the file cache is at its maximum size, close
			// the least recently used (lru) file from it
			remove_oldest();
		}
		lru_file_entry e;
		e.file_ptr.reset(new (std::nothrow)file);
		if (!e.file_ptr)
		{
			ec = error_code(ENOMEM, get_posix_category());
			return e.file_ptr;
		}
		std::string full_path = combine_path(p, fs.file_path(*fe));
		if (!e.file_ptr->open(full_path, m, ec))
			return boost::intrusive_ptr<file>();
		e.mode = m;
		e.key = st;
		m_files.insert(std::make_pair(std::make_pair(st, fs.file_index(*fe)), e));
		TORRENT_ASSERT(e.file_ptr->is_open());

#if TORRENT_USE_OVERLAPPED
		// when we're using overlapped I/O, register this file with
		// the I/O completion port
		if (m_iocp != INVALID_HANDLE_VALUE)
			CreateIoCompletionPort(e.file_ptr->native_handle(), m_iocp, 0, 1);
#endif
		return e.file_ptr;
	}

	void file_pool::remove_oldest()
	{
#if defined TORRENT_DEBUG && defined BOOST_HAS_PTHREADS
		TORRENT_ASSERT(m_owning_thread == NULL || m_owning_thread == pthread_self());
#endif
		file_set::iterator i = std::min_element(m_files.begin(), m_files.end()
			, boost::bind(&lru_file_entry::last_use, boost::bind(&file_set::value_type::second, _1))
				< boost::bind(&lru_file_entry::last_use, boost::bind(&file_set::value_type::second, _2)));
		if (i == m_files.end()) return;
		m_files.erase(i);
	}

	void file_pool::release(void* st, int file_index)
	{
#if defined TORRENT_DEBUG && defined BOOST_HAS_PTHREADS
		TORRENT_ASSERT(m_owning_thread == NULL || m_owning_thread == pthread_self());
#endif
		file_set::iterator i = m_files.find(std::make_pair(st, file_index));
		if (i != m_files.end()) m_files.erase(i);
	}

	// closes files belonging to the specified
	// storage. If 0 is passed, all files are closed
	void file_pool::release(void* st)
	{
#if defined TORRENT_DEBUG && defined BOOST_HAS_PTHREADS
		TORRENT_ASSERT(m_owning_thread == NULL || m_owning_thread == pthread_self());
#endif
		if (st == 0)
		{
			m_files.clear();
			return;
		}

		for (file_set::iterator i = m_files.begin();
			i != m_files.end();)
		{
			if (i->second.key == st)
				m_files.erase(i++);
			else
				++i;
		}
	}

	void file_pool::resize(int size)
	{
#if defined TORRENT_DEBUG && defined BOOST_HAS_PTHREADS
		TORRENT_ASSERT(m_owning_thread == NULL || m_owning_thread == pthread_self());
#endif
		TORRENT_ASSERT(size > 0);
		if (size == m_size) return;
		m_size = size;
		if (int(m_files.size()) <= m_size) return;

		// close the least recently used files
		while (int(m_files.size()) > m_size)
			remove_oldest();
	}

}

