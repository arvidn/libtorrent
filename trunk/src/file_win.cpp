/*

Copyright (c) 2003, Magnus Jonsson & Arvid Norberg
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

#include "libtorrent/file.hpp"
#include <sstream>

#include <windows.h>

#define DWORD_MAX 0xffffffffu

namespace
{
	// must be used to not leak memory in case something would throw
	class auto_LocalFree
	{
	public:
		auto_LocalFree(HLOCAL memory)
			: m_memory(memory)
		{
		}
		~auto_LocalFree()
		{
			if (m_memory)
				LocalFree(m_memory);
		}
	private:
		HLOCAL m_memory;
	};

	void throw_exception(const char* thrower)
	{
		char *buffer = 0;
		int err = GetLastError();

		#ifdef _UNICODE
			FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_ALLOCATE_BUFFER, 0, err, 0, (LPWSTR)(LPCSTR)&buffer, 0, 0);
		#else
			FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_ALLOCATE_BUFFER, 0, err, 0, (LPSTR)&buffer, 0, 0);
		#endif

		auto_LocalFree auto_free(buffer); // needed for exception safety
		std::stringstream s;
		s << (thrower ? thrower : "NULL") << ": " << (buffer ? buffer : "NULL");

		throw libtorrent::file_error(s.str());
	}
}

namespace libtorrent
{

	struct file::impl : boost::noncopyable
	{
		enum open_flags
		{
			read_flag = 1,
			write_flag = 2
		};

		enum seek_mode
		{
			seek_begin = FILE_BEGIN,
			seek_from_here = FILE_CURRENT,
			seek_end = FILE_END
		};

		impl()
		{
			m_file_handle = INVALID_HANDLE_VALUE;
		}

		void open(const char *file_name, open_flags flags)
		{
			assert(file_name);
			assert(flags & (read_flag | write_flag));

			DWORD access_mask = 0;
			if (flags & read_flag)
				access_mask |= GENERIC_READ;
			if (flags & write_flag)
				access_mask |= GENERIC_WRITE;

			assert(access_mask & (GENERIC_READ | GENERIC_WRITE));

		#ifdef _UNICODE
			HANDLE new_handle = CreateFile(
				(LPCWSTR)file_name
				, access_mask
				, FILE_SHARE_READ
				, 0
				, (flags & read_flag)?OPEN_EXISTING:OPEN_ALWAYS
				, FILE_ATTRIBUTE_NORMAL
				, 0);
		#else
			HANDLE new_handle = CreateFile(
				file_name
				, access_mask
				, FILE_SHARE_READ
				, 0
				, (flags & read_flag)?OPEN_EXISTING:OPEN_ALWAYS
				, FILE_ATTRIBUTE_NORMAL
				, 0);
		#endif

			if (new_handle == INVALID_HANDLE_VALUE)
			{
				std::stringstream s;
				s << "couldn't open file '" << file_name << "'";
				throw file_error(s.str());
			}
			// will only close old file if the open succeeded
			close();
			m_file_handle = new_handle;
		}

		void close()
		{
			if (m_file_handle != INVALID_HANDLE_VALUE)
			{
				CloseHandle(m_file_handle);
				m_file_handle = INVALID_HANDLE_VALUE;
			}
		}

		~impl()
		{
			close();
		}

		size_type write(const char* buffer, size_type num_bytes)
		{
			assert(buffer);
			assert(num_bytes > 0);
			assert((DWORD)num_bytes == num_bytes);
			DWORD bytes_written = 0;
			if (num_bytes != 0)
			{
				if (FALSE == WriteFile(
					m_file_handle
					, buffer
					, (DWORD)num_bytes
					, &bytes_written
					, 0))
				{
					throw_exception("file::write");
				}
			}
			return bytes_written;
		}
		
		size_type read(char* buffer, size_type num_bytes)
		{
			assert(buffer);
			assert(num_bytes > 0);
			assert((DWORD)num_bytes == num_bytes);

			DWORD bytes_read = 0;
			if (num_bytes != 0)
			{
				if (FALSE == ReadFile(
					m_file_handle
					, buffer
					, (DWORD)num_bytes
					, &bytes_read
					, 0))
				{
					throw_exception("file::read");
				}
			}
			return bytes_read;
		}

		void seek(size_type pos, seek_mode from_where)
		{
			assert(pos >= 0 || from_where != seek_begin);
			assert(pos <= 0 || from_where != seek_end);
			LARGE_INTEGER offs;
			offs.QuadPart = pos;
			if (FALSE == SetFilePointerEx(
				m_file_handle
				, offs
				, &offs
				, from_where))
			{
				throw_exception("file::seek");
			}
		}
		
		size_type tell()
		{
			LARGE_INTEGER offs;
			offs.QuadPart = 0;

			// is there any other way to get offset?
			if (FALSE == SetFilePointerEx(
				m_file_handle
				, offs
				, &offs
				, FILE_CURRENT))
			{
				throw_exception("file::tell");
			}

			size_type pos=offs.QuadPart;
			assert(pos>=0);
			return pos;
		}
/*
		size_type size()
		{
			LARGE_INTEGER s;
			if (FALSE == GetFileSizeEx(m_file_handle, &s))
			{
				throw_exception("file::size");
			}
			
			size_type size = s.QuadPart;
			assert(size >= 0);
			return size;
		}
*/
	private:

		HANDLE m_file_handle;

	};
}

namespace libtorrent
{

	const file::seek_mode file::begin(file::impl::seek_begin);
	const file::seek_mode file::end(file::impl::seek_end);

	const file::open_mode file::in(file::impl::read_flag);
	const file::open_mode file::out(file::impl::write_flag);

	file::file()
		: m_impl(new libtorrent::file::impl())
	{
	}
	file::file(boost::filesystem::path const& p, open_mode m)
		: m_impl(new libtorrent::file::impl())
	{
		open(p,m);
	}

	file::~file()
	{
	}

	void file::open(boost::filesystem::path const& p, open_mode m)
	{
		assert(p.is_complete());
		m_impl->open(p.native_file_string().c_str(), impl::open_flags(m.m_mask));
	}

	void file::close()
	{
		m_impl->close();
	}

	size_type file::write(const char* buffer, size_type num_bytes)
	{
		return m_impl->write(buffer, num_bytes);
	}

	size_type file::read(char* buffer, size_type num_bytes)
	{
		return m_impl->read(buffer, num_bytes);
	}

	void file::seek(size_type pos, seek_mode m)
	{
		m_impl->seek(pos,impl::seek_mode(m.m_val));
	}
	
	size_type file::tell()
	{
		return m_impl->tell();
	}
}
