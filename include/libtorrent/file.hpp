/*

Copyright (c) 2003-2018, Arvid Norberg
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

#include "libtorrent/config.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/noncopyable.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/function.hpp>

#ifdef TORRENT_WINDOWS
// windows part
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>
#include <sys/types.h>
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
#include <sys/types.h>
#include <dirent.h> // for DIR

#undef _FILE_OFFSET_BITS

#endif

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/error_code.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/time.hpp"

namespace libtorrent
{
#ifdef TORRENT_WINDOWS
	typedef HANDLE handle_type;
#else
	typedef int handle_type;
#endif

	struct file_status
	{
		boost::int64_t file_size;
		boost::uint64_t atime;
		boost::uint64_t mtime;
		boost::uint64_t ctime;
		enum {
#if defined TORRENT_WINDOWS
			fifo = 0x1000, // named pipe (fifo)
			character_special = 0x2000,  // character special
			directory = 0x4000,  // directory
			regular_file = 0x8000  // regular
#else
			fifo = 0010000, // named pipe (fifo)
			character_special = 0020000,  // character special
			directory = 0040000,  // directory
			block_special = 0060000,  // block special
			regular_file = 0100000,  // regular
			link = 0120000,  // symbolic link
			socket = 0140000  // socket
#endif
		} modes_t;
		int mode;
	};

	// internal flags for stat_file
	enum { dont_follow_links = 1 };
	TORRENT_EXTRA_EXPORT void stat_file(std::string const& f, file_status* s
		, error_code& ec, int flags = 0);
	TORRENT_EXTRA_EXPORT void rename(std::string const& f
		, std::string const& newf, error_code& ec);
	TORRENT_EXTRA_EXPORT void create_directories(std::string const& f
		, error_code& ec);
	TORRENT_EXTRA_EXPORT void create_directory(std::string const& f
		, error_code& ec);
	TORRENT_EXTRA_EXPORT void remove_all(std::string const& f
		, error_code& ec);
	TORRENT_EXTRA_EXPORT void remove(std::string const& f, error_code& ec);
	TORRENT_EXTRA_EXPORT bool exists(std::string const& f, error_code& ec);
	TORRENT_EXTRA_EXPORT bool exists(std::string const& f);
	TORRENT_EXTRA_EXPORT boost::int64_t file_size(std::string const& f);
	TORRENT_EXTRA_EXPORT bool is_directory(std::string const& f
		, error_code& ec);
	TORRENT_EXTRA_EXPORT void recursive_copy(std::string const& old_path
		, std::string const& new_path, error_code& ec);
	TORRENT_EXTRA_EXPORT void copy_file(std::string const& f
		, std::string const& newf, error_code& ec);
	TORRENT_EXTRA_EXPORT void move_file(std::string const& f
		, std::string const& newf, error_code& ec);

	// file is expected to exist, link will be created to point to it. If hard
	// links are not supported by the filesystem or OS, the file will be copied.
	TORRENT_EXTRA_EXPORT void hard_link(std::string const& file
		, std::string const& link, error_code& ec);

	TORRENT_EXTRA_EXPORT std::string split_path(std::string const& f);
	TORRENT_EXTRA_EXPORT char const* next_path_element(char const* p);
	TORRENT_EXTRA_EXPORT std::string extension(std::string const& f);
	TORRENT_EXTRA_EXPORT std::string remove_extension(std::string const& f);
	TORRENT_EXTRA_EXPORT void replace_extension(std::string& f, std::string const& ext);
	TORRENT_EXTRA_EXPORT bool is_root_path(std::string const& f);


	// internal used by create_torrent.hpp
	TORRENT_EXTRA_EXPORT std::string parent_path(std::string const& f);
	TORRENT_EXTRA_EXPORT bool has_parent_path(std::string const& f);
	TORRENT_EXTRA_EXPORT char const* filename_cstr(char const* f);

	// internal used by create_torrent.hpp
	TORRENT_EXTRA_EXPORT std::string filename(std::string const& f);
	TORRENT_EXTRA_EXPORT std::string combine_path(std::string const& lhs
		, std::string const& rhs);
	TORRENT_EXTRA_EXPORT void append_path(std::string& branch
		, std::string const& leaf);
	TORRENT_EXTRA_EXPORT void append_path(std::string& branch
		, char const* str, int len);
	// internal used by create_torrent.hpp
	TORRENT_EXTRA_EXPORT std::string complete(std::string const& f);
	TORRENT_EXTRA_EXPORT bool is_complete(std::string const& f);
	TORRENT_EXTRA_EXPORT std::string current_working_directory();
#if TORRENT_USE_UNC_PATHS
	TORRENT_EXTRA_EXPORT std::string canonicalize_path(std::string const& f);
#endif

	// TODO: move this into a separate header file, TU pair
	class TORRENT_EXTRA_EXPORT directory : public boost::noncopyable
	{
	public:
		directory(std::string const& path, error_code& ec);
		~directory();
		void next(error_code& ec);
		std::string file() const;
		boost::uint64_t inode() const;
		bool done() const { return m_done; }
	private:
#ifdef TORRENT_WINDOWS
		HANDLE m_handle;
		int m_inode;
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

	struct file;

#ifdef TORRENT_DEBUG_FILE_LEAKS
	struct file_handle
	{
		file_handle();
		file_handle(file* f);
		file_handle(file_handle const& fh);
		~file_handle();
		file* operator->();
		file const* operator->() const;
		file& operator*();
		file const& operator*() const;
		file* get();
		file const* get() const;
		operator bool() const;
		file_handle& reset(file* f = NULL);

		char stack[2048];
	private:
		boost::shared_ptr<file> m_file;
	};

	void TORRENT_EXTRA_EXPORT print_open_files(char const* event, char const* name);
#else
	typedef boost::shared_ptr<file> file_handle;
#endif

	struct TORRENT_EXTRA_EXPORT file: boost::noncopyable
	{
		// the open mode for files. Used for the file constructor or
		// file::open().
		enum open_mode_t
		{
			// open the file for reading only
			read_only = 0,

			// open the file for writing only
			write_only = 1,

			// open the file for reading and writing
			read_write = 2,
			
			// the mask for the bits determining read or write mode
			rw_mask = read_only | write_only | read_write,

			// open the file in sparse mode (if supported by the
			// filesystem).
			sparse = 0x4,

			// don't update the access timestamps on the file (if
			// supported by the operating system and filesystem).
			// this generally improves disk performance.
			no_atime = 0x8,

			// open the file for random access. This disables read-ahead
			// logic
			random_access = 0x10,

			// prevent the file from being opened by another process
			// while it's still being held open by this handle
			lock_file = 0x20,

			// don't put any pressure on the OS disk cache
			// because of access to this file. We expect our
			// files to be fairly large, and there is already
			// a cache at the bittorrent block level. This
			// may improve overall system performance by
			// leaving running applications in the page cache
			no_cache = 0x40,

			// this is only used for readv/writev flags
			coalesce_buffers = 0x100,

			// when creating a file, set the hidden attribute (windows only)
			attribute_hidden = 0x200,

			// when creating a file, set the executable attribute
			attribute_executable = 0x400,

			// the mask of all attribute bits
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
		bool set_size(boost::int64_t size, error_code& ec);

		int open_mode() const { return m_open_mode; }

		boost::int64_t writev(boost::int64_t file_offset, iovec_t const* bufs, int num_bufs
			, error_code& ec, int flags = 0);
		boost::int64_t readv(boost::int64_t file_offset, iovec_t const* bufs, int num_bufs
			, error_code& ec, int flags = 0);

		boost::int64_t get_size(error_code& ec) const;

		// return the offset of the first byte that
		// belongs to a data-region
		boost::int64_t sparse_end(boost::int64_t start) const;

		handle_type native_handle() const { return m_file_handle; }

#ifdef TORRENT_DISK_STATS
		boost::uint32_t file_id() const { return m_file_id; }
#endif

#ifdef TORRENT_DEBUG_FILE_LEAKS
		void print_info(FILE* out) const;
#endif

	private:

		handle_type m_file_handle;
#ifdef TORRENT_DISK_STATS
		boost::uint32_t m_file_id;
#endif

		int m_open_mode;
#if defined TORRENT_WINDOWS
		static bool has_manage_volume_privs;
#endif

#ifdef TORRENT_DEBUG_FILE_LEAKS
		std::string m_file_path;
#endif
	};

	TORRENT_EXTRA_EXPORT int bufs_size(file::iovec_t const* bufs, int num_bufs);

}

#endif // TORRENT_FILE_HPP_INCLUDED

