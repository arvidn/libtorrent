/*

Copyright (c) 2012, Arvid Norberg
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

#include <string>
#include <string.h>
#include <stdio.h>
#include <boost/cstdint.hpp>
#include <iconv.h>
#include <vector>
#include <mutex>

#include "escape_json.hpp"
#include "libtorrent/utf8.hpp"
#include "libtorrent/assert.hpp"

namespace libtorrent
{

std::string escape_json(string_view input)
{
	if (input.empty()) return "";

	std::vector<std::uint32_t> wide;
	wide.resize(input.size());

	static std::mutex iconv_mutex;
	// only one thread can use this handle at a time
	std::unique_lock<std::mutex> l(iconv_mutex);

	static iconv_t iconv_handle = iconv_open("UTF-32", "UTF-8");
	if (iconv_handle == iconv_t(-1)) return "(iconv error)";

	size_t insize = input.size();
	size_t outsize = insize * sizeof(std::uint32_t);
	char const* in = input.data();
	char* out = reinterpret_cast<char*>(wide.data());
	size_t retval = iconv(iconv_handle, (char**)&in, &insize
		, &out, &outsize);
	l.unlock();

	if (retval == (size_t)-1) return "(iconv error)";
	if (insize != 0) return "(iconv error)";
	if (outsize > input.size() * 4) return "(iconv error)";
	TORRENT_ASSERT(wide.size() >= outsize);
	wide.resize(wide.size() - outsize / sizeof(std::uint32_t));

	std::string ret;
	for (std::vector<std::uint32_t>::const_iterator s = wide.begin(); s != wide.end(); ++s)
	{
		if (*s > 0x1f && *s < 0x80 && *s != '"' && *s != '\\')
		{
			ret += *s;
		}
		else
		{
			ret += '\\';
			switch(*s)
			{
				case '"': ret += '"'; break;
				case '\\': ret += '\\'; break;
				case '\n': ret += 'n'; break;
				case '\r': ret += 'r'; break;
				case '\t': ret += 't'; break;
				case '\b': ret += 'b'; break;
				case '\f': ret += 'f'; break;
				default:
				{
					char buf[20];
					snprintf(buf, sizeof(buf), "u%04x", std::uint16_t(*s));
					ret += buf;
				}
			}
		}
	}
	return ret;
}

}

