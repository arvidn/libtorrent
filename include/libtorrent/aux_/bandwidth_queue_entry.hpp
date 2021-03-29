/*

Copyright (c) 2007, 2009, 2012, 2014-2015, 2020, Arvid Norberg
Copyright (c) 2016, 2018, 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_BANDWIDTH_QUEUE_ENTRY_HPP_INCLUDED
#define TORRENT_BANDWIDTH_QUEUE_ENTRY_HPP_INCLUDED

#include <memory>

#include "libtorrent/aux_/bandwidth_limit.hpp"
#include "libtorrent/aux_/bandwidth_socket.hpp"
#include "libtorrent/aux_/array.hpp"

namespace lt::aux {

struct TORRENT_EXTRA_EXPORT bw_request
{
	bw_request(std::shared_ptr<bandwidth_socket> pe
		, int blk, int prio);

	std::shared_ptr<bandwidth_socket> peer;
	// 1 is normal prio
	int priority;
	// the number of bytes assigned to this request so far
	int assigned;
	// once assigned reaches this, we dispatch the request function
	int request_size;

	// the max number of rounds for this request to survive
	// this ensures that requests gets responses at very low
	// rate limits, when the requested size would take a long
	// time to satisfy
	int ttl;

	// loops over the bandwidth channels and assigns bandwidth
	// from the most limiting one
	int assign_bandwidth();

	static constexpr int max_bandwidth_channels = 10;
	// we don't actually support more than 10 channels per peer
	aux::array<bandwidth_channel*, max_bandwidth_channels> channel{};
};

}

#endif
