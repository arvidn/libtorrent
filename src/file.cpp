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
	enum { mode_in = 1, mode_out = 2 };

	std::ios_base::openmode map_open_mode(fs::path const& p, int m)
	{
		std::ios_base::openmode ret(std::ios_base::binary);
		if ((m & mode_in) || fs::exists(p)) ret |= std::ios_base::in;
		if (m & mode_out) ret |= std::ios_base::out;
		return ret;
	}
}

namespace libtorrent
{

	const file::open_mode file::in(mode_in);
	const file::open_mode file::out(mode_out);

	const file::seek_mode file::begin(1);
	const file::seek_mode file::end(2);

	// TODO: this implementation will behave strange if
	// a file is opened for both reading and writing
	// and both read from and written to. Since the
	// write-file position isn't updated when reading.
	struct file::impl
	{
		impl(): m_open_mode(0) {}

		impl(fs::path const& path, int mode)
			: m_file(path, map_open_mode(path, mode))
			, m_open_mode(mode)
		{
			assert(mode == mode_in ||mode == mode_out);

			if (m_file.fail())
				throw file_error("open failed '" + path.native_file_string() + "'");
		}

		void open(fs::path const& path, int mode)
		{
			if (m_file.is_open()) m_file.close();
			m_file.clear();
			m_file.open(path, map_open_mode(path, mode));
			m_open_mode = mode;

			assert(mode == mode_in ||mode == mode_out);

			if (m_file.fail())
				throw file_error("open failed '" + path.native_file_string() + "'");
		}

		void close()
		{
			m_file.close();
			m_open_mode = 0;
		}

		size_type read(char* buf, size_type num_bytes)
		{
			assert(m_open_mode == mode_in);
			assert(m_file.is_open());

			// TODO: split the read if num_bytes > 2 gig
			m_file.read(buf, num_bytes);
			return m_file.gcount();
		}

		size_type write(const char* buf, size_type num_bytes)
		{
			assert(m_open_mode == mode_out);
			assert(m_file.is_open());

			// TODO: split the write if num_bytes > 2 gig
			std::ostream::pos_type a = m_file.tellp();
			m_file.write(buf, num_bytes);
			return m_file.tellp() - a;
		}

		void seek(size_type offset, int m)
		{
			assert(m_open_mode);
			assert(m_file.is_open());

			std::ios_base::seekdir d =
				(m == 1) ? std::ios_base::beg : std::ios_base::end;
			m_file.clear();
			if (m_open_mode == mode_in)
				m_file.seekg(offset, d);
			else
				m_file.seekp(offset, d);
		}

		size_type tell()
		{
			assert(m_open_mode);
			assert(m_file.is_open());

			m_file.clear();
			if (m_open_mode == mode_in)
				return m_file.tellg();
			else
				return m_file.tellp();
		}
	
		fs::fstream m_file;
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
