/*

Copyright (c) 2007, Arvid Norberg
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

#ifndef TORRENT_XML_PARSE_HPP
#define TORRENT_XML_PARSE_HPP

namespace libtorrent
{
	const int xml_start_tag = 0;
	const int xml_end_tag = 1;
	const int xml_string = 2;

	template <class CallbackType>	
	void xml_parse(char* p, char* end, CallbackType callback)
	{
		for(;p != end; ++p)
		{
			char const* start = p;
			// look for tag start
			for(; *p != '<' && p != end; ++p);

			if (p != start)
			{
				if (p != end)
				{
					assert(*p == '<');
					*p = 0;
				}
				callback(xml_string, start);
				if (p != end) *p = '<';
			}
		
			if (p == end) break;
		
			// skip '<'
			++p;	

			// parse the name of the tag. Ignore attributes
			for (start = p; p != end && *p != '>'; ++p)
			{
				// terminate the string at the first space
				// to ignore tag attributes
				if (*p == ' ') *p = 0;
			}

			// parse error
			if (p == end) break;
			
			assert(*p == '>');
			*p = 0;

			if (*start == '/')
			{
				++start;
				callback(xml_end_tag, start);
			}
			else
			{
				callback(xml_start_tag, start);
			}
			*p = '>';
		}
	
	}

}


#endif

