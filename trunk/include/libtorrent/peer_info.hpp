/*

Copyright (c) 2003, Arvid Norberg
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

#ifndef TORRENT_PEER_INFO_HPP_INCLUDED
#define TORRENT_PEER_INFO_HPP_INCLUDED

#include <vector>
#include "libtorrent/socket.hpp"
#include "libtorrent/peer_id.hpp"

namespace libtorrent
{
	struct peer_info
	{
		enum
		{
			interesting = 0x1,
			choked = 0x2,
			remote_interested = 0x4,
			remote_choked = 0x8,
			supports_extensions = 0x10,
			local_connection = 0x20
		};
		unsigned int flags;
		address ip;
		float up_speed;
		float down_speed;
		unsigned int total_download;
		unsigned int total_upload;
		peer_id id;
		std::vector<bool> pieces;
		int upload_limit; // from peer_connection
		int upload_ceiling; // from the global upload limiter

		int load_balancing;

		// this is the number of requests
		// we have sent to this peer
		// that we haven't got a response
		// for yet
		int download_queue_length;

		// this is the number of requests
		// the peer has sent to us
		// that we haven't sent yet
		int upload_queue_length;

		// the currently downloading piece
		// if piece index is -1 all associated
		// members are just set to 0
		int downloading_piece_index;
		int downloading_block_index;
		int downloading_progress;
		int downloading_total;
	};

}

#endif // TORRENT_PEER_INFO_HPP_INCLUDED
