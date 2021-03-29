/*

Copyright (c) 2014-2020, Arvid Norberg
Copyright (c) 2016-2017, 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <cstring>

#include "libtorrent/aux_/xml_parse.hpp"
#include "libtorrent/aux_/string_util.hpp"

namespace lt::aux {

	void xml_parse(string_view input
		, std::function<void(int, string_view, string_view)> callback)
	{
		char const* p = input.data();
		char const* end = input.data() + input.size();
		for (;p != end; ++p)
		{
			char const* start = p;
			// look for tag start
			for (; p != end && *p != '<'; ++p);

			if (p != start)
			{
				callback(xml_string, {start, std::size_t(p - start)}, {});
			}

			if (p == end) break;

			// skip '<'
			++p;
			if (p != end && p + 8 < end && string_begins_no_case("![CDATA[", p))
			{
				// CDATA. match '![CDATA['
				p += 8;
				start = p;
				while (p != end && !string_begins_no_case("]]>", p - 2)) ++p;

				// parse error
				if (p == end)
				{
					callback(xml_parse_error, "unexpected end of file", {});
					break;
				}

				callback(xml_string, {start, std::size_t(p - start - 2)}, {});
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
				callback(xml_parse_error, "unexpected end of file", {});
				break;
			}

			TORRENT_ASSERT(*p == '>');

			char const* tag_end = p;
			if (*start == '/')
			{
				++start;
				callback(xml_end_tag, {start, std::size_t(tag_name_end - start)}, {});
			}
			else if (*(p - 1) == '/')
			{
				callback(xml_empty_tag, {start, std::size_t(std::min(tag_name_end - start, p - start - 1))}, {});
				tag_end = p - 1;
			}
			else if (*start == '?' && *(p - 1) == '?')
			{
				++start;
				callback(xml_declaration_tag, {start, std::size_t(std::min(tag_name_end - start, p - start - 1))}, {});
				tag_end = p - 1;
			}
			else if (start + 5 < p && std::memcmp(start, "!--", 3) == 0 && std::memcmp(p - 2, "--", 2) == 0)
			{
				start += 3;
				callback(xml_comment, {start, std::size_t(tag_name_end - start - 2)}, {});
				continue;
			}
			else
			{
				callback(xml_start_tag, {start, std::size_t(tag_name_end - start)}, {});
			}

			// parse attributes
			for (char const* i = tag_name_end; i < tag_end; ++i)
			{
				char const* val_start = nullptr;

				// find start of attribute name
				while (i != tag_end && is_space(*i)) ++i;
				if (i == tag_end) break;
				start = i;
				// find end of attribute name
				while (i != tag_end && *i != '=' && !is_space(*i)) ++i;
				auto const name_len = static_cast<std::size_t>(i - start);

				// look for equality sign
				for (; i != tag_end && *i != '='; ++i);

				// no equality sign found. Report this as xml_tag_content
				// instead of a series of key value pairs
				if (i == tag_end)
				{
					callback(xml_tag_content, {start, std::size_t(i - start)}, {});
					break;
				}

				++i;
				while (i != tag_end && is_space(*i)) ++i;
				// check for parse error (values must be quoted)
				if (i == tag_end || (*i != '\'' && *i != '\"'))
				{
					callback(xml_parse_error, "unquoted attribute value", {});
					break;
				}
				char quote = *i;
				++i;
				val_start = i;
				for (; i != tag_end && *i != quote; ++i);
				// parse error (missing end quote)
				if (i == tag_end)
				{
					callback(xml_parse_error, "missing end quote on attribute", {});
					break;
				}
				callback(xml_attribute, {start, name_len}, {val_start, std::size_t(i - val_start)});
			}
		}
	}

}
