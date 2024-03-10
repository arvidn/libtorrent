/*

Copyright (c) 2023, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_VISIT_BLOCK_IOVECS
#define TORRENT_VISIT_BLOCK_IOVECS

#include "libtorrent/span.hpp"
#include "libtorrent/aux_/alloca.hpp"

namespace libtorrent::aux {

// Fun is a function object that's called with f(span<span<char const>>, int)
// and is expected to return a bool. true=interrupt, false=continue
template <typename Fun, typename BlockEntry>
void visit_block_iovecs(span<BlockEntry const> blocks
	, Fun const& f)
{
	TORRENT_ASSERT(blocks.size() > 0);
	TORRENT_ALLOCA(iovec, span<char const>, blocks.size());

	int count = 0;

	int start_idx = 0;
	int idx = 0;

	for (auto& be : blocks)
	{
		auto const buf = be.write_buf();
		if (count > 0 && buf.empty())
		{
			bool const interrupt = f(iovec.first(count), start_idx);
			if (interrupt) return;

			start_idx = idx;
			count = 0;
		}

		if (buf.empty())
		{
			++idx;
			start_idx = idx;
			continue;
		}

		iovec[count] = buf;
		++count;
		++idx;
	}

	if (count > 0)
	{
		f(iovec.first(count), start_idx);
	}
}

}

#endif
