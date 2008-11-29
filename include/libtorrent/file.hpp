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

namespace libtorrent
{
	namespace fs = boost::filesystem;

	class TORRENT_EXPORT file: public boost::noncopyable
	{
	public:

		class seek_mode
		{
		friend class file;
		private:
			seek_mode(int v): m_val(v) {}
			int m_val;
		};

		static const seek_mode begin;
		static const seek_mode end;

		class open_mode
		{
		friend class file;
		public:

			open_mode(): m_mask(0) {}
			open_mode operator|(open_mode m) const
			{ return open_mode(m.m_mask | m_mask); }

			open_mode operator&(open_mode m) const
			{ return open_mode(m.m_mask & m_mask); }

			open_mode operator|=(open_mode m)
			{
				m_mask |= m.m_mask;
				return *this;
			}

			bool operator==(open_mode m) const { return m_mask == m.m_mask; }
			bool operator!=(open_mode m) const { return m_mask != m.m_mask; }
			operator bool() const { return m_mask != 0; }

		private:

			open_mode(int val): m_mask(val) {}
			int m_mask;
		};

		static const open_mode in;
		static const open_mode out;

		file();
		file(fs::path const& p, open_mode m, error_code& ec);
		~file();

		bool open(fs::path const& p, open_mode m, error_code& ec);
		bool is_open() const;
		void close();
		bool set_size(size_type size, error_code& ec);

		size_type write(const char*, size_type num_bytes, error_code& ec);
		size_type read(char*, size_type num_bytes, error_code& ec);

		size_type seek(size_type pos, seek_mode m, error_code& ec);
		size_type tell(error_code& ec);

	private:

#ifdef TORRENT_WINDOWS
		HANDLE m_file_handle;
#else
		int m_fd;
#endif
#ifdef TORRENT_DEBUG
		open_mode m_open_mode;
#endif

	};

}

#endif // TORRENT_FILE_HPP_INCLUDED

