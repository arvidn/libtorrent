/*

Copyright (c) 2007-2014, Arvid Norberg
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

#ifndef TORRENT_GZIP_HPP_INCLUDED
#define TORRENT_GZIP_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/error_code.hpp"

#include <vector>

namespace libtorrent
{

	TORRENT_EXTRA_EXPORT void inflate_gzip(
		char const* in, int size
		, std::vector<char>& buffer
		, int maximum_size
		, error_code& error);

	// get the ``error_category`` for zip errors
	TORRENT_EXPORT boost::system::error_category& get_gzip_category();

	namespace gzip_errors
	{
		// libtorrent uses boost.system's ``error_code`` class to represent errors. libtorrent has
		// its own error category get_gzip_category() whith the error codes defined by error_code_enum.
		enum error_code_enum
		{
			// Not an error
			no_error = 0,

			// the supplied gzip buffer has invalid header
			invalid_gzip_header,

			// the gzip buffer would inflate to more bytes than the specified
			// maximum size, and was rejected.
			inflated_data_too_large,

			// available inflate data did not terminate
			data_did_not_terminate,

			// output space exhausted before completing inflate
			space_exhausted,

			// invalid block type (type == 3)
 			invalid_block_type,

			// stored block length did not match one's complement
			invalid_stored_block_length,
			
			// dynamic block code description: too many length or distance codes
			too_many_length_or_distance_codes,

			// dynamic block code description: code lengths codes incomplete
			code_lengths_codes_incomplete,

			// dynamic block code description: repeat lengths with no first length
			repeat_lengths_with_no_first_length,

			// dynamic block code description: repeat more than specified lengths
			repeat_more_than_specified_lengths,

			// dynamic block code description: invalid literal/length code lengths
			invalid_literal_length_code_lengths,

			// dynamic block code description: invalid distance code lengths
			invalid_distance_code_lengths,

			// invalid literal/length or distance code in fixed or dynamic block
			invalid_literal_code_in_block,

			// distance is too far back in fixed or dynamic block
			distance_too_far_back_in_block,

			// an unknown error occurred during gzip inflation
			unknown_gzip_error,

			// the number of error codes
			error_code_max
		};

		// hidden
		TORRENT_EXPORT boost::system::error_code make_error_code(error_code_enum e);
	}

}

#if BOOST_VERSION >= 103500
namespace boost { namespace system {

template<>
struct is_error_code_enum<libtorrent::gzip_errors::error_code_enum>
{ static const bool value = true; };

template<>
struct is_error_condition_enum<libtorrent::gzip_errors::error_code_enum>
{ static const bool value = true; };

} }
#endif // BOOST_VERSION

#endif

