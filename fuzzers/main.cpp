/*

Copyright (c) 2019, 2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/
#include <iostream>
#include <cstdint>
#include <vector>
#include <fstream>

extern "C" int LLVMFuzzerTestOneInput(uint8_t const*, size_t);

int main(int const argc, char const** argv)
{
	if (argc < 2)
	{
		std::cout << "usage: " << argv[0] << " test-case-file\n";
		return 1;
	}

	std::fstream f(argv[1], std::ios_base::in | std::ios_base::binary);
	f.seekg(0, std::ios_base::end);
	auto const s = f.tellg();
	f.seekg(0, std::ios_base::beg);
	std::vector<std::uint8_t> v(static_cast<std::size_t>(s));
	f.read(reinterpret_cast<char*>(v.data()), v.size());

	return LLVMFuzzerTestOneInput(v.data(), v.size());
}

