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
#include "libtorrent/string_util.hpp"

namespace libtorrent
{

	TORRENT_EXTRA_EXPORT void xml_parse(char const* p, char const* end
		, boost::function<void(int,char const*,int,char const*,int)> callback)
	{
		for(;p != end; ++p)
		{
			char const* start = p;
			int token;
			// look for tag start
			for(; p != end && *p != '<'; ++p);

			if (p != start)
			{
				token = xml_string;
				const int name_len = p - start;
				callback(token, start, name_len, NULL, 0);
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
					callback(token, start, strlen(start), NULL, 0);
					break;
				}

				token = xml_string;
				const int name_len = p - start - 2;
				callback(token, start, name_len, NULL, 0);
				continue;
			}

			// parse the name of the tag.
			for (start = p; p != end && *p != '>' && !is_space(*p); ++p);

			char const* tag_name_end = p;

			// skip the attributes for now
			for (; p != end && *p != '>'; ++p);

			// parse error
			if (p == end)
			{
				token = xml_parse_error;
				start = "unexpected end of file";
				callback(token, start, strlen(start), NULL, 0);
				break;
			}

			TORRENT_ASSERT(*p == '>');

			char const* tag_end = p;
			if (*start == '/')
			{
				++start;
				token = xml_end_tag;
				const int name_len = tag_name_end - start;
				callback(token, start, name_len, NULL, 0);
			}
			else if (*(p-1) == '/')
			{
				token = xml_empty_tag;
				const int name_len = (std::min)(tag_name_end - start, p - start - 1);
				callback(token, start, name_len, NULL, 0);
				tag_end = p - 1;
			}
			else if (*start == '?' && *(p-1) == '?')
			{
				++start;
				token = xml_declaration_tag;
				const int name_len = (std::min)(tag_name_end - start, p - start - 1);
				callback(token, start, name_len, NULL, 0);
				tag_end = p - 1;
			}
			else if (start + 5 < p && std::memcmp(start, "!--", 3) == 0 && std::memcmp(p-2, "--", 2) == 0)
			{
				start += 3;
				token = xml_comment;
				const int name_len = tag_name_end - start - 2;
				callback(token, start, name_len, NULL, 0);
				tag_end = p - 2;
				continue;
			}
			else
			{
				token = xml_start_tag;
				const int name_len = tag_name_end - start;
				callback(token, start, name_len, NULL, 0);
			}

			// parse attributes
			for (char const* i = tag_name_end; i < tag_end; ++i)
			{
				char const* val_start = NULL;

				// find start of attribute name
				for (; i != tag_end && is_space(*i); ++i);
				if (i == tag_end) break;
				start = i;
				// find end of attribute name
				for (; i != tag_end && *i != '=' && !is_space(*i); ++i);
				const int name_len = i - start;

				// look for equality sign
				for (; i != tag_end && *i != '='; ++i);

				// no equality sign found. Report this as xml_tag_content
				// instead of a series of key value pairs
				if (i == tag_end)
				{
					token = xml_tag_content;
					callback(token, start, i - start, NULL, 0);
					break;
				}

				++i;
				for (; i != tag_end && is_space(*i); ++i);
				// check for parse error (values must be quoted)
				if (i == tag_end || (*i != '\'' && *i != '\"'))
				{
					token = xml_parse_error;
					start = "unquoted attribute value";
					callback(token, start, strlen(start), NULL, 0);
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
					start = "missing end quote on attribute";
					callback(token, start, strlen(start), NULL, 0);
					break;
				}
				const int val_len = i - val_start;
				token = xml_attribute;
				callback(token, start, name_len, val_start, val_len);
			}
		}
	}

}

