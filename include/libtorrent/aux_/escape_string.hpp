/*

Copyright (c) 2003-2014, Arvid Norberg
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
#include "libtorrent/config.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/string_view.hpp"
#include "libtorrent/flags.hpp"

namespace libtorrent {

	// hidden
	using encode_string_flags_t = flags::bitfield_flag<std::uint8_t, struct encode_string_flags_tag>;

	namespace string
	{
		// use lower case alphabet used with i2p
		constexpr encode_string_flags_t lowercase = 0_bit;
		// don't insert padding
		constexpr encode_string_flags_t no_padding = 1_bit;
		// shortcut used for addresses as sha256 hashes
		constexpr encode_string_flags_t i2p = lowercase | no_padding;
	}

	TORRENT_EXTRA_EXPORT std::string unescape_string(string_view s, error_code& ec);
	// replaces all disallowed URL characters by their %-encoding
	TORRENT_EXTRA_EXPORT std::string escape_string(string_view str);
	// same as escape_string but does not encode '/'
	TORRENT_EXTRA_EXPORT std::string escape_path(string_view str);
	// if the url does not appear to be encoded, and it contains illegal url characters
	// it will be encoded
	TORRENT_EXTRA_EXPORT std::string maybe_url_encode(std::string const& url);

	TORRENT_EXTRA_EXPORT string_view trim(string_view);
	TORRENT_EXTRA_EXPORT string_view::size_type find(string_view haystack
		, string_view needle, string_view::size_type pos);

#if TORRENT_ABI_VERSION == 1
	// deprecated in 1.2
	// convert a file://-URL to a proper path
	TORRENT_EXTRA_EXPORT std::string resolve_file_url(std::string const& url);
#endif

	// returns true if the given string (not 0-terminated) contains
	// characters that would need to be escaped if used in a URL
	TORRENT_EXTRA_EXPORT bool need_encoding(char const* str, int len);

	// encodes a string using the base64 scheme
	TORRENT_EXTRA_EXPORT std::string base64encode(std::string const& s);
#if TORRENT_USE_I2P
	// encodes a string using the base32 scheme
	TORRENT_EXTRA_EXPORT std::string base32encode(string_view s, encode_string_flags_t flags = {});
#endif
	TORRENT_EXTRA_EXPORT std::string base32decode(string_view s);

	// replaces \ with /
	TORRENT_EXTRA_EXPORT void convert_path_to_posix(std::string& path);
#ifdef TORRENT_WINDOWS
	TORRENT_EXTRA_EXPORT void convert_path_to_windows(std::string& path);
#endif

	TORRENT_EXTRA_EXPORT std::string read_until(char const*& str, char delim
		, char const* end);

#if defined TORRENT_WINDOWS
	TORRENT_EXTRA_EXPORT std::wstring convert_to_wstring(std::string const& s);
	TORRENT_EXTRA_EXPORT std::string convert_from_wstring(std::wstring const& s);
#endif

#if TORRENT_USE_ICONV || TORRENT_USE_LOCALE || defined TORRENT_WINDOWS
	TORRENT_EXTRA_EXPORT std::string convert_to_native(std::string const& s);
	TORRENT_EXTRA_EXPORT std::string convert_from_native(std::string const& s);
#else
	// internal
	inline std::string const& convert_to_native(std::string const& s) { return s; }
	// internal
	inline std::string const& convert_from_native(std::string const& s) { return s; }
#endif
}

#endif // TORRENT_ESCAPE_STRING_HPP_INCLUDED
