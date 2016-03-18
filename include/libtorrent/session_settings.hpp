/*

Copyright (c) 2003-2016, Arvid Norberg
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
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/aux_/proxy_settings.hpp"

#include <boost/cstdint.hpp>
#include <string>
#include <vector>
#include <utility>

namespace libtorrent
{

#ifndef TORRENT_NO_DEPRECATE

	typedef aux::proxy_settings proxy_settings;

	// This holds most of the session-wide settings in libtorrent. Pass this
	// to session::set_settings() to change the settings, initialize it from
	// session::get_settings() to get the current settings.
	struct TORRENT_EXPORT session_settings
	{
		// initializes the session_settings to the default settings.
		session_settings(std::string const& user_agent = "libtorrent/"
			LIBTORRENT_VERSION);
		~session_settings();
#if __cplusplus >= 201103L
		session_settings(session_settings const&) = default;
		session_settings& operator=(session_settings const&) = default;
#endif

		// automatically set to the libtorrent version you're using in order to
		// be forward binary compatible. This field should not be changed.
		int version;

		// the client identification to the tracker. The recommended format of
		// this string is: "ClientName/ClientVersion
		// libtorrent/libtorrentVersion". This name will not only be used when
		// making HTTP requests, but also when sending extended headers to peers
		// that support that extension.
		std::string user_agent;

		// the number of seconds the tracker connection will wait from when it
		// sent the request until it considers the tracker to have timed-out.
		// Default value is 60 seconds.
		int tracker_completion_timeout;

		// the number of seconds to wait to receive any data from the tracker. If
		// no data is received for this number of seconds, the tracker will be
		// considered as having timed out. If a tracker is down, this is the kind
		// of timeout that will occur. The default value is 20 seconds.
		int tracker_receive_timeout;

		// the time to wait when sending a stopped message before considering a
		// tracker to have timed out. this is usually shorter, to make the client
		// quit faster
		//
		// This is given in seconds. Default is 10 seconds.
		int stop_tracker_timeout;

		// the maximum number of bytes in a tracker response. If a response size
		// passes this number it will be rejected and the connection will be
		// closed. On gzipped responses this size is measured on the uncompressed
		// data. So, if you get 20 bytes of gzip response that'll expand to 2
		// megs, it will be interrupted before the entire response has been
		// uncompressed (given your limit is lower than 2 megs). Default limit is
		// 1 megabyte.
		int tracker_maximum_response_length;

		// controls the number of seconds from a request is sent until it times
		// out if no piece response is returned.
		int piece_timeout;

		// the number of seconds one block (16kB) is expected to be received
		// within. If it's not, the block is requested from a different peer
		int request_timeout;

		// the length of the request queue given in the number of seconds it
		// should take for the other end to send all the pieces. i.e. the actual
		// number of requests depends on the download rate and this number.
		int request_queue_time;

		// the number of outstanding block requests a peer is allowed to queue up
		// in the client. If a peer sends more requests than this (before the
		// first one has been sent) the last request will be dropped. the higher
		// this is, the faster upload speeds the client can get to a single peer.
		int max_allowed_in_request_queue;

		// the maximum number of outstanding requests to send to a peer. This
		// limit takes precedence over request_queue_time. i.e. no matter the
		// download speed, the number of outstanding requests will never exceed
		// this limit.
		int max_out_request_queue;

		// if a whole piece can be downloaded in this number of seconds, or less,
		// the peer_connection will prefer to request whole pieces at a time from
		// this peer. The benefit of this is to better utilize disk caches by
		// doing localized accesses and also to make it easier to identify bad
		// peers if a piece fails the hash check.
		int whole_pieces_threshold;

		// the number of seconds to wait for any activity on the peer wire before
		// closing the connectiong due to time out. This defaults to 120 seconds,
		// since that's what's specified in the protocol specification. After
		// half the time out, a keep alive message is sent.
		int peer_timeout;

		// same as peer_timeout, but only applies to url-seeds. this is usually
		// set lower, because web servers are expected to be more reliable. This
		// value defaults to 20 seconds.
		int urlseed_timeout;

		// controls the pipelining with the web server. When using persistent
		// connections to HTTP 1.1 servers, the client is allowed to send more
		// requests before the first response is received. This number controls
		// the number of outstanding requests to use with url-seeds. Default is
		// 5.
		int urlseed_pipeline_size;

		// time to wait until a new retry takes place
		int urlseed_wait_retry;

		// sets the upper limit on the total number of files this session will
		// keep open. The reason why files are left open at all is that some anti
		// virus software hooks on every file close, and scans the file for
		// viruses. deferring the closing of the files will be the difference
		// between a usable system and a completely hogged down system. Most
		// operating systems also has a limit on the total number of file
		// descriptors a process may have open. It is usually a good idea to find
		// this limit and set the number of connections and the number of files
		// limits so their sum is slightly below it.
		int file_pool_size;

		// determines if connections from the same IP address as existing
		// connections should be rejected or not. Multiple connections from the
		// same IP address is not allowed by default, to prevent abusive behavior
		// by peers. It may be useful to allow such connections in cases where
		// simulations are run on the same machie, and all peers in a swarm has
		// the same IP address.
		bool allow_multiple_connections_per_ip;

		// the maximum times we try to connect to a peer before stop connecting
		// again. If a peer succeeds, its failcounter is reset. If a peer is
		// retrieved from a peer source (other than DHT) the failcount is
		// decremented by one, allowing another try.
		int max_failcount;

		// the number of seconds to wait to reconnect to a peer. this time is
		// multiplied with the failcount.
		int min_reconnect_time;

		// the number of seconds to wait after a connection attempt is initiated
		// to a peer until it is considered as having timed out. The default is
		// 10 seconds. This setting is especially important in case the number of
		// half-open connections are limited, since stale half-open connection
		// may delay the connection of other peers considerably.
		int peer_connect_timeout;

#ifndef TORRENT_NO_DEPRECATE
		// deprecated, use set_peer_class_filter() instead
		// if set to true, upload, download and unchoke limits
		// are ignored for peers on the local network.
		bool ignore_limits_on_local_network;
#endif

		// the number of connection attempts that are made per second. If a
		// number < 0 is specified, it will default to 200 connections per
		// second. If 0 is specified, it means don't make outgoing connections at
		// all.
		int connection_speed;

		// if this is set to true, have messages will be sent to peers that
		// already have the piece. This is typically not necessary, but it might
		// be necessary for collecting statistics in some cases. Default is
		// false.
		bool send_redundant_have;

		// prevents outgoing bitfields from being full. If the client is seed, a
		// few bits will be set to 0, and later filled in with have-messages.
		// This is an old attempt to prevent certain ISPs from stopping people
		// from seeding.
		bool lazy_bitfields;

		// if a peer is uninteresting and uninterested for longer than this
		// number of seconds, it will be disconnected. default is 10 minutes
		int inactivity_timeout;

		// the number of seconds between chokes/unchokes. On this interval, peers
		// are re-evaluated for being choked/unchoked. This is defined as 30
		// seconds in the protocol, and it should be significantly longer than
		// what it takes for TCP to ramp up to it's max rate.
		int unchoke_interval;

		// the number of seconds between each *optimistic* unchoke. On this
		// timer, the currently optimistically unchoked peer will change.
		int optimistic_unchoke_interval;

		// the ip address passed along to trackers as the ``&ip=`` parameter. If
		// left as the default (an empty string), that parameter is omitted. Most
		// trackers ignore this argument. This is here for completeness for
		// edge-cases where it may be useful.
		std::string announce_ip;

		// the number of peers we want from each tracker request. It defines what
		// is sent as the ``&num_want=`` parameter to the tracker. Stopped
		// messages always send num_want=0. This setting control what to say in
		// the case where we actually want peers.
		int num_want;

		// specifies the number of pieces we need before we switch to rarest
		// first picking. This defaults to 4, which means the 4 first pieces in
		// any torrent are picked at random, the following pieces are picked in
		// rarest first order.
		int initial_picker_threshold;

		// the number of allowed pieces to send to choked peers that supports the
		// fast extensions
		int allowed_fast_set_size;

		// options for session_settings::suggest_mode.
		enum suggest_mode_t
		{
			// the default. will not send out suggest messages.
			no_piece_suggestions = 0,

			// send out suggest messages for the most recent pieces that are in
			// the read cache.
			suggest_read_cache = 1
		};

		// this determines which pieces will be suggested to peers suggest read
		// cache will make libtorrent suggest pieces that are fresh in the disk
		// read cache, to potentially lower disk access and increase the cache
		// hit ratio
		//
		// for options, see suggest_mode_t.
		int suggest_mode;

		// the maximum number of bytes a connection may have pending in the disk
		// write queue before its download rate is being throttled. This prevents
		// fast downloads to slow medias to allocate more memory indefinitely.
		// This should be set to at least 16 kB to not completely disrupt normal
		// downloads. If it's set to 0, you will be starving the disk thread and
		// nothing will be written to disk. this is a per session setting.
		//
		// When this limit is reached, the peer connections will stop reading
		// data from their sockets, until the disk thread catches up. Setting
		// this too low will severly limit your download rate.
		int max_queued_disk_bytes;

#ifndef TORRENT_NO_DEPRECATE
		// not used anymore
		int max_queued_disk_bytes_low_watermark;
#endif

		// the number of seconds to wait for a handshake response from a peer. If
		// no response is received within this time, the peer is disconnected.
		int handshake_timeout;

		// determines how the DHT is used. If this is true, the DHT will only be
		// used for torrents where all trackers in its tracker list has failed.
		// Either by an explicit error message or a time out. This is false by
		// default, which means the DHT is used by default regardless of if the
		// trackers fail or not.
		bool use_dht_as_fallback;

		// determines whether or not the torrent's piece hashes are kept in
		// memory after the torrent becomes a seed or not. If it is set to
		// ``true`` the hashes are freed once the torrent is a seed (they're not
		// needed anymore since the torrent won't download anything more). If
		// it's set to false they are not freed. If they are freed, the
		// torrent_info returned by get_torrent_info() will return an object that
		// may be incomplete, that cannot be passed back to async_add_torrent()
		// and add_torrent() for instance.
		bool free_torrent_hashes;

		// indicates whether or not the UPnP implementation should ignore any
		// broadcast response from a device whose address is not the configured
		// router for this machine. i.e. it's a way to not talk to other people's
		// routers by mistake.
		bool upnp_ignore_nonrouters;

		// This is the minimum send buffer target size (send buffer includes
		// bytes pending being read from disk). For good and snappy seeding
		// performance, set this fairly high, to at least fit a few blocks. This
		// is essentially the initial window size which will determine how fast
		// we can ramp up the send rate
		int send_buffer_low_watermark;

		// the upper limit of the send buffer low-watermark.
		//
		// if the send buffer has fewer bytes than this, we'll read another 16kB
		// block onto it. If set too small, upload rate capacity will suffer. If
		// set too high, memory will be wasted. The actual watermark may be lower
		// than this in case the upload rate is low, this is the upper limit.
		int send_buffer_watermark;

		// the current upload rate to a peer is multiplied by this factor to get
		// the send buffer watermark. The factor is specified as a percentage.
		// i.e. 50 indicates a factor of 0.5.
		//
		// This product is clamped to the send_buffer_watermark setting to not
		// exceed the max. For high speed upload, this should be set to a greater
		// value than 100. The default is 50.
		//
		// For high capacity connections, setting this higher can improve upload
		// performance and disk throughput. Setting it too high may waste RAM and
		// create a bias towards read jobs over write jobs.
		int send_buffer_watermark_factor;

		// the different choking algorithms available. Set
		// session_settings::choking_algorithm to one of these
		enum choking_algorithm_t
		{
			// the traditional choker with a fixed number of unchoke slots, as
			// specified by session::set_max_uploads()..
			fixed_slots_choker = 0,

#ifndef TORRENT_NO_DEPRECATE
			// opens at least the number of slots as specified by
			// session::set_max_uploads() but opens up more slots if the upload
			// capacity is not saturated. This unchoker will work just like the
			// ``fixed_slot_choker`` if there's no global upload rate limit set.
			auto_expand_choker = 1,
#endif

			// opens up unchoke slots based on the upload rate achieved to peers.
			// The more slots that are opened, the marginal upload rate required
			// to open up another slot increases.
			rate_based_choker = 1,

			// attempts to optimize download rate by finding the reciprocation
			// rate of each peer individually and prefers peers that gives the
			// highest *return on investment*. It still allocates all upload
			// capacity, but shuffles it around to the best peers first. For this
			// choker to be efficient, you need to set a global upload rate limit
			// session_settings::upload_rate_limit. For more information about
			// this choker, see the paper_.
			// 
			// .. _paper: http://bittyrant.cs.washington.edu/#papers
			bittyrant_choker  = 2
		};

		// specifies which algorithm to use to determine which peers to unchoke.
		// This setting replaces the deprecated settings ``auto_upload_slots``
		// and ``auto_upload_slots_rate_based``. For options, see
		// choking_algorithm_t.
		int choking_algorithm;

		// the different choking algorithms available when seeding. Set
		// session_settings::seed_choking_algorithm to one of these
		enum seed_choking_algorithm_t
		{
			// round-robins the peers that are unchoked when seeding. This
			// distributes the upload bandwidht uniformly and fairly. It minimizes
			// the ability for a peer to download everything without
			// redistributing it.
			round_robin,

			// unchokes the peers we can send to the fastest. This might be a bit
			// more reliable in utilizing all available capacity.
			fastest_upload,

			// prioritizes peers who have just started or are just about to finish
			// the download. The intention is to force peers in the middle of the
			// download to trade with each other.
			anti_leech
		};

		// controls the seeding unchoke behavior. For options, see
		// seed_choking_algorithm_t.
		int seed_choking_algorithm;

		// specifies if parole mode should be used. Parole mode means that peers
		// that participate in pieces that fail the hash check are put in a mode
		// where they are only allowed to download whole pieces. If the whole
		// piece a peer in parole mode fails the hash check, it is banned. If a
		// peer participates in a piece that passes the hash check, it is taken
		// out of parole mode.
		bool use_parole_mode;

		// the disk write and read  cache. It is specified in units of 16 KiB
		// blocks. Buffers that are part of a peer's send or receive buffer also
		// count against this limit. Send and receive buffers will never be
		// denied to be allocated, but they will cause the actual cached blocks
		// to be flushed or evicted. If this is set to -1, the cache size is
		// automatically set to the amount of physical RAM available in the
		// machine divided by 8. If the amount of physical RAM cannot be
		// determined, it's set to 1024 (= 16 MiB).
		// 
		// Disk buffers are allocated using a pool allocator, the number of
		// blocks that are allocated at a time when the pool needs to grow can be
		// specified in ``cache_buffer_chunk_size``. This defaults to 16 blocks.
		// Lower numbers saves memory at the expense of more heap allocations. It
		// must be at least 1.
		int cache_size;

		// this is the number of disk buffer blocks (16 kiB) that should be
		// allocated at a time. It must be at least 1. Lower number saves memory
		// at the expense of more heap allocations
		// setting this to zero means 'automatic', i.e. proportional
		// to the total disk cache size
		int cache_buffer_chunk_size;

		// the number of seconds a write cache entry sits idle in the cache
		// before it's forcefully flushed to disk.
		int cache_expiry;

		// when set to true (default), the disk cache is also used to cache
		// pieces read from disk. Blocks for writing pieces takes presedence.
		bool use_read_cache;
		bool use_write_cache;

		// this will make the disk cache never flush a write
		// piece if it would cause is to have to re-read it
		// once we want to calculate the piece hash
		bool dont_flush_write_cache;

#ifndef TORRENT_NO_DEPRECATE
		// defaults to 0. If set to something greater than 0, the disk read cache
		// will not be evicted by cache misses and will explicitly be controlled
		// based on the rarity of pieces. Rare pieces are more likely to be
		// cached. This would typically be used together with ``suggest_mode``
		// set to ``suggest_read_cache``. The value is the number of pieces to
		// keep in the read cache. If the actual read cache can't fit as many, it
		// will essentially be clamped.
		bool explicit_read_cache;

		// the number of seconds in between each refresh of a part of the
		// explicit read cache. Torrents take turns in refreshing and this is the
		// time in between each torrent refresh. Refreshing a torrent's explicit
		// read cache means scanning all pieces and picking a random set of the
		// rarest ones. There is an affinity to pick pieces that are already in
		// the cache, so that subsequent refreshes only swaps in pieces that are
		// rarer than whatever is in the cache at the time.
		int explicit_cache_interval;
#endif

		// the buffer modes to use for reading and writing. Set
		// session_settings::disk_io_read_mode and disk_io_write_mode to one of
		// these.
		enum io_buffer_mode_t
		{
			// This is the default and files are opened normally, with the OS
			// caching reads and writes.
			enable_os_cache = 0,
			// This will open files in unbuffered mode for files where every read
			// and write would be sector aligned. Using aligned disk offsets is a
			// requirement on some operating systems.
			disable_os_cache_for_aligned_files = 1,
			// This opens all files in unbuffered mode (if allowed by the
			// operating system). Linux and Windows, for instance, require disk
			// offsets to be sector aligned, and in those cases, this option is
			// the same as ``disable_os_caches_for_aligned_files``.
			disable_os_cache = 2
		};

		// determines how files are opened when they're in read only mode versus
		// read and write mode. For options, see io_buffer_mode_t.
		//
		// One reason to disable caching is that it may help the operating system
		// from growing its file cache indefinitely. Since some OSes only allow
		// aligned files to be opened in unbuffered mode, It is recommended to
		// make the largest file in a torrent the first file (with offset 0) or
		// use pad files to align all files to piece boundries.
		int disk_io_write_mode;
		int disk_io_read_mode;

		// allocate separate, contiguous, buffers for read and write calls. Only
		// used where writev/readv cannot be used will use more RAM but may
		// improve performance
		bool coalesce_reads;
		bool coalesce_writes;

		// if set to something other than (0, 0) is a range of ports used to bind
		// outgoing sockets to. This may be useful for users whose router allows
		// them to assign QoS classes to traffic based on its local port. It is a
		// range instead of a single port because of the problems with failing to
		// reconnect to peers if a previous socket to that peer and port is in
		// ``TIME_WAIT`` state.
		// 
		//.. warning::
		//	setting outgoing ports will limit the ability to keep multiple
		//	connections to the same client, even for different torrents. It is not
		//	recommended to change this setting. Its main purpose is to use as an
		//	escape hatch for cheap routers with QoS capability but can only
		//	classify flows based on port numbers.
		int outgoing_port;
		int num_outgoing_ports;

		// determines the TOS byte set in the IP header of every packet sent to
		// peers (including web seeds). The default value for this is ``0x0`` (no
		// marking). One potentially useful TOS mark is ``0x20``, this represents
		// the *QBone scavenger service*. For more details, see QBSS_.
		// 
		// .. _`QBSS`: http://qbone.internet2.edu/qbss/
		char peer_tos;

		// for auto managed torrents, these are the limits they are subject to.
		// If there are too many torrents some of the auto managed ones will be
		// paused until some slots free up.
		// 
		// ``active_dht_limit`` and ``active_tracker_limit`` limits the number of
		// torrents that will be active on the DHT and their tracker. If the
		// active limit is set higher than these numbers, some torrents will be
		// "active" in the sense that they will accept incoming connections, but
		// not announce on the DHT or their trackers.
		//
		// ``active_lsd_limit`` is the max number of torrents to announce to the
		// local network over the local service discovery protocol. By default
		// this is 80, which is no more than one announce every 5 seconds
		// (assuming the default announce interval of 5 minutes).
		// 
		// ``active_limit`` is a hard limit on the number of active torrents.
		// This applies even to slow torrents.
		// 
		// You can have more torrents *active*, even though they are not
		// announced to the DHT, lsd or their tracker. If some peer knows about
		// you for any reason and tries to connect, it will still be accepted,
		// unless the torrent is paused, which means it won't accept any
		// connections.
		//
		// ``active_downloads`` and ``active_seeds`` controls how many active
		// seeding and downloading torrents the queuing mechanism allows. The
		// target number of active torrents is ``min(active_downloads +
		// active_seeds, active_limit)``. ``active_downloads`` and
		// ``active_seeds`` are upper limits on the number of downloading
		// torrents and seeding torrents respectively. Setting the value to -1
		// means unlimited.
		// 
		// For example if there are 10 seeding torrents and 10 downloading
		// torrents, and ``active_downloads`` is 4 and ``active_seeds`` is 4,
		// there will be 4 seeds active and 4 downloading torrents. If the
		// settings are ``active_downloads`` = 2 and ``active_seeds`` = 4, then
		// there will be 2 downloading torrents and 4 seeding torrents active.
		// Torrents that are not auto managed are also counted against these
		// limits. If there are non-auto managed torrents that use up all the
		// slots, no auto managed torrent will be activated.
		int active_downloads;
		int active_seeds;
		int active_dht_limit;
		int active_tracker_limit;
		int active_lsd_limit;
		int active_limit;

		// prefer seeding torrents when determining which torrents to give active
		// slots to, the default is false which gives preference to downloading
		// torrents
		bool auto_manage_prefer_seeds;

		// if true, torrents without any payload transfers are not subject to the
		// ``active_seeds`` and ``active_downloads`` limits. This is intended to
		// make it more likely to utilize all available bandwidth, and avoid
		// having torrents that don't transfer anything block the active slots.
		bool dont_count_slow_torrents;

		// the number of seconds in between recalculating which torrents to
		// activate and which ones to queue
		int auto_manage_interval;

		// when a seeding torrent reaches either the share ratio (bytes up /
		// bytes down) or the seed time ratio (seconds as seed / seconds as
		// downloader) or the seed time limit (seconds as seed) it is considered
		// done, and it will leave room for other torrents the default value for
		// share ratio is 2 the default seed time ratio is 7, because that's a
		// common asymmetry ratio on connections. these are specified as
		// percentages
		//
		//.. note::
		//	This is an out-dated option that doesn't make much sense. It will be
		//	removed in future versions of libtorrent
		float share_ratio_limit;

		// the seeding time / downloading time ratio limit for considering a
		// seeding torrent to have met the seed limit criteria. See queuing_.
		float seed_time_ratio_limit;

		// seed time limit is specified in seconds
		//
		// the limit on the time a torrent has been an active seed (specified in
		// seconds) before it is considered having met the seed limit criteria.
		// See queuing_.
		int seed_time_limit;

		// controls a feature where libtorrent periodically can disconnect the
		// least useful peers in the hope of connecting to better ones.
		// ``peer_turnover_interval`` controls the interval of this optimistic
		// disconnect. It defaults to every 5 minutes, and is specified in
		// seconds.
		// 
		// ``peer_turnover`` Is the fraction of the peers that are disconnected.
		// This is a float where 1.f represents all peers an 0 represents no
		// peers. It defaults to 4% (i.e. 0.04f)
		//
		// ``peer_turnover_cutoff`` is the cut off trigger for optimistic
		// unchokes. If a torrent has more than this fraction of its connection
		// limit, the optimistic unchoke is triggered. This defaults to 90% (i.e.
		// 0.9f).
		int peer_turnover_interval;

		// the percentage of peers to disconnect every
		// turnoever interval (if we're at the peer limit)
		// defaults to 4%
		// this is specified in percent
		float peer_turnover;

		// when we are connected to more than
		// limit * peer_turnover_cutoff peers
		// disconnect peer_turnover fraction
		// of the peers. It is specified in percent
		float peer_turnover_cutoff;

		// specifies whether libtorrent should close connections where both ends
		// have no utility in keeping the connection open. For instance if both
		// ends have completed their downloads, there's no point in keeping it
		// open. This defaults to ``true``.
		bool close_redundant_connections;

		// the number of seconds between scrapes of queued torrents (auto managed
		// and paused torrents). Auto managed torrents that are paused, are
		// scraped regularly in order to keep track of their downloader/seed
		// ratio. This ratio is used to determine which torrents to seed and
		// which to pause.
		int auto_scrape_interval;

		// the minimum number of seconds between any automatic scrape (regardless
		// of torrent). In case there are a large number of paused auto managed
		// torrents, this puts a limit on how often a scrape request is sent.
		int auto_scrape_min_interval;

		// the maximum number of peers in the list of known peers. These peers
		// are not necessarily connected, so this number should be much greater
		// than the maximum number of connected peers. Peers are evicted from the
		// cache when the list grows passed 90% of this limit, and once the size
		// hits the limit, peers are no longer added to the list. If this limit
		// is set to 0, there is no limit on how many peers we'll keep in the
		// peer list.
		int max_peerlist_size;

		// the max peer list size used for torrents that are paused. This default
		// to the same as ``max_peerlist_size``, but can be used to save memory
		// for paused torrents, since it's not as important for them to keep a
		// large peer list.
		int max_paused_peerlist_size;

		// the minimum allowed announce interval for a tracker. This is specified
		// in seconds, defaults to 5 minutes and is used as a sanity check on
		// what is returned from a tracker. It mitigates hammering misconfigured
		// trackers.
		int min_announce_interval;

		// If true, partial pieces are picked before pieces that are more rare.
		// If false, rare pieces are always prioritized, unless the number of
		// partial pieces is growing out of proportion.
		bool prioritize_partial_pieces;

		// the number of seconds a torrent is considered active after it was
		// started, regardless of upload and download speed. This is so that
		// newly started torrents are not considered inactive until they have a
		// fair chance to start downloading.
		int auto_manage_startup;

		// if set to true, the estimated TCP/IP overhead is drained from the rate
		// limiters, to avoid exceeding the limits with the total traffic
		bool rate_limit_ip_overhead;

		// controls how multi tracker torrents are treated. If this is set to
		// true, all trackers in the same tier are announced to in parallel. If
		// all trackers in tier 0 fails, all trackers in tier 1 are announced as
		// well. If it's set to false, the behavior is as defined by the multi
		// tracker specification. It defaults to false, which is the same
		// behavior previous versions of libtorrent has had as well.
		bool announce_to_all_trackers;

		// controls how multi tracker torrents are treated. When this is set to
		// true, one tracker from each tier is announced to. This is the uTorrent
		// behavior. This is false by default in order to comply with the
		// multi-tracker specification.
		bool announce_to_all_tiers;

		// true by default. It means that trackers may be rearranged in a way
		// that udp trackers are always tried before http trackers for the same
		// hostname. Setting this to fails means that the trackers' tier is
		// respected and there's no preference of one protocol over another.
		bool prefer_udp_trackers;

		// when this is set to true, a piece has to have been forwarded to a
		// third peer before another one is handed out. This is the traditional
		// definition of super seeding.
		bool strict_super_seeding;

		// the number of pieces to send to a peer, when seeding, before rotating
		// in another peer to the unchoke set. It defaults to 3 pieces, which
		// means that when seeding, any peer we've sent more than this number of
		// pieces to will be unchoked in favour of a choked peer.
		int seeding_piece_quota;

		// is a limit of the number of *sparse regions* in a torrent. A sparse
		// region is defined as a hole of pieces we have not yet downloaded, in
		// between pieces that have been downloaded. This is used as a hack for
		// windows vista which has a bug where you cannot write files with more
		// than a certain number of sparse regions. This limit is not hard, it
		// will be exceeded. Once it's exceeded, pieces that will maintain or
		// decrease the number of sparse regions are prioritized. To disable this
		// functionality, set this to 0. It defaults to 0 on all platforms except
		// windows.
		int max_sparse_regions;

		// if lock disk cache is set to true the disk cache that's in use, will
		// be locked in physical memory, preventing it from being swapped out.
		bool lock_disk_cache;

		// the number of piece requests we will reject in a row while a peer is
		// choked before the peer is considered abusive and is disconnected.
		int max_rejects;

		// specifies the buffer sizes set on peer sockets. 0 (which is the
		// default) means the OS default (i.e. don't change the buffer sizes).
		// The socket buffer sizes are changed using setsockopt() with
		// SOL_SOCKET/SO_RCVBUF and SO_SNDBUFFER.
		int recv_socket_buffer_size;
		int send_socket_buffer_size;

		// chooses between two ways of reading back piece data from disk when its
		// complete and needs to be verified against the piece hash. This happens
		// if some blocks were flushed to the disk out of order. Everything that
		// is flushed in order is hashed as it goes along. Optimizing for speed
		// will allocate space to fit all the the remaingin, unhashed, part of
		// the piece, reads the data into it in a single call and hashes it. This
		// is the default. If ``optimizing_hashing_for_speed`` is false, a single
		// block will be allocated (16 kB), and the unhashed parts of the piece
		// are read, one at a time, and hashed in this single block. This is
		// appropriate on systems that are memory constrained.
		bool optimize_hashing_for_speed;

		// the number of milliseconds to sleep
		// in between disk read operations when checking torrents. This defaults
		// to 0, but can be set to higher numbers to slow down the rate at which
		// data is read from the disk while checking. This may be useful for
		// background tasks that doesn't matter if they take a bit longer, as long
		// as they leave disk I/O time for other processes.
		int file_checks_delay_per_block;

		// the disk cache algorithms available. Set
		// session_settings::disk_cache_algorithm to one of these.
		enum disk_cache_algo_t
		{
			// This flushes the entire piece, in the write cache, that was least
			// recently written to.
			lru,

			// will flush the largest sequences of contiguous blocks from the
			// write cache, regarless of the piece's last use time.
			largest_contiguous,

			// will prioritize flushing blocks that will avoid having to read them
			// back in to verify the hash of the piece once it's done. This is
			// especially useful for high throughput setups, where reading from
			// the disk is especially expensive.
			avoid_readback
		};

		// tells the disk I/O thread which cache flush algorithm to use.
		// This is specified by the disk_cache_algo_t enum.
		disk_cache_algo_t disk_cache_algorithm;

		// the number of blocks to read into the read cache when a read cache
		// miss occurs. Setting this to 0 is essentially the same thing as
		// disabling read cache. The number of blocks read into the read cache is
		// always capped by the piece boundry.
		// 
		// When a piece in the write cache has ``write_cache_line_size``
		// contiguous blocks in it, they will be flushed. Setting this to 1
		// effectively disables the write cache.
		int read_cache_line_size;

		// whenever a contiguous range of this many blocks is found in the write
		// cache, it is flushed immediately
		int write_cache_line_size;

		// the number of seconds from a disk write errors occur on a torrent
		// until libtorrent will take it out of the upload mode, to test if the
		// error condition has been fixed.
		// 
		// libtorrent will only do this automatically for auto managed torrents.
		// 
		// You can explicitly take a torrent out of upload only mode using
		// set_upload_mode().
		int optimistic_disk_retry;

		// controls if downloaded pieces are verified against the piece hashes in
		// the torrent file or not. The default is false, i.e. to verify all
		// downloaded data. It may be useful to turn this off for performance
		// profiling and simulation scenarios. Do not disable the hash check for
		// regular bittorrent clients.
		bool disable_hash_checks;

		// if this is true, disk read operations are sorted by their physical
		// offset on disk before issued to the operating system. This is useful
		// if async I/O is not supported. It defaults to true if async I/O is not
		// supported and fals otherwise. disk I/O operations are likely to be
		// reordered regardless of this setting when async I/O is supported by
		// the OS.
		bool allow_reordered_disk_operations;

		// if this is true, i2p torrents are allowed to also get peers from other
		// sources than the tracker, and connect to regular IPs, not providing
		// any anonymization. This may be useful if the user is not interested in
		// the anonymization of i2p, but still wants to be able to connect to i2p
		// peers.
		bool allow_i2p_mixed;

		// the max number of suggested piece indices received from a peer that's
		// remembered. If a peer floods suggest messages, this limit prevents
		// libtorrent from using too much RAM. It defaults to 10.
		int max_suggest_pieces;

		// If set to true (it defaults to false), piece requests that have been
		// skipped enough times when piece messages are received, will be
		// considered lost. Requests are considered skipped when the returned
		// piece messages are re-ordered compared to the order of the requests.
		// This was an attempt to get out of dead-locks caused by BitComet peers
		// silently ignoring some requests. It may cause problems at high rates,
		// and high level of reordering in the uploading peer, that's why it's
		// disabled by default.
		bool drop_skipped_requests;

		// determines if the disk I/O should use a normal
		// or low priority policy. This defaults to true, which means that
		// it's low priority by default. Other processes doing disk I/O will
		// normally take priority in this mode. This is meant to improve the
		// overall responsiveness of the system while downloading in the
		// background. For high-performance server setups, this might not
		// be desirable.
		bool low_prio_disk;

		// the time between local
		// network announces for a torrent. By default, when local service
		// discovery is enabled a torrent announces itself every 5 minutes.
		// This interval is specified in seconds.
		int local_service_announce_interval;

		// the number of seconds between announcing
		// torrents to the distributed hash table (DHT). This is specified to
		// be 15 minutes which is its default.
		int dht_announce_interval;

		// the number of seconds libtorrent
		// will keep UDP tracker connection tokens around for. This is specified
		// to be 60 seconds, and defaults to that. The higher this value is, the
		// fewer packets have to be sent to the UDP tracker. In order for higher
		// values to work, the tracker needs to be configured to match the
		// expiration time for tokens.
		int udp_tracker_token_expiry;

		// if this is set to true, read cache blocks
		// that are hit by peer read requests are removed from the disk cache
		// to free up more space. This is useful if you don't expect the disk
		// cache to create any cache hits from other peers than the one who
		// triggered the cache line to be read into the cache in the first place.
		bool volatile_read_cache;

		// enables the disk cache to adjust the size
		// of a cache line generated by peers to depend on the upload rate
		// you are sending to that peer. The intention is to optimize the RAM
		// usage of the cache, to read ahead further for peers that you're
		// sending faster to.
		bool guided_read_cache;

		// the minimum number of seconds any read cache line is kept in the
		// cache. This defaults to one second but may be greater if
		// ``guided_read_cache`` is enabled. Having a lower bound on the time a
		// cache line stays in the cache is an attempt to avoid swapping the same
		// pieces in and out of the cache in case there is a shortage of spare
		// cache space.
		int default_cache_min_age;

		// the number of optimistic unchoke slots to use. It defaults to 0, which
		// means automatic. Having a higher number of optimistic unchoke slots
		// mean you will find the good peers faster but with the trade-off to use
		// up more bandwidth. When this is set to 0, libtorrent opens up 20% of
		// your allowed upload slots as optimistic unchoke slots.
		int num_optimistic_unchoke_slots;

		// this is a linux-only option and passes in the ``O_NOATIME`` to
		// ``open()`` when opening files. This may lead to some disk performance
		// improvements.
		bool no_atime_storage;

		// the assumed reciprocation rate from peers when using the BitTyrant
		// choker. This defaults to 14 kiB/s. If set too high, you will
		// over-estimate your peers and be more altruistic while finding the true
		// reciprocation rate, if it's set too low, you'll be too stingy and
		// waste finding the true reciprocation rate.
		int default_est_reciprocation_rate;

		// specifies how many percent the extimated reciprocation rate should be
		// increased by each unchoke interval a peer is still choking us back.
		// This defaults to 20%. This only applies to the BitTyrant choker.
		int increase_est_reciprocation_rate;

		// specifies how many percent the estimated reciprocation rate should be
		// decreased by each unchoke interval a peer unchokes us. This default to
		// 3%. This only applies to the BitTyrant choker.
		int decrease_est_reciprocation_rate;

		// defaults to false. If a torrent has been paused by the auto managed
		// feature in libtorrent, i.e. the torrent is paused and auto managed,
		// this feature affects whether or not it is automatically started on an
		// incoming connection. The main reason to queue torrents, is not to make
		// them unavailable, but to save on the overhead of announcing to the
		// trackers, the DHT and to avoid spreading one's unchoke slots too thin.
		// If a peer managed to find us, even though we're no in the torrent
		// anymore, this setting can make us start the torrent and serve it.
		bool incoming_starts_queued_torrents;

		// when set to true, the downloaded counter sent to trackers will include
		// the actual number of payload bytes donwnloaded including redundant
		// bytes. If set to false, it will not include any redundany bytes
		bool report_true_downloaded;

		// defaults to true, and controls when a block may be requested twice. If
		// this is ``true``, a block may only be requested twice when there's ay
		// least one request to every piece that's left to download in the
		// torrent. This may slow down progress on some pieces sometimes, but it
		// may also avoid downloading a lot of redundant bytes. If this is
		// ``false``, libtorrent attempts to use each peer connection to its max,
		// by always requesting something, even if it means requesting something
		// that has been requested from another peer already.
		bool strict_end_game_mode;

		// if set to true, the local peer discovery (or Local Service Discovery)
		// will not only use IP multicast, but also broadcast its messages. This
		// can be useful when running on networks that don't support multicast.
		// Since broadcast messages might be expensive and disruptive on
		// networks, only every 8th announce uses broadcast.
		bool broadcast_lsd;

		// these all determines if libtorrent should attempt to make outgoing
		// connections of the specific type, or allow incoming connection. By
		// default all of them are enabled.
		bool enable_outgoing_utp;
		bool enable_incoming_utp;
		bool enable_outgoing_tcp;
		bool enable_incoming_tcp;

		// the max number of peers we accept from pex messages from a single peer.
		// this limits the number of concurrent peers any of our peers claims to
		// be connected to. If they clain to be connected to more than this, we'll
		// ignore any peer that exceeds this limit
		int max_pex_peers;

		// determines if the storage, when loading resume data files, should
		// verify that the file modification time with the timestamps in the
		// resume data. This defaults to false, which means timestamps are taken
		// into account, and resume data is less likely to accepted (torrents are
		// more likely to be fully checked when loaded). It might be useful to
		// set this to true if your network is faster than your disk, and it
		// would be faster to redownload potentially missed pieces than to go
		// through the whole storage to look for them.
		bool ignore_resume_timestamps;

		// determines if the storage should check the whole files when resume
		// data is incomplete or missing or whether it should simply assume we
		// don't have any of the data. By default, this is determined by the
		// existance of any of the files. By setting this setting to true, the
		// files won't be checked, but will go straight to download mode.
		bool no_recheck_incomplete_resume;

		// defaults to false. When set to true, the client tries to hide its
		// identity to a certain degree. The peer-ID will no longer include the
		// client's fingerprint. The user-agent will be reset to an empty string.
		// It will also try to not leak other identifying information, such as
		// your local listen port, your IP etc.
		// 
		// If you're using I2P, a VPN or a proxy, it might make sense to enable
		// anonymous mode.
		bool anonymous_mode;

		// disables any communication that's not going over a proxy. Enabling
		// this requires a proxy to be configured as well, see
		// ``set_proxy_settings``. The listen sockets are closed, and incoming
		// connections will only be accepted through a SOCKS5 or I2P proxy (if a
		// peer proxy is set up and is run on the same machine as the tracker
		// proxy). This setting also disabled peer country lookups, since those
		// are done via DNS lookups that aren't supported by proxies.
		bool force_proxy;

		// specifies the number of milliseconds between internal ticks. This is
		// the frequency with which bandwidth quota is distributed to peers. It
		// should not be more than one second (i.e. 1000 ms). Setting this to a
		// low value (around 100) means higher resolution bandwidth quota
		// distribution, setting it to a higher value saves CPU cycles.
		int tick_interval;

		// specifies whether downloads from web seeds is reported to the
		// tracker or not. Defaults to on
		bool report_web_seed_downloads;

		// specifies the target share ratio for share mode torrents. This
		// defaults to 3, meaning we'll try to upload 3 times as much as we
		// download. Setting this very high, will make it very conservative and
		// you might end up not downloading anything ever (and not affecting your
		// share ratio). It does not make any sense to set this any lower than 2.
		// For instance, if only 3 peers need to download the rarest piece, it's
		// impossible to download a single piece and upload it more than 3 times.
		// If the share_mode_target is set to more than 3, nothing is downloaded.
		int share_mode_target;

		// sets the session-global limits of upload and download rate limits, in
		// bytes per second. The local rates refer to peers on the local network.
		// By default peers on the local network are not rate limited.
		// 
		// These rate limits are only used for local peers (peers within the same
		// subnet as the client itself) and it is only used when
		// ``session_settings::ignore_limits_on_local_network`` is set to true
		// (which it is by default). These rate limits default to unthrottled,
		// but can be useful in case you want to treat local peers
		// preferentially, but not quite unthrottled.
		// 
		// A value of 0 means unlimited.
		int upload_rate_limit;
		int download_rate_limit;
		int local_upload_rate_limit;
		int local_download_rate_limit;

		// sets the rate limit on the DHT. This is specified in bytes per second
		// and defaults to 4000. For busy boxes with lots of torrents that
		// requires more DHT traffic, this should be raised.
		int dht_upload_rate_limit;

		// the max number of unchoked peers in the session. The number of unchoke
		// slots may be ignored depending on what ``choking_algorithm`` is set
		// to. A value of -1 means infinite.
		int unchoke_slots_limit;

		// sets the maximum number of half-open connections libtorrent will have
		// when connecting to peers. A half-open connection is one where
		// connect() has been called, but the connection still hasn't been
		// established (nor failed). Windows XP Service Pack 2 sets a default,
		// system wide, limit of the number of half-open connections to 10. So,
		// this limit can be used to work nicer together with other network
		// applications on that system. The default is to have no limit, and
		// passing -1 as the limit, means to have no limit. When limiting the
		// number of simultaneous connection attempts, peers will be put in a
		// queue waiting for their turn to get connected.
		int half_open_limit;

		// sets a global limit on the number of connections opened. The number of
		// connections is set to a hard minimum of at least two per torrent, so
		// if you set a too low connections limit, and open too many torrents,
		// the limit will not be met.
		int connections_limit;

		// the number of extra incoming connections allowed temporarily, in order
		// to support replacing peers
		int connections_slack;

		// the target delay for uTP sockets in milliseconds. A high value will
		// make uTP connections more aggressive and cause longer queues in the
		// upload bottleneck. It cannot be too low, since the noise in the
		// measurements would cause it to send too slow. The default is 50
		// milliseconds.
		int utp_target_delay;

		// the number of bytes the uTP congestion window can increase at the most
		// in one RTT. This defaults to 300 bytes. If this is set too high, the
		// congestion controller reacts too hard to noise and will not be stable,
		// if it's set too low, it will react slow to congestion and not back off
		// as fast.
		int utp_gain_factor;

		// the shortest allowed uTP socket timeout, specified in milliseconds.
		// This defaults to 500 milliseconds. The timeout depends on the RTT of
		// the connection, but is never smaller than this value. A connection
		// times out when every packet in a window is lost, or when a packet is
		// lost twice in a row (i.e. the resent packet is lost as well).
		// 
		// The shorter the timeout is, the faster the connection will recover
		// from this situation, assuming the RTT is low enough.
		int utp_min_timeout;

		// the number of SYN packets that are sent (and timed out) before
		// giving up and closing the socket.
		int utp_syn_resends;

		// the number of resent packets sent on a closed socket before giving up
		int utp_fin_resends;

		// the number of times a packet is sent (and lossed or timed out)
		// before giving up and closing the connection.
		int utp_num_resends;

		// the number of milliseconds of timeout for the initial SYN packet for
		// uTP connections. For each timed out packet (in a row), the timeout is
		// doubled.
		int utp_connect_timeout;

#ifndef TORRENT_NO_DEPRECATE
		// number of milliseconds of delaying ACKing packets the most
		int utp_delayed_ack;
#endif

		// controls if the uTP socket manager is allowed to increase the socket
		// buffer if a network interface with a large MTU is used (such as
		// loopback or ethernet jumbo frames). This defaults to true and might
		// improve uTP throughput. For RAM constrained systems, disabling this
		// typically saves around 30kB in user space and probably around 400kB in
		// kernel socket buffers (it adjusts the send and receive buffer size on
		// the kernel socket, both for IPv4 and IPv6).
		bool utp_dynamic_sock_buf;

		// controls how the congestion window is changed when a packet loss is
		// experienced. It's specified as a percentage multiplier for ``cwnd``.
		// By default it's set to 50 (i.e. cut in half). Do not change this value
		// unless you know what you're doing. Never set it higher than 100.
		int utp_loss_multiplier;

		// the options for session_settings::mixed_mode_algorithm.
		enum bandwidth_mixed_algo_t
		{
			// disables the mixed mode bandwidth balancing
			prefer_tcp = 0,

			// does not throttle uTP, throttles TCP to the same proportion
			// of throughput as there are TCP connections
			peer_proportional = 1
		};

		// determines how to treat TCP connections when there are uTP
		// connections. Since uTP is designed to yield to TCP, there's an
		// inherent problem when using swarms that have both TCP and uTP
		// connections. If nothing is done, uTP connections would often be
		// starved out for bandwidth by the TCP connections. This mode is
		// ``prefer_tcp``. The ``peer_proportional`` mode simply looks at the
		// current throughput and rate limits all TCP connections to their
		// proportional share based on how many of the connections are TCP. This
		// works best if uTP connections are not rate limited by the global rate
		// limiter, see rate_limit_utp.
		//
		// see bandwidth_mixed_algo_t for options.
		int mixed_mode_algorithm;

#ifndef TORRENT_NO_DEPRECATE
		// deprecated, use set_peer_class_filter() instead
		// set to true if uTP connections should be rate limited
		// defaults to false
		bool rate_limit_utp;
#endif

		// the value passed in to listen() for the listen socket. It is the
		// number of outstanding incoming connections to queue up while we're not
		// actively waiting for a connection to be accepted. The default is 5
		// which should be sufficient for any normal client. If this is a high
		// performance server which expects to receive a lot of connections, or
		// used in a simulator or test, it might make sense to raise this number.
		// It will not take affect until listen_on() is called again (or for the
		// first time).
		int listen_queue_size;

		// if true, the ``&ip=`` argument in tracker requests (unless otherwise
		// specified) will be set to the intermediate IP address, if the user is
		// double NATed. If ther user is not double NATed, this option has no
		// affect.
		bool announce_double_nat;

		// the number of peers to try to connect to immediately when the first
		// tracker response is received for a torrent. This is a boost to given
		// to new torrents to accelerate them starting up. The normal connect
		// scheduler is run once every second, this allows peers to be connected
		// immediately instead of waiting for the session tick to trigger
		// connections.
		int torrent_connect_boost;

		// determines if seeding (and finished) torrents should attempt to make
		// outgoing connections or not. By default this is true. It may be set to
		// false in very specific applications where the cost of making outgoing
		// connections is high, and there are no or small benefits of doing so.
		// For instance, if no nodes are behind a firewall or a NAT, seeds don't
		// need to make outgoing connections.
		bool seeding_outgoing_connections;

		// if true (which is the default), libtorrent will not connect to any
		// peers on priviliged ports (<= 1023). This can mitigate using
		// bittorrent swarms for certain DDoS attacks.
		bool no_connect_privileged_ports;

		// the maximum number of alerts queued up internally. If alerts are not
		// popped, the queue will eventually fill up to this level. This defaults
		// to 1000.
		int alert_queue_size;

		// the maximum allowed size (in bytes) to be received
		// by the metadata extension, i.e. magnet links. It defaults to 1 MiB.
		int max_metadata_size;

		// true by default, which means the number of connection attempts per
		// second may be limited to below the ``connection_speed``, in case we're
		// close to bump up against the limit of number of connections. The
		// intention of this setting is to more evenly distribute our connection
		// attempts over time, instead of attempting to connectin in batches, and
		// timing them out in batches.
		bool smooth_connects;

		// defaults to false. When set to true, web connections will include a
		// user-agent with every request, as opposed to just the first request in
		// a connection.
		bool always_send_user_agent;

		// defaults to true. It determines whether the IP filter applies to
		// trackers as well as peers. If this is set to false, trackers are
		// exempt from the IP filter (if there is one). If no IP filter is set,
		// this setting is irrelevant.
		bool apply_ip_filter_to_trackers;

		// used to avoid starvation of read jobs in the disk I/O thread. By
		// default, read jobs are deferred, sorted by physical disk location and
		// serviced once all write jobs have been issued. In scenarios where the
		// download rate is enough to saturate the disk, there's a risk the read
		// jobs will never be serviced. With this setting, every *x* write job,
		// issued in a row, will instead pick one read job off of the sorted
		// queue, where *x* is ``read_job_every``.
		int read_job_every;

		// defaults to true and will attempt to optimize disk reads by giving the
		// operating system heads up of disk read requests as they are queued in
		// the disk job queue. This gives a significant performance boost for
		// seeding.
		bool use_disk_read_ahead;

		// determines whether or not to lock files which libtorrent is
		// downloading to or seeding from. This is implemented using
		// ``fcntl(F_SETLK)`` on unix systems and by not passing in
		// ``SHARE_READ`` and ``SHARE_WRITE`` on windows. This might prevent 3rd
		// party processes from corrupting the files under libtorrent's feet.
		bool lock_files;

		// the number of threads to use for hash checking of pieces
		// defaults to 1. If set to 0, the disk thread is used for hashing
		int hashing_threads;

		// the number of blocks to keep outstanding at any given time when
		// checking torrents. Higher numbers give faster re-checks but uses
		// more memory. Specified in number of 16 kiB blocks
		int checking_mem_usage;

		// if set to > 0, pieces will be announced to other peers before they are
		// fully downloaded (and before they are hash checked). The intention is
		// to gain 1.5 potential round trip times per downloaded piece. When
		// non-zero, this indicates how many milliseconds in advance pieces
		// should be announced, before they are expected to be completed.
		int predictive_piece_announce;

		// when false, bytes off the socket is received directly into the disk
		// buffer. This requires many more calls to recv(). When using a
		// contiguous recv buffer, the download rate can be much higher
		bool contiguous_recv_buffer;

		//#error this should not be an option, it should depend on whether or not we're seeding or downloading

		// for some aio back-ends, the number of io-threads to use
		int aio_threads;
		// for some aio back-ends, the max number of outstanding jobs
		int aio_max;

		// the number of threads to use to call async_write_some on peer sockets.
		// When seeding at extremely high speeds, using 2 or more threads here
		// may make sense. Also when using SSL peer connections
		int network_threads;

		// if this is set, it is interpreted as a file path to where to create an
		// mmaped file to back the disk cache. this is mostly useful to introduce
		// another caching layer between RAM and hard drives. Typically you would
		// point this to an SSD drive.
		std::string mmap_cache;

		// sets the listen port for SSL connections. If this is set to 0, no SSL
		// listen port is opened. Otherwise a socket is opened on this port. This
		// setting is only taken into account when opening the regular listen
		// port, and won't re-open the listen socket simply by changing this
		// setting.
		// 
		// if this is 0, outgoing SSL connections are disabled
		// 
		// It defaults to port 4433.
		int ssl_listen;

		// ``tracker_backoff`` determines how aggressively to back off from
		// retrying failing trackers. This value determines *x* in the following
		// formula, determining the number of seconds to wait until the next
		// retry:
		// 
		// 	delay = 5 + 5 * x / 100 * fails^2
		// 
		// It defaults to 250.
		// 
		// This setting may be useful to make libtorrent more or less aggressive
		// in hitting trackers.
		// 
		int tracker_backoff;

		// enables banning web seeds. By default, web seeds that send corrupt
		// data are banned.
		bool ban_web_seeds;

		// specifies the max number of bytes to receive into RAM buffers when
		// downloading stuff over HTTP. Specifically when specifying a URL to a
		// .torrent file when adding a torrent or when announcing to an HTTP
		// tracker. The default is 2 MiB.
		int max_http_recv_buffer_size;

		// enables or disables the share mode extension. This is enabled by
		// default.
		bool support_share_mode;

		// enables or disables the merkle tree torrent support. This is enabled
		// by default.
		bool support_merkle_torrents;

		// enables or disables reporting redundant bytes to the tracker. This is
		// enabled by default.
		bool report_redundant_bytes;

		// the version string to advertise for this client in the peer protocol
		// handshake. If this is empty the user_agent is used
		std::string handshake_client_version;

		// if this is true, the disk cache uses a pool allocator for disk cache
		// blocks. Enabling this improves performance of the disk cache with the
		// side effect that the disk cache is less likely and slower at returning
		// memory to the kernel when cache pressure is low.
		bool use_disk_cache_pool;

		// the download and upload rate limits for a torrent to be considered
		// active by the queuing mechanism. A torrent whose download rate is less
		// than ``inactive_down_rate`` and whose upload rate is less than
		// ``inactive_up_rate`` for ``auto_manage_startup`` seconds, is
		// considered inactive, and another queued torrent may be startert.
		// This logic is disabled if ``dont_count_slow_torrents`` is false.
		int inactive_down_rate;
		int inactive_up_rate;
	};
#endif

	// structure used to hold configuration options for the DHT
	//
	// The ``dht_settings`` struct used to contain a ``service_port`` member to
	// control which port the DHT would listen on and send messages from. This
	// field is deprecated and ignored. libtorrent always tries to open the UDP
	// socket on the same port as the TCP socket.
	struct TORRENT_EXPORT dht_settings
	{
		// initialized dht_settings to the default values
		dht_settings()
			: max_peers_reply(100)
			, search_branching(5)
#ifndef TORRENT_NO_DEPRECATE
			, service_port(0)
#endif
			, max_fail_count(20)
			, max_torrents(2000)
			, max_dht_items(700)
			, max_peers(5000)
			, max_torrent_search_reply(20)
			, restrict_routing_ips(true)
			, restrict_search_ips(true)
			, extended_routing_table(true)
			, aggressive_lookups(true)
			, privacy_lookups(false)
			, enforce_node_id(false)
			, ignore_dark_internet(true)
			, block_timeout(5 * 60)
			, block_ratelimit(5)
			, read_only(false)
			, item_lifetime(0)
		{}

		// the maximum number of peers to send in a reply to ``get_peers``
		int max_peers_reply;

		// the number of concurrent search request the node will send when
		// announcing and refreshing the routing table. This parameter is called
		// alpha in the kademlia paper
		int search_branching;

#ifndef TORRENT_NO_DEPRECATE
		// the listen port for the dht. This is a UDP port. zero means use the
		// same as the tcp interface
		int service_port;
#endif

		// the maximum number of failed tries to contact a node before it is
		// removed from the routing table. If there are known working nodes that
		// are ready to replace a failing node, it will be replaced immediately,
		// this limit is only used to clear out nodes that don't have any node
		// that can replace them.
		int max_fail_count;

		// the total number of torrents to track from the DHT. This is simply an
		// upper limit to make sure malicious DHT nodes cannot make us allocate
		// an unbounded amount of memory.
		int max_torrents;

		// max number of items the DHT will store
		int max_dht_items;

		// the max number of peers to store per torrent (for the DHT)
		int max_peers;

		// the max number of torrents to return in a torrent search query to the
		// DHT
		int max_torrent_search_reply;

		// determines if the routing table entries should restrict entries to one
		// per IP. This defaults to true, which helps mitigate some attacks on
		// the DHT. It prevents adding multiple nodes with IPs with a very close
		// CIDR distance.
		//
		// when set, nodes whose IP address that's in the same /24 (or /64 for
		// IPv6) range in the same routing table bucket. This is an attempt to
		// mitigate node ID spoofing attacks also restrict any IP to only have a
		// single entry in the whole routing table
		bool restrict_routing_ips;

		// determines if DHT searches should prevent adding nodes with IPs with
		// very close CIDR distance. This also defaults to true and helps
		// mitigate certain attacks on the DHT.
		bool restrict_search_ips;

		// makes the first buckets in the DHT routing table fit 128, 64, 32 and
		// 16 nodes respectively, as opposed to the standard size of 8. All other
		// buckets have size 8 still.
		bool extended_routing_table;

		// slightly changes the lookup behavior in terms of how many outstanding
		// requests we keep. Instead of having branch factor be a hard limit, we
		// always keep *branch factor* outstanding requests to the closest nodes.
		// i.e. every time we get results back with closer nodes, we query them
		// right away. It lowers the lookup times at the cost of more outstanding
		// queries.
		bool aggressive_lookups;

		// when set, perform lookups in a way that is slightly more expensive,
		// but which minimizes the amount of information leaked about you.
		bool privacy_lookups;

		// when set, node's whose IDs that are not correctly generated based on
		// its external IP are ignored. When a query arrives from such node, an
		// error message is returned with a message saying "invalid node ID".
		bool enforce_node_id;

		// ignore DHT messages from parts of the internet we wouldn't expect to
		// see any traffic from
		bool ignore_dark_internet;

		// the number of seconds a DHT node is banned if it exceeds the rate
		// limit. The rate limit is averaged over 10 seconds to allow for bursts
		// above the limit.
		int block_timeout;

		// the max number of packets per second a DHT node is allowed to send
		// without getting banned.
		int block_ratelimit;

		// when set, the other nodes won't keep this node in their routing
		// tables, it's meant for low-power and/or ephemeral devices that
		// cannot support the DHT, it is also useful for mobile devices which
		// are sensitive to network traffic and battery life.
		// this node no longer responds to 'query' messages, and will place a
		// 'ro' key (value = 1) in the top-level message dictionary of outgoing
		// query messages.
		bool read_only;

		// the number of seconds a immutable/mutable item will be expired.
		// default is 0, means never expires.
		int item_lifetime;
	};


#ifndef TORRENT_NO_DEPRECATE
	// The ``pe_settings`` structure is used to control the settings related
	// to peer protocol encryption.
	struct TORRENT_EXPORT pe_settings
	{
		// initializes the encryption settings with the default vaues
		pe_settings()
			: out_enc_policy(enabled)
			, in_enc_policy(enabled)
			, allowed_enc_level(both)
			, prefer_rc4(false)
		{}

		// the encoding policy options for use with pe_settings::out_enc_policy
		// and pe_settings::in_enc_policy.
		enum enc_policy
		{
			// Only encrypted connections are allowed. Incoming connections that
			// are not encrypted are closed and if the encrypted outgoing
			// connection fails, a non-encrypted retry will not be made.
			forced,

			// encrypted connections are enabled, but non-encrypted connections
			// are allowed. An incoming non-encrypted connection will be accepted,
			// and if an outgoing encrypted connection fails, a non- encrypted
			// connection will be tried.
			enabled,

			// only non-encrypted connections are allowed.
			disabled
		};

		// the encryption levels, to be used with pe_settings::allowed_enc_level.
		enum enc_level
		{
			// use only plaintext encryption
			plaintext = 1,
			// use only rc4 encryption
			rc4 = 2,
			// allow both
			both = 3
		};

		// control the settings for incoming
		// and outgoing connections respectively.
		// see enc_policy enum for the available options.
		boost::uint8_t out_enc_policy;
		boost::uint8_t in_enc_policy;

		// determines the encryption level of the
		// connections.  This setting will adjust which encryption scheme is
		// offered to the other peer, as well as which encryption scheme is
		// selected by the client. See enc_level enum for options.
		boost::uint8_t allowed_enc_level;

		// if the allowed encryption level is both, setting this to
		// true will prefer rc4 if both methods are offered, plaintext
		// otherwise
		bool prefer_rc4;
	};
#endif // TORRENT_NO_DEPRECATE

}

#endif
