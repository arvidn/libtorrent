/*

Copyright (c) 2019-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <cstdint>
#include <cstdlib>
#include "libtorrent/read_resume_data.hpp"
#include "libtorrent/write_resume_data.hpp"
#include "libtorrent/add_torrent_params.hpp"

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
	lt::error_code ec;
	auto ret = lt::read_resume_data({reinterpret_cast<char const*>(data), int(size)}, ec);

	// a rename to an empty name is meaningless and dropped by
	// import_filenames()/import_path_elements() downstream anyway; it must
	// never survive parsing into add_torrent_params in the first place
	for (auto const& [idx, name] : ret.renamed_files)
		if (name.empty())
			std::abort();
	for (auto const& [idx, name] : ret.renamed_path_elements)
		if (name.empty())
			std::abort();

	auto buf = write_resume_data_buf(ret);
	return 0;
}

