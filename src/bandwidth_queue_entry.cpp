/*

Copyright (c) 2009, 2012, 2016-2017, 2019-2020, Arvid Norberg
Copyright (c) 2009, Georg Rudoy
Copyright (c) 2018, 2020, Alden Torres
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

#include <cstdint>
#include <algorithm>

#include "libtorrent/aux_/bandwidth_queue_entry.hpp"

namespace libtorrent {
namespace aux {

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
}
