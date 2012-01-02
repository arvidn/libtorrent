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

#if TORRENT_USE_AIO
#include <aio.h>
#endif

#if TORRENT_USE_IOSUBMIT
#include <libaio.h>
#endif

namespace libtorrent
{
#ifdef TORRENT_WINDOWS
	typedef HANDLE handle_type;
#else
	typedef int handle_type;
#endif

	struct aiocb_pool;
	struct async_handler;

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
			// when a file is opened with no_buffer
			// file offsets have to be aligned to
			// pos_alignment() and buffer addresses
			// to buf_alignment() and read/write sizes
			// to size_alignment()
			read_only = 0,
			write_only = 1,
			read_write = 2,
			rw_mask = read_only | write_only | read_write,
			no_buffer = 4,
			sparse = 8,
			no_atime = 16,
			random_access = 32,
			lock_file = 64,

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

		// aiocb_t is a very thin wrapper around
		// posix AIOs aiocb and window's OVERLAPPED
		// structure. There's also a platform independent
		// version that doesn't use aynch. I/O
		struct aiocb_t;

		struct aiocb_base
		{
			aiocb_t* prev;
			aiocb_t* next;
			async_handler* handler;
			// used to keep the file alive while
			// waiting for the async operation
			boost::intrusive_ptr<file> file_ptr;

			// when coalescing reads/writes, this is the buffer
			// used. It's heap allocated
			char* buffer;

			// when coalescing reads, we need to save the iovecs
			// so that we can copy the resulting buffer into the
			// original buffers when we're done. In that case
			// this will point to aiocb_pool::max_iovec elements.
			// it's also used for AIO APIs that actually support
			// iovecs.
			file::iovec_t* vec;

			// the number of buffers specified in vec that are
			// used for this I/O
			int num_vec;

			// the flags passed to the read or write operation
			int flags;
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
			bool in_use;
#endif
			aiocb_base();
			~aiocb_base();
		};

#if TORRENT_USE_AIO
		struct aiocb_t : aiocb_base
		{
			aiocb cb;
			size_t nbytes() const { return cb.aio_nbytes; }
		};

		enum
		{
			read_op = LIO_READ,
			write_op = LIO_WRITE
		};
#elif TORRENT_USE_IOSUBMIT
		struct aiocb_t : aiocb_base
		{
			iocb cb;
			// return value of the async. operation
			int ret;
			// if ret < 0, this is the errno value the
			// operation failed with
			int error;
#if TORRENT_USE_IOSUBMIT_VEC
			int num_bytes;
			size_t nbytes() const { return num_bytes; }
#else
			size_t nbytes() const { return cb.u.c.nbytes; }
#endif
		};

		enum
		{
#if TORRENT_USE_IOSUBMIT_VEC
			read_op = IO_CMD_PREADV,
			write_op = IO_CMD_PWRITEV
#else
			read_op = IO_CMD_PREAD,
			write_op = IO_CMD_PWRITE
#endif
		};
#elif TORRENT_USE_OVERLAPPED
		struct aiocb_t : aiocb_base
		{
			OVERLAPPED ov;
			int op;
			size_t size;
			void* buf;
			size_t nbytes() const { return size; }
		};

		enum
		{
			read_op = 1,
			write_op = 2
		};
#elif TORRENT_USE_SYNCIO
		// if there is no aio support on this platform
		// fall back to an operation that's sortable
		// by physical disk offset
		struct aiocb_t : aiocb_base
		{
			// used to insert jobs ordered
			size_type phys_offset;
			int op;
			size_type offset;
			size_type size;
			void* buf;
			size_t nbytes() const { return size; }
		};

		enum
		{
			read_op = 1,
			write_op = 2
		};
#else
#error which disk I/O API are we using?
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

		// when opened in unbuffered mode, this is the
		// required alignment of file_offsets. i.e.
		// any (file_offset & (pos_alignment()-1)) == 0
		// is a precondition to read and write operations
		int pos_alignment() const;

		// when opened in unbuffered mode, this is the
		// required alignment of buffer addresses
		int buf_alignment() const;

		// read/write buffer sizes needs to be aligned to
		// this when in unbuffered mode
		int size_alignment() const;

		// flags for writev, readv, async_writev and async_readv
		enum
		{
			coalesce_buffers = 1,
			resolve_phys_offset = 2,
			sequential_access = 4
		};

		size_type writev(size_type file_offset, iovec_t const* bufs, int num_bufs
			, error_code& ec, int flags = 0);
		size_type readv(size_type file_offset, iovec_t const* bufs, int num_bufs
			, error_code& ec, int flags = 0);
		void hint_read(size_type file_offset, int len);

		// returns a chain of aiocb_t structures
		aiocb_t* async_writev(size_type offset, iovec_t const* bufs
			, int num_bufs, aiocb_pool& pool, int flags = 0);
		aiocb_t* async_readv(size_type offset, iovec_t const* bufs
			, int num_bufs, aiocb_pool& pool, int flags = 0);

		size_type get_size(error_code& ec) const;

		// return the offset of the first byte that
		// belongs to a data-region
		size_type sparse_end(size_type start) const;

		size_type phys_offset(size_type offset);

		handle_type native_handle() const { return m_file_handle; }

#ifdef TORRENT_DISK_STATS
		boost::uint32_t file_id() const { return m_file_id; }
#endif

	private:

		// allocates aiocb structures and links them
		// together and returns the pointer to the first
		// element in the (doubly) linked list
		aiocb_t* async_io(size_type offset
			, iovec_t const* bufs, int num_bufs, int op
			, aiocb_pool& pool, int flags);

		handle_type m_file_handle;
#ifdef TORRENT_DISK_STATS
		boost::uint32_t m_file_id;
#endif

#if defined TORRENT_WINDOWS && TORRENT_USE_WSTRING
		std::wstring m_path;
#elif defined TORRENT_WINDOWS
		std::string m_path;
#endif // TORRENT_WINDOWS

#if defined TORRENT_WINDOWS || defined TORRENT_LINUX || defined TORRENT_DEBUG
		static void init_file();
	public:
		static int m_page_size;
	private:
#endif
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

	// this struct is used to hold the handler while
	// waiting for all async operations to complete
	struct async_handler
	{
		async_handler(ptime now) : transferred(0), references(0), started(now)
		{
#ifdef TORRENT_DISK_STATS
			file_access_log = 0;
#endif
		}
		boost::function<void(async_handler*)> handler;
		storage_error error;
		size_t transferred;
		int references;
		ptime started;

		void done(storage_error const& ec, size_t bytes_transferred
			, file::aiocb_t const* aio, aiocb_pool* pool);

#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		~async_handler()
		{
			TORRENT_ASSERT(references == 0);
			references = 0xf0f0f0f0;
		}
#endif

#ifdef TORRENT_DISK_STATS
		FILE* file_access_log;
#endif
	};

	// returns two chains, one with jobs that were issued and
	// one with jobs that couldn't be issued
	std::pair<file::aiocb_t*, file::aiocb_t*> issue_aios(file::aiocb_t* aios
		, aiocb_pool& pool, int& num_issued);

	file::aiocb_t* reap_aios(file::aiocb_t* aios
		, aiocb_pool& pool);

	// reaps one aiocb element. If the operation is
	// not complete, it just returns false. If it is
	// complete, processes it, unlinks it, frees it
	// and returns true.
	bool reap_aio(file::aiocb_t* aio
		, aiocb_pool& pool);

#if TORRENT_USE_OVERLAPPED
	void iovec_to_file_segment(file::iovec_t const* bufs, int num_bufs
		, FILE_SEGMENT_ELEMENT* seg);
#endif

	// return file::read_op or file::write_op
	inline int aio_op(file::aiocb_t const* aio)
	{
#if TORRENT_USE_SYNCIO \
	|| TORRENT_USE_OVERLAPPED
		return aio->op;
#elif TORRENT_USE_AIO \
	|| TORRENT_USE_IOSUBMIT \
	|| TORRENT_USE_IOSUBMIT_VEC
		return aio->cb.aio_lio_opcode;
#else
#error which disk I/O API are we using?
#endif
	}

	inline boost::uint64_t aio_offset(file::aiocb_t const* aio)
	{
#if TORRENT_USE_SYNCIO
		return aio->offset;
#elif TORRENT_USE_OVERLAPPED
		return boost::uint64_t(aio->ov.Offset) | (boost::uint64_t(aio->ov.OffsetHigh) << 32);
#elif TORRENT_USE_AIO
		return aio->cb.aio_offset;
#elif TORRENT_USE_IOSUBMIT
		return aio->cb.u.c.offset;
#elif TORRENT_USE_IOSUBMIT_VEC
		return aio->cb.u.v.offset;
#else
#error which disk I/O API are we using?
#endif
	}

	// since aiocb_t derives from aiocb_base, the platforma specific
	// type is not at the top of the aiocb_t type
	// that's why we need to adjust the pointer from the platform specific
	// pointer into the wrapper, aiocb_t
#if TORRENT_USE_OVERLAPPED
	file::aiocb_t* to_aiocb(OVERLAPPED* in);
#elif TORRENT_USE_AIO
	file::aiocb_t* to_aiocb(aiocb* in);
#elif TORRENT_USE_IOSUBMIT \
	|| TORRENT_USE_IOSUBMIT_VEC
	file::aiocb_t* to_aiocb(iocb* in);
#endif

}

#endif // TORRENT_FILE_HPP_INCLUDED

