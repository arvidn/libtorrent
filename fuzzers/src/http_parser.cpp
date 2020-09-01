/*

Copyright (c) 2019-2020, Arvid Norberg
Copyright (c) 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/http_parser.hpp"
#include "libtorrent/string_view.hpp"

void feed_bytes(lt::aux::http_parser& parser, lt::string_view str)
{
	for (int chunks = 1; chunks < 70; ++chunks)
	{
		parser.reset();
		lt::string_view recv_buf;
		for (;;)
		{
			int const chunk_size = std::min(chunks, int(str.size() - recv_buf.size()));
			if (chunk_size == 0) break;
			recv_buf = str.substr(0, recv_buf.size() + std::size_t(chunk_size));
			bool error = false;
			parser.incoming(recv_buf, error);
			if (error) break;
		}
	}
}

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
	lt::aux::http_parser p;
	feed_bytes(p, {reinterpret_cast<char const*>(data), size});
	return 0;
}

