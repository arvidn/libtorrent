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
#include <stdexcept>

#include <boost/noncopyable.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/cstdint.hpp>

namespace libtorrent
{

	struct file_error: std::runtime_error
	{
		file_error(std::string const& msg): std::runtime_error(msg) {}
	};

	class file: public boost::noncopyable
	{
	public:

		typedef boost::int64_t size_type;

		class seek_mode
		{
		friend class file;
		private:
			seek_mode(int v): m_val(v) {}
			int m_val;
		};

		const static seek_mode begin;
		const static seek_mode end;

		class open_mode
		{
		friend class file;
		public:

			open_mode(): m_mask(0) {}

			open_mode operator|(open_mode m) const
			{ return open_mode(m.m_mask | m_mask); }

			open_mode operator|=(open_mode m)
			{
				m_mask |= m.m_mask;
				return *this;
			}

		private:

			open_mode(int val): m_mask(val) {}
			int m_mask;
		};

		const static open_mode in;
		const static open_mode out;

		file();
		file(boost::filesystem::path const& p, open_mode m);
		~file();

		void open(boost::filesystem::path const& p, open_mode m);
		void close();

		size_type write(const char*, size_type num_bytes);
		size_type read(char*, size_type num_bytes);

		void seek(size_type pos, seek_mode m = begin);
		size_type tell();

	private:

		struct impl;
		const std::auto_ptr<impl> m_impl;

	};

}

#endif // TORRENT_FILE_HPP_INCLUDED

