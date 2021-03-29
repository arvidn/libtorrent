/*

Copyright (c) 2019-2020, Arvid Norberg
Copyright (c) 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/upnp.hpp"
#include "libtorrent/aux_/xml_parse.hpp"

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
	using namespace std::placeholders;

	lt::parse_state s;
	lt::aux::xml_parse({reinterpret_cast<char const*>(data), size}
		, std::bind(&lt::find_control_url, _1, _2, std::ref(s)));
	return 0;
}
