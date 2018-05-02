/*

Copyright (c) 2006-2018, Arvid Norberg
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

#include "libtorrent/config.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/version.hpp>
#include <boost/bind.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/assert.hpp"
#include "libtorrent/file_pool.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/file_storage.hpp" // for file_entry
#include "libtorrent/aux_/time.hpp"

namespace libtorrent
{
	file_pool::file_pool(int size)
		: m_size(size)
		, m_low_prio_io(true)
	{
	}

	file_pool::~file_pool()
	{
	}

#ifdef TORRENT_WINDOWS
	void set_low_priority(file_handle const& f)
	{
		// file prio is only supported on vista and up
		// so load the functions dynamically
		typedef enum _FILE_INFO_BY_HANDLE_CLASS {
			FileBasicInfo,
			FileStandardInfo,
			FileNameInfo,
			FileRenameInfo,
			FileDispositionInfo,
			FileAllocationInfo,
			FileEndOfFileInfo,
			FileStreamInfo,
			FileCompressionInfo,
			FileAttributeTagInfo,
			FileIdBothDirectoryInfo,
			FileIdBothDirectoryRestartInfo,
			FileIoPriorityHintInfo,
			FileRemoteProtocolInfo,
			MaximumFileInfoByHandleClass
		} FILE_INFO_BY_HANDLE_CLASS, *PFILE_INFO_BY_HANDLE_CLASS;

		typedef enum _PRIORITY_HINT {
			IoPriorityHintVeryLow = 0,
			IoPriorityHintLow,
			IoPriorityHintNormal,
			MaximumIoPriorityHintType
		} PRIORITY_HINT;

		typedef struct _FILE_IO_PRIORITY_HINT_INFO {
			PRIORITY_HINT PriorityHint;
		} FILE_IO_PRIORITY_HINT_INFO, *PFILE_IO_PRIORITY_HINT_INFO;

		typedef BOOL (WINAPI *SetFileInformationByHandle_t)(HANDLE hFile, FILE_INFO_BY_HANDLE_CLASS FileInformationClass, LPVOID lpFileInformation, DWORD dwBufferSize);
		static SetFileInformationByHandle_t SetFileInformationByHandle = NULL;

		static bool failed_kernel_load = false;

		if (failed_kernel_load) return;

		if (SetFileInformationByHandle == NULL)
		{
			HMODULE kernel32 = LoadLibraryA("kernel32.dll");
			if (kernel32 == NULL)
			{
				failed_kernel_load = true;
				return;
			}

			SetFileInformationByHandle = (SetFileInformationByHandle_t)GetProcAddress(kernel32, "SetFileInformationByHandle");
			if (SetFileInformationByHandle == NULL)
			{
				failed_kernel_load = true;
				return;
			}
		}

		TORRENT_ASSERT(SetFileInformationByHandle);

		FILE_IO_PRIORITY_HINT_INFO io_hint;
		io_hint.PriorityHint = IoPriorityHintLow;
		SetFileInformationByHandle(f->native_handle(),
			FileIoPriorityHintInfo, &io_hint, sizeof(io_hint));
	}
#endif // TORRENT_WINDOWS

	file_handle file_pool::open_file(void* st, std::string const& p
		, int file_index, file_storage const& fs, int m, error_code& ec)
	{
		// potentially used to hold a reference to a file object that's
		// about to be destructed. If we have such object we assign it to
		// this member to be destructed after we release the mutex. On some
		// operating systems (such as OSX) closing a file may take a long
		// time. We don't want to hold the mutex for that.
		file_handle defer_destruction;

		mutex::scoped_lock l(m_mutex);

#if TORRENT_USE_ASSERTS
		// we're not allowed to open a file
		// from a deleted storage!
		TORRENT_ASSERT(std::find(m_deleted_storages.begin(), m_deleted_storages.end()
			, std::make_pair(fs.name(), static_cast<void const*>(&fs)))
			== m_deleted_storages.end());
#endif

		TORRENT_ASSERT(st != 0);
		TORRENT_ASSERT(is_complete(p));
		TORRENT_ASSERT((m & file::rw_mask) == file::read_only
			|| (m & file::rw_mask) == file::read_write);
		file_set::iterator i = m_files.find(std::make_pair(st, file_index));
		if (i != m_files.end())
		{
			lru_file_entry& e = i->second;
			e.last_use = aux::time_now();

			// if we asked for a file in write mode,
			// and the cached file is is not opened in
			// write mode, re-open it
			if ((((e.mode & file::rw_mask) != file::read_write)
				&& ((m & file::rw_mask) == file::read_write))
				|| (e.mode & file::random_access) != (m & file::random_access))
			{
				// close the file before we open it with
				// the new read/write privileges, since windows may
				// file opening a file twice. However, since there may
				// be outstanding operations on it, we can't close the
				// file, we can only delete our reference to it.
				// if this is the only reference to the file, it will be closed
				defer_destruction = e.file_ptr;
				e.file_ptr = boost::make_shared<file>();

				std::string full_path = fs.file_path(file_index, p);
				if (!e.file_ptr->open(full_path, m, ec))
				{
					m_files.erase(i);
					return file_handle();
				}
#ifdef TORRENT_WINDOWS
				if (m_low_prio_io)
					set_low_priority(e.file_ptr);
#endif

				TORRENT_ASSERT(e.file_ptr->is_open());
				e.mode = m;
			}
			return e.file_ptr;
		}

		lru_file_entry e;
		e.file_ptr = boost::make_shared<file>();
		if (!e.file_ptr)
		{
			ec = error_code(boost::system::errc::not_enough_memory, generic_category());
			return e.file_ptr;
		}
		std::string full_path = fs.file_path(file_index, p);
		if (!e.file_ptr->open(full_path, m, ec))
			return file_handle();
#ifdef TORRENT_WINDOWS
		if (m_low_prio_io)
			set_low_priority(e.file_ptr);
#endif
		e.mode = m;
		file_handle file_ptr = e.file_ptr;
		m_files.insert(std::make_pair(std::make_pair(st, file_index), e));
		TORRENT_ASSERT(file_ptr->is_open());

		if (int(m_files.size()) >= m_size)
		{
			// the file cache is at its maximum size, close
			// the least recently used (lru) file from it
			defer_destruction = remove_oldest(l);
		}
		return file_ptr;
	}

	void file_pool::get_status(std::vector<pool_file_status>* files, void* st) const
	{
		mutex::scoped_lock l(m_mutex);

		file_set::const_iterator start = m_files.lower_bound(std::make_pair(st, 0));
		file_set::const_iterator end = m_files.upper_bound(std::make_pair(st, INT_MAX));

		for (file_set::const_iterator i = start; i != end; ++i)
		{
			pool_file_status s;
			s.file_index = i->first.second;
			s.open_mode = i->second.mode;
			s.last_use = i->second.last_use;
			files->push_back(s);
		}
	}

	file_handle file_pool::remove_oldest(mutex::scoped_lock&)
	{
		file_set::iterator i = std::min_element(m_files.begin(), m_files.end()
			, boost::bind(&lru_file_entry::last_use, boost::bind(&file_set::value_type::second, _1))
				< boost::bind(&lru_file_entry::last_use, boost::bind(&file_set::value_type::second, _2)));
		if (i == m_files.end()) return file_handle();

		file_handle file_ptr = i->second.file_ptr;
		m_files.erase(i);

		// closing a file may be long running operation (mac os x)
		// let the calling function destruct it after releasing the mutex
		return file_ptr;
	}

	void file_pool::release(void* st, int file_index)
	{
		mutex::scoped_lock l(m_mutex);

		file_set::iterator i = m_files.find(std::make_pair(st, file_index));
		if (i == m_files.end()) return;

		file_handle file_ptr = i->second.file_ptr;
		m_files.erase(i);

		// closing a file may take a long time (mac os x), so make sure
		// we're not holding the mutex
		l.unlock();
		file_ptr.reset();
	}

	// closes files belonging to the specified
	// storage. If 0 is passed, all files are closed
	void file_pool::release(void* st)
	{
		mutex::scoped_lock l(m_mutex);

		if (st == 0)
		{
			m_files.clear();
			l.unlock();
			return;
		}

		file_set::iterator begin = m_files.lower_bound(std::make_pair(st, 0));
		file_set::iterator end = m_files.upper_bound(std::make_pair(st, std::numeric_limits<int>::max()));

		std::vector<file_handle> to_close;
		while (begin != end)
		{
			to_close.push_back(begin->second.file_ptr);
			m_files.erase(begin++);
		}
		l.unlock();
		// the files are closed here while the lock is not held
	}

#if TORRENT_USE_ASSERTS
	void file_pool::mark_deleted(file_storage const& fs)
	{
		mutex::scoped_lock l(m_mutex);
		m_deleted_storages.push_back(std::make_pair(fs.name()
			, static_cast<void const*>(&fs)));
		if(m_deleted_storages.size() > 100)
			m_deleted_storages.erase(m_deleted_storages.begin());
	}

	bool file_pool::assert_idle_files(void* st) const
	{
		mutex::scoped_lock l(m_mutex);

		for (file_set::const_iterator i = m_files.begin();
			i != m_files.end(); ++i)
		{
			if (i->first.first == st && !i->second.file_ptr.unique())
				return false;
		}
		return true;
	}
#endif

	void file_pool::resize(int size)
	{
		// these are destructed _after_ the mutex is released
		std::vector<file_handle> defer_destruction;

		mutex::scoped_lock l(m_mutex);

		TORRENT_ASSERT(size > 0);

		if (size == m_size) return;
		m_size = size;
		if (int(m_files.size()) <= m_size) return;

		// close the least recently used files
		while (int(m_files.size()) > m_size)
			defer_destruction.push_back(remove_oldest(l));
	}

	void file_pool::close_oldest()
	{
		mutex::scoped_lock l(m_mutex);

		file_set::iterator i = std::min_element(m_files.begin(), m_files.end()
			, boost::bind(&lru_file_entry::opened, boost::bind(&file_set::value_type::second, _1))
				< boost::bind(&lru_file_entry::opened, boost::bind(&file_set::value_type::second, _2)));
		if (i == m_files.end()) return;

		file_handle file_ptr = i->second.file_ptr;
		m_files.erase(i);

		// closing a file may be long running operation (mac os x)
		l.unlock();
		file_ptr.reset();
		l.lock();
	}
}

