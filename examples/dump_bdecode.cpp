/*

Copyright (c) 2019-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <iostream>
#include <fstream>

#include "libtorrent/bencode.hpp"
#include "libtorrent/bdecode.hpp"
#include "libtorrent/span.hpp"

namespace {

std::vector<char> load_file(char const* filename)
{
	std::fstream in;
	in.exceptions(std::ifstream::failbit);
	in.open(filename, std::ios_base::in | std::ios_base::binary);
	in.seekg(0, std::ios_base::end);
	size_t const size = size_t(in.tellg());
	in.seekg(0, std::ios_base::beg);
	std::vector<char> ret(size);
	in.read(ret.data(), int(size));
	return ret;
}

[[noreturn]] void print_usage()
{
	std::cerr << R"(usage: dump_bdecode file [options]
    OPTIONS:
    --items-limit <count>    set the upper limit of the number of bencode items
                             in the bencoded file.
    --depth-limit <count>    set the recursion limit in the bdecoder
)";
	std::exit(1);
}

} // anonymous namespace

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
