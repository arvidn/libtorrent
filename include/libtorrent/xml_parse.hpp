/*

Copyright (c) 2007-2018, Arvid Norberg
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

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <cctype>
#include <cstring>

#include <boost/function.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/config.hpp"
#include "libtorrent/assert.hpp"

namespace libtorrent
{
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
	// neither string is null terminated, but their lengths are specified via
	// name_len and val_len respectively
	TORRENT_EXTRA_EXPORT void xml_parse(char const* p, char const* end
		, boost::function<void(int,char const*,int,char const*,int)> callback);
}


#endif

