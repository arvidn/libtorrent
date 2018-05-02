/*

Copyright (c) 2006-2018, Arvid Norberg
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

#ifndef TORRENT_UTF8_HPP_INCLUDED
#define TORRENT_UTF8_HPP_INCLUDED

#include "libtorrent/export.hpp"

// on windows we need these functions for
// convert_to_native and convert_from_native
#if TORRENT_USE_WSTRING || defined TORRENT_WINDOWS

#include <boost/cstdint.hpp>
#include <string>
#include <cwchar>

namespace libtorrent
{

	// internal
	// results from UTF-8 conversion functions utf8_wchar and
	// wchar_utf8
	enum utf8_conv_result_t
	{
		// conversion successful
		conversion_ok,

		// partial character in source, but hit end
		source_exhausted,

		// insuff. room in target for conversion
		target_exhausted,

		// source sequence is illegal/malformed
		source_illegal
	};

	// ``utf8_wchar`` converts a UTF-8 string (``utf8``) to a wide character
	// string (``wide``). ``wchar_utf8`` converts a wide character string
	// (``wide``) to a UTF-8 string (``utf8``). The return value is one of
	// the enumeration values from utf8_conv_result_t.
	TORRENT_EXTRA_EXPORT utf8_conv_result_t utf8_wchar(
		const std::string &utf8, std::wstring &wide);
	TORRENT_EXTRA_EXPORT utf8_conv_result_t wchar_utf8(
		const std::wstring &wide, std::string &utf8);

	TORRENT_EXTRA_EXPORT std::pair<boost::int32_t, int>
		parse_utf8_codepoint(char const* str, int len);
}
#endif // !BOOST_NO_STD_WSTRING

#endif

