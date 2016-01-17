/*

Copyright (c) 2007-2016, Arvid Norberg
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
#include "libtorrent/gzip.hpp"

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
	struct gzip_error_category : boost::system::error_category
	{
		virtual const char* name() const BOOST_SYSTEM_NOEXCEPT;
		virtual std::string message(int ev) const BOOST_SYSTEM_NOEXCEPT;
		virtual boost::system::error_condition default_error_condition(int ev) const BOOST_SYSTEM_NOEXCEPT
		{ return boost::system::error_condition(ev, *this); }
	};

	const char* gzip_error_category::name() const BOOST_SYSTEM_NOEXCEPT
	{
		return "gzip error";
	}

	std::string gzip_error_category::message(int ev) const BOOST_SYSTEM_NOEXCEPT
	{
		static char const* msgs[] =
		{
			"no error",
			"invalid gzip header",
			"inflated data too large",
			"available inflate data did not terminate",
			"output space exhausted before completing inflate",
			"invalid block type (type == 3)",
			"stored block length did not match one's complement",
			"dynamic block code description: too many length or distance codes",
			"dynamic block code description: code lengths codes incomplete",
			"dynamic block code description: repeat lengths with no first length",
			"dynamic block code description: repeat more than specified lengths",
			"dynamic block code description: invalid literal/length code lengths",
			"dynamic block code description: invalid distance code lengths",
			"invalid literal/length or distance code in fixed or dynamic block",
			"distance is too far back in fixed or dynamic block",
			"unknown gzip error",
		};
		if (ev < 0 || ev >= int(sizeof(msgs)/sizeof(msgs[0])))
			return "Unknown error";
		return msgs[ev];
	}

	boost::system::error_category& get_gzip_category()
	{
		static gzip_error_category gzip_category;
		return gzip_category;
	}

	namespace gzip_errors
	{
		boost::system::error_code make_error_code(error_code_enum e)
		{
			return boost::system::error_code(e, get_gzip_category());
		}
	}

	namespace
	{
	// returns -1 if gzip header is invalid or the header size in bytes
	int gzip_header(const char* buf, int size)
	{
		TORRENT_ASSERT(buf != 0);

		const unsigned char* buffer = reinterpret_cast<const unsigned char*>(buf);
		const int total_size = size;

		// gzip is defined in https://tools.ietf.org/html/rfc1952

		// The zip header cannot be shorter than 10 bytes
		if (size < 10 || buf == 0) return -1;

		// check the magic header of gzip
		if ((buffer[0] != GZIP_MAGIC0) || (buffer[1] != GZIP_MAGIC1)) return -1;

		int method = buffer[2];
		int flags = buffer[3];

		// check for reserved flag and make sure it's compressed with the correct metod
		// we only support deflate
		if (method != 8 || (flags & FRESERVED) != 0) return -1;

		// skip time, xflags, OS code. The first 10 bytes of the header:
		// +---+---+---+---+---+---+---+---+---+---+
		// |ID1|ID2|CM |FLG|     MTIME     |XFL|OS | (more-->)
		// +---+---+---+---+---+---+---+---+---+---+

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
//			buffer += 2;
		}

		return total_size - size;
	}
	} // anonymous namespace

	TORRENT_EXTRA_EXPORT void inflate_gzip(
		char const* in
		, int size
		, std::vector<char>& buffer
		, int maximum_size
		, error_code& ec)
	{
		ec.clear();
		TORRENT_ASSERT(maximum_size > 0);

		int header_len = gzip_header(in, size);
		if (header_len < 0)
		{
			ec = gzip_errors::invalid_gzip_header;
			return;
		}

		// start off with 4 kilobytes and grow
		// if needed
		boost::uint32_t destlen = 4096;
		int ret = 0;
		boost::uint32_t srclen = size - header_len;
		in += header_len;

		do
		{
			TORRENT_TRY {
				buffer.resize(destlen);
			} TORRENT_CATCH(std::exception&) {
				ec = errors::no_memory;
				return;
			}

			ret = puff(reinterpret_cast<unsigned char*>(&buffer[0]), &destlen
				, reinterpret_cast<const unsigned char*>(in), &srclen);

			// if the destination buffer wasn't large enough, double its
			// size and try again. Unless it's already at its max, in which
			// case we fail
			if (ret == 1) // 1:  output space exhausted before completing inflate
			{
				if (destlen == boost::uint32_t(maximum_size))
				{
					ec = gzip_errors::inflated_data_too_large;
					return;
				}

				destlen *= 2;
				if (destlen > boost::uint32_t(maximum_size))
					destlen = maximum_size;
			}
		} while (ret == 1);

		if (ret != 0)
		{
			switch (ret)
			{
				case   2: ec = gzip_errors::data_did_not_terminate; return;
				case   1: ec = gzip_errors::space_exhausted; return;
				case  -1: ec = gzip_errors::invalid_block_type; return;
				case  -2: ec = gzip_errors::invalid_stored_block_length; return;
				case  -3: ec = gzip_errors::too_many_length_or_distance_codes; return;
				case  -4: ec = gzip_errors::code_lengths_codes_incomplete; return;
				case  -5: ec = gzip_errors::repeat_lengths_with_no_first_length; return;
				case  -6: ec = gzip_errors::repeat_more_than_specified_lengths; return;
				case  -7: ec = gzip_errors::invalid_literal_length_code_lengths; return;
				case  -8: ec = gzip_errors::invalid_distance_code_lengths; return;
				case  -9: ec = gzip_errors::invalid_literal_code_in_block; return;
				case -10: ec = gzip_errors::distance_too_far_back_in_block; return;
				default: ec = gzip_errors::unknown_gzip_error; return;
			}
		}

		if (destlen > buffer.size())
		{
			ec = gzip_errors::unknown_gzip_error;
			return;
		}

		buffer.resize(destlen);
	}

}

