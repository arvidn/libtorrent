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

#include <boost/filesystem/operations.hpp>
#include "libtorrent/file.hpp"
#include <sstream>

#ifdef WIN32

#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
typedef int mode_t;

#else

#define _FILE_OFFSET_BITS 64
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#endif

#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifndef O_RANDOM
#define O_RANDOM 0
#endif


namespace fs = boost::filesystem;

namespace
{
	enum { mode_in = 1, mode_out = 2 };

	mode_t map_open_mode(int m)
	{
//		if (m == (mode_in | mode_out)) return O_RDWR | O_BINARY;
		if (m == mode_out) return O_WRONLY | O_CREAT | O_BINARY | O_RANDOM;
		if (m == mode_in) return O_RDONLY | O_BINARY | O_RANDOM;
		assert(false);
		return 0;
	}
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
			close();
			m_fd = ::open(
				path.native_file_string().c_str()
				, map_open_mode(mode)
				, S_IREAD | S_IWRITE);
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

			std::stringstream str;
			str << "fd: " << m_fd << "\n";

			::close(m_fd);
			m_fd = -1;
			m_open_mode = 0;
		}

		size_type read(char* buf, size_type num_bytes)
		{
			assert(m_open_mode == mode_in);
			assert(m_fd != -1);

			size_type ret = ::read(m_fd, buf, num_bytes);
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
			assert(m_open_mode == mode_out);
			assert(m_fd != -1);

			size_type ret = ::write(m_fd, buf, num_bytes);
			if (ret == -1)
			{
				std::stringstream msg;
				msg << "write failed: " << strerror(errno);
				throw file_error(msg.str());
			}
			return ret;
		}

		void seek(size_type offset, int m)
		{
			assert(m_open_mode);
			assert(m_fd != -1);

			int seekdir = (m == 1)?SEEK_SET:SEEK_END;
#ifdef WIN32
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

		}

		size_type tell()
		{
			assert(m_open_mode);
			assert(m_fd != -1);

#ifdef WIN32
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

	void file::seek(size_type pos, file::seek_mode m)
	{
		m_impl->seek(pos, m.m_val);
	}

	size_type file::tell()
	{
		return m_impl->tell();
	}

}
