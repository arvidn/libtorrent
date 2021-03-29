/*

Copyright (c) 2004-2005, 2007, 2009, 2012-2018, 2020, Arvid Norberg
Copyright (c) 2015, Mikhail Titov
Copyright (c) 2016, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_ESCAPE_STRING_HPP_INCLUDED
#define TORRENT_ESCAPE_STRING_HPP_INCLUDED

#include <string>
#include "libtorrent/config.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/string_view.hpp"
#include "libtorrent/flags.hpp"

namespace lt {

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
	TORRENT_EXTRA_EXPORT std::string maybe_url_encode(string_view url);

	TORRENT_EXTRA_EXPORT string_view trim(string_view);
	TORRENT_EXTRA_EXPORT string_view::size_type find(string_view haystack
		, string_view needle, string_view::size_type pos);

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

	TORRENT_EXTRA_EXPORT std::string read_until(char const*& str, char delim
		, char const* end);

#if defined TORRENT_WINDOWS
	TORRENT_EXTRA_EXPORT std::wstring convert_to_wstring(std::string const& s);
	TORRENT_EXTRA_EXPORT std::string convert_from_wstring(std::wstring const& s);
#endif

#if TORRENT_NATIVE_UTF8
	inline std::string const& convert_to_native(std::string const& s) { return s; }
	inline std::string const& convert_from_native(std::string const& s) { return s; }
#else
	TORRENT_EXTRA_EXPORT std::string convert_to_native(std::string const& s);
	TORRENT_EXTRA_EXPORT std::string convert_from_native(std::string const& s);
#endif
}

#endif // TORRENT_ESCAPE_STRING_HPP_INCLUDED
