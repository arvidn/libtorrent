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

namespace libtorrent
{
	boost::array<char, 3 + std::numeric_limits<size_type>::digits10> TORRENT_EXPORT to_string(size_type n);
	bool TORRENT_EXPORT is_digit(char c);
	bool TORRENT_EXPORT isprint(char c);

	std::string TORRENT_EXPORT unescape_string(std::string const& s);
	std::string TORRENT_EXPORT escape_string(const char* str, int len);
	std::string TORRENT_EXPORT escape_path(const char* str, int len);

	// encodes a string using the base64 scheme
	TORRENT_EXPORT std::string base64encode(std::string const& s);
	// encodes a string using the base32 scheme
	TORRENT_EXPORT std::string base32encode(std::string const& s);
	TORRENT_EXPORT std::string base32decode(std::string const& s);

	TORRENT_EXPORT boost::optional<std::string> url_has_argument(
		std::string const& url, std::string argument);

	TORRENT_EXPORT std::string to_hex(std::string const& s);
}

#endif // TORRENT_ESCAPE_STRING_HPP_INCLUDED
