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
#include "libtorrent/version.hpp"

#include <string>

namespace libtorrent
{

	struct TORRENT_EXPORT proxy_settings
	{
		proxy_settings() : port(0), type(none)
			, proxy_hostnames(true)
			, proxy_peer_connections(true)
		{}

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
			http_pw,
			// route through a i2p SAM proxy
			i2p_proxy
		};
		
		proxy_type type;

		// when set to true, hostname are resolved
		// through the proxy (if supported)
		bool proxy_hostnames;

		// if true, use this proxy for peers too
		bool proxy_peer_connections;
	};

	struct TORRENT_EXPORT session_settings
	{
		session_settings(std::string const& user_agent_ = "libtorrent/"
			LIBTORRENT_VERSION)
			: version(LIBTORRENT_VERSION_NUM)
			, user_agent(user_agent_)
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
			, peer_connect_timeout(15)
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
			, suggest_mode(no_piece_suggestions)
			, max_queued_disk_bytes(256 * 1024)
			, max_queued_disk_bytes_low_watermark(0)
			, handshake_timeout(10)
#ifndef TORRENT_DISABLE_DHT
			, use_dht_as_fallback(false)
#endif
			, free_torrent_hashes(true)
			, upnp_ignore_nonrouters(false)
 			, send_buffer_watermark(700 * 1024)
			, send_buffer_watermark_factor(1)
#ifndef TORRENT_NO_DEPRECATE
			// deprecated in 0.16
			, auto_upload_slots(true)
			, auto_upload_slots_rate_based(true)
#endif
			, choking_algorithm(fixed_slots_choker)
			, seed_choking_algorithm(round_robin)
			, use_parole_mode(true)
			, cache_size(1024)
			, cache_buffer_chunk_size(16)
			, cache_expiry(60)
			, use_read_cache(true)
			, dont_flush_write_cache(false)
			, explicit_read_cache(0)
			, explicit_cache_interval(30)
			, disk_io_write_mode(0)
			, disk_io_read_mode(0)
			, coalesce_reads(false)
			, coalesce_writes(false)
			, outgoing_ports(0,0)
			, peer_tos(0)
			, active_downloads(3)
			, active_seeds(5)
			, active_dht_limit(88) // don't announce more than once every 40 seconds
			, active_tracker_limit(360) // don't announce to trackers more than once every 5 seconds
			, active_lsd_limit(60) // don't announce to local network more than once every 5 seconds
			, active_limit(15)
			, auto_manage_prefer_seeds(false)
			, dont_count_slow_torrents(true)
			, auto_manage_interval(30)
			, share_ratio_limit(2.f)
			, seed_time_ratio_limit(7.f)
			, seed_time_limit(24 * 60 * 60) // 24 hours
			, peer_turnover_interval(300)
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
			, disk_cache_algorithm(avoid_readback)
			, read_cache_line_size(32)
			, write_cache_line_size(128)
			, optimistic_disk_retry(10 * 60)
			, disable_hash_checks(false)
#if TORRENT_USE_AIO || TORRENT_USE_OVERLAPPED
			, allow_reordered_disk_operations(false)
#else
			, allow_reordered_disk_operations(true)
#endif
			, allow_i2p_mixed(false)
			, max_suggest_pieces(10)
			, drop_skipped_requests(false)
			, low_prio_disk(true)
			, local_service_announce_interval(5 * 60)
			, dht_announce_interval(15 * 60)
			, udp_tracker_token_expiry(60)
			, volatile_read_cache(false)
			, guided_read_cache(true)
			, default_cache_min_age(1)
			, num_optimistic_unchoke_slots(0)
			, no_atime_storage(true)
			, default_est_reciprocation_rate(16000)
			, increase_est_reciprocation_rate(20)
			, decrease_est_reciprocation_rate(3)
			, incoming_starts_queued_torrents(false)
			, report_true_downloaded(false)
			, strict_end_game_mode(true)
			, default_peer_upload_rate(0)
			, default_peer_download_rate(0)
			, broadcast_lsd(true)
			, enable_outgoing_utp(true)
			, enable_incoming_utp(true)
			, enable_outgoing_tcp(true)
			, enable_incoming_tcp(true)
			, max_pex_peers(200)
			, ignore_resume_timestamps(false)
			, no_recheck_incomplete_resume(false)
			, anonymous_mode(false)
			, tick_interval(100)
			, report_web_seed_downloads(true)
			, share_mode_target(3)
			, upload_rate_limit(0)
			, download_rate_limit(0)
			, local_upload_rate_limit(0)
			, local_download_rate_limit(0)
			, dht_upload_rate_limit(4000)
			, unchoke_slots_limit(8)
			, half_open_limit(0)
			, connections_limit(200)
			, utp_target_delay(75) // milliseconds
			, utp_gain_factor(1500) // bytes per rtt
			, utp_min_timeout(500) // milliseconds
			, utp_syn_resends(2)
			, utp_fin_resends(2)
			, utp_num_resends(6)
			, utp_connect_timeout(3000) // milliseconds
			, utp_delayed_ack(0) // milliseconds
			, utp_dynamic_sock_buf(true)
			, mixed_mode_algorithm(peer_proportional)
			, rate_limit_utp(false)
			, listen_queue_size(5)
			, announce_double_nat(false)
			, torrent_connect_boost(10)
			, seeding_outgoing_connections(true)
			, no_connect_privileged_ports(true)
			, alert_queue_size(1000)
			, max_metadata_size(3*1024*1024)
			, smooth_connects(true)
			, always_send_user_agent(false)
			, apply_ip_filter_to_trackers(true)
			, read_job_every(10)
			, use_disk_read_ahead(true)
		{}

		// libtorrent version. Used for forward binary compatibility
		int version;

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
		std::string announce_ip;

		// the num want sent to trackers
		int num_want;

		// while we have fewer pieces than this, pick
		// random pieces instead of rarest first.
		int initial_picker_threshold;

		// the number of allowed pieces to send to peers
		// that supports the fast extensions
		int allowed_fast_set_size;

		// this determines which pieces will be suggested to peers
		// suggest read cache will make libtorrent suggest pieces
		// that are fresh in the disk read cache, to potentially
		// lower disk access and increase the cache hit ratio
		enum { no_piece_suggestions = 0, suggest_read_cache = 1 };
		int suggest_mode;

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

		// this is the low watermark for the disk buffer queue.
		// whenever the number of queued bytes exceed the
		// max_queued_disk_bytes, libtorrent will wait for
		// it to drop below this value before issuing more
		// reads from the sockets. If set to 0, the
		// low watermark will be half of the max queued disk bytes
		int max_queued_disk_bytes_low_watermark;

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

		// the current upload rate to a peer is multiplied by
		// this factor to get the send buffer watermark. This
		// product is clamped to the send_buffer_watermark
		// setting to not exceed the max. For high speed
		// upload, this should be set to a greater value than
		// 1. The default is 1.
		int send_buffer_watermark_factor;

#ifndef TORRENT_NO_DEPRECATE
		// deprecated in 0.16
		bool auto_upload_slots;
		bool auto_upload_slots_rate_based;
#endif

		enum choking_algorithm_t
		{
			fixed_slots_choker,
			auto_expand_choker,
			rate_based_choker,
			bittyrant_choker
		};

		int choking_algorithm;

		enum seed_choking_algorithm_t
		{
			round_robin,
			fastest_upload,
			anti_leech
		};
 
		// the choking algorithm to use for seeding torrents
		int seed_choking_algorithm;
		
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

		// this will make the disk cache never flush a write
		// piece if it would cause is to have to re-read it
		// once we want to calculate the piece hash
		bool dont_flush_write_cache;

		// don't implicitly cache pieces in the read cache,
		// only cache pieces that are explicitly asked to be
		// cached.
		bool explicit_read_cache;

		// the number of seconds between refreshes of
		// explicit caches
		int explicit_cache_interval;

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
		// active_dht_limit and active_tracker_limit limits the
		// number of torrents that will be active on the DHT
		// versus the tracker. If the active limit is set higher
		// than these numbers, some torrents will be "active" in
		// the sense that they will accept incoming connections,
		// but not announce on the DHT or the tracker
		int active_downloads;
		int active_seeds;
		int active_dht_limit;
		int active_tracker_limit;
		int active_lsd_limit;
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

		// the interval (in seconds) between optimistic disconnects
		// if the disconnects happen and how many peers are disconnected
		// is controlled by peer_turnover and peer_turnover_cutoff
		int peer_turnover_interval;

		// the percentage of peers to disconnect every
		// turnoever interval (if we're at the peer limit)
		// defaults to 2/50:th
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
		{ lru, largest_contiguous, avoid_readback };

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

		// if this is true, disk read operations are
		// sorted by their physical offset on disk before
		// issued to the operating system. This is useful
		// if async I/O is not supported. It defaults to
		// true if async I/O is not supported and fals
		// otherwise.
		// disk I/O operations are likely to be reordered
		// regardless of this setting when async I/O
		// is supported by the OS.
		bool allow_reordered_disk_operations;

		// if this is true, i2p torrents are allowed
		// to also get peers from other sources than
		// the tracker, and connect to regular IPs,
		// not providing any anonymization. This may
		// be useful if the user is not interested in
		// the anonymization of i2p, but still wants to
		// be able to connect to i2p peers.
		bool allow_i2p_mixed;

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

		// number of seconds between DHT announces for
		// torrents. Defaults to 15 minutes
		int dht_announce_interval;

		// the number of seconds a connection ID received
		// from a UDP tracker is valid for. This is specified
		// as 60 seconds
		int udp_tracker_token_expiry;

		// if this is set to true, any block read from the
		// disk cache will be dropped from the cache immediately
		// following. This may be useful if the block is not
		// expected to be hit again. It would save some memory
		bool volatile_read_cache;

		// if this is set to true, the size of the cache line
		// generated by a particular read request depends on the
		// rate you're sending to that peer. This optimizes the
		// memory usage of the disk read cache by reading
		// further ahead for peers that you're uploading at high
		// rates to
		bool guided_read_cache;

		// this is the default minimum time any read cache line
		// is kept in the cache.
		int default_cache_min_age;

		// the global number of optimistic unchokes
		// 0 means automatic
		int num_optimistic_unchoke_slots;

		// if set to true, files won't have their atime updated
		// on disk reads. This works on linux
		bool no_atime_storage;

		// === BitTyrant unchoker settings ==

		// when using BitTyrant choker, this is the default
		// assumed reciprocation rate. This is where each peer starts
		int default_est_reciprocation_rate;

		// this is the increase of the estimated reciprocation rate
		// in percent. We increase by this amount once every unchoke
		// interval that we are choked by the other peer and we have
		// unchoked them
		int increase_est_reciprocation_rate;

		// each unchoke interval that we stay unchoked by the other
		// peer, and we have unchoked this peer as well, we decrease
		// our estimate of the reciprocation rate, since we might have
		// over-estimated it
		int decrease_est_reciprocation_rate;

		// if set to true, an incoming connection to a torrent that's
		// paused and auto-managed will make the torrent start.
		bool incoming_starts_queued_torrents;

		// when set to true, the downloaded counter sent to trackers
		// will include the actual number of payload bytes donwnloaded
		// including redundant bytes. If set to false, it will not include
		// any redundany bytes
		bool report_true_downloaded;

		// if set to true, libtorrent won't request a piece multiple times
		// until every piece is requested
		bool strict_end_game_mode;

		// each peer will have these limits set on it
		int default_peer_upload_rate;
		int default_peer_download_rate;

		// if this is true, the broadcast socket will not only use IP multicast
		// but also send the messages on the broadcast address. This is false by
		// default in order to avoid flooding networks for no good reason. If
		// a network is known not to support multicast, this can be enabled
		bool broadcast_lsd;

		// when set to true, libtorrent will try to make outgoing utp connections
		bool enable_outgoing_utp;

		// if set to false, libtorrent will reject incoming utp connections
		bool enable_incoming_utp;

		// when set to false, no outgoing TCP connections will be made
		bool enable_outgoing_tcp;

		// if set to false, libtorrent will reject incoming tcp connections
		bool enable_incoming_tcp;

		// the max number of peers we accept from pex messages from a single peer.
		// this limits the number of concurrent peers any of our peers claims to
		// be connected to. If they clain to be connected to more than this, we'll
		// ignore any peer that exceeds this limit
		int max_pex_peers;

		// when set to true, the file modification time is ignored when loading
		// resume data. The resume data includes the expected timestamp of each
		// file and is typically compared to make sure the files haven't changed
		// since the last session
		bool ignore_resume_timestamps;

		// normally, if a resume file is incomplete (typically there's no
		// "file sizes" field) the torrent is queued for a full check. If
		// this settings is set to true, instead libtorrent will assume
		// we have none of the files and go straight to download
		bool no_recheck_incomplete_resume;

		// when this is true, libtorrent will take actions to make sure any
		// privacy sensitive information is leaked out from the client. This
		// mode is assumed to be combined with using a proxy for all your
		// traffic. With this option, your true IP address will not be exposed
		bool anonymous_mode;

		// the number of milliseconds between internal ticks. Should be no
		// more than one second (i.e. 1000).
		int tick_interval;

		// specifies whether downloads from web seeds is reported to the
		// tracker or not. Defaults to on
		bool report_web_seed_downloads;

		// this is the target share ratio for share-mode torrents
		int share_mode_target;

		// max upload rate in bytes per second for the session
		int upload_rate_limit;

		// max download rate in bytes per second for the session
		int download_rate_limit;

		// max upload rate in bytes per second for peers on the local
		// network, in the session
		int local_upload_rate_limit;

		// max download rate in bytes per second for peers on the local
		// network, in the session
		int local_download_rate_limit;

		// max upload rate used by the DHT in bytes per second
		int dht_upload_rate_limit;

		// the max number of unchoke slots in the session (might be
		// overridden by unchoke algorithm)
		int unchoke_slots_limit;

		// the max number of half-open TCP connections
		int half_open_limit;

		// the max number of connections in the session
		int connections_limit;

		// target delay, milliseconds
		int utp_target_delay;

		// max number of bytes to increase cwnd per rtt in uTP
		// congestion controller
		int utp_gain_factor;

		// the shortest allowed uTP connection timeout in milliseconds
		// defaults to 500 milliseconds. The shorter timeout, the
		// faster the connection recovers from a loss of an entire window
		int utp_min_timeout;

		// the number of SYN packets that are sent before giving up
		int utp_syn_resends;
		
		// the number of resent packets sent on a closed socket before giving up
		int utp_fin_resends;

		// the number of times to send a packet before giving up
		int utp_num_resends;

		// initial timeout for uTP SYN packets
		int utp_connect_timeout;

		// number of milliseconds of delaying ACKing packets the most
		int utp_delayed_ack;

		// set to true if the uTP socket buffer size is allowed to increase
		// dynamically based on the NIC MTU setting. This is true by default
		// and improves uTP performance for networks with larger frame sizes
		// including loopback
		bool utp_dynamic_sock_buf;

		enum bandwidth_mixed_algo_t
		{
			// disables the mixed mode bandwidth balancing
			prefer_tcp = 0,

			// does not throttle uTP, throttles TCP to the same proportion
			// of throughput as there are TCP connections
			peer_proportional = 1

		};
		// the algorithm to use to balance bandwidth between tcp
		// connections and uTP connections
		int mixed_mode_algorithm;

		// set to true if uTP connections should be rate limited
		// defaults to false
		bool rate_limit_utp;

		// this is the number passed in to listen(). i.e.
		// the number of connections to accept while we're
		// not waiting in an accept() call.
		int listen_queue_size;

		// if this is true, the &ip= argument in tracker requests
		// (unless otherwise specified) will be set to the intermediate
		// IP address if the user is double NATed. If ther user is not
		// double NATed, this option does not have an affect
		bool announce_double_nat;

		// the first tracker response after a torrent is started
		// will cause this many connections to be made immediately.
		// instead of waiting for the connection scheduler which
		// triggeres every second
		int torrent_connect_boost;

		// this controls whether or not seeding (and complete) torrents
		// attempt to make outgoing connections or not. It defaults to
		// true, but can be set to zero for specific applications where
		// making outgoing connections is costly and known to not
		// add any benefits
		bool seeding_outgoing_connections;

		// when this is true, libtorrent will not attempt to make outgoing
		// connections to peers whose port is < 1024. This is a safety
		// precaution to avoid being part of a DDoS attack
		bool no_connect_privileged_ports;

		// the max alert queue size
		int alert_queue_size;

		// the max allowed size for metadata received by the
		// ut_metadata extension (i.e. magnet links)
		int max_metadata_size;

		// attempt to smooth out connects to avoid getting spikes in
		// opening connections and timing out connections
		bool smooth_connects;

		// always send user-agent
		bool always_send_user_agent;

		// if true, trackers will also be filtered by the IP
		// filter, otherwise they are exempt
		bool apply_ip_filter_to_trackers;

		// to avoid write jobs starving read jobs, if this many
		// write jobs have been taking priority in a row, service
		// one read job
		int read_job_every;

		// issue posix_fadvise() or fcntl(F_RDADVISE) for disk reads
		// ahead of time
		bool use_disk_read_ahead;
	};

#ifndef TORRENT_DISABLE_DHT
	struct dht_settings
	{
		dht_settings()
			: max_peers_reply(100)
			, search_branching(5)
#ifndef TORRENT_NO_DEPRECATE
			, service_port(0)
#endif
			, max_fail_count(20)
			, max_torrents(3000)
			, max_feed_items(3000)
			, max_torrent_search_reply(20)
			, restrict_routing_ips(true)
			, restrict_search_ips(true)
		{}
		
		// the maximum number of peers to send in a
		// reply to get_peers
		int max_peers_reply;

		// the number of simultanous "connections" when
		// searching the DHT.
		int search_branching;
		
#ifndef TORRENT_NO_DEPRECATE
		// the listen port for the dht. This is a UDP port.
		// zero means use the same as the tcp interface
		int service_port;
#endif
		
		// the maximum number of times a node can fail
		// in a row before it is removed from the table.
		int max_fail_count;

		// this is the max number of torrents the DHT will track
		int max_torrents;

		// max number of feed items the DHT will store
		int max_feed_items;

		// the max number of torrents to return in a
		// torrent search query to the DHT
		int max_torrent_search_reply;

		// when set, nodes whose IP address that's in
		// the same /24 (or /64 for IPv6) range in the
		// same routing table bucket. This is an attempt
		// to mitigate node ID spoofing attacks
		// also restrict any IP to only have a single
		// entry in the whole routing table
		bool restrict_routing_ips;

		// applies the same IP restrictions on nodes
		// received during a DHT search (traversal algorithm)
		bool restrict_search_ips;
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
