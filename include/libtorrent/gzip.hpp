/*

Copyright (c) 2008-2009, 2014-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_GZIP_HPP_INCLUDED
#define TORRENT_GZIP_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/span.hpp"

#include <vector>

namespace lt {

	TORRENT_EXTRA_EXPORT void inflate_gzip(
		span<char const> in
		, std::vector<char>& buffer
		, int maximum_size
		, error_code& ec);

	// get the ``error_category`` for zip errors
	TORRENT_EXPORT boost::system::error_category& gzip_category();

#if TORRENT_ABI_VERSION == 1
	TORRENT_DEPRECATED
	inline boost::system::error_category& get_gzip_category()
	{ return gzip_category(); }
#endif

namespace gzip_errors {
	// libtorrent uses boost.system's ``error_code`` class to represent errors. libtorrent has
	// its own error category get_gzip_category() with the error codes defined by error_code_enum.
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
} // namespace gzip_errors

} // namespace lt

namespace boost {
namespace system {

template<>
struct is_error_code_enum<lt::gzip_errors::error_code_enum>
{ static const bool value = true; };

template<>
struct is_error_condition_enum<lt::gzip_errors::error_code_enum>
{ static const bool value = true; };

}
}

#endif
