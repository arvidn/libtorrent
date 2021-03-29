/*

Copyright (c) 2007, 2009, 2011-2016, 2020, Arvid Norberg
Copyright (c) 2016, 2018, 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_BANDWIDTH_MANAGER_HPP_INCLUDED
#define TORRENT_BANDWIDTH_MANAGER_HPP_INCLUDED

#include <memory>
#include <vector>

#include "libtorrent/aux_/invariant_check.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/aux_/bandwidth_limit.hpp"
#include "libtorrent/aux_/bandwidth_queue_entry.hpp"
#include "libtorrent/aux_/bandwidth_socket.hpp"
#include "libtorrent/time.hpp"

namespace lt::aux {

struct TORRENT_EXTRA_EXPORT bandwidth_manager
{
	explicit bandwidth_manager(int channel);

	void close();

#if TORRENT_USE_ASSERTS
	bool is_queued(bandwidth_socket const* peer) const;
#endif

	int queue_size() const;
	std::int64_t queued_bytes() const;

	// non prioritized means that, if there's a line for bandwidth,
	// others will cut in front of the non-prioritized peers.
	// this is used by web seeds
	// returns the number of bytes to assign to the peer, or 0
	// if the peer's 'assign_bandwidth' callback will be called later
	int request_bandwidth(std::shared_ptr<bandwidth_socket> peer
		, int blk, int priority, bandwidth_channel** chan, int num_channels);

#if TORRENT_USE_INVARIANT_CHECKS
	void check_invariant() const;
#endif

	void update_quotas(time_duration const& dt);

private:

	// these are the consumers that want bandwidth
	std::vector<bw_request> m_queue;
	// the number of bytes all the requests in queue are for
	std::int64_t m_queued_bytes;

	// this is the channel within the consumers
	// that bandwidth is assigned to (upload or download)
	int m_channel;

	bool m_abort;
};

}

#endif
