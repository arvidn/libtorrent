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

#include "libtorrent/socket.hpp"
#include "libtorrent/deadline_timer.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/size_type.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/bitfield.hpp"

namespace libtorrent
{
	struct TORRENT_EXPORT peer_info
	{
		enum
		{
			interesting = 0x1,
			choked = 0x2,
			remote_interested = 0x4,
			remote_choked = 0x8,
			supports_extensions = 0x10,
			local_connection = 0x20,
			handshake = 0x40,
			connecting = 0x80,
			queued = 0x100,
			on_parole = 0x200,
			seed = 0x400,
			optimistic_unchoke = 0x800,
			snubbed = 0x1000,
			upload_only = 0x2000,
			endgame_mode = 0x4000,
			holepunched = 0x8000,
			i2p_socket = 0x10000,
			utp_socket = 0x20000
#ifndef TORRENT_DISABLE_ENCRYPTION
			, rc4_encrypted = 0x100000,
			plaintext_encrypted = 0x200000
#endif
		};

		unsigned int flags;

		enum peer_source_flags
		{
			tracker = 0x1,
			dht = 0x2,
			pex = 0x4,
			lsd = 0x8,
			resume_data = 0x10,
			incoming = 0x20
		};

		int source;

		// bw_idle: the channel is not used
		// bw_limit: the channel is waiting for quota
		// bw_network: the channel is waiting for an async write
		//   for read operation to complete
		// bw_disk: the peer is waiting for the disk io thread
		// this is a bitmask, a peer can wait for network and
		// disk at the same time!
		enum bw_state { bw_idle = 0, bw_limit = 1, bw_network = 2, bw_disk = 4 };
#ifndef TORRENT_NO_DEPRECATE
		enum bw_state_deprecated { bw_torrent = bw_limit, bw_global = bw_limit };
#endif

		char read_state;
		char write_state;
		
		tcp::endpoint ip;
		int up_speed;
		int down_speed;
		int payload_up_speed;
		int payload_down_speed;
		size_type total_download;
		size_type total_upload;
		peer_id pid;
		bitfield pieces;
		int upload_limit;
		int download_limit;

		// time since last request
		time_duration last_request;

		// time since last download or upload
		time_duration last_active;

		// the time until all blocks in the request
		// queue will be d
		time_duration download_queue_time;
		int queue_bytes;

		// the number of seconds until the current
		// pending request times out
		int request_timeout;

		// the size of the send buffer for this peer, in bytes
		int send_buffer_size;
		// the number bytes that's actually used of the send buffer
		int used_send_buffer;

		int receive_buffer_size;
		int used_receive_buffer;

		// the number of failed hashes for this peer
		int num_hashfails;

#ifndef TORRENT_DISABLE_RESOLVE_COUNTRIES
		// in case the session settings is set
		// to resolve countries, this is set to
		// the two character country code this
		// peer resides in.
		char country[2];
#endif

#ifndef TORRENT_DISABLE_GEO_IP
		// atonomous system this peer belongs to
		std::string inet_as_name;
		int inet_as;
#endif

		size_type load_balancing;

		// this is the number of requests
		// we have sent to this peer
		// that we haven't got a response
		// for yet
		int download_queue_length;
		
		// the number of block requests that have
		// timed out, and are still in the download
		// queue
		int timed_out_requests;

		// the number of busy requests in the download
		// queue. A budy request is a request for a block
		// we've also requested from a different peer
		int busy_requests;

		// the number of request messages
		// waiting to be sent inside the send buffer
		int requests_in_buffer;

		// the number of requests that is
		// tried to be maintained (this is
		// typically a function of download speed)
		int target_dl_queue_length;

		// this is the number of requests
		// the peer has sent to us
		// that we haven't sent yet
		int upload_queue_length;

		// the number of times this IP
		// has failed to connect
		int failcount;

		// the currently downloading piece
		// if piece index is -1 all associated
		// members are just set to 0
		int downloading_piece_index;
		int downloading_block_index;
		int downloading_progress;
		int downloading_total;
		
		std::string client;
		
		enum
		{
			standard_bittorrent = 0,
			web_seed = 1,
			http_seed = 2,
			bittorrent_utp = 3
		};
		int connection_type;
		
		// approximate peer download rate
		int remote_dl_rate;

		// number of bytes this peer has in
		// the disk write queue
		int pending_disk_bytes;

		// numbers used for bandwidth limiting
		int send_quota;
		int receive_quota;

		// estimated rtt to peer, in milliseconds
		int rtt;

		// the number of pieces this peer has
		int num_pieces;

		// the highest transfer rates seen for this peer
		int download_rate_peak;
		int upload_rate_peak;
		
		// the peers progress
		float progress; // [0, 1]
		int progress_ppm; // [0, 1000000]

		int estimated_reciprocation_rate;

		tcp::endpoint local_endpoint;
	};

	struct TORRENT_EXPORT peer_list_entry
	{
		enum flags_t
		{
			banned = 1
		};
		
		tcp::endpoint ip;
		int flags;
		boost::uint8_t failcount;
		boost::uint8_t source;
	};

	// defined in policy.cpp
	int source_rank(int source_bitmask);
}

#endif // TORRENT_PEER_INFO_HPP_INCLUDED
