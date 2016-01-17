/*

Copyright (c) 2007-2016, Arvid Norberg
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

#ifndef TORRENT_BANDWIDTH_QUEUE_ENTRY_HPP_INCLUDED
#define TORRENT_BANDWIDTH_QUEUE_ENTRY_HPP_INCLUDED

#include <boost/intrusive_ptr.hpp>
#include "libtorrent/bandwidth_limit.hpp"
#include "libtorrent/bandwidth_socket.hpp"

namespace libtorrent {

struct TORRENT_EXTRA_EXPORT bw_request
{
	bw_request(boost::intrusive_ptr<bandwidth_socket> const& pe
		, int blk, int prio);

	boost::intrusive_ptr<bandwidth_socket> peer;
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

	enum { max_bandwidth_channels = 5 };
	// we don't actually support more than 5 channels per peer
	bandwidth_channel* channel[max_bandwidth_channels];
};

}

#endif

