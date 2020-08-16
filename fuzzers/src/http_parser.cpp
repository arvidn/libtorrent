/*

Copyright (c) 2017, Arvid Norberg
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

#include "libtorrent/http_parser.hpp"
#include "libtorrent/version.hpp"
#include "libtorrent/string_view.hpp"

void feed_bytes(lt::http_parser& parser, lt::string_view str)
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
	lt::http_parser p;
	feed_bytes(p, {reinterpret_cast<char const*>(data), size});
	return 0;
}

