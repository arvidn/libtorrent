/*

Copyright (c) 2003, Magnus Jonsson
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

#ifndef TORRENT_ALLOCATE_RESOURCES_HPP_INCLUDED
#define TORRENT_ALLOCATE_RESOURCES_HPP_INCLUDED

#include <map>
#include <utility>

#include <boost/shared_ptr.hpp>

#include "libtorrent/resource_request.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/session.hpp"

namespace libtorrent
{
	class peer_connection;
	class torrent;

	int saturated_add(int a, int b);

	// Function to allocate a limited resource fairly among many consumers.
	// It takes into account the current use, and the consumer's desired use.
	// Should be invoked periodically to allow it adjust to the situation (make
	// sure "used" is updated between calls!).
	// If resources = std::numeric_limits<int>::max() it means there is an infinite
	// supply of resources (so everyone can get what they want).

	void allocate_resources(
		int resources
		, std::map<sha1_hash, boost::shared_ptr<torrent> >& torrents
		, resource_request torrent::* res);

	void allocate_resources(
		int resources
		, std::map<tcp::endpoint, peer_connection*>& connections
		, resource_request peer_connection::* res);

	// Used for global limits.
	void allocate_resources(
		int resources
		, std::vector<session*>& _sessions
		, resource_request session::* res);
}


#endif
