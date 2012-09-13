/*

Copyright (c) 2012, Arvid Norberg, Magnus Jonsson
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

#include "base64.hpp"

namespace libtorrent
{

static const char b64table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char b64_value(char c)
{
	const char *v = strchr(b64table, c);
	if (v == NULL) return -1;
	return v - b64table;
}

std::string base64decode(std::string const& in)
{
	std::string ret;
	if (in.size() < 4) return ret;

	char const* src = in.c_str();
	char const* end = in.c_str() + in.size();
	while (end - src >= 4)
	{

		char a, b, c, d;
		// skip non base64 characters
		// TODO: make sure src doesn't exceed end
		while ((a = b64_value(src[0])) < 0) ++src;
		while ((b = b64_value(src[1])) < 0) ++src;
		while ((c = b64_value(src[2])) < 0) ++src;
		while ((d = b64_value(src[3])) < 0) ++src;
		ret.push_back((a << 2) | (b >> 4));
		if (src[1] == '=') break;
		ret.push_back((b << 4) | (c >> 2));
		if (src[2] == '=') break;
		ret.push_back((c << 6) | d);
		if (src[3] == '=') break;
		src += 4;
	}
	return ret;
}

}

