/*

Copyright (c) 2007, Arvid Norberg
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

#include "libtorrent/assert.hpp"
#include "libtorrent/puff.hpp"

#include <vector>
#include <string>

namespace
{
	enum
	{
		FTEXT = 0x01,
		FHCRC = 0x02,
		FEXTRA = 0x04,
		FNAME = 0x08,
		FCOMMENT = 0x10,
		FRESERVED = 0xe0,

		GZIP_MAGIC0 = 0x1f,
		GZIP_MAGIC1 = 0x8b
	};

}

namespace libtorrent
{
	// returns -1 if gzip header is invalid or the header size in bytes
	int gzip_header(const char* buf, int size)
	{
		TORRENT_ASSERT(buf != 0);

		const unsigned char* buffer = reinterpret_cast<const unsigned char*>(buf);
		const int total_size = size;

		// The zip header cannot be shorter than 10 bytes
		if (size < 10 || buf == 0) return -1;

		// check the magic header of gzip
		if ((buffer[0] != GZIP_MAGIC0) || (buffer[1] != GZIP_MAGIC1)) return -1;

		int method = buffer[2];
		int flags = buffer[3];

		// check for reserved flag and make sure it's compressed with the correct metod
		if (method != 8 || (flags & FRESERVED) != 0) return -1;

		// skip time, xflags, OS code
		size -= 10;
		buffer += 10;

		if (flags & FEXTRA)
		{
			int extra_len;

			if (size < 2) return -1;

			extra_len = (buffer[1] << 8) | buffer[0];

			if (size < (extra_len+2)) return -1;
			size -= (extra_len + 2);
			buffer += (extra_len + 2);
		}

		if (flags & FNAME)
		{
			while (size && *buffer)
			{
				--size;
				++buffer;
			}
			if (!size || *buffer) return -1;

			--size;
			++buffer;
		}

		if (flags & FCOMMENT)
		{
			while (size && *buffer)
			{
				--size;
				++buffer;
			}
			if (!size || *buffer) return -1;

			--size;
			++buffer;
		}

		if (flags & FHCRC)
		{
			if (size < 2) return -1;

			size -= 2;
			buffer += 2;
		}

		return total_size - size;
	}

	// TODO: 2 it would be nice to use proper error handling here
	bool inflate_gzip(
		char const* in
		, int size
		, std::vector<char>& buffer
		, int maximum_size
		, std::string& error)
	{
		TORRENT_ASSERT(maximum_size > 0);

		int header_len = gzip_header(in, size);
		if (header_len < 0)
		{
			error = "invalid gzip header";
			return true;
		}

		// start off with 4 kilobytes and grow
		// if needed
		boost::uint32_t destlen = 4096;
		int ret = 0;
		do
		{
			TORRENT_TRY {
				buffer.resize(destlen);
			} TORRENT_CATCH(std::exception& e) {
				error = "out of memory";
				return true;
			}
   
			boost::uint32_t srclen = size - header_len;
			in += header_len;
			ret = puff((unsigned char*)&buffer[0], &destlen, (unsigned char*)in, &srclen);
   
			// if the destination buffer wasn't large enough, double its
			// size and try again. Unless it's already at its max, in which
			// case we fail
			if (ret == -1)
			{
				if (destlen == maximum_size)
				{
					error = "inflated data too big";
					return true;
				}

				destlen *= 2;
				if (destlen > maximum_size)
					destlen = maximum_size;
				continue;
			}
		} while (false);

		if (destlen > buffer.size())
		{
			error = "internal gzip error";
			return true;
		}

		buffer.resize(destlen);

		if (ret != 0)
		{
			error = "error while inflating data";
			return true;
		}
		return false;
	}

}

