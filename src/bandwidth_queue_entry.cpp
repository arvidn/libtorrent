/*

Copyright (c) 2009, Georg Rudoy
Copyright (c) 2009, 2012, 2016-2017, 2019-2020, Arvid Norberg
Copyright (c) 2018, 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <cstdint>
#include <algorithm>

#include "libtorrent/aux_/bandwidth_queue_entry.hpp"

namespace lt::aux {

	bw_request::bw_request(std::shared_ptr<bandwidth_socket> pe
		, int blk, int prio)
		: peer(std::move(pe))
		, priority(prio)
		, assigned(0)
		, request_size(blk)
		, ttl(20)
	{
		TORRENT_ASSERT(priority > 0);
	}

	int bw_request::assign_bandwidth()
	{
		TORRENT_ASSERT(assigned < request_size);
		int quota = request_size - assigned;
		TORRENT_ASSERT(quota >= 0);
		--ttl;
		if (quota == 0) return quota;

		for (int j = 0; j < 5 && channel[j]; ++j)
		{
			if (channel[j]->throttle() == 0) continue;
			if (channel[j]->tmp == 0) continue;
			quota = std::min(int(std::int64_t(channel[j]->distribute_quota)
				* priority / channel[j]->tmp), quota);
		}
		assigned += quota;
		for (int j = 0; j < 5 && channel[j]; ++j)
			channel[j]->use_quota(quota);
		TORRENT_ASSERT(assigned <= request_size);
		return quota;
	}

	static_assert(std::is_nothrow_move_constructible<bw_request>::value
		, "should be nothrow move constructible");
	static_assert(std::is_nothrow_move_assignable<bw_request>::value
		, "should be nothrow move assignable");
}
