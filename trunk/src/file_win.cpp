#include <windows.h>
#include "libtorrent/file.hpp"


#define DWORD_MAX 0xffffffffu

namespace libtorrent {

	struct file::impl : boost::noncopyable
	{
		enum open_flags { read_flag=1,
		                  write_flag=2 };
		enum seek_mode  { seek_begin=FILE_BEGIN,
			              seek_from_here=FILE_CURRENT,
						  seek_end=FILE_END };

		impl()
		{
			m_file_handle=INVALID_HANDLE_VALUE;
		}

		void open(const char *file_name, open_flags flags)
		{
			assert(file_name);
			assert(flags&(read_flag|write_flag));

			DWORD access_mask=0;
			if (flags&read_flag)
				access_mask|=GENERIC_READ;
			if (flags&write_flag)
				access_mask|=GENERIC_WRITE;

			assert(access_mask&(GENERIC_READ|GENERIC_WRITE));

			HANDLE new_handle=CreateFile(file_name,access_mask,FILE_SHARE_READ,NULL,
				OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);

			if (new_handle==INVALID_HANDLE_VALUE)
			{
				throw file_error("could not open file");
			}
			// will only close old file if the open succeeded
			close();
			m_file_handle=new_handle;
		}

		void close()
		{
			if (m_file_handle!=INVALID_HANDLE_VALUE)
			{
				CloseHandle(m_file_handle);
				m_file_handle=INVALID_HANDLE_VALUE;
			}
		}

		~impl()
		{
			close();
		}

		size_type write(const char* buffer, size_type num_bytes)
		{
			assert(buffer);
			assert(num_bytes>0);
			assert((DWORD)num_bytes==num_bytes);
			DWORD bytes_written=0;
			if (num_bytes!=0)
			{
				if (FALSE == WriteFile(m_file_handle,buffer,(DWORD)num_bytes,&bytes_written,NULL))
					throw file_error("file::impl::write: couldn't write to file");
			}
			return bytes_written;
		}
		
		size_type read(char* buffer, size_type num_bytes)
		{
			assert(buffer);
			assert(num_bytes>0);
			assert((DWORD)num_bytes==num_bytes);

			DWORD bytes_read=0;
			if (num_bytes!=0)
			{
				if (FALSE == ReadFile(m_file_handle,buffer,(DWORD)num_bytes,&bytes_read,NULL))
					throw file_error("file::impl::read: couldn't write to file");
			}
			return bytes_read;
		}

		void seek(size_type pos, seek_mode from_where)
		{
			assert(pos>=0);
			LARGE_INTEGER offs;
			offs.QuadPart=pos;
				// FILE_CURRENT
				// FILE_END
			if (FALSE == SetFilePointerEx(m_file_handle,offs,&offs,from_where))
				throw file_error("file::impl::seek: error");
		}
		
		size_type tell()
		{
			LARGE_INTEGER offs;
			offs.QuadPart=0;

			// is there any other way to get offset?
			if (FALSE == SetFilePointerEx(m_file_handle,offs,&offs,FILE_CURRENT))
				throw file_error("file::impl::tell: error");

			size_type pos=offs.QuadPart;
			assert(pos>=0);
			return pos;
		}

		size_type size()
		{
			LARGE_INTEGER s;
			if (FALSE == GetFileSizeEx(m_file_handle,&s))
				throw file_error("file::impl::size: error");
			
			size_type size=s.QuadPart;
			assert(size>=0);
			return size;
		}
	private:
		HANDLE m_file_handle;
	};
}

namespace libtorrent {

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
		m_impl->open(p.native_file_string().c_str(),impl::open_flags(m.m_mask));
	}

	void file::close()
	{
		m_impl->close();
	}

	size_type file::write(const char* buffer, size_type num_bytes)
	{
		return m_impl->write(buffer,num_bytes);
	}

	size_type file::read(char* buffer, size_type num_bytes)
	{
		return m_impl->read(buffer,num_bytes);
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