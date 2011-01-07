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
#include <vector>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/noncopyable.hpp>
#include <boost/filesystem/path.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/error_code.hpp"
#include "libtorrent/size_type.hpp"
#include "libtorrent/config.hpp"

#ifdef TORRENT_WINDOWS
// windows part
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>
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
#undef _FILE_OFFSET_BITS
#endif
namespace libtorrent
{
	namespace fs = boost::filesystem;

	class TORRENT_EXPORT file: public boost::noncopyable
	{
	public:

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
			mode_mask = rw_mask | no_buffer,
			sparse = 8,

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
		file(fs::path const& p, int m, error_code& ec);
		~file();

		bool open(fs::path const& p, int m, error_code& ec);
		bool is_open() const;
		void close();
		bool set_size(size_type size, error_code& ec);

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

		size_type writev(size_type file_offset, iovec_t const* bufs, int num_bufs, error_code& ec);
		size_type readv(size_type file_offset, iovec_t const* bufs, int num_bufs, error_code& ec);

		size_type get_size(error_code& ec) const;

		// return the offset of the first byte that
		// belongs to a data-region
		size_type sparse_end(size_type start) const;

		size_type phys_offset(size_type offset);

#ifdef TORRENT_WINDOWS
		HANDLE native_handle() const { return m_file_handle; }
#else
		int native_handle() const { return m_fd; }
#endif

	private:

#ifdef TORRENT_WINDOWS
		HANDLE m_file_handle;
#if TORRENT_USE_WPATH
		std::wstring m_path;
#else
		std::string m_path;
#endif
#else
		int m_fd;
#endif
#if defined TORRENT_WINDOWS || defined TORRENT_LINUX || defined TORRENT_DEBUG
		static void init_file();
		static int m_page_size;
#endif
		int m_open_mode;
#if defined TORRENT_WINDOWS || defined TORRENT_LINUX
		mutable int m_sector_size;
#endif
#if defined TORRENT_WINDOWS
		mutable int m_cluster_size;
#endif
	};

}

#endif // TORRENT_FILE_HPP_INCLUDED

