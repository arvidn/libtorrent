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

#ifndef TORRENT_ESCAPE_STRING_HPP_INCLUDED
#define TORRENT_ESCAPE_STRING_HPP_INCLUDED

#include <string>
#include <limits>
#include <boost/optional.hpp>
#include <boost/array.hpp>
#include "libtorrent/config.hpp"
#include "libtorrent/size_type.hpp"
#include "libtorrent/error_code.hpp"

namespace libtorrent
{
	TORRENT_EXPORT boost::array<char, 3 + std::numeric_limits<size_type>::digits10> to_string(size_type n);
	TORRENT_EXPORT bool is_digit(char c);
	TORRENT_EXPORT bool is_print(char c);
	TORRENT_EXPORT bool is_space(char c);
	TORRENT_EXPORT char to_lower(char c);

	TORRENT_EXPORT bool string_begins_no_case(char const* s1, char const* s2);
	TORRENT_EXPORT bool string_equal_no_case(char const* s1, char const* s2);

	TORRENT_EXPORT std::string unescape_string(std::string const& s, error_code& ec);
	// replaces all disallowed URL characters by their %-encoding
	TORRENT_EXPORT std::string escape_string(const char* str, int len);
	// same as escape_string but does not encode '/'
	TORRENT_EXPORT std::string escape_path(const char* str, int len);
	// if the url does not appear to be encoded, and it contains illegal url characters
	// it will be encoded
	TORRENT_EXPORT std::string maybe_url_encode(std::string const& url);

	TORRENT_EXPORT bool need_encoding(char const* str, int len);

	// encodes a string using the base64 scheme
	TORRENT_EXPORT std::string base64encode(std::string const& s);
	// encodes a string using the base32 scheme
	TORRENT_EXPORT std::string base32encode(std::string const& s);
	TORRENT_EXPORT std::string base32decode(std::string const& s);

	TORRENT_EXPORT boost::optional<std::string> url_has_argument(
		std::string const& url, std::string argument, size_t* out_pos = 0);

	TORRENT_EXPORT std::string read_until(char const*& str, char delim, char const* end);
	TORRENT_EXPORT std::string to_hex(std::string const& s);
	TORRENT_EXPORT bool is_hex(char const *in, int len);
	TORRENT_EXPORT void to_hex(char const *in, int len, char* out);
	TORRENT_EXPORT bool from_hex(char const *in, int len, char* out);

#if TORRENT_USE_WPATH
	TORRENT_EXPORT std::wstring convert_to_wstring(std::string const& s);
#endif
	
#if defined TORRENT_WINDOWS || TORRENT_USE_LOCALE_FILENAMES
	TORRENT_EXPORT std::string convert_to_native(std::string const& s);
#else
	inline std::string const& convert_to_native(std::string const& s) { return s; }
#endif		
	
}

#endif // TORRENT_ESCAPE_STRING_HPP_INCLUDED

