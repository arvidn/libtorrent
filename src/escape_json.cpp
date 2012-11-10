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
#include <stdio.h>

#include "escape_json.hpp"

namespace libtorrent
{

std::string escape_json(std::string const& in)
{
	std::string ret;
	for (std::string::const_iterator s = in.begin(); s != in.end(); ++s)
	{
		if (*s > 0x1f && *s != '"' && *s != '\\')
		{
			ret += *s;
		}
		else
		{
			ret += '\\';
			switch(*s)
			{
				case '"':
				case '\\':
					ret += *s;
					break;
				case '\n': ret += 'n'; break;
				case '\r': ret += 'r'; break;
				case '\t': ret += 't'; break;
				case '\b': ret += 'b'; break;
				case '\f': ret += 'f'; break;
				default:
				{
					char buf[20];
					snprintf(buf, sizeof(buf), "u0%03o", int(*s));
					ret += buf;
				}
			}
		}
	}
	return ret;
}

}

