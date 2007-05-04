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

#ifdef _WIN32
// windows part
#include "libtorrent/utf8.hpp"

#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifndef _MODE_T_
typedef int mode_t;
#endif

#ifdef UNICODE
#include "libtorrent/storage.hpp"
#endif

#else
// unix part
#define _FILE_OFFSET_BITS 64
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#include <boost/static_assert.hpp>
// make sure the _FILE_OFFSET_BITS define worked
// on this platform
BOOST_STATIC_ASSERT(sizeof(lseek(0, 0, 0)) >= 8);

#endif

#include <boost/filesystem/operations.hpp>
#include "libtorrent/file.hpp"
#include <sstream>

#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifndef O_RANDOM
#define O_RANDOM 0
#endif

#ifdef UNICODE
#include "libtorrent/storage.hpp"
#endif


namespace fs = boost::filesystem;

namespace
{
	enum { mode_in = 1, mode_out = 2 };

	mode_t map_open_mode(int m)
	{
		if (m == (mode_in | mode_out)) return O_RDWR | O_CREAT | O_BINARY | O_RANDOM;
		if (m == mode_out) return O_WRONLY | O_CREAT | O_BINARY | O_RANDOM;
		if (m == mode_in) return O_RDONLY | O_BINARY | O_RANDOM;
		assert(false);
		return 0;
	}

#ifdef WIN32
	std::string utf8_native(std::string const& s)
	{
		try
		{
			std::wstring ws;
			libtorrent::utf8_wchar(s, ws);
			std::size_t size = wcstombs(0, ws.c_str(), 0);
			if (size == std::size_t(-1)) return s;
			std::string ret;
			ret.resize(size);
			size = wcstombs(&ret[0], ws.c_str(), size + 1);
			if (size == wchar_t(-1)) return s;
			ret.resize(size);
			return ret;
		}
		catch(std::exception)
		{
			return s;
		}
	}
#else
	std::string utf8_native(std::string const& s)
	{
		return s;
	}
#endif

}

namespace libtorrent
{

	const file::open_mode file::in(mode_in);
	const file::open_mode file::out(mode_out);

	const file::seek_mode file::begin(1);
	const file::seek_mode file::end(2);

	struct file::impl
	{
		impl()
			: m_fd(-1)
			, m_open_mode(0)
		{}

		impl(fs::path const& path, int mode)
			: m_fd(-1)
			, m_open_mode(0)
		{
			open(path, mode);
		}

		~impl()
		{
			close();
		}

		void open(fs::path const& path, int mode)
		{
			assert(path.is_complete());
			close();
#if defined(_WIN32) && defined(UNICODE)
			std::wstring wpath(safe_convert(path.native_file_string()));
			m_fd = ::_wopen(
				wpath.c_str()
				, map_open_mode(mode)
				, S_IREAD | S_IWRITE);
#else
#ifdef _WIN32
			m_fd = ::_open(
#else
			m_fd = ::open(
#endif
				utf8_native(path.native_file_string()).c_str()
				, map_open_mode(mode)
#ifdef _WIN32
				, S_IREAD | S_IWRITE);
#else
				, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
#endif
#endif
			if (m_fd == -1)
			{
				std::stringstream msg;
				msg << "open failed: '" << path.native_file_string() << "'. "
					<< strerror(errno);
				throw file_error(msg.str());
			}
			m_open_mode = mode;
		}

		void close()
		{
			if (m_fd == -1) return;

#ifdef _WIN32
			::_close(m_fd);
#else
			::close(m_fd);
#endif
			m_fd = -1;
			m_open_mode = 0;
		}

		size_type read(char* buf, size_type num_bytes)
		{
			assert(m_open_mode & mode_in);
			assert(m_fd != -1);

#ifdef _WIN32
			size_type ret = ::_read(m_fd, buf, num_bytes);
#else
			size_type ret = ::read(m_fd, buf, num_bytes);
#endif
			if (ret == -1)
			{
				std::stringstream msg;
				msg << "read failed: " << strerror(errno);
				throw file_error(msg.str());
			}
			return ret;
		}

		size_type write(const char* buf, size_type num_bytes)
		{
			assert(m_open_mode & mode_out);
			assert(m_fd != -1);

			// TODO: Test this a bit more, what happens with random failures in
			// the files?
//			if ((rand() % 100) > 80)
//				throw file_error("debug");

#ifdef _WIN32
			size_type ret = ::_write(m_fd, buf, num_bytes);
#else
			size_type ret = ::write(m_fd, buf, num_bytes);
#endif
			if (ret == -1)
			{
				std::stringstream msg;
				msg << "write failed: " << strerror(errno);
				throw file_error(msg.str());
			}
			return ret;
		}

		size_type seek(size_type offset, int m)
		{
			assert(m_open_mode);
			assert(m_fd != -1);

			int seekdir = (m == 1)?SEEK_SET:SEEK_END;
#ifdef _WIN32
			size_type ret = _lseeki64(m_fd, offset, seekdir);
#else
			size_type ret = lseek(m_fd, offset, seekdir);
#endif

			// For some strange reason this fails
			// on win32. Use windows specific file
			// wrapper instead.
			if (ret == -1)
			{
				std::stringstream msg;
				msg << "seek failed: '" << strerror(errno)
					<< "' fd: " << m_fd
					<< " offset: " << offset
					<< " seekdir: " << seekdir;
				throw file_error(msg.str());
			}
			return ret;
		}

		size_type tell()
		{
			assert(m_open_mode);
			assert(m_fd != -1);

#ifdef _WIN32
			return _telli64(m_fd);
#else
			return lseek(m_fd, 0, SEEK_CUR);
#endif
		}

		int m_fd;
		int m_open_mode;
	};

	// pimpl forwardings

	file::file() : m_impl(new impl()) {}

	file::file(boost::filesystem::path const& p, file::open_mode m)
		: m_impl(new impl(p, m.m_mask))
	{}

	file::~file() {}

	void file::open(boost::filesystem::path const& p, file::open_mode m)
	{
		m_impl->open(p, m.m_mask);
	}

	void file::close()
	{
		m_impl->close();
	}

	size_type file::write(const char* buf, size_type num_bytes)
	{
		return m_impl->write(buf, num_bytes);
	}

	size_type file::read(char* buf, size_type num_bytes)
	{
		return m_impl->read(buf, num_bytes);
	}

	size_type file::seek(size_type pos, file::seek_mode m)
	{
		return m_impl->seek(pos, m.m_val);
	}

	size_type file::tell()
	{
		return m_impl->tell();
	}

}
