/*

Copyright (c) 2007-2016, Arvid Norberg
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


#include "libtorrent/xml_parse.hpp"

namespace libtorrent
{

	TORRENT_EXTRA_EXPORT void xml_parse(char* p, char* end
		, boost::function<void(int,char const*,char const*)> callback)
	{
		for(;p != end; ++p)
		{
			char const* start = p;
			char const* val_start = 0;
			int token;
			// look for tag start
			for(; p != end && *p != '<'; ++p);

			if (p != start)
			{
				if (p != end)
				{
					TORRENT_ASSERT(*p == '<');
					*p = 0;
				}
				token = xml_string;
				callback(token, start, val_start);
				if (p != end) *p = '<';
			}

			if (p == end) break;
		
			// skip '<'
			++p;
			if (p != end && p+8 < end && string_begins_no_case("![CDATA[", p))
			{
				// CDATA. match '![CDATA['
				p += 8;
				start = p;
				while (p != end && !string_begins_no_case("]]>", p-2)) ++p;

				// parse error
				if (p == end)
				{
					token = xml_parse_error;
					start = "unexpected end of file";
					callback(token, start, val_start);
					break;
				}
			
				token = xml_string;
				char tmp = p[-2];
				p[-2] = 0;
				callback(token, start, val_start);
				p[-2] = tmp;
				continue;
			}

			// parse the name of the tag.
			for (start = p; p != end && *p != '>' && !is_space(*p); ++p);

			char* tag_name_end = p;

			// skip the attributes for now
			for (; p != end && *p != '>'; ++p);

			// parse error
			if (p == end)
			{
				token = xml_parse_error;
				start = "unexpected end of file";
				callback(token, start, val_start);
				break;
			}
			
			TORRENT_ASSERT(*p == '>');
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
			else if (*start == '?' && *(p-1) == '?')
			{
				*(p-1) = 0;
				++start;
				token = xml_declaration_tag;
				callback(token, start, val_start);
				*(p-1) = '?';
				tag_end = p - 1;
			}
			else if (start + 5 < p && std::memcmp(start, "!--", 3) == 0 && std::memcmp(p-2, "--", 2) == 0)
			{
				start += 3;
				*(p-2) = 0;
				token = xml_comment;
				callback(token, start, val_start);
				*(p-2) = '-';
				tag_end = p - 2;
				continue;
			}
			else
			{
				token = xml_start_tag;
				callback(token, start, val_start);
			}

			*tag_name_end = save;

			// parse attributes
			for (char* i = tag_name_end; i < tag_end; ++i)
			{
				// find start of attribute name
				for (; i != tag_end && is_space(*i); ++i);
				if (i == tag_end) break;
				start = i;
				// find end of attribute name
				for (; i != tag_end && *i != '=' && !is_space(*i); ++i);
				char* name_end = i;

				// look for equality sign
				for (; i != tag_end && *i != '='; ++i);

				// no equality sign found. Report this as xml_tag_content
				// instead of a series of key value pairs
				if (i == tag_end)
				{
					char tmp = *i;
					*i = 0; // null terminate the content string
					token = xml_tag_content;
					val_start = 0;
					callback(token, start, val_start);
					*i = tmp;
					break;
				}

				++i;
				for (; i != tag_end && is_space(*i); ++i);
				// check for parse error (values must be quoted)
				if (i == tag_end || (*i != '\'' && *i != '\"'))
				{
					token = xml_parse_error;
					val_start = 0;
					start = "unquoted attribute value";
					callback(token, start, val_start);
					break;
				}
				char quote = *i;
				++i;
				val_start = i;
				for (; i != tag_end && *i != quote; ++i);
				// parse error (missing end quote)
				if (i == tag_end)
				{
					token = xml_parse_error;
					val_start = 0;
					start = "missing end quote on attribute";
					callback(token, start, val_start);
					break;
				}
				save = *i;
				*i = 0;
				*name_end = 0;
				token = xml_attribute;
				callback(token, start, val_start);
				*name_end = '=';
				*i = save;
			}
		}
	}

}

