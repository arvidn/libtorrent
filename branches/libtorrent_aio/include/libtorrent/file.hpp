/*

Copyright (c) 2003, Arvid Norberg
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

#ifndef TORRENT_FILE_HPP_INCLUDED
#define TORRENT_FILE_HPP_INCLUDED

#include <memory>
#include <string>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/noncopyable.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/config.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/size_type.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/intrusive_ptr_base.hpp"

#ifdef TORRENT_WINDOWS
// windows part
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#else
// posix part
#define _FILE_OFFSET_BITS 64

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include <unistd.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h> // for DIR

#undef _FILE_OFFSET_BITS

#endif

#include <boost/function.hpp>

namespace libtorrent
{
#ifdef TORRENT_WINDOWS
	typedef HANDLE handle_type;
#else
	typedef int handle_type;
#endif

	struct file_status
	{
		size_type file_size;
		time_t atime;
		time_t mtime;
		time_t ctime;
		enum {
#if defined TORRENT_WINDOWS
			directory = _S_IFDIR,
			regular_file = _S_IFREG
#else
			fifo = S_IFIFO,
			character_special = S_IFCHR,
			directory = S_IFDIR,
			block_special = S_IFBLK,
			regular_file = S_IFREG,
			link = S_IFLNK,
			socket = S_IFSOCK
#endif
		} modes_t;
		int mode;
	};

	enum stat_flags_t { dont_follow_links = 1 };
	TORRENT_EXPORT void stat_file(std::string f, file_status* s
		, error_code& ec, int flags = 0);
	TORRENT_EXPORT void rename(std::string const& f
		, std::string const& newf, error_code& ec);
	TORRENT_EXPORT void create_directories(std::string const& f
		, error_code& ec);
	TORRENT_EXPORT void create_directory(std::string const& f
		, error_code& ec);
	TORRENT_EXPORT void remove_all(std::string const& f
		, error_code& ec);
	TORRENT_EXPORT void remove(std::string const& f, error_code& ec);
	TORRENT_EXPORT bool exists(std::string const& f);
	TORRENT_EXPORT size_type file_size(std::string const& f);
	TORRENT_EXPORT bool is_directory(std::string const& f
		, error_code& ec);
	TORRENT_EXPORT void copy_file(std::string const& f
		, std::string const& newf, error_code& ec);

	TORRENT_EXPORT std::string split_path(std::string const& f);
	TORRENT_EXPORT char const* next_path_element(char const* p);
	TORRENT_EXPORT std::string extension(std::string const& f);
	TORRENT_EXPORT void replace_extension(std::string& f, std::string const& ext);
	TORRENT_EXPORT bool is_root_path(std::string const& f);
	TORRENT_EXPORT std::string parent_path(std::string const& f);
	TORRENT_EXPORT bool has_parent_path(std::string const& f);
	TORRENT_EXPORT char const* filename_cstr(char const* f);
	TORRENT_EXPORT std::string filename(std::string const& f);
	TORRENT_EXPORT std::string combine_path(std::string const& lhs
		, std::string const& rhs);
	TORRENT_EXPORT std::string complete(std::string const& f);
	TORRENT_EXPORT bool is_complete(std::string const& f);
	TORRENT_EXPORT std::string current_working_directory();
#if TORRENT_USE_UNC_PATHS
	TORRENT_EXTRA_EXPORT std::string canonicalize_path(std::string const& f);
#endif

	class TORRENT_EXPORT directory : public boost::noncopyable
	{
	public:
		directory(std::string const& path, error_code& ec);
		~directory();
		void next(error_code& ec);
		std::string file() const;
		bool done() const { return m_done; }
	private:
#ifdef TORRENT_WINDOWS
		HANDLE m_handle;
#if TORRENT_USE_WSTRING
		WIN32_FIND_DATAW m_fd;
#else
		WIN32_FIND_DATAA m_fd;
#endif
#else
		DIR* m_handle;
		// the dirent struct contains a zero-sized
		// array at the end, it will end up referring
		// to the m_name field
		struct dirent m_dirent;
		char m_name[TORRENT_MAX_PATH + 1]; // +1 to make room for null
#endif
		bool m_done;
	};

	struct TORRENT_EXPORT file: boost::noncopyable, intrusive_ptr_base<file>
	{
		enum
		{
			read_only = 0,
			write_only = 1,
			read_write = 2,
			rw_mask = read_only | write_only | read_write,
			sparse = 4,
			no_atime = 8,
			random_access = 16,
			lock_file = 32,

			attribute_hidden = 0x1000,
			attribute_executable = 0x2000,
			attribute_mask = attribute_hidden | attribute_executable
		};

#ifdef TORRENT_WINDOWS
		struct iovec_t
		{
			void* iov_base;
			size_t iov_len;
		};
#else
		typedef iovec iovec_t;
#endif

		// use a typedef for the type of iovec_t::iov_base
		// since it may differ
#ifdef TORRENT_SOLARIS
		typedef char* iovec_base_t;
#else
		typedef void* iovec_base_t;
#endif

		file();
		file(std::string const& p, int m, error_code& ec);
		~file();

		bool open(std::string const& p, int m, error_code& ec);
		bool is_open() const;
		void close();
		bool set_size(size_type size, error_code& ec);

		// called when we're done writing to the file.
		// On windows this will clear the sparse bit
		void finalize();

		int open_mode() const { return m_open_mode; }

		// flags for writev and readv
		enum {
			coalesce_buffers = 1,
			sequential_access = 2
		};

		size_type writev(size_type file_offset, iovec_t const* bufs, int num_bufs
			, error_code& ec, int flags = 0);
		size_type readv(size_type file_offset, iovec_t const* bufs, int num_bufs
			, error_code& ec, int flags = 0);

		size_type get_size(error_code& ec) const;

		// return the offset of the first byte that
		// belongs to a data-region
		size_type sparse_end(size_type start) const;

		handle_type native_handle() const { return m_file_handle; }

#ifdef TORRENT_DISK_STATS
		boost::uint32_t file_id() const { return m_file_id; }
#endif

	private:

		handle_type m_file_handle;
#ifdef TORRENT_DISK_STATS
		boost::uint32_t m_file_id;
#endif

#if defined TORRENT_WINDOWS && TORRENT_USE_WSTRING
		std::wstring m_path;
#elif defined TORRENT_WINDOWS
		std::string m_path;
#endif // TORRENT_WINDOWS

		int m_open_mode;
#if defined TORRENT_WINDOWS || defined TORRENT_LINUX
		mutable int m_sector_size;
#endif
#if defined TORRENT_WINDOWS
		mutable int m_cluster_size;
#endif

	};

#ifdef TORRENT_DISK_STATS
	void write_disk_log(FILE* f, file::aiocb_t const* aio, bool complete, ptime timestamp);
#endif

}

#endif // TORRENT_FILE_HPP_INCLUDED

