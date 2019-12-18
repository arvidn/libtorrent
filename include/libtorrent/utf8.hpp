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

#include "libtorrent/aux_/export.hpp"

#include <cstdint>
#include <string>
#include <cwchar>

#include "libtorrent/string_view.hpp"
#include "libtorrent/error_code.hpp"

namespace libtorrent {

	namespace utf8_errors
	{
		// internal
		enum error_code_enum
		{
			// conversion successful
			conversion_ok,

			// partial character in source, but hit end
			source_exhausted,

			// insufficient room in target for conversion
			target_exhausted,

			// source sequence is illegal/malformed
			source_illegal
		};

		// hidden
		TORRENT_EXPORT error_code make_error_code(error_code_enum e);
	}

	TORRENT_EXPORT boost::system::error_category const& utf8_category();

	// ``utf8_wchar`` converts a UTF-8 string (``utf8``) to a wide character
	// string (``wide``). ``wchar_utf8`` converts a wide character string
	// (``wide``) to a UTF-8 string (``utf8``). The return value is one of
	// the enumeration values from utf8_conv_result_t.
	TORRENT_EXTRA_EXPORT std::wstring utf8_wchar(string_view utf8, error_code& ec);
	TORRENT_EXTRA_EXPORT std::wstring utf8_wchar(string_view utf8);
	TORRENT_EXTRA_EXPORT std::string wchar_utf8(wstring_view wide, error_code& ec);
	TORRENT_EXTRA_EXPORT std::string wchar_utf8(wstring_view wide);

	// TODO: 3 take a string_view here
	TORRENT_EXTRA_EXPORT std::pair<std::int32_t, int>
		parse_utf8_codepoint(string_view str);
}

#endif
