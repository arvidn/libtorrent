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
#include <boost/filesystem/fstream.hpp>

#include "libtorrent/file.hpp"

namespace fs = boost::filesystem;

namespace
{
	std::ios_base::openmode map_open_mode(fs::path const& p, int m)
	{
		std::ios_base::openmode ret(std::ios_base::binary);
		if ((m & 1) || fs::exists(p)) ret |= std::ios_base::in;
		if (m & 2) ret |= std::ios_base::out;
		return ret;
	}
}

namespace libtorrent
{

	const file::open_mode file::in(1);
	const file::open_mode file::out(2);

	const file::seek_mode file::begin(1);
	const file::seek_mode file::end(2);

	struct file::impl
	{
		impl() {}

		impl(fs::path const& path, int mode)
			: m_file(path, map_open_mode(path, mode))
		{
			if (m_file.fail())
				throw file_error("open failed '" + path.native_file_string() + "'");
		}

		void open(fs::path const& path, int mode)
		{
			if (m_file.is_open()) m_file.close();
			m_file.clear();
			m_file.open(path, map_open_mode(path, mode));

			if (m_file.fail())
				throw file_error("open failed '" + path.native_file_string() + "'");
		}

		void close()
		{
			m_file.close();
		}

		size_type read(char* buf, size_type num_bytes)
		{
			// TODO: split the read if num_bytes > 2 gig
			m_file.read(buf, num_bytes);
			return m_file.gcount();
		}

		size_type write(const char* buf, size_type num_bytes)
		{
			// TODO: split the write if num_bytes > 2 gig
			m_file.write(buf, num_bytes);
			return 0;
		}

		void seek(size_type offset, int m)
		{
			std::ios_base::seekdir d =
				(m == 1) ? std::ios_base::beg : std::ios_base::end;
			m_file.seekp(offset, d);
			m_file.seekg(offset, d);
		}

		size_type tell()
		{
			return std::max(m_file.tellg(), m_file.tellp());
		}
	
		fs::fstream m_file;
	};


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

	file::size_type file::write(const char* buf, file::size_type num_bytes)
	{
		return m_impl->write(buf, num_bytes);
	}

	file::size_type file::read(char* buf, file::size_type num_bytes)
	{
		return m_impl->read(buf, num_bytes);
	}

	void file::seek(file::size_type pos, file::seek_mode m)
	{
		m_impl->seek(pos, m.m_val);
	}

	file::size_type file::tell()
	{
		return m_impl->tell();
	}

}
