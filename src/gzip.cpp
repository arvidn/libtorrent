/*

Copyright (c) 2008, 2010, 2014-2019, Arvid Norberg
Copyright (c) 2017, 2019, Alden Torres
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

#include <string>

namespace {

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

namespace libtorrent {

	struct gzip_error_category final : boost::system::error_category
	{
		const char* name() const BOOST_SYSTEM_NOEXCEPT override;
		std::string message(int ev) const override;
		boost::system::error_condition default_error_condition(int ev) const BOOST_SYSTEM_NOEXCEPT override
		{ return {ev, *this}; }
	};

	const char* gzip_error_category::name() const BOOST_SYSTEM_NOEXCEPT
	{
		return "gzip error";
	}

	std::string gzip_error_category::message(int ev) const
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

	boost::system::error_category& gzip_category()
	{
		static gzip_error_category category;
		return category;
	}

	namespace gzip_errors
	{
		boost::system::error_code make_error_code(error_code_enum e)
		{
			return {e, gzip_category()};
		}
	}

namespace {

	// returns -1 if gzip header is invalid or the header size in bytes
	int gzip_header(span<char const> const in)
	{
		// The zip header cannot be shorter than 10 bytes
		if (in.size() < 10) return -1;

		span<unsigned char const> buffer(
			reinterpret_cast<const unsigned char*>(in.data()), in.size());

		// gzip is defined in https://tools.ietf.org/html/rfc1952

		// check the magic header of gzip
		if ((buffer[0] != GZIP_MAGIC0) || (buffer[1] != GZIP_MAGIC1)) return -1;

		int const method = buffer[2];
		int const flags = buffer[3];

		// check for reserved flag and make sure it's compressed with the correct metod
		// we only support deflate
		if (method != 8 || (flags & FRESERVED) != 0) return -1;

		// skip time, xflags, OS code. The first 10 bytes of the header:
		// +---+---+---+---+---+---+---+---+---+---+
		// |ID1|ID2|CM |FLG|     MTIME     |XFL|OS | (more-->)
		// +---+---+---+---+---+---+---+---+---+---+

		buffer = buffer.subspan(10);

		if (flags & FEXTRA)
		{
			if (buffer.size() < 2) return -1;

			auto const extra_len = (buffer[1] << 8) | buffer[0];
			if (buffer.size() < extra_len + 2) return -1;
			buffer = buffer.subspan(extra_len + 2);
		}

		if (flags & FNAME)
		{
			if (buffer.empty()) return -1;
			while (buffer[0] != 0)
			{
				buffer = buffer.subspan(1);
				if (buffer.empty()) return -1;
			}
			buffer = buffer.subspan(1);
		}

		if (flags & FCOMMENT)
		{
			if (buffer.empty()) return -1;
			while (buffer[0] != 0)
			{
				buffer = buffer.subspan(1);
				if (buffer.empty()) return -1;
			}
			buffer = buffer.subspan(1);
		}

		if (flags & FHCRC)
		{
			if (buffer.size() < 2) return -1;
			buffer = buffer.subspan(2);
		}

		return static_cast<int>(in.size() - buffer.size());
	}
	} // anonymous namespace

	void inflate_gzip(span<char const> in
		, std::vector<char>& buffer
		, int maximum_size
		, error_code& ec)
	{
		ec.clear();
		TORRENT_ASSERT(maximum_size > 0);

		int const header_len = gzip_header(in);
		if (header_len < 0)
		{
			ec = gzip_errors::invalid_gzip_header;
			return;
		}

		// start off with 4 kilobytes and grow
		// if needed
		unsigned long destlen = 4096;
		int ret = 0;
		in = in.subspan(header_len);
		unsigned long srclen = std::uint32_t(in.size());

		do
		{
			try
			{
				buffer.resize(destlen);
			}
			catch (std::exception const&)
			{
				ec = errors::no_memory;
				return;
			}

			ret = puff(reinterpret_cast<unsigned char*>(buffer.data())
				, &destlen
				, reinterpret_cast<const unsigned char*>(in.data())
				, &srclen);

			// if the destination buffer wasn't large enough, double its
			// size and try again. Unless it's already at its max, in which
			// case we fail
			if (ret == 1) // 1:  output space exhausted before completing inflate
			{
				if (destlen == std::uint32_t(maximum_size))
				{
					ec = gzip_errors::inflated_data_too_large;
					return;
				}

				destlen *= 2;
				if (destlen > std::uint32_t(maximum_size))
					destlen = std::uint32_t(maximum_size);
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
