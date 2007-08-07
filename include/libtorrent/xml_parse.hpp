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
	enum
	{
		xml_start_tag = 0,
		xml_end_tag = 1,
		xml_empty_tag = 2,
		xml_string = 3,
		xml_attribute = 4
	};

	// callback(int type, char const* str, char const* str2)
	// str2 is only used for attributes. str is name and str2 is value

	template <class CallbackType>	
	void xml_parse(char* p, char* end, CallbackType callback)
	{
		for(;p != end; ++p)
		{
			char const* start = p;
			char const* val_start = 0;
			int token;
			// look for tag start
			for(; *p != '<' && p != end; ++p);

			if (p != start)
			{
				if (p != end)
				{
					assert(*p == '<');
					*p = 0;
				}
				token = xml_string;
				callback(token, start, val_start);
				if (p != end) *p = '<';
			}

			if (p == end) break;
		
			// skip '<'
			++p;	

			// parse the name of the tag.
			for (start = p; p != end && *p != '>' && *p != ' '; ++p);

			char* tag_name_end = p;

			// skip the attributes for now
			for (; p != end && *p != '>'; ++p);

			// parse error
			if (p == end) break;
			
			assert(*p == '>');
			// save the character that terminated the tag name
			// it could be both '>' and ' '.
			char save = *tag_name_end;
			*tag_name_end = 0;

			char* tag_end = p;
			if (*start == '/')
			{
				++start;
				token = xml_end_tag;
				callback(token, start, val_start);
			}
			else if (*(p-1) == '/')
			{
				*(p-1) = 0;
				token = xml_empty_tag;
				callback(token, start, val_start);
				*(p-1) = '/';
				tag_end = p - 1;
			}
			else
			{
				token = xml_start_tag;
				callback(token, start, val_start);
			}

			*tag_name_end = save;

			// parse attributes
			start = tag_name_end;
			for (char* i = tag_name_end; i < tag_end; ++i)
			{
				if (*i != '=') continue;
				assert(*start == ' ');
				++start;
				val_start = i;
				for (; i != tag_end && *i != ' '; ++i);
				save = *i;
				*i = 0;
				const_cast<char&>(*val_start) = 0;
				++val_start;
				token = xml_attribute;
				callback(token, start, val_start);
				--val_start;
				const_cast<char&>(*val_start) = '=';
				*i = save;
				start = i;
			}
		}
	
	}

}


#endif

