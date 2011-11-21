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

#ifndef TORRENT_SESSION_SETTINGS_HPP_INCLUDED
#define TORRENT_SESSION_SETTINGS_HPP_INCLUDED

#include "libtorrent/version.hpp"
#include "libtorrent/config.hpp"

namespace libtorrent
{

	struct TORRENT_EXPORT proxy_settings
	{
		proxy_settings() : port(0), type(none) {}

		std::string hostname;
		int port;

		std::string username;
		std::string password;

		enum proxy_type
		{
			// a plain tcp socket is used, and
			// the other settings are ignored.
			none,
			// socks4 server, requires username.
			socks4,
			// the hostname and port settings are
			// used to connect to the proxy. No
			// username or password is sent.
			socks5,
			// the hostname and port are used to
			// connect to the proxy. the username
			// and password are used to authenticate
			// with the proxy server.
			socks5_pw,
			// the http proxy is only available for
			// tracker and web seed traffic
			// assumes anonymous access to proxy
			http,
			// http proxy with basic authentication
			// uses username and password
			http_pw
		};
		
		proxy_type type;
	
	};

	struct TORRENT_EXPORT session_settings
	{
		session_settings(std::string const& user_agent_ = "libtorrent/"
			LIBTORRENT_VERSION)
			: user_agent(user_agent_)
			, tracker_completion_timeout(60)
			, tracker_receive_timeout(40)
			, stop_tracker_timeout(5)
			, tracker_maximum_response_length(1024*1024)
			, piece_timeout(20)
			, request_timeout(50)
			, request_queue_time(3)
			, max_allowed_in_request_queue(250)
			, max_out_request_queue(200)
			, whole_pieces_threshold(20)
			, peer_timeout(120)
			, urlseed_timeout(20)
			, urlseed_pipeline_size(5)
			, urlseed_wait_retry(30)
			, file_pool_size(40)
			, allow_multiple_connections_per_ip(false)
			, max_failcount(3)
			, min_reconnect_time(60)
			, peer_connect_timeout(7)
			, ignore_limits_on_local_network(true)
			, connection_speed(10)
			, send_redundant_have(false)
			, lazy_bitfields(true)
			, inactivity_timeout(600)
			, unchoke_interval(15)
			, optimistic_unchoke_interval(30)
			, num_want(200)
			, initial_picker_threshold(4)
			, allowed_fast_set_size(10)
			, max_queued_disk_bytes(256 * 1024)
			, handshake_timeout(10)
#ifndef TORRENT_DISABLE_DHT
			, use_dht_as_fallback(false)
#endif
			, free_torrent_hashes(true)
			, upnp_ignore_nonrouters(false)
 			, send_buffer_watermark(100 * 1024)
			, auto_upload_slots(true)
			, auto_upload_slots_rate_based(false)
			, use_parole_mode(true)
			, cache_size(1024)
			, cache_buffer_chunk_size(16)
			, cache_expiry(60)
			, use_read_cache(true)
			, disk_io_write_mode(0)
			, disk_io_read_mode(0)
			, coalesce_reads(false)
			, coalesce_writes(false)
			, outgoing_ports(0,0)
			, peer_tos(0)
			, active_downloads(8)
			, active_seeds(5)
			, active_limit(15)
			, auto_manage_prefer_seeds(false)
			, dont_count_slow_torrents(true)
			, auto_manage_interval(30)
			, share_ratio_limit(2.f)
			, seed_time_ratio_limit(7.f)
			, seed_time_limit(24 * 60 * 60) // 24 hours
			, peer_turnover(2 / 50.f)
			, peer_turnover_cutoff(.9f)
			, close_redundant_connections(true)
			, auto_scrape_interval(1800)
			, auto_scrape_min_interval(300)
			, max_peerlist_size(4000)
			, max_paused_peerlist_size(4000)
			, min_announce_interval(5 * 60)
			, prioritize_partial_pieces(false)
			, auto_manage_startup(120)
			, rate_limit_ip_overhead(true)
			, announce_to_all_trackers(false)
			, announce_to_all_tiers(false)
			, prefer_udp_trackers(true)
			, strict_super_seeding(false)
			, seeding_piece_quota(20)
#ifdef TORRENT_WINDOWS
			, max_sparse_regions(30000)
#else
			, max_sparse_regions(0)
#endif
#ifndef TORRENT_DISABLE_MLOCK
			, lock_disk_cache(false)
#endif
			, max_rejects(50)
			, recv_socket_buffer_size(0)
			, send_socket_buffer_size(0)
			, optimize_hashing_for_speed(true)
			, file_checks_delay_per_block(0)
			, disk_cache_algorithm(largest_contiguous)
			, read_cache_line_size(16)
			, write_cache_line_size(32)
			, optimistic_disk_retry(10 * 60)
			, disable_hash_checks(false)
			, allow_reordered_disk_operations(true)
			, max_suggest_pieces(10)
			, drop_skipped_requests(false)
			, low_prio_disk(true)
			, local_service_announce_interval(5 * 60)
			, udp_tracker_token_expiry(60)
			, report_true_downloaded(false)
			, strict_end_game_mode(true)
			, broadcast_lsd(false)
		{}

		// this is the user agent that will be sent to the tracker
		// when doing requests. It is used to identify the client.
		// It cannot contain \r or \n
		std::string user_agent;

		// the number of seconds to wait until giving up on a
		// tracker request if it hasn't finished
		int tracker_completion_timeout;
		
		// the number of seconds where no data is received
		// from the tracker until it should be considered
		// as timed out
		int tracker_receive_timeout;

		// the time to wait when sending a stopped message
		// before considering a tracker to have timed out.
		// this is usually shorter, to make the client quit
		// faster
		int stop_tracker_timeout;

		// if the content-length is greater than this value
		// the tracker connection will be aborted
		int tracker_maximum_response_length;

		// the number of seconds from a request is sent until
		// it times out if no piece response is returned.
		int piece_timeout;

		// the number of seconds one block (16kB) is expected
		// to be received within. If it's not, the block is
		// requested from a different peer
		int request_timeout;

		// the length of the request queue given in the number
		// of seconds it should take for the other end to send
		// all the pieces. i.e. the actual number of requests
		// depends on the download rate and this number.
		int request_queue_time;
		
		// the number of outstanding block requests a peer is
		// allowed to queue up in the client. If a peer sends
		// more requests than this (before the first one has
		// been sent) the last request will be dropped.
		// the higher this is, the faster upload speeds the
		// client can get to a single peer.
		int max_allowed_in_request_queue;
		
		// the maximum number of outstanding requests to
		// send to a peer. This limit takes precedence over
		// request_queue_time.
		int max_out_request_queue;

		// if a whole piece can be downloaded in this number
		// of seconds, or less, the peer_connection will prefer
		// to request whole pieces at a time from this peer.
		// The benefit of this is to better utilize disk caches by
		// doing localized accesses and also to make it easier
		// to identify bad peers if a piece fails the hash check.
		int whole_pieces_threshold;
		
		// the number of seconds to wait for any activity on
		// the peer wire before closing the connectiong due
		// to time out.
		int peer_timeout;
		
		// same as peer_timeout, but only applies to url-seeds.
		// this is usually set lower, because web servers are
		// expected to be more reliable.
		int urlseed_timeout;
		
		// controls the pipelining size of url-seeds
		int urlseed_pipeline_size;

		// time to wait until a new retry takes place
		int urlseed_wait_retry;
		
		// sets the upper limit on the total number of files this
		// session will keep open. The reason why files are
		// left open at all is that some anti virus software
		// hooks on every file close, and scans the file for
		// viruses. deferring the closing of the files will
		// be the difference between a usable system and
		// a completely hogged down system. Most operating
		// systems also has a limit on the total number of
		// file descriptors a process may have open. It is
		// usually a good idea to find this limit and set the
		// number of connections and the number of files
		// limits so their sum is slightly below it.
		int file_pool_size;
		
		// false to not allow multiple connections from the same
		// IP address. true will allow it.
		bool allow_multiple_connections_per_ip;

		// the number of times we can fail to connect to a peer
		// before we stop retrying it.
		int max_failcount;
		
		// the number of seconds to wait to reconnect to a peer.
		// this time is multiplied with the failcount.
		int min_reconnect_time;

		// this is the timeout for a connection attempt. If
		// the connect does not succeed within this time, the
		// connection is dropped. The time is specified in seconds.
		int peer_connect_timeout;

		// if set to true, upload, download and unchoke limits
		// are ignored for peers on the local network.
		bool ignore_limits_on_local_network;

		// the number of connection attempts that
		// are made per second.
		int connection_speed;

		// if this is set to true, have messages will be sent
		// to peers that already have the piece. This is
		// typically not necessary, but it might be necessary
		// for collecting statistics in some cases. Default is false.
		bool send_redundant_have;

		// if this is true, outgoing bitfields will never be fuil. If the
		// client is seed, a few bits will be set to 0, and later filled
		// in with have messages. This is to prevent certain ISPs
		// from stopping people from seeding.
		bool lazy_bitfields;

		// if a peer is uninteresting and uninterested for longer
		// than this number of seconds, it will be disconnected.
		// default is 10 minutes
		int inactivity_timeout;

		// the number of seconds between chokes/unchokes
		int unchoke_interval;

		// the number of seconds between
		// optimistic unchokes
		int optimistic_unchoke_interval;

		// if this is set, this IP will be reported do the
		// tracker in the ip= parameter.
		address announce_ip;

		// the num want sent to trackers
		int num_want;

		// while we have fewer pieces than this, pick
		// random pieces instead of rarest first.
		int initial_picker_threshold;

		// the number of allowed pieces to send to peers
		// that supports the fast extensions
		int allowed_fast_set_size;

		// the maximum number of bytes a connection may have
		// pending in the disk write queue before its download
		// rate is being throttled. This prevents fast downloads
		// to slow medias to allocate more and more memory
		// indefinitely. This should be set to at least 16 kB
		// to not completely disrupt normal downloads. If it's
		// set to 0, you will be starving the disk thread and
		// nothing will be written to disk.
		// this is a per session setting.
		int max_queued_disk_bytes;

		// the number of seconds to wait for a handshake
		// response from a peer. If no response is received
		// within this time, the peer is disconnected.
		int handshake_timeout;

#ifndef TORRENT_DISABLE_DHT
		// while this is true, the dht will not be used unless the
		// tracker is online
		bool use_dht_as_fallback;
#endif

		// if this is true, the piece hashes will be freed, in order
		// to save memory, once the torrent is seeding. This will
		// make the get_torrent_info() function to return an incomplete
		// torrent object that cannot be passed back to add_torrent()
		bool free_torrent_hashes;

		// when this is true, the upnp port mapper will ignore
		// any upnp devices that don't have an address that matches
		// our currently configured router.
		bool upnp_ignore_nonrouters;

 		// if the send buffer has fewer bytes than this, we'll
 		// read another 16kB block onto it. If set too small,
 		// upload rate capacity will suffer. If set too high,
 		// memory will be wasted.
 		// The actual watermark may be lower than this in case
 		// the upload rate is low, this is the upper limit.
 		int send_buffer_watermark;

		// if auto_upload_slots is true, and a global upload
		// limit is set and the upload rate is less than 90%
		// of the upload limit, on new slot is opened up. If
		// the upload rate is >= upload limit for an extended
		// period of time, one upload slot is closed. The
		// upload slots are never automatically decreased below
		// the manual settings, through max_uploads.
		bool auto_upload_slots;

		// this only affects the auto upload slots mechanism.
		// if auto_upload_slots is false, this field is not
		// considered.
		bool auto_upload_slots_rate_based;

		// if set to true, peers that participate in a failing
		// piece is put in parole mode. i.e. They will only
		// download whole pieces until they either fail or pass.
		// they are taken out of parole mode as soon as they
		// participate in a piece that passes.
		bool use_parole_mode;

		// the disk write cache, specified in 16 KiB blocks.
		// default is 1024 (= 16 MiB). -1 means automatic, which
		// adjusts the cache size depending on the amount
		// of physical RAM in the machine.
		int cache_size;

		// this is the number of disk buffer blocks (16 kiB)
		// that should be allocated at a time. It must be
		// at least 1. Lower number saves memory at the expense
		// of more heap allocations
		int cache_buffer_chunk_size;

		// the number of seconds a write cache entry sits
		// idle in the cache before it's forcefully flushed
		// to disk. Default is 60 seconds.
		int cache_expiry;

		// when true, the disk I/O thread uses the disk
		// cache for caching blocks read from disk too
		bool use_read_cache;

		enum io_buffer_mode_t
		{
			enable_os_cache = 0,
			disable_os_cache_for_aligned_files = 1,
			disable_os_cache = 2
		};
		int disk_io_write_mode;
		int disk_io_read_mode;

		bool coalesce_reads;
		bool coalesce_writes;

		// if != (0, 0), this is the range of ports that
		// outgoing connections will be bound to. This
		// is useful for users that have routers that
		// allow QoS settings based on local port.
		std::pair<int, int> outgoing_ports;

		// the TOS byte of all peer traffic (including
		// web seeds) is set to this value. The default
		// is the QBSS scavenger service
		// http://qbone.internet2.edu/qbss/
		// For unmarked packets, set to 0
		char peer_tos;

		// for auto managed torrents, these are the limits
		// they are subject to. If there are too many torrents
		// some of the auto managed ones will be paused until
		// some slots free up.
		int active_downloads;
		int active_seeds;
		int active_limit;

		// prefer seeding torrents when determining which torrents to give 
		// active slots to, the default is false which gives preference to
		// downloading torrents
		bool auto_manage_prefer_seeds;
		
		// if this is true, torrents that don't have any significant
		// transfers are not counted as active when determining which
		// auto managed torrents to pause and resume
		bool dont_count_slow_torrents;

		// the number of seconds in between recalculating which
		// torrents to activate and which ones to queue
		int auto_manage_interval;
	
		// when a seeding torrent reaches eaither the share ratio
		// (bytes up / bytes down) or the seed time ratio
		// (seconds as seed / seconds as downloader) or the seed
		// time limit (seconds as seed) it is considered
		// done, and it will leave room for other torrents
		// the default value for share ratio is 2
		// the default seed time ratio is 7, because that's a common
		// asymmetry ratio on connections
		float share_ratio_limit;
		float seed_time_ratio_limit;
		int seed_time_limit;

		// the percentage of peers to disconnect every
		// 90 seconds (if we're at the peer limit)
		// defaults to 1/50:th
		float peer_turnover;

		// when we are connected to more than
		// limit * peer_turnover_cutoff peers
		// disconnect peer_turnover fraction
		// of the peers
		float peer_turnover_cutoff;

		// if this is true (default) connections where both
		// ends have no utility in keeping the connection open
		// are closed. for instance if both ends have completed
		// their downloads
		bool close_redundant_connections;

		// the number of seconds between scrapes of
		// queued torrents (auto managed and paused)
		int auto_scrape_interval;

		// the minimum number of seconds between any
		// automatic scrape (regardless of torrent)
		int auto_scrape_min_interval;

		// the max number of peers in the peer list
		// per torrent. This is the peers we know
		// about, not necessarily connected to.
		int max_peerlist_size;

		// when a torrent is paused, this is the max peer
		// list size that's used
		int max_paused_peerlist_size;

		// any announce intervals reported from a tracker
		// that is lower than this, will be clamped to this
		// value. It's specified in seconds
		int min_announce_interval;

		// if true, partial pieces are picked before pieces
		// that are more rare
		bool prioritize_partial_pieces;

		// the number of seconds a torrent is considered
		// active after it was started, regardless of
		// upload and download speed. This is so that
		// newly started torrents are not considered
		// inactive until they have a fair chance to
		// start downloading.
		int auto_manage_startup;

		// if set to true, the estimated TCP/IP overhead is
		// drained from the rate limiters, to avoid exceeding
		// the limits with the total traffic
		bool rate_limit_ip_overhead;

		// this announces to all trackers within the current
		// tier. Trackers within a tier are supposed to share
		// peers, this could be used for trackers that don't,
		// and require the clients to announce to all of them.
		bool announce_to_all_trackers;

		// if set to true, multi tracker torrents are treated
		// the same way uTorrent treats them. It defaults to
		// false in order to comply with the extension definition.
		// When this is enabled, one tracker from each tier is
		// announced
		bool announce_to_all_tiers;

		// when this is set to true, if there is a tracker entry
		// with udp:// protocol, it is preferred over the same
		// tracker over http://.
		bool prefer_udp_trackers;

		// when set to true, a piece has to have been forwarded
		// to a third peer before another one is handed out
		bool strict_super_seeding;

		// the number of pieces to send to each peer when seeding
		// before rotating to a new peer
		int seeding_piece_quota;

		// the maximum number of sparse regions before starting
		// to prioritize pieces close to other pieces (to maintain
		// the number of sparse regions). This is set to 30000 on
		// windows because windows vista has a new limit on the
		// numbers of sparse regions one file may have
		// if it is set to 0 this behavior is disabled
		// this is a hack to avoid a terrible bug on windows
		// don't use unless you have to, it screws with rarest-first
		// piece selection, and reduces swarm performance
		int max_sparse_regions;

#ifndef TORRENT_DISABLE_MLOCK
		// if this is set to true, the memory allocated for the
		// disk cache will be locked in physical RAM, never to
		// be swapped out
		bool lock_disk_cache;
#endif

		// the number of times to reject requests while being
		// choked before disconnecting a peer for being malicious
		int max_rejects;

		// sets the socket send and receive buffer sizes
		// 0 means OS default
		int recv_socket_buffer_size;
		int send_socket_buffer_size;

		// if this is set to false, the hashing will be
		// optimized for memory usage instead of the
		// number of read operations
		bool optimize_hashing_for_speed;

		// if > 0, file checks will have a short
		// delay between disk operations, to make it 
		// less intrusive on the system as a whole
		// blocking the disk. This delay is specified
		// in milliseconds and the delay will be this
		// long per 16kiB block
		// the default of 10 ms/16kiB will limit
		// the checking rate to 1.6 MiB per second
		int file_checks_delay_per_block;

		enum disk_cache_algo_t
		{ lru, largest_contiguous };

		disk_cache_algo_t disk_cache_algorithm;

		// the number of blocks that will be read ahead
		// when reading a block into the read cache
		int read_cache_line_size;

		// whenever a contiguous range of this many
		// blocks is found in the write cache, it
		// is flushed immediately
		int write_cache_line_size;

		// this is the number of seconds a disk failure
		// occurs until libtorrent will re-try.
		int optimistic_disk_retry;

		// when set to true, all data downloaded from
		// peers will be assumed to be correct, and not
		// tested to match the hashes in the torrent
		// this is only useful for simulation and
		// testing purposes (typically combined with
		// disabled_storage)
		bool disable_hash_checks;

		// if this is true, disk read operations may
		// be re-ordered based on their physical disk
		// read offset. This greatly improves throughput
		// when uploading to many peers. This assumes
		// a traditional hard drive with a read head
		// and spinning platters. If your storage medium
		// is a solid state drive, this optimization
		// doesn't give you an benefits
		bool allow_reordered_disk_operations;

		// the max number of pieces that a peer can
		// suggest to use before we start dropping
		// previous suggested piece
		int max_suggest_pieces;

		// if set to true, requests that have have not been
		// satisfied after the equivalence of the entire
		// request queue has been received, will be considered lost
		bool drop_skipped_requests;

		// if this is set to true, the disk I/O will be
		// run at lower-than-normal priority. This is
		// intended to make the machine more responsive
		// to foreground tasks, while bittorrent runs
		// in the background
		bool low_prio_disk;

		// number of seconds between local service announces for
		// torrents. Defaults to 5 minutes
		int local_service_announce_interval;

		// the number of seconds a connection ID received
		// from a UDP tracker is valid for. This is specified
		// as 60 seconds
		int udp_tracker_token_expiry;

		// when set to true, the downloaded counter sent to trackers
		// will include the actual number of payload bytes donwnloaded
		// including redundant bytes. If set to false, it will not include
		// any redundany bytes
		bool report_true_downloaded;

		// if set to true, libtorrent won't request a piece multiple times
		// until every piece is requested
		bool strict_end_game_mode;

		// if this is true, the broadcast socket will not only use IP multicast
		// but also send the messages on the broadcast address. This is false by
		// default in order to avoid flooding networks for no good reason. If
		// a network is known not to support multicast, this can be enabled
		bool broadcast_lsd;
	};

#ifndef TORRENT_DISABLE_DHT
	struct dht_settings
	{
		dht_settings()
			: max_peers_reply(100)
			, search_branching(5)
			, service_port(0)
			, max_fail_count(20)
		{}
		
		// the maximum number of peers to send in a
		// reply to get_peers
		int max_peers_reply;

		// the number of simultanous "connections" when
		// searching the DHT.
		int search_branching;
		
		// the listen port for the dht. This is a UDP port.
		// zero means use the same as the tcp interface
		int service_port;
		
		// the maximum number of times a node can fail
		// in a row before it is removed from the table.
		int max_fail_count;
	};
#endif

#ifndef TORRENT_DISABLE_ENCRYPTION

	struct pe_settings
	{
		pe_settings()
			: out_enc_policy(enabled)
			, in_enc_policy(enabled)
			, allowed_enc_level(both)
			, prefer_rc4(false)
		{}

		enum enc_policy
		{
			forced,  // disallow non encrypted connections
			enabled, // allow encrypted and non encrypted connections
			disabled // disallow encrypted connections
		};

		enum enc_level
		{
			plaintext, // use only plaintext encryption
			rc4, // use only rc4 encryption 
			both // allow both
		};

		enc_policy out_enc_policy;
		enc_policy in_enc_policy;

		enc_level allowed_enc_level;
		// if the allowed encryption level is both, setting this to
		// true will prefer rc4 if both methods are offered, plaintext
		// otherwise
		bool prefer_rc4;
	};
#endif

}

#endif
