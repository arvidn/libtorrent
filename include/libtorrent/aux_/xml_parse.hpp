/*

Copyright (c) 2007, 2011-2017, 2019-2020, Arvid Norberg
Copyright (c) 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_XML_PARSE_HPP
#define TORRENT_XML_PARSE_HPP

#include <functional>

#include "libtorrent/config.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/string_view.hpp"

namespace lt::aux {

	enum
	{
		xml_start_tag,
		xml_end_tag,
		xml_empty_tag,
		xml_declaration_tag,
		xml_string,
		xml_attribute,
		xml_comment,
		xml_parse_error,
		// used for tags that don't follow the convention of
		// key-value pairs inside the tag brackets. Like !DOCTYPE
		xml_tag_content
	};

	// callback(int type, char const* name, int name_len
	//   , char const* val, int val_len)
	// name is element or attribute name
	// val is attribute value
	// neither string is 0-terminated, but their lengths are specified via
	// name_len and val_len respectively
	TORRENT_EXTRA_EXPORT void xml_parse(string_view input
		, std::function<void(int, string_view, string_view)> callback);
}

#endif
