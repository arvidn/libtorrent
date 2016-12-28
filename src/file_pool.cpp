/*

Copyright (c) 2006-2016, Arvid Norberg
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

#include "libtorrent/assert.hpp"
#include "libtorrent/file_pool.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/units.hpp"
#ifdef TORRENT_WINDOWS
#include "libtorrent/aux_/win_util.hpp"
#endif

#include <limits>

namespace libtorrent
{
	file_pool::file_pool(int size)
		: m_size(size)
		, m_low_prio_io(true)
	{
	}

	file_pool::~file_pool() = default;

#ifdef TORRENT_WINDOWS
	void set_low_priority(file_handle const& f)
	{
		// file prio is only supported on vista and up
		// so load the functions dynamically
		typedef enum {
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
		} FILE_INFO_BY_HANDLE_CLASS_LOCAL;

		typedef enum {
			IoPriorityHintVeryLow = 0,
			IoPriorityHintLow,
			IoPriorityHintNormal,
			MaximumIoPriorityHintType
		} PRIORITY_HINT_LOCAL;

		typedef struct {
			PRIORITY_HINT_LOCAL PriorityHint;
		} FILE_IO_PRIORITY_HINT_INFO_LOCAL;

		typedef BOOL (WINAPI *SetFileInformationByHandle_t)(HANDLE hFile, FILE_INFO_BY_HANDLE_CLASS_LOCAL FileInformationClass, LPVOID lpFileInformation, DWORD dwBufferSize);
		auto SetFileInformationByHandle =
			aux::get_library_procedure<aux::kernel32, SetFileInformationByHandle_t>("SetFileInformationByHandle");

		if (SetFileInformationByHandle == nullptr) return;

		FILE_IO_PRIORITY_HINT_INFO_LOCAL io_hint;
		io_hint.PriorityHint = IoPriorityHintLow;
		SetFileInformationByHandle(f->native_handle(),
			FileIoPriorityHintInfo, &io_hint, sizeof(io_hint));
	}
#endif // TORRENT_WINDOWS

	file_handle file_pool::open_file(void* st, std::string const& p
		, file_index_t const file_index, file_storage const& fs, int m, error_code& ec)
	{
		// potentially used to hold a reference to a file object that's
		// about to be destructed. If we have such object we assign it to
		// this member to be destructed after we release the std::mutex. On some
		// operating systems (such as OSX) closing a file may take a long
		// time. We don't want to hold the std::mutex for that.
		file_handle defer_destruction;

		std::unique_lock<std::mutex> l(m_mutex);

#if TORRENT_USE_ASSERTS
		// we're not allowed to open a file
		// from a deleted storage!
		TORRENT_ASSERT(std::find(m_deleted_storages.begin(), m_deleted_storages.end()
			, std::make_pair(fs.name(), static_cast<void const*>(&fs)))
			== m_deleted_storages.end());
#endif

		TORRENT_ASSERT(st != nullptr);
		TORRENT_ASSERT(is_complete(p));
		TORRENT_ASSERT((m & file::rw_mask) == file::read_only
			|| (m & file::rw_mask) == file::read_write);
		auto const i = m_files.find(std::make_pair(st, file_index));
		if (i != m_files.end())
		{
			lru_file_entry& e = i->second;
			e.last_use = aux::time_now();

			if (e.key != st && ((e.mode & file::rw_mask) != file::read_only
				|| (m & file::rw_mask) != file::read_only))
			{
				// this means that another instance of the storage
				// is using the exact same file.
				ec = errors::file_collision;
				return file_handle();
			}

			e.key = st;
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
				e.file_ptr = std::make_shared<file>();

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
		e.file_ptr = std::make_shared<file>();
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
		e.key = st;
		m_files.insert(std::make_pair(std::make_pair(st, file_index), e));
		TORRENT_ASSERT(e.file_ptr->is_open());

		file_handle file_ptr = e.file_ptr;

		// the file is not in our cache
		if (int(m_files.size()) >= m_size)
		{
			// the file cache is at its maximum size, close
			// the least recently used (lru) file from it
			remove_oldest(l);
		}
		return file_ptr;
	}

	std::vector<pool_file_status> file_pool::get_status(void* st) const
	{
		std::vector<pool_file_status> ret;
		{
			std::unique_lock<std::mutex> l(m_mutex);

			auto const start = m_files.lower_bound(std::make_pair(st, file_index_t(0)));
			auto const end = m_files.upper_bound(std::make_pair(st
				, std::numeric_limits<file_index_t>::max()));

			for (auto i = start; i != end; ++i)
				ret.push_back({i->first.second, i->second.mode, i->second.last_use});
		}
		return ret;
	}

	void file_pool::remove_oldest(std::unique_lock<std::mutex>& l)
	{
		using value_type = decltype(m_files)::value_type;
		auto const i = std::min_element(m_files.begin(), m_files.end()
			, [] (value_type const& lhs, value_type const& rhs)
				{ return lhs.second.last_use < rhs.second.last_use; });
		if (i == m_files.end()) return;

		file_handle file_ptr = i->second.file_ptr;
		m_files.erase(i);

		// closing a file may be long running operation (mac os x)
		l.unlock();
		file_ptr.reset();
		l.lock();
	}

	void file_pool::release(void* st, file_index_t file_index)
	{
		std::unique_lock<std::mutex> l(m_mutex);

		auto const i = m_files.find(std::make_pair(st, file_index));
		if (i == m_files.end()) return;

		file_handle file_ptr = i->second.file_ptr;
		m_files.erase(i);

		// closing a file may be long running operation (mac os x)
		l.unlock();
		file_ptr.reset();
	}

	// closes files belonging to the specified
	// storage. If 0 is passed, all files are closed
	void file_pool::release(void* st)
	{
		std::unique_lock<std::mutex> l(m_mutex);

		if (st == nullptr)
		{
			std::map<std::pair<void*, file_index_t>, lru_file_entry> tmp;
			tmp.swap(m_files);
			l.unlock();
			return;
		}

		std::vector<file_handle> to_close;
		for (auto i = m_files.begin(); i != m_files.end();)
		{
			if (i->second.key == st)
			{
				to_close.push_back(i->second.file_ptr);
				m_files.erase(i++);
			}
			else
				++i;
		}
		l.unlock();
		// the files are closed here
	}

#if TORRENT_USE_ASSERTS
	void file_pool::mark_deleted(file_storage const& fs)
	{
		std::unique_lock<std::mutex> l(m_mutex);
		m_deleted_storages.push_back(std::make_pair(fs.name()
			, static_cast<void const*>(&fs)));
		if(m_deleted_storages.size() > 100)
			m_deleted_storages.erase(m_deleted_storages.begin());
	}

	bool file_pool::assert_idle_files(void* st) const
	{
		std::unique_lock<std::mutex> l(m_mutex);

		for (auto const& i : m_files)
		{
			if (i.second.key == st && !i.second.file_ptr.unique())
				return false;
		}
		return true;
	}
#endif

	void file_pool::resize(int size)
	{
		std::unique_lock<std::mutex> l(m_mutex);

		TORRENT_ASSERT(size > 0);

		if (size == m_size) return;
		m_size = size;
		if (int(m_files.size()) <= m_size) return;

		// close the least recently used files
		while (int(m_files.size()) > m_size)
			remove_oldest(l);
	}

}
