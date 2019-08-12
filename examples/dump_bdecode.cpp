/*

Copyright (c) 2019, Arvid Norberg
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

#include <iostream>
#include <fstream>

#include "libtorrent/bencode.hpp"
#include "libtorrent/bdecode.hpp"
#include "libtorrent/span.hpp"

std::vector<char> load_file(char const* filename)
{
	std::fstream in;
	in.exceptions(std::ifstream::failbit);
	in.open(filename, std::ios_base::in | std::ios_base::binary);
	in.seekg(0, std::ios_base::end);
	size_t const size = size_t(in.tellg());
	in.seekg(0, std::ios_base::beg);
	std::vector<char> ret(size);
	in.read(ret.data(), size);
	return ret;
}

void print_usage()
{
	std::cerr << R"(usage: dump_bdecode file [options]
    OPTIONS:
    --items-limit <count>    set the upper limit of the number of bencode items
                             in the bencoded file.
    --depth-limit <count>    set the recursion limit in the bdecoder
)";
	std::exit(1);
}

int main(int argc, char const* argv[]) try
{
	lt::span<char const*> args(argv, argc);

	// strip executable name
	args = args.subspan(1);

	if (args.empty()) print_usage();

	char const* filename = args[0];
	args = args.subspan(1);

	int max_decode_depth = 1000;
	int max_decode_tokens = 2000000;

	using namespace lt::literals;

	while (!args.empty())
	{
		if (args[0] == "--items-limit"_sv && args.size() > 1)
		{
			max_decode_tokens = atoi(args[1]);
			args = args.subspan(2);
		}
		else if (args[0] == "--depth-limit"_sv && args.size() > 1)
		{
			max_decode_depth = atoi(args[1]);
			args = args.subspan(2);
		}
		else
		{
			std::cerr << "unknown option: " << args[0] << "\n";
			print_usage();
		}
	}

	std::vector<char> buf = load_file(filename);
	int pos = -1;
	lt::error_code ec;
	lt::bdecode_node const e = lt::bdecode(buf, ec, &pos, max_decode_depth
		, max_decode_tokens);

	if (ec) {
		std::cerr << "failed to decode: '" << ec.message() << "' at character: " << pos<< "\n";
		return 1;
	}

	std::printf("%s\n", print_entry(e).c_str());
}
catch (std::exception const& e)
{
	std::cerr << "ERROR: " << e.what() << "\n";
}
