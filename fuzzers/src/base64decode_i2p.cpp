/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/escape_string.hpp"
#include "libtorrent/error_code.hpp"

#include <algorithm>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
	lt::string_view const input(reinterpret_cast<char const*>(data), size);
	try
	{
		std::vector<char> const decoded = lt::base64decode_i2p(input);

		// the decoder only accepts canonical encodings, so anything it accepts
		// must re-encode to exactly the same string. The i2p alphabet is the
		// standard one with '+' and '/' replaced
		std::string encoded = lt::base64encode(std::string(decoded.begin(), decoded.end()));
		std::replace(encoded.begin(), encoded.end(), '+', '-');
		std::replace(encoded.begin(), encoded.end(), '/', '~');
		if (encoded != input)
			std::abort();
	}
	catch (lt::system_error const&)
	{}
	return 0;
}
