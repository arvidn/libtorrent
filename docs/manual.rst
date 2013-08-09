============================
libtorrent API Documentation
============================

:Author: Arvid Norberg, arvid@rasterbar.com
:Version: 1.0.0

.. contents:: Table of contents
  :depth: 1
  :backlinks: none

overview
========

The interface of libtorrent consists of a few classes. The main class is
the ``session``, it contains the main loop that serves all torrents.

The basic usage is as follows:

* construct a session
* load session state from settings file (see `load_state() save_state()`_)
* start extensions (see `add_extension()`_).
* start DHT, LSD, UPnP, NAT-PMP etc (see `start_dht() stop_dht() set_dht_settings() dht_state() is_dht_running()`_
  `start_lsd() stop_lsd()`_, `start_upnp() stop_upnp()`_ and `start_natpmp() stop_natpmp()`_)
* parse .torrent-files and add them to the session (see `bdecode() bencode()`_ and `async_add_torrent() add_torrent()`_)
* main loop (see session_)

	* query the torrent_handles for progress (see torrent_handle_)
	* query the session for information
	* add and remove torrents from the session at run-time

* save resume data for all torrent_handles (optional, see
  `save_resume_data()`_)
* save session state (see `load_state() save_state()`_)
* destruct session object

Each class and function is described in this manual.

For a description on how to create torrent files, see make_torrent_.

.. _make_torrent: make_torrent.html

things to keep in mind
======================

A common problem developers are facing is torrents stopping without explanation.
Here is a description on which conditions libtorrent will stop your torrents,
how to find out about it and what to do about it.

Make sure to keep track of the paused state, the error state and the upload
mode of your torrents. By default, torrents are auto-managed, which means
libtorrent will pause them, unpause them, scrape them and take them out
of upload-mode automatically.

Whenever a torrent encounters a fatal error, it will be stopped, and the
``torrent_status::error`` will describe the error that caused it. If a torrent
is auto managed, it is scraped periodically and paused or resumed based on
the number of downloaders per seed. This will effectively seed torrents that
are in the greatest need of seeds.

If a torrent hits a disk write error, it will be put into upload mode. This
means it will not download anything, but only upload. The assumption is that
the write error is caused by a full disk or write permission errors. If the
torrent is auto-managed, it will periodically be taken out of the upload
mode, trying to write things to the disk again. This means torrent will recover
from certain disk errors if the problem is resolved. If the torrent is not
auto managed, you have to call `set_upload_mode()`_ to turn
downloading back on again.

network primitives
==================

There are a few typedefs in the ``libtorrent`` namespace which pulls
in network types from the ``asio`` namespace. These are::

	typedef asio::ip::address address;
	typedef asio::ip::address_v4 address_v4;
	typedef asio::ip::address_v6 address_v6;
	using asio::ip::tcp;
	using asio::ip::udp;

These are declared in the ``<libtorrent/socket.hpp>`` header.

The ``using`` statements will give easy access to::

	tcp::endpoint
	udp::endpoint

Which are the endpoint types used in libtorrent. An endpoint is an address
with an associated port.

For documentation on these types, please refer to the `asio documentation`_.

.. _`asio documentation`: http://asio.sourceforge.net/asio-0.3.8/doc/asio/reference.html

session customization
=====================

You have some control over session configuration through the ``session_settings`` object. You
create it and fill it with your settings and then use ``session::set_settings()``
to apply them.

You have control over proxy and authorization settings and also the user-agent
that will be sent to the tracker. The user-agent will also be used to identify the
client with other peers.

session_settings
----------------

::

	struct session_settings
	{
		session_settings();
		int version;
		std::string user_agent;
		int tracker_completion_timeout;
		int tracker_receive_timeout;
		int stop_tracker_timeout;
		int tracker_maximum_response_length;

		int piece_timeout;
		float request_queue_time;
		int max_allowed_in_request_queue;
		int max_out_request_queue;
		int whole_pieces_threshold;
		int peer_timeout;
		int urlseed_timeout;
		int urlseed_pipeline_size;
		int file_pool_size;
		bool allow_multiple_connections_per_ip;
		int max_failcount;
		int min_reconnect_time;
		int peer_connect_timeout;
		bool ignore_limits_on_local_network;
		int connection_speed;
		bool send_redundant_have;
		bool lazy_bitfields;
		int inactivity_timeout;
		int unchoke_interval;
		int optimistic_unchoke_interval;
		std::string announce_ip;
		int num_want;
		int initial_picker_threshold;
		int allowed_fast_set_size;

		enum { no_piece_suggestions = 0, suggest_read_cache = 1 };
		int suggest_mode;
		int max_queued_disk_bytes;
		int handshake_timeout;
		bool use_dht_as_fallback;
		bool free_torrent_hashes;
		bool upnp_ignore_nonrouters;
		int send_buffer_watermark;
		int send_buffer_watermark_factor;

	#ifndef TORRENT_NO_DEPRECATE
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

		int seed_choking_algorithm;

		bool use_parole_mode;
		int cache_size;
		int cache_buffer_chunk_size;
		int cache_expiry;
		bool use_read_cache;
		bool explicit_read_cache;
		int explicit_cache_interval;

		enum io_buffer_mode_t
		{
			enable_os_cache = 0,
			disable_os_cache_for_aligned_files = 1,
			disable_os_cache = 2
		};
		int disk_io_write_mode;
		int disk_io_read_mode;

		std::pair<int, int> outgoing_ports;
		char peer_tos;

		int active_downloads;
		int active_seeds;
		int active_dht_limit;
		int active_tracker_limit;
		int active_limit;
		bool auto_manage_prefer_seeds;
		bool dont_count_slow_torrents;
		int auto_manage_interval;
		float share_ratio_limit;
		float seed_time_ratio_limit;
		int seed_time_limit;
		int peer_turnover_interval;
		float peer_turnover;
		float peer_turnover_cutoff;
		bool close_redundant_connections;

		int auto_scrape_interval;
		int auto_scrape_min_interval;

		int max_peerlist_size;

		int min_announce_interval;

		bool prioritize_partial_pieces;
		int auto_manage_startup;

		bool rate_limit_ip_overhead;

		bool announce_to_all_trackers;
		bool announce_to_all_tiers;

		bool prefer_udp_trackers;
		bool strict_super_seeding;

		int seeding_piece_quota;

		int max_sparse_regions;

		bool lock_disk_cache;

		int max_rejects;

		int recv_socket_buffer_size;
		int send_socket_buffer_size;

		bool optimize_hashing_for_speed;

		int file_checks_delay_per_block;

		enum disk_cache_algo_t
		{ lru, largest_contiguous, avoid_readback };

		disk_cache_algo_t disk_cache_algorithm;

		int read_cache_line_size;
		int write_cache_line_size;

		int optimistic_disk_retry;
		bool disable_hash_checks;

		int max_suggest_pieces;

		bool drop_skipped_requests;

		bool low_prio_disk;
		int local_service_announce_interval;
		int dht_announce_interval;

		int udp_tracker_token_expiry;
		bool volatile_read_cache;
		bool guided_read_cache;
		bool default_cache_min_age;

		int num_optimistic_unchoke_slots;
		bool no_atime_storage;
		int default_est_reciprocation_rate;
		int increase_est_reciprocation_rate;
		int decrease_est_reciprocation_rate;
		bool incoming_starts_queued_torrents;
		bool report_true_downloaded;
		bool strict_end_game_mode;

		bool broadcast_lsd;

		bool enable_outgoing_utp;
		bool enable_incoming_utp;
		bool enable_outgoing_tcp;
		bool enable_incoming_tcp;
		int max_pex_peers;
		bool ignore_resume_timestamps;
		bool no_recheck_incomplete_resume;
		bool anonymous_mode;
		bool force_proxy;
		int tick_interval;
		int share_mode_target;

		int upload_rate_limit;
		int download_rate_limit;
		int local_upload_rate_limit;
		int local_download_rate_limit;
		int dht_upload_rate_limit;
		int unchoke_slots_limit;
		int half_open_limit;
		int connections_limit;

		int utp_target_delay;
		int utp_gain_factor;
		int utp_min_timeout;
		int utp_syn_resends;
		int utp_num_resends;
		int utp_connect_timeout;
		bool utp_dynamic_sock_buf;
		int utp_loss_multiplier;

		enum bandwidth_mixed_algo_t
		{
			prefer_tcp = 0,
			peer_proportional = 1

		};
		int mixed_mode_algorithm;
		bool rate_limit_utp;

		int listen_queue_size;

		bool announce_double_nat;

		int torrent_connect_boost;
		bool seeding_outgoing_connections;

		bool no_connect_privileged_ports;
		int alert_queue_size;
		int max_metadata_size;
		bool smooth_connects;
		bool always_send_user_agent;
		bool apply_ip_filter_to_trackers;
		int read_job_every;
		bool use_disk_read_ahead;
		bool lock_files;

		int ssl_listen;

		int tracker_backoff;

		bool ban_web_seeds;
		int max_http_recv_buffer_size;

		bool support_share_mode;
		bool support_merkle_torrents;
		bool report_redundant_bytes;
		std::string handshake_client_version;
		bool use_disk_cache_pool;
	};

``version`` is automatically set to the libtorrent version you're using
in order to be forward binary compatible. This field should not be changed.

``user_agent`` this is the client identification to the tracker.
The recommended format of this string is:
"ClientName/ClientVersion libtorrent/libtorrentVersion".
This name will not only be used when making HTTP requests, but also when
sending extended headers to peers that support that extension.

``tracker_completion_timeout`` is the number of seconds the tracker
connection will wait from when it sent the request until it considers the
tracker to have timed-out. Default value is 60 seconds.

``tracker_receive_timeout`` is the number of seconds to wait to receive
any data from the tracker. If no data is received for this number of
seconds, the tracker will be considered as having timed out. If a tracker
is down, this is the kind of timeout that will occur. The default value
is 20 seconds.

``stop_tracker_timeout`` is the time to wait for tracker responses when
shutting down the session object. This is given in seconds. Default is
10 seconds.

``tracker_maximum_response_length`` is the maximum number of bytes in a
tracker response. If a response size passes this number it will be rejected
and the connection will be closed. On gzipped responses this size is measured
on the uncompressed data. So, if you get 20 bytes of gzip response that'll
expand to 2 megs, it will be interrupted before the entire response has been
uncompressed (given your limit is lower than 2 megs). Default limit is
1 megabyte.

``piece_timeout`` controls the number of seconds from a request is sent until
it times out if no piece response is returned.

``request_queue_time`` is the length of the request queue given in the number
of seconds it should take for the other end to send all the pieces. i.e. the
actual number of requests depends on the download rate and this number.
	
``max_allowed_in_request_queue`` is the number of outstanding block requests
a peer is allowed to queue up in the client. If a peer sends more requests
than this (before the first one has been handled) the last request will be
dropped. The higher this is, the faster upload speeds the client can get to a
single peer.

``max_out_request_queue`` is the maximum number of outstanding requests to
send to a peer. This limit takes precedence over ``request_queue_time``. i.e.
no matter the download speed, the number of outstanding requests will never
exceed this limit.

``whole_pieces_threshold`` is a limit in seconds. if a whole piece can be
downloaded in at least this number of seconds from a specific peer, the
peer_connection will prefer requesting whole pieces at a time from this peer.
The benefit of this is to better utilize disk caches by doing localized
accesses and also to make it easier to identify bad peers if a piece fails
the hash check.

``peer_timeout`` is the number of seconds the peer connection should
wait (for any activity on the peer connection) before closing it due
to time out. This defaults to 120 seconds, since that's what's specified
in the protocol specification. After half the time out, a keep alive message
is sent.

``urlseed_timeout`` is the same as ``peer_timeout`` but applies only to
url seeds. This value defaults to 20 seconds.

``urlseed_pipeline_size`` controls the pipelining with the web server. When
using persistent connections to HTTP 1.1 servers, the client is allowed to
send more requests before the first response is received. This number controls
the number of outstanding requests to use with url-seeds. Default is 5.

``file_pool_size`` is the the upper limit on the total number of files this
session will keep open. The reason why files are left open at all is that
some anti virus software hooks on every file close, and scans the file for
viruses. deferring the closing of the files will be the difference between
a usable system and a completely hogged down system. Most operating systems
also has a limit on the total number of file descriptors a process may have
open. It is usually a good idea to find this limit and set the number of
connections and the number of files limits so their sum is slightly below it.

``allow_multiple_connections_per_ip`` determines if connections from the
same IP address as existing connections should be rejected or not. Multiple
connections from the same IP address is not allowed by default, to prevent
abusive behavior by peers. It may be useful to allow such connections in
cases where simulations are run on the same machie, and all peers in a
swarm has the same IP address.

``max_failcount`` is the maximum times we try to connect to a peer before
stop connecting again. If a peer succeeds, the failcounter is reset. If
a peer is retrieved from a peer source (other than DHT) the failcount is
decremented by one, allowing another try.

``min_reconnect_time`` is the time to wait between connection attempts. If
the peer fails, the time is multiplied by fail counter.

``peer_connect_timeout`` the number of seconds to wait after a connection
attempt is initiated to a peer until it is considered as having timed out.
The default is 10 seconds. This setting is especially important in case
the number of half-open connections are limited, since stale half-open
connection may delay the connection of other peers considerably.

``ignore_limits_on_local_network``, if set to true, upload, download and
unchoke limits are ignored for peers on the local network.

``connection_speed`` is the number of connection attempts that
are made per second. If a number < 0 is specified, it will default to
200 connections per second. If 0 is specified, it means don't make
outgoing connections at all.

``send_redundant_have`` controls if have messages will be sent
to peers that already have the piece. This is typically not necessary,
but it might be necessary for collecting statistics in some cases.
Default is false.

``lazy_bitfields`` prevents outgoing bitfields from being full. If the
client is seed, a few bits will be set to 0, and later filled in with
have-messages. This is to prevent certain ISPs from stopping people
from seeding.

``inactivity_timeout``, if a peer is uninteresting and uninterested
for longer than this number of seconds, it will be disconnected.
Default is 10 minutes

``unchoke_interval`` is the number of seconds between chokes/unchokes.
On this interval, peers are re-evaluated for being choked/unchoked. This
is defined as 30 seconds in the protocol, and it should be significantly
longer than what it takes for TCP to ramp up to it's max rate.

``optimistic_unchoke_interval`` is the number of seconds between
each *optimistic* unchoke. On this timer, the currently optimistically
unchoked peer will change.

``announce_ip`` is the ip address passed along to trackers as the ``&ip=`` parameter.
If left as the default (an empty string), that parameter is omitted.

``num_want`` is the number of peers we want from each tracker request. It defines
what is sent as the ``&num_want=`` parameter to the tracker.

``initial_picker_threshold`` specifies the number of pieces we need before we
switch to rarest first picking. This defaults to 4, which means the 4 first
pieces in any torrent are picked at random, the following pieces are picked
in rarest first order.

``allowed_fast_set_size`` is the number of pieces we allow peers to download
from us without being unchoked.

``suggest_mode`` controls whether or not libtorrent will send out suggest
messages to create a bias of its peers to request certain pieces. The modes
are:

* ``no_piece_suggestsions`` which is the default and will not send out suggest
  messages.
* ``suggest_read_cache`` which will send out suggest messages for the most
  recent pieces that are in the read cache.

``max_queued_disk_bytes`` is the number maximum number of bytes, to be
written to disk, that can wait in the disk I/O thread queue. This queue
is only for waiting for the disk I/O thread to receive the job and either
write it to disk or insert it in the write cache. When this limit is reached,
the peer connections will stop reading data from their sockets, until the disk
thread catches up. Setting this too low will severly limit your download rate.

``handshake_timeout`` specifies the number of seconds we allow a peer to
delay responding to a protocol handshake. If no response is received within
this time, the connection is closed.

``use_dht_as_fallback`` determines how the DHT is used. If this is true,
the DHT will only be used for torrents where all trackers in its tracker
list has failed. Either by an explicit error message or a time out. This
is false by default, which means the DHT is used by default regardless of
if the trackers fail or not.

``free_torrent_hashes`` determines whether or not the torrent's piece hashes
are kept in memory after the torrent becomes a seed or not. If it is set to
``true`` the hashes are freed once the torrent is a seed (they're not
needed anymore since the torrent won't download anything more). If it's set
to false they are not freed. If they are freed, the torrent_info_ returned
by get_torrent_info() will return an object that may be incomplete, that
cannot be passed back to `async_add_torrent() add_torrent()`_ for instance.

``upnp_ignore_nonrouters`` indicates whether or not the UPnP implementation
should ignore any broadcast response from a device whose address is not the
configured router for this machine. i.e. it's a way to not talk to other
people's routers by mistake.

``send_buffer_watermark`` is the upper limit of the send buffer low-watermark.
if the send buffer has fewer bytes than this, we'll read another 16kB block
onto it. If set too small, upload rate capacity will suffer. If set too high,
memory will be wasted. The actual watermark may be lower than this in case
the upload rate is low, this is the upper limit.

``send_buffer_watermark_factor`` is multiplied to the peer's upload rate
to determine the low-watermark for the peer. It is specified as a percentage,
which means 100 represents a factor of 1.
The low-watermark is still clamped to not exceed the ``send_buffer_watermark``
upper limit. This defaults to 50. For high capacity connections, setting this
higher can improve upload performance and disk throughput. Setting it too
high may waste RAM and create a bias towards read jobs over write jobs.

``auto_upload_slots`` defaults to true. When true, if there is a global upload
limit set and the current upload rate is less than 90% of that, another upload
slot is opened. If the upload rate has been saturated for an extended period
of time, on upload slot is closed. The number of upload slots will never be
less than what has been set by ``session::set_max_uploads()``. To query the
current number of upload slots, see ``session_status::allowed_upload_slots``.

When ``auto_upload_slots_rate_based`` is set, and ``auto_upload_slots`` is set,
the max upload slots setting is used as a minimum number of unchoked slots.
This algorithm is designed to prevent the peer from spreading its upload
capacity too thin, but still open more slots in order to utilize the full capacity.

``choking_algorithm`` specifies which algorithm to use to determine which peers
to unchoke. This setting replaces the deprecated settings ``auto_upload_slots``
and ``auto_upload_slots_rate_based``.

The options for choking algorithms are:

* ``fixed_slots_choker`` is the traditional choker with a fixed number of unchoke
  slots (as specified by ``session::set_max_uploads()``).

* ``auto_expand_choker`` opens at least the number of slots as specified by
  ``session::set_max_uploads()`` but opens up more slots if the upload capacity
  is not saturated. This unchoker will work just like the ``fixed_slot_choker``
  if there's no global upload rate limit set.

* ``rate_based_choker`` opens up unchoke slots based on the upload rate
  achieved to peers. The more slots that are opened, the marginal upload
  rate required to open up another slot increases.

* ``bittyrant_choker`` attempts to optimize download rate by finding the
  reciprocation rate of each peer individually and prefers peers that gives
  the highest *return on investment*. It still allocates all upload capacity,
  but shuffles it around to the best peers first. For this choker to be
  efficient, you need to set a global upload rate limit
  (``session_settings::upload_rate_limit``). For more information about this
  choker, see the paper_.

.. _paper: http://bittyrant.cs.washington.edu/#papers

``seed_choking_algorithm`` controls the seeding unchoke behavior. The available
options are:

* ``round_robin`` which round-robins the peers that are unchoked when seeding. This
  distributes the upload bandwidht uniformly and fairly. It minimizes the ability
  for a peer to download everything without redistributing it.

* ``fastest_upload`` unchokes the peers we can send to the fastest. This might be
  a bit more reliable in utilizing all available capacity.

* ``anti_leech`` prioritizes peers who have just started or are just about to finish
  the download. The intention is to force peers in the middle of the download to
  trade with each other.

``use_parole_mode`` specifies if parole mode should be used. Parole mode means
that peers that participate in pieces that fail the hash check are put in a mode
where they are only allowed to download whole pieces. If the whole piece a peer
in parole mode fails the hash check, it is banned. If a peer participates in a
piece that passes the hash check, it is taken out of parole mode.

``cache_size`` is the disk write and read  cache. It is specified in units of
16 KiB blocks. Buffers that are part of a peer's send or receive buffer also
count against this limit. Send and receive buffers will never be denied to be
allocated, but they will cause the actual cached blocks to be flushed or evicted.
If this is set to -1, the cache size is automatically set to the amount
of physical RAM available in the machine divided by 8. If the amount of physical
RAM cannot be determined, it's set to 1024 (= 16 MiB).

Disk buffers are allocated using a pool allocator, the number of blocks that
are allocated at a time when the pool needs to grow can be specified in
``cache_buffer_chunk_size``. This defaults to 16 blocks. Lower numbers
saves memory at the expense of more heap allocations. It must be at least 1.

``cache_expiry`` is the number of seconds from the last cached write to a piece
in the write cache, to when it's forcefully flushed to disk. Default is 60 second.

``use_read_cache``, is set to true (default), the disk cache is also used to
cache pieces read from disk. Blocks for writing pieces takes presedence.

``explicit_read_cache`` defaults to 0. If set to something greater than 0, the
disk read cache will not be evicted by cache misses and will explicitly be
controlled based on the rarity of pieces. Rare pieces are more likely to be
cached. This would typically be used together with ``suggest_mode`` set to
``suggest_read_cache``. The value is the number of pieces to keep in the read
cache. If the actual read cache can't fit as many, it will essentially be clamped.

``explicit_cache_interval`` is the number of seconds in between each refresh of
a part of the explicit read cache. Torrents take turns in refreshing and this
is the time in between each torrent refresh. Refreshing a torrent's explicit
read cache means scanning all pieces and picking a random set of the rarest ones.
There is an affinity to pick pieces that are already in the cache, so that
subsequent refreshes only swaps in pieces that are rarer than whatever is in
the cache at the time.

``disk_io_write_mode`` and ``disk_io_read_mode`` determines how files are
opened when they're in read only mode versus read and write mode. The options
are:

	* enable_os_cache
		This is the default and files are opened normally, with the OS caching
		reads and writes.
	* disable_os_cache_for_aligned_files
		This will open files in unbuffered mode for files where every read and
		write would be sector aligned. Using aligned disk offsets is a requirement
		on some operating systems.
	* disable_os_cache
		This opens all files in unbuffered mode (if allowed by the operating system).
		Linux and Windows, for instance, require disk offsets to be sector aligned,
		and in those cases, this option is the same as ``disable_os_caches_for_aligned_files``.

One reason to disable caching is that it may help the operating system from growing
its file cache indefinitely. Since some OSes only allow aligned files to be opened
in unbuffered mode, It is recommended to make the largest file in a torrent the first
file (with offset 0) or use pad files to align all files to piece boundries.

``outgoing_ports``, if set to something other than (0, 0) is a range of ports
used to bind outgoing sockets to. This may be useful for users whose router
allows them to assign QoS classes to traffic based on its local port. It is
a range instead of a single port because of the problems with failing to reconnect
to peers if a previous socket to that peer and port is in ``TIME_WAIT`` state.

.. warning:: setting outgoing ports will limit the ability to keep multiple
	connections to the same client, even for different torrents. It is not
	recommended to change this setting. Its main purpose is to use as an
	escape hatch for cheap routers with QoS capability but can only classify
	flows based on port numbers.

``peer_tos`` determines the TOS byte set in the IP header of every packet
sent to peers (including web seeds). The default value for this is ``0x0``
(no marking). One potentially useful TOS mark is ``0x20``, this represents
the *QBone scavenger service*. For more details, see QBSS_.

.. _`QBSS`: http://qbone.internet2.edu/qbss/

``active_downloads`` and ``active_seeds`` controls how many active seeding and
downloading torrents the queuing mechanism allows. The target number of active
torrents is ``min(active_downloads + active_seeds, active_limit)``.
``active_downloads`` and ``active_seeds`` are upper limits on the number of
downloading torrents and seeding torrents respectively. Setting the value to
-1 means unlimited.

For example if there are 10 seeding torrents and 10 downloading torrents, and
``active_downloads`` is 4 and ``active_seeds`` is 4, there will be 4 seeds
active and 4 downloading torrents. If the settings are ``active_downloads`` = 2
and ``active_seeds`` = 4, then there will be 2 downloading torrents and 4 seeding
torrents active. Torrents that are not auto managed are also counted against these
limits. If there are non-auto managed torrents that use up all the slots, no
auto managed torrent will be activated.

``auto_manage_prefer_seeds`` specifies if libtorrent should prefer giving seeds
active slots or downloading torrents.  The default is ``false``.

if ``dont_count_slow_torrents`` is true, torrents without any payload transfers are
not subject to the ``active_seeds`` and ``active_downloads`` limits. This is intended
to make it more likely to utilize all available bandwidth, and avoid having torrents
that don't transfer anything block the active slots.

``active_limit`` is a hard limit on the number of active torrents. This applies even to
slow torrents.

``active_dht_limit`` is the max number of torrents to announce to the DHT. By default
this is set to 88, which is no more than one DHT announce every 10 seconds.

``active_tracker_limit`` is the max number of torrents to announce to their trackers.
By default this is 360, which is no more than one announce every 5 seconds.

``active_lsd_limit`` is the max number of torrents to announce to the local network
over the local service discovery protocol. By default this is 80, which is no more
than one announce every 5 seconds (assuming the default announce interval of 5 minutes).

You can have more torrents *active*, even though they are not announced to the DHT,
lsd or their tracker. If some peer knows about you for any reason and tries to connect,
it will still be accepted, unless the torrent is paused, which means it won't accept
any connections.

``auto_manage_interval`` is the number of seconds between the torrent queue
is updated, and rotated.

``share_ratio_limit`` is the upload / download ratio limit for considering a
seeding torrent have met the seed limit criteria. See queuing_.

``seed_time_ratio_limit`` is the seeding time / downloading time ratio limit
for considering a seeding torrent to have met the seed limit criteria. See queuing_.

``seed_time_limit`` is the limit on the time a torrent has been an active seed
(specified in seconds) before it is considered having met the seed limit criteria.
See queuing_.

``peer_turnover_interval`` controls a feature where libtorrent periodically can disconnect
the least useful peers in the hope of connecting to better ones. This settings controls
the interval of this optimistic disconnect. It defaults to every 5 minutes, and
is specified in seconds.

``peer_turnover`` Is the fraction of the peers that are disconnected. This is
a float where 1.f represents all peers an 0 represents no peers. It defaults to
4% (i.e. 0.04f)

``peer_turnover_cutoff`` is the cut off trigger for optimistic unchokes. If a torrent
has more than this fraction of its connection limit, the optimistic unchoke is
triggered. This defaults to 90% (i.e. 0.9f).

``close_redundant_connections`` specifies whether libtorrent should close
connections where both ends have no utility in keeping the connection open.
For instance if both ends have completed their downloads, there's no point
in keeping it open. This defaults to ``true``.

``auto_scrape_interval`` is the number of seconds between scrapes of
queued torrents (auto managed and paused torrents). Auto managed
torrents that are paused, are scraped regularly in order to keep
track of their downloader/seed ratio. This ratio is used to determine
which torrents to seed and which to pause.

``auto_scrape_min_interval`` is the minimum number of seconds between any
automatic scrape (regardless of torrent). In case there are a large number
of paused auto managed torrents, this puts a limit on how often a scrape
request is sent.

``max_peerlist_size`` is the maximum number of peers in the list of
known peers. These peers are not necessarily connected, so this number
should be much greater than the maximum number of connected peers.
Peers are evicted from the cache when the list grows passed 90% of
this limit, and once the size hits the limit, peers are no longer
added to the list. If this limit is set to 0, there is no limit on
how many peers we'll keep in the peer list.

``max_paused_peerlist_size`` is the max peer list size used for torrents
that are paused. This default to the same as ``max_peerlist_size``, but
can be used to save memory for paused torrents, since it's not as
important for them to keep a large peer list.

``min_announce_interval`` is the minimum allowed announce interval
for a tracker. This is specified in seconds, defaults to 5 minutes and
is used as a sanity check on what is returned from a tracker. It
mitigates hammering misconfigured trackers.

If ``prioritize_partial_pieces`` is true, partial pieces are picked
before pieces that are more rare. If false, rare pieces are always
prioritized, unless the number of partial pieces is growing out of
proportion.

``auto_manage_startup`` is the number of seconds a torrent is considered
active after it was started, regardless of upload and download speed. This
is so that newly started torrents are not considered inactive until they
have a fair chance to start downloading.

If ``rate_limit_ip_overhead`` is set to true, the estimated TCP/IP overhead is
drained from the rate limiters, to avoid exceeding the limits with the total traffic

``announce_to_all_trackers`` controls how multi tracker torrents are
treated. If this is set to true, all trackers in the same tier are
announced to in parallel. If all trackers in tier 0 fails, all trackers
in tier 1 are announced as well. If it's set to false, the behavior is as
defined by the multi tracker specification. It defaults to false, which
is the same behavior previous versions of libtorrent has had as well.

``announce_to_all_tiers`` also controls how multi tracker torrents are
treated. When this is set to true, one tracker from each tier is announced
to. This is the uTorrent behavior. This is false by default in order
to comply with the multi-tracker specification.

``prefer_udp_trackers`` is true by default. It means that trackers may
be rearranged in a way that udp trackers are always tried before http
trackers for the same hostname. Setting this to fails means that the
trackers' tier is respected and there's no preference of one protocol
over another.

``strict_super_seeding`` when this is set to true, a piece has to
have been forwarded to a third peer before another one is handed out.
This is the traditional definition of super seeding.

``seeding_piece_quota`` is the number of pieces to send to a peer,
when seeding, before rotating in another peer to the unchoke set.
It defaults to 3 pieces, which means that when seeding, any peer we've
sent more than this number of pieces to will be unchoked in favour of
a choked peer.

``max_sparse_regions`` is a limit of the number of *sparse regions* in
a torrent. A sparse region is defined as a hole of pieces we have not
yet downloaded, in between pieces that have been downloaded. This is
used as a hack for windows vista which has a bug where you cannot
write files with more than a certain number of sparse regions. This
limit is not hard, it will be exceeded. Once it's exceeded, pieces
that will maintain or decrease the number of sparse regions are
prioritized. To disable this functionality, set this to 0. It defaults
to 0 on all platforms except windows.

``lock_disk_cache`` if lock disk cache is set to true the disk cache
that's in use, will be locked in physical memory, preventing it from
being swapped out.

``max_rejects`` is the number of piece requests we will reject in a row
while a peer is choked before the peer is considered abusive and is
disconnected.


``recv_socket_buffer_size`` and ``send_socket_buffer_size`` specifies
the buffer sizes set on peer sockets. 0 (which is the default) means
the OS default (i.e. don't change the buffer sizes). The socket buffer
sizes are changed using setsockopt() with SOL_SOCKET/SO_RCVBUF and
SO_SNDBUFFER.

``optimize_hashing_for_speed`` chooses between two ways of reading back
piece data from disk when its complete and needs to be verified against
the piece hash. This happens if some blocks were flushed to the disk
out of order. Everything that is flushed in order is hashed as it goes
along. Optimizing for speed will allocate space to fit all the the
remaingin, unhashed, part of the piece, reads the data into it in a single
call and hashes it. This is the default. If ``optimizing_hashing_for_speed``
is false, a single block will be allocated (16 kB), and the unhashed parts
of the piece are read, one at a time, and hashed in this single block. This
is appropriate on systems that are memory constrained.

``file_checks_delay_per_block`` is the number of milliseconds to sleep
in between disk read operations when checking torrents. This defaults
to 0, but can be set to higher numbers to slow down the rate at which
data is read from the disk while checking. This may be useful for
background tasks that doesn't matter if they take a bit longer, as long
as they leave disk I/O time for other processes.

``disk_cache_algorithm`` tells the disk I/O thread which cache flush
algorithm to use. The default algorithm is largest_contiguous. This
flushes the entire piece, in the write cache, that was least recently
written to. This is specified by the ``session_settings::lru`` enum
value. ``session_settings::largest_contiguous`` will flush the largest
sequences of contiguous blocks from the write cache, regarless of the
piece's last use time. ``session_settings::avoid_readback`` will prioritize
flushing blocks that will avoid having to read them back in to verify
the hash of the piece once it's done. This is especially useful for high
throughput setups, where reading from the disk is especially expensive.

``read_cache_line_size`` is the number of blocks to read into the read
cache when a read cache miss occurs. Setting this to 0 is essentially
the same thing as disabling read cache. The number of blocks read
into the read cache is always capped by the piece boundry.

When a piece in the write cache has ``write_cache_line_size`` contiguous
blocks in it, they will be flushed. Setting this to 1 effectively
disables the write cache.

``optimistic_disk_retry`` is the number of seconds from a disk write
errors occur on a torrent until libtorrent will take it out of the
upload mode, to test if the error condition has been fixed.

libtorrent will only do this automatically for auto managed torrents.

You can explicitly take a torrent out of upload only mode using
`set_upload_mode()`_.

``disable_hash_checks`` controls if downloaded pieces are verified against
the piece hashes in the torrent file or not. The default is false, i.e.
to verify all downloaded data. It may be useful to turn this off for performance
profiling and simulation scenarios. Do not disable the hash check for regular
bittorrent clients.

``max_suggest_pieces`` is the max number of suggested piece indices received
from a peer that's remembered. If a peer floods suggest messages, this limit
prevents libtorrent from using too much RAM. It defaults to 10.

If ``drop_skipped_requests`` is set to true (it defaults to false), piece
requests that have been skipped enough times when piece messages
are received, will be considered lost. Requests are considered skipped
when the returned piece messages are re-ordered compared to the order
of the requests. This was an attempt to get out of dead-locks caused by
BitComet peers silently ignoring some requests. It may cause problems
at high rates, and high level of reordering in the uploading peer, that's
why it's disabled by default.

``low_prio_disk`` determines if the disk I/O should use a normal
or low priority policy. This defaults to true, which means that
it's low priority by default. Other processes doing disk I/O will
normally take priority in this mode. This is meant to improve the
overall responsiveness of the system while downloading in the
background. For high-performance server setups, this might not
be desirable.

``local_service_announce_interval`` is the time between local
network announces for a torrent. By default, when local service
discovery is enabled a torrent announces itself every 5 minutes.
This interval is specified in seconds.

``dht_announce_interval`` is the number of seconds between announcing
torrents to the distributed hash table (DHT). This is specified to
be 15 minutes which is its default.

``dht_max_torrents`` is the max number of torrents we will track
in the DHT.

``udp_tracker_token_expiry`` is the number of seconds libtorrent
will keep UDP tracker connection tokens around for. This is specified
to be 60 seconds, and defaults to that. The higher this value is, the
fewer packets have to be sent to the UDP tracker. In order for higher
values to work, the tracker needs to be configured to match the
expiration time for tokens.

``volatile_read_cache``, if this is set to true, read cache blocks
that are hit by peer read requests are removed from the disk cache
to free up more space. This is useful if you don't expect the disk
cache to create any cache hits from other peers than the one who
triggered the cache line to be read into the cache in the first place.

``guided_read_cache`` enables the disk cache to adjust the size
of a cache line generated by peers to depend on the upload rate
you are sending to that peer. The intention is to optimize the RAM
usage of the cache, to read ahead further for peers that you're
sending faster to.

``default_cache_min_age`` is the minimum number of seconds any read
cache line is kept in the cache. This defaults to one second but
may be greater if ``guided_read_cache`` is enabled. Having a lower
bound on the time a cache line stays in the cache is an attempt
to avoid swapping the same pieces in and out of the cache in case
there is a shortage of spare cache space.

``num_optimistic_unchoke_slots`` is the number of optimistic unchoke
slots to use. It defaults to 0, which means automatic. Having a higher
number of optimistic unchoke slots mean you will find the good peers
faster but with the trade-off to use up more bandwidth. When this is
set to 0, libtorrent opens up 20% of your allowed upload slots as
optimistic unchoke slots.

``no_atime_storage`` this is a linux-only option and passes in the
``O_NOATIME`` to ``open()`` when opening files. This may lead to
some disk performance improvements.

``default_est_reciprocation_rate`` is the assumed reciprocation rate
from peers when using the BitTyrant choker. This defaults to 14 kiB/s.
If set too high, you will over-estimate your peers and be more altruistic
while finding the true reciprocation rate, if it's set too low, you'll
be too stingy and waste finding the true reciprocation rate.

``increase_est_reciprocation_rate`` specifies how many percent the
extimated reciprocation rate should be increased by each unchoke
interval a peer is still choking us back. This defaults to 20%.
This only applies to the BitTyrant choker.

``decrease_est_reciprocation_rate`` specifies how many percent the
estimated reciprocation rate should be decreased by each unchoke
interval a peer unchokes us. This default to 3%.
This only applies to the BitTyrant choker.

``incoming_starts_queued_torrents`` defaults to false. If a torrent
has been paused by the auto managed feature in libtorrent, i.e.
the torrent is paused and auto managed, this feature affects whether
or not it is automatically started on an incoming connection. The
main reason to queue torrents, is not to make them unavailable, but
to save on the overhead of announcing to the trackers, the DHT and to
avoid spreading one's unchoke slots too thin. If a peer managed to
find us, even though we're no in the torrent anymore, this setting
can make us start the torrent and serve it.

When ``report_true_downloaded`` is true, the ``&downloaded=`` argument
sent to trackers will include redundant downloaded bytes. It defaults
to ``false``, which means redundant bytes are not reported to the tracker.

``strict_end_game_mode`` defaults to true, and controls when a block
may be requested twice. If this is ``true``, a block may only be requested
twice when there's ay least one request to every piece that's left to
download in the torrent. This may slow down progress on some pieces
sometimes, but it may also avoid downloading a lot of redundant bytes.
If this is ``false``, libtorrent attempts to use each peer connection
to its max, by always requesting something, even if it means requesting
something that has been requested from another peer already.

if ``broadcast_lsd`` is set to true, the local peer discovery
(or Local Service Discovery) will not only use IP multicast, but also
broadcast its messages. This can be useful when running on networks
that don't support multicast. Since broadcast messages might be
expensive and disruptive on networks, only every 8th announce uses
broadcast.

``enable_outgoing_utp``, ``enable_incoming_utp``, ``enable_outgoing_tcp``,
``enable_incoming_tcp`` all determines if libtorrent should attempt to make
outgoing connections of the specific type, or allow incoming connection. By
default all of them are enabled.

``ignore_resume_timestamps`` determines if the storage, when loading
resume data files, should verify that the file modification time
with the timestamps in the resume data. This defaults to false, which
means timestamps are taken into account, and resume data is less likely
to accepted (torrents are more likely to be fully checked when loaded).
It might be useful to set this to true if your network is faster than your
disk, and it would be faster to redownload potentially missed pieces than
to go through the whole storage to look for them.

``no_recheck_incomplete_resume`` determines if the storage should check
the whole files when resume data is incomplete or missing or whether
it should simply assume we don't have any of the data. By default, this
is determined by the existance of any of the files. By setting this setting
to true, the files won't be checked, but will go straight to download
mode.

``anonymous_mode`` defaults to false. When set to true, the client tries
to hide its identity to a certain degree. The peer-ID will no longer
include the client's fingerprint. The user-agent will be reset to an
empty string.

If you're using I2P, it might make sense to enable anonymous mode.

``force_proxy`` disables any communication that's not going over a proxy.
Enabling this requires a proxy to be configured as well, see ``set_proxy_settings``.
The listen sockets are closed, and incoming connections will
only be accepted through a SOCKS5 or I2P proxy (if a peer proxy is set up and
is run on the same machine as the tracker proxy). This setting also
disabled peer country lookups, since those are done via DNS lookups that
aren't supported by proxies.

``tick_interval`` specifies the number of milliseconds between internal
ticks. This is the frequency with which bandwidth quota is distributed to
peers. It should not be more than one second (i.e. 1000 ms). Setting this
to a low value (around 100) means higher resolution bandwidth quota distribution,
setting it to a higher value saves CPU cycles.

``share_mode_target`` specifies the target share ratio for share mode torrents.
This defaults to 3, meaning we'll try to upload 3 times as much as we download.
Setting this very high, will make it very conservative and you might end up
not downloading anything ever (and not affecting your share ratio). It does
not make any sense to set this any lower than 2. For instance, if only 3 peers
need to download the rarest piece, it's impossible to download a single piece
and upload it more than 3 times. If the share_mode_target is set to more than 3,
nothing is downloaded.

``upload_rate_limit``, ``download_rate_limit``, ``local_upload_rate_limit``
and ``local_download_rate_limit`` sets the session-global limits of upload
and download rate limits, in bytes per second. The local rates refer to peers
on the local network. By default peers on the local network are not rate limited.

These rate limits are only used for local peers (peers within the same subnet as
the client itself) and it is only used when ``session_settings::ignore_limits_on_local_network``
is set to true (which it is by default). These rate limits default to unthrottled,
but can be useful in case you want to treat local peers preferentially, but not
quite unthrottled.

A value of 0 means unlimited.

``dht_upload_rate_limit`` sets the rate limit on the DHT. This is specified in
bytes per second and defaults to 4000. For busy boxes with lots of torrents
that requires more DHT traffic, this should be raised.

``unchoke_slots_limit`` is the max number of unchoked peers in the session. The
number of unchoke slots may be ignored depending on what ``choking_algorithm``
is set to. A value of -1 means infinite.

``half_open_limit`` sets the maximum number of half-open connections
libtorrent will have when connecting to peers. A half-open connection is one
where connect() has been called, but the connection still hasn't been established
(nor failed). Windows XP Service Pack 2 sets a default, system wide, limit of
the number of half-open connections to 10. So, this limit can be used to work
nicer together with other network applications on that system. The default is
to have no limit, and passing -1 as the limit, means to have no limit. When
limiting the number of simultaneous connection attempts, peers will be put in
a queue waiting for their turn to get connected.

``connections_limit`` sets a global limit on the number of connections
opened. The number of connections is set to a hard minimum of at least two per
torrent, so if you set a too low connections limit, and open too many torrents,
the limit will not be met.

``utp_target_delay`` is the target delay for uTP sockets in milliseconds. A high
value will make uTP connections more aggressive and cause longer queues in the upload
bottleneck. It cannot be too low, since the noise in the measurements would cause
it to send too slow. The default is 50 milliseconds.

``utp_gain_factor`` is the number of bytes the uTP congestion window can increase
at the most in one RTT. This defaults to 300 bytes. If this is set too high,
the congestion controller reacts too hard to noise and will not be stable, if it's
set too low, it will react slow to congestion and not back off as fast.

``utp_min_timeout`` is the shortest allowed uTP socket timeout, specified in milliseconds.
This defaults to 500 milliseconds. The timeout depends on the RTT of the connection, but
is never smaller than this value. A connection times out when every packet in a window
is lost, or when a packet is lost twice in a row (i.e. the resent packet is lost as well).

The shorter the timeout is, the faster the connection will recover from this situation,
assuming the RTT is low enough.

``utp_syn_resends`` is the number of SYN packets that are sent (and timed out) before
giving up and closing the socket.

``utp_num_resends`` is the number of times a packet is sent (and lossed or timed out)
before giving up and closing the connection.

``utp_connect_timeout`` is the number of milliseconds of timeout for the initial SYN
packet for uTP connections. For each timed out packet (in a row), the timeout is doubled.

``utp_dynamic_sock_buf`` controls if the uTP socket manager is allowed to increase
the socket buffer if a network interface with a large MTU is used (such as loopback
or ethernet jumbo frames). This defaults to true and might improve uTP throughput.
For RAM constrained systems, disabling this typically saves around 30kB in user space
and probably around 400kB in kernel socket buffers (it adjusts the send and receive
buffer size on the kernel socket, both for IPv4 and IPv6).

``utp_loss_multiplier`` controls how the congestion window is changed when a packet
loss is experienced. It's specified as a percentage multiplier for ``cwnd``. By default
it's set to 50 (i.e. cut in half). Do not change this value unless you know what
you're doing. Never set it higher than 100.

The ``mixed_mode_algorithm`` determines how to treat TCP connections when there are
uTP connections. Since uTP is designed to yield to TCP, there's an inherent problem
when using swarms that have both TCP and uTP connections. If nothing is done, uTP
connections would often be starved out for bandwidth by the TCP connections. This mode
is ``prefer_tcp``. The ``peer_proportional`` mode simply looks at the current throughput
and rate limits all TCP connections to their proportional share based on how many of
the connections are TCP. This works best if uTP connections are not rate limited by
the global rate limiter (which they aren't by default).

``rate_limit_utp`` determines if uTP connections should be throttled by the global rate
limiter or not. By default they are.

``listen_queue_size`` is the value passed in to listen() for the listen socket.
It is the number of outstanding incoming connections to queue up while we're not
actively waiting for a connection to be accepted. The default is 5 which should
be sufficient for any normal client. If this is a high performance server which
expects to receive a lot of connections, or used in a simulator or test, it
might make sense to raise this number. It will not take affect until listen_on()
is called again (or for the first time).

if ``announce_double_nat`` is true, the ``&ip=`` argument in tracker requests
(unless otherwise specified) will be set to the intermediate IP address, if the
user is double NATed. If ther user is not double NATed, this option has no affect.

``torrent_connect_boost`` is the number of peers to try to connect to immediately
when the first tracker response is received for a torrent. This is a boost to
given to new torrents to accelerate them starting up. The normal connect scheduler
is run once every second, this allows peers to be connected immediately instead
of waiting for the session tick to trigger connections.

``seeding_outgoing_connections`` determines if seeding (and finished) torrents
should attempt to make outgoing connections or not. By default this is true. It
may be set to false in very specific applications where the cost of making
outgoing connections is high, and there are no or small benefits of doing so.
For instance, if no nodes are behind a firewall or a NAT, seeds don't need to
make outgoing connections.

if ``no_connect_privileged_ports`` is true (which is the default), libtorrent
will not connect to any peers on priviliged ports (<= 1023). This can mitigate
using bittorrent swarms for certain DDoS attacks.

``alert_queue_size`` is the maximum number of alerts queued up internally. If
alerts are not popped, the queue will eventually fill up to this level. This
defaults to 1000.

``max_metadata_size`` is the maximum allowed size (in bytes) to be received
by the metadata extension, i.e. magnet links. It defaults to 1 MiB.

``smooth_connects`` is true by default, which means the number of connection
attempts per second may be limited to below the ``connection_speed``, in case
we're close to bump up against the limit of number of connections. The intention
of this setting is to more evenly distribute our connection attempts over time,
instead of attempting to connectin in batches, and timing them out in batches.

``always_send_user_agent`` defaults to false. When set to true, web connections
will include a user-agent with every request, as opposed to just the first
request in a connection.

``apply_ip_filter_to_trackers`` defaults to true. It determines whether the
IP filter applies to trackers as well as peers. If this is set to false,
trackers are exempt from the IP filter (if there is one). If no IP filter
is set, this setting is irrelevant.

``read_job_every`` is used to avoid starvation of read jobs in the disk I/O
thread. By default, read jobs are deferred, sorted by physical disk location
and serviced once all write jobs have been issued. In scenarios where the
download rate is enough to saturate the disk, there's a risk the read jobs will
never be serviced. With this setting, every *x* write job, issued in a row, will
instead pick one read job off of the sorted queue, where *x* is ``read_job_every``.

``use_disk_read_ahead`` defaults to true and will attempt to optimize disk reads
by giving the operating system heads up of disk read requests as they are queued
in the disk job queue. This gives a significant performance boost for seeding.

``lock_files`` determines whether or not to lock files which libtorrent is downloading
to or seeding from. This is implemented using ``fcntl(F_SETLK)`` on unix systems and
by not passing in ``SHARE_READ`` and ``SHARE_WRITE`` on windows. This might prevent
3rd party processes from corrupting the files under libtorrent's feet.

``ssl_listen`` sets the listen port for SSL connections. If this is set to 0,
no SSL listen port is opened. Otherwise a socket is opened on this port. This
setting is only taken into account when opening the regular listen port, and
won't re-open the listen socket simply by changing this setting.

It defaults to port 4433.

``tracker_backoff`` determines how aggressively to back off from retrying
failing trackers. This value determines *x* in the following formula, determining
the number of seconds to wait until the next retry:

	delay = 5 + 5 * x / 100 * fails^2

It defaults to 250.

This setting may be useful to make libtorrent more or less aggressive in hitting
trackers.

``ban_web_seeds`` enables banning web seeds. By default, web seeds that send
corrupt data are banned.

``max_http_recv_buffer_size`` specifies the max number of bytes to receive into
RAM buffers when downloading stuff over HTTP. Specifically when specifying a
URL to a .torrent file when adding a torrent or when announcing to an HTTP
tracker. The default is 2 MiB.

``support_share_mode`` enables or disables the share mode extension. This is
enabled by default.

``support_merkle_torrents`` enables or disables the merkle tree torrent support.
This is enabled by default.

``report_redundant_bytes`` enables or disables reporting redundant bytes to the tracker.
This is enabled by default.

``handshake_client_version`` is the client name advertized in the peer handshake. If
set to an empty string, the user_agent string is used.

``use_disk_cache_pool`` enables using a pool allocator for disk cache blocks. This is
disabled by default. Enabling it makes the cache perform better at high throughput.
It also makes the cache less likely and slower at returning memory back to the system
once allocated.

exceptions
==========

Many functions in libtorrent have two versions, one that throws exceptions on
errors and one that takes an ``error_code`` reference which is filled with the
error code on errors.

There is one exception class that is used for errors in libtorrent, it is based
on boost.system's ``error_code`` class to carry the error code.

libtorrent_exception
--------------------

::

	struct libtorrent_exception: std::exception
	{
		libtorrent_exception(error_code const& s);
		virtual const char* what() const throw();
		virtual ~libtorrent_exception() throw() {}
		boost::system::error_code error() const;
	};


error_code
==========

The names of these error codes are declared in then ``libtorrent::errors`` namespace.

HTTP errors are reported in the ``libtorrent::http_category``, with error code enums in
the ``libtorrent::errors`` namespace.

====== =========================================
code   symbol                                   
====== =========================================
100    cont                                     
------ -----------------------------------------
200    ok                                       
------ -----------------------------------------
201    created                                  
------ -----------------------------------------
202    accepted                                 
------ -----------------------------------------
204    no_content                               
------ -----------------------------------------
300    multiple_choices                         
------ -----------------------------------------
301    moved_permanently                        
------ -----------------------------------------
302    moved_temporarily                        
------ -----------------------------------------
304    not_modified                             
------ -----------------------------------------
400    bad_request                              
------ -----------------------------------------
401    unauthorized                             
------ -----------------------------------------
403    forbidden                                
------ -----------------------------------------
404    not_found                                
------ -----------------------------------------
500    internal_server_error                    
------ -----------------------------------------
501    not_implemented                          
------ -----------------------------------------
502    bad_gateway                              
------ -----------------------------------------
503    service_unavailable                      
====== =========================================

translating error codes
-----------------------

The error_code::message() function will typically return a localized error string,
for system errors. That is, errors that belong to the generic or system category.

Errors that belong to the libtorrent error category are not localized however, they
are only available in english. In order to translate libtorrent errors, compare the
error category of the ``error_code`` object against ``libtorrent::get_libtorrent_category()``,
and if matches, you know the error code refers to the list above. You can provide
your own mapping from error code to string, which is localized. In this case, you
cannot rely on ``error_code::message()`` to generate your strings.

The numeric values of the errors are part of the API and will stay the same, although
new error codes may be appended at the end.

Here's a simple example of how to translate error codes::

	std::string error_code_to_string(boost::system::error_code const& ec)
	{
		if (ec.category() != libtorrent::get_libtorrent_category())
		{
			return ec.message();
		}
		// the error is a libtorrent error

		int code = ec.value();
		static const char const* swedish[] =
		{
			"inget fel",
			"en fil i torrenten kolliderar med en fil fran en annan torrent",
			"hash check misslyckades",
			"torrent filen ar inte en dictionary",
			"'info'-nyckeln saknas eller ar korrupt i torrentfilen",
			"'info'-faltet ar inte en dictionary",
			"'piece length' faltet saknas eller ar korrupt i torrentfilen",
			"torrentfilen saknar namnfaltet",
			"ogiltigt namn i torrentfilen (kan vara en attack)",
			// ... more strings here
		};

		// use the default error string in case we don't have it
		// in our translated list
		if (code < 0 || code >= sizeof(swedish)/sizeof(swedish[0]))
			return ec.message();

		return swedish[code];
	}

storage_interface
=================

The storage interface is a pure virtual class that can be implemented to
customize how and where data for a torrent is stored. The default storage
implementation uses regular files in the filesystem, mapping the files in the
torrent in the way one would assume a torrent is saved to disk. Implementing
your own storage interface makes it possible to store all data in RAM, or in
some optimized order on disk (the order the pieces are received for instance),
or saving multifile torrents in a single file in order to be able to take
advantage of optimized disk-I/O.

It is also possible to write a thin class that uses the default storage but
modifies some particular behavior, for instance encrypting the data before
it's written to disk, and decrypting it when it's read again.

The storage interface is based on slots, each slot is 'piece_size' number
of bytes. All access is done by writing and reading whole or partial
slots. One slot is one piece in the torrent, but the data in the slot
does not necessarily correspond to the piece with the same index (in
compact allocation mode it won't).

libtorrent comes with two built-in storage implementations; ``default_storage``
and ``disabled_storage``. Their constructor functions are called ``default_storage_constructor``
and ``disabled_storage_constructor`` respectively. The disabled storage does
just what it sounds like. It throws away data that's written, and it
reads garbage. It's useful mostly for benchmarking and profiling purpose.


The interface looks like this::

	struct storage_interface
	{
		virtual bool initialize(bool allocate_files) = 0;
		virtual bool has_any_file() = 0;
		virtual void hint_read(int slot, int offset, int len);
		virtual int readv(file::iovec_t const* bufs, int slot, int offset, int num_bufs) = 0;
		virtual int writev(file::iovec_t const* bufs, int slot, int offset, int num_bufs) = 0;
		virtual int sparse_end(int start) const;
		virtual bool move_storage(fs::path save_path) = 0;
		virtual bool verify_resume_data(lazy_entry const& rd, error_code& error) = 0;
		virtual bool write_resume_data(entry& rd) const = 0;
		virtual bool move_slot(int src_slot, int dst_slot) = 0;
		virtual bool swap_slots(int slot1, int slot2) = 0;
		virtual bool swap_slots3(int slot1, int slot2, int slot3) = 0;
		virtual bool rename_file(int file, std::string const& new_name) = 0;
		virtual bool release_files() = 0;
		virtual bool delete_files() = 0;
		virtual void finalize_file(int index) {}
		virtual ~storage_interface() {}

		// non virtual functions

		disk_buffer_pool* disk_pool();
		void set_error(std::string const& file, error_code const& ec) const;
		error_code const& error() const;
		std::string const& error_file() const;
		void clear_error();
	};


initialize()
------------

	::

		bool initialize(bool allocate_files) = 0;

This function is called when the storage is to be initialized. The default storage
will create directories and empty files at this point. If ``allocate_files`` is true,
it will also ``ftruncate`` all files to their target size.

Returning ``true`` indicates an error occurred.

has_any_file()
--------------

	::

		virtual bool has_any_file() = 0;

This function is called when first checking (or re-checking) the storage for a torrent.
It should return true if any of the files that is used in this storage exists on disk.
If so, the storage will be checked for existing pieces before starting the download.

hint_read()
-----------

	::

		void hint_read(int slot, int offset, int len);

This function is called when a read job is queued. It gives the storage wrapper an
opportunity to hint the operating system about this coming read. For instance, the
storage may call ``posix_fadvise(POSIX_FADV_WILLNEED)`` or ``fcntl(F_RDADVISE)``.

readv() writev()
----------------

	::

		int readv(file::iovec_t const* buf, int slot, int offset, int num_bufs) = 0;
		int write(const char* buf, int slot, int offset, int size) = 0;

These functions should read or write the data in or to the given ``slot`` at the given ``offset``.
It should read or write ``num_bufs`` buffers sequentially, where the size of each buffer
is specified in the buffer array ``bufs``. The file::iovec_t type has the following members::

	struct iovec_t
	{
		void* iov_base;
		size_t iov_len;
	};

The return value is the number of bytes actually read or written, or -1 on failure. If
it returns -1, the error code is expected to be set to

Every buffer in ``bufs`` can be assumed to be page aligned and be of a page aligned size,
except for the last buffer of the torrent. The allocated buffer can be assumed to fit a
fully page aligned number of bytes though. This is useful when reading and writing the
last piece of a file in unbuffered mode.

The ``offset`` is aligned to 16 kiB boundries  *most of the time*, but there are rare
exceptions when it's not. Specifically if the read cache is disabled/or full and a
client requests unaligned data, or the file itself is not aligned in the torrent.
Most clients request aligned data.

sparse_end()
------------

	::

		int sparse_end(int start) const;

This function is optional. It is supposed to return the first piece, starting at
``start`` that is fully contained within a data-region on disk (i.e. non-sparse
region). The purpose of this is to skip parts of files that can be known to contain
zeros when checking files.

move_storage()
--------------

	::

		bool move_storage(fs::path save_path) = 0;

This function should move all the files belonging to the storage to the new save_path.
The default storage moves the single file or the directory of the torrent.

Before moving the files, any open file handles may have to be closed, like
``release_files()``.

Returning ``false`` indicates an error occurred.


verify_resume_data()
--------------------

	::

		bool verify_resume_data(lazy_entry const& rd, error_code& error) = 0;

This function should verify the resume data ``rd`` with the files
on disk. If the resume data seems to be up-to-date, return true. If
not, set ``error`` to a description of what mismatched and return false.

The default storage may compare file sizes and time stamps of the files.

Returning ``false`` indicates an error occurred.


write_resume_data()
-------------------

	::

		bool write_resume_data(entry& rd) const = 0;

This function should fill in resume data, the current state of the
storage, in ``rd``. The default storage adds file timestamps and
sizes.

Returning ``true`` indicates an error occurred.


move_slot()
-----------

	::

		bool move_slot(int src_slot, int dst_slot) = 0;

This function should copy or move the data in slot ``src_slot`` to
the slot ``dst_slot``. This is only used in compact mode.

If the storage caches slots, this could be implemented more
efficient than reading and writing the data.

Returning ``true`` indicates an error occurred.


swap_slots()
------------

	::

		bool swap_slots(int slot1, int slot2) = 0;

This function should swap the data in ``slot1`` and ``slot2``. The default
storage uses a scratch buffer to read the data into, then moving the other
slot and finally writing back the temporary slot's data

This is only used in compact mode.

Returning ``true`` indicates an error occurred.


swap_slots3()
-------------

	::

		bool swap_slots3(int slot1, int slot2, int slot3) = 0;

This function should do a 3-way swap, or shift of the slots. ``slot1``
should move to ``slot2``, which should be moved to ``slot3`` which in turn
should be moved to ``slot1``.

This is only used in compact mode.

Returning ``true`` indicates an error occurred.


rename_file()
-------------

	::

		bool rename_file(int file, std::string const& new_name) = 0;

Rename file with index ``file`` to the thame ``new_name``. If there is an error,
``true`` should be returned.


release_files()
---------------

	::

		bool release_files() = 0;

This function should release all the file handles that it keeps open to files
belonging to this storage. The default implementation just calls
``file_pool::release_files(this)``.

Returning ``true`` indicates an error occurred.


delete_files()
--------------

	::

		bool delete_files() = 0;

This function should delete all files and directories belonging to this storage.

Returning ``true`` indicates an error occurred.

The ``disk_buffer_pool`` is used to allocate and free disk buffers. It has the
following members::

	struct disk_buffer_pool : boost::noncopyable
	{
		char* allocate_buffer(char const* category);
		void free_buffer(char* buf);

		char* allocate_buffers(int blocks, char const* category);
		void free_buffers(char* buf, int blocks);

		int block_size() const { return m_block_size; }

		void release_memory();
	};

finalize_file()
---------------

	::

		virtual void finalize_file(int index);

This function is called each time a file is completely downloaded. The
storage implementation can perform last operations on a file. The file will
not be opened for writing after this.

``index`` is the index of the file that completed.

On windows the default storage implementation clears the sparse file flag
on the specified file.

example
-------

This is an example storage implementation that stores all pieces in a ``std::map``,
i.e. in RAM. It's not necessarily very useful in practice, but illustrates the
basics of implementing a custom storage.

::

	struct temp_storage : storage_interface
	{
		temp_storage(file_storage const& fs) : m_files(fs) {}
		virtual bool initialize(bool allocate_files) { return false; }
		virtual bool has_any_file() { return false; }
		virtual int read(char* buf, int slot, int offset, int size)
		{
			std::map<int, std::vector<char> >::const_iterator i = m_file_data.find(slot);
			if (i == m_file_data.end()) return 0;
			int available = i->second.size() - offset;
			if (available <= 0) return 0;
			if (available > size) available = size;
			memcpy(buf, &i->second[offset], available);
			return available;
		}
		virtual int write(const char* buf, int slot, int offset, int size)
		{
			std::vector<char>& data = m_file_data[slot];
			if (data.size() < offset + size) data.resize(offset + size);
			std::memcpy(&data[offset], buf, size);
			return size;
		}
		virtual bool rename_file(int file, std::string const& new_name)
		{ assert(false); return false; }
		virtual bool move_storage(std::string const& save_path) { return false; }
		virtual bool verify_resume_data(lazy_entry const& rd, error_code& error) { return false; }
		virtual bool write_resume_data(entry& rd) const { return false; }
		virtual bool move_slot(int src_slot, int dst_slot) { assert(false); return false; }
		virtual bool swap_slots(int slot1, int slot2) { assert(false); return false; }
		virtual bool swap_slots3(int slot1, int slot2, int slot3) { assert(false); return false; }
		virtual size_type physical_offset(int slot, int offset)
		{ return slot * m_files.piece_length() + offset; };
		virtual sha1_hash hash_for_slot(int slot, partial_hash& ph, int piece_size)
		{
			int left = piece_size - ph.offset;
			assert(left >= 0);
			if (left > 0)
			{
				std::vector<char>& data = m_file_data[slot];
				// if there are padding files, those blocks will be considered
				// completed even though they haven't been written to the storage.
				// in this case, just extend the piece buffer to its full size
				// and fill it with zeroes.
				if (data.size() < piece_size) data.resize(piece_size, 0);
				ph.h.update(&data[ph.offset], left);
			}
			return ph.h.final();
		}
		virtual bool release_files() { return false; }
		virtual bool delete_files() { return false; }
	
		std::map<int, std::vector<char> > m_file_data;
		file_storage m_files;
	};

	storage_interface* temp_storage_constructor(
		file_storage const& fs, file_storage const* mapped
		, std::string const& path, file_pool& fp
		, std::vector<boost::uint8_t> const& prio)
	{
		return new temp_storage(fs);
	}

magnet links
============

Magnet links are URIs that includes an info-hash, a display name and optionally
a tracker url. The idea behind magnet links is that an end user can click on a
link in a browser and have it handled by a bittorrent application, to start a
download, without any .torrent file.

The format of the magnet URI is:

**magnet:?xt=urn:btih:** *Base32 encoded info-hash* [ **&dn=** *name of download* ] [ **&tr=** *tracker URL* ]*

queuing
=======

libtorrent supports *queuing*. Which means it makes sure that a limited number of
torrents are being downloaded at any given time, and once a torrent is completely
downloaded, the next in line is started.

Torrents that are *auto managed* are subject to the queuing and the active torrents
limits. To make a torrent auto managed, set ``auto_managed`` to true when adding the
torrent (see `async_add_torrent() add_torrent()`_).

The limits of the number of downloading and seeding torrents are controlled via
``active_downloads``, ``active_seeds`` and ``active_limit`` in session_settings_. 
These limits takes non auto managed torrents into account as well. If there are 
more non-auto managed torrents being downloaded than the ``active_downloads`` 
setting, any auto managed torrents will be queued until torrents are removed so 
that the number drops below the limit.

The default values are 8 active downloads and 5 active seeds.

At a regular interval, torrents are checked if there needs to be any re-ordering of
which torrents are active and which are queued. This interval can be controlled via
``auto_manage_interval`` in session_settings_. It defaults to every 30 seconds.

For queuing to work, resume data needs to be saved and restored for all torrents.
See `save_resume_data()`_.

downloading
-----------

Torrents that are currently being downloaded or incomplete (with bytes still to download)
are queued. The torrents in the front of the queue are started to be actively downloaded
and the rest are ordered with regards to their queue position. Any newly added torrent
is placed at the end of the queue. Once a torrent is removed or turns into a seed, its
queue position is -1 and all torrents that used to be after it in the queue, decreases their
position in order to fill the gap.

The queue positions are always in a sequence without any gaps.

Lower queue position means closer to the front of the queue, and will be started sooner than
torrents with higher queue positions.

To query a torrent for its position in the queue, or change its position, see:
`queue_position() queue_position_up() queue_position_down() queue_position_top() queue_position_bottom()`_.

seeding
-------

Auto managed seeding torrents are rotated, so that all of them are allocated a fair
amount of seeding. Torrents with fewer completed *seed cycles* are prioritized for
seeding. A seed cycle is completed when a torrent meets either the share ratio limit
(uploaded bytes / downloaded bytes), the share time ratio (time seeding / time
downloaing) or seed time limit (time seeded).

The relevant settings to control these limits are ``share_ratio_limit``,
``seed_time_ratio_limit`` and ``seed_time_limit`` in session_settings_.


fast resume
===========

The fast resume mechanism is a way to remember which pieces are downloaded
and where they are put between sessions. You can generate fast resume data by
calling `save_resume_data()`_ on torrent_handle_. You can
then save this data to disk and use it when resuming the torrent. libtorrent
will not check the piece hashes then, and rely on the information given in the
fast-resume data. The fast-resume data also contains information about which
blocks, in the unfinished pieces, were downloaded, so it will not have to
start from scratch on the partially downloaded pieces.

To use the fast-resume data you simply give it to `async_add_torrent() add_torrent()`_, and it
will skip the time consuming checks. It may have to do the checking anyway, if
the fast-resume data is corrupt or doesn't fit the storage for that torrent,
then it will not trust the fast-resume data and just do the checking.

file format
-----------

The file format is a bencoded dictionary containing the following fields:

+--------------------------+--------------------------------------------------------------+
| ``file-format``          | string: "libtorrent resume file"                             |
|                          |                                                              |
+--------------------------+--------------------------------------------------------------+
| ``file-version``         | integer: 1                                                   |
|                          |                                                              |
+--------------------------+--------------------------------------------------------------+
| ``info-hash``            | string, the info hash of the torrent this data is saved for. |
|                          |                                                              |
+--------------------------+--------------------------------------------------------------+
| ``blocks per piece``     | integer, the number of blocks per piece. Must be: piece_size |
|                          | / (16 * 1024). Clamped to be within the range [1, 256]. It   |
|                          | is the number of blocks per (normal sized) piece. Usually    |
|                          | each block is 16 * 1024 bytes in size. But if piece size is  |
|                          | greater than 4 megabytes, the block size will increase.      |
|                          |                                                              |
+--------------------------+--------------------------------------------------------------+
| ``pieces``               | A string with piece flags, one character per piece.          |
|                          | Bit 1 means we have that piece.                              |
|                          | Bit 2 means we have verified that this piece is correct.     |
|                          | This only applies when the torrent is in seed_mode.          |
+--------------------------+--------------------------------------------------------------+
| ``slots``                | list of integers. The list maps slots to piece indices. It   |
|                          | tells which piece is on which slot. If piece index is -2 it  |
|                          | means it is free, that there's no piece there. If it is -1,  |
|                          | means the slot isn't allocated on disk yet. The pieces have  |
|                          | to meet the following requirement:                           |
|                          |                                                              |
|                          | If there's a slot at the position of the piece index,        |
|                          | the piece must be located in that slot.                      |
|                          |                                                              |
+--------------------------+--------------------------------------------------------------+
| ``total_uploaded``       | integer. The number of bytes that have been uploaded in      |
|                          | total for this torrent.                                      |
+--------------------------+--------------------------------------------------------------+
| ``total_downloaded``     | integer. The number of bytes that have been downloaded in    |
|                          | total for this torrent.                                      |
+--------------------------+--------------------------------------------------------------+
| ``active_time``          | integer. The number of seconds this torrent has been active. |
|                          | i.e. not paused.                                             |
+--------------------------+--------------------------------------------------------------+
| ``seeding_time``         | integer. The number of seconds this torrent has been active  |
|                          | and seeding.                                                 |
+--------------------------+--------------------------------------------------------------+
| ``num_seeds``            | integer. An estimate of the number of seeds on this torrent  |
|                          | when the resume data was saved. This is scrape data or based |
|                          | on the peer list if scrape data is unavailable.              |
+--------------------------+--------------------------------------------------------------+
| ``num_downloaders``      | integer. An estimate of the number of downloaders on this    |
|                          | torrent when the resume data was last saved. This is used as |
|                          | an initial estimate until we acquire up-to-date scrape info. |
+--------------------------+--------------------------------------------------------------+
| ``upload_rate_limit``    | integer. In case this torrent has a per-torrent upload rate  |
|                          | limit, this is that limit. In bytes per second.              |
+--------------------------+--------------------------------------------------------------+
| ``download_rate_limit``  | integer. The download rate limit for this torrent in case    |
|                          | one is set, in bytes per second.                             |
+--------------------------+--------------------------------------------------------------+
| ``max_connections``      | integer. The max number of peer connections this torrent     |
|                          | may have, if a limit is set.                                 |
+--------------------------+--------------------------------------------------------------+
| ``max_uploads``          | integer. The max number of unchoked peers this torrent may   |
|                          | have, if a limit is set.                                     |
+--------------------------+--------------------------------------------------------------+
| ``seed_mode``            | integer. 1 if the torrent is in seed mode, 0 otherwise.      |
+--------------------------+--------------------------------------------------------------+
| ``file_priority``        | list of integers. One entry per file in the torrent. Each    |
|                          | entry is the priority of the file with the same index.       |
+--------------------------+--------------------------------------------------------------+
| ``piece_priority``       | string of bytes. Each byte is interpreted as an integer and  |
|                          | is the priority of that piece.                               |
+--------------------------+--------------------------------------------------------------+
| ``auto_managed``         | integer. 1 if the torrent is auto managed, otherwise 0.      |
+--------------------------+--------------------------------------------------------------+
| ``sequential_download``  | integer. 1 if the torrent is in sequential download mode,    |
|                          | 0 otherwise.                                                 |
+--------------------------+--------------------------------------------------------------+
| ``paused``               | integer. 1 if the torrent is paused, 0 otherwise.            |
+--------------------------+--------------------------------------------------------------+
| ``trackers``             | list of lists of strings. The top level list lists all       |
|                          | tracker tiers. Each second level list is one tier of         |
|                          | trackers.                                                    |
+--------------------------+--------------------------------------------------------------+
| ``mapped_files``         | list of strings. If any file in the torrent has been         |
|                          | renamed, this entry contains a list of all the filenames.    |
|                          | In the same order as in the torrent file.                    |
+--------------------------+--------------------------------------------------------------+
| ``url-list``             | list of strings. List of url-seed URLs used by this torrent. |
|                          | The urls are expected to be properly encoded and not contain |
|                          | any illegal url characters.                                  |
+--------------------------+--------------------------------------------------------------+
| ``httpseeds``            | list of strings. List of httpseed URLs used by this torrent. |
|                          | The urls are expected to be properly encoded and not contain |
|                          | any illegal url characters.                                  |
+--------------------------+--------------------------------------------------------------+
| ``merkle tree``          | string. In case this torrent is a merkle torrent, this is a  |
|                          | string containing the entire merkle tree, all nodes,         |
|                          | including the root and all leaves. The tree is not           |
|                          | necessarily complete, but complete enough to be able to send |
|                          | any piece that we have, indicated by the have bitmask.       |
+--------------------------+--------------------------------------------------------------+
| ``peers``                | list of dictionaries. Each dictionary has the following      |
|                          | layout:                                                      |
|                          |                                                              |
|                          | +----------+-----------------------------------------------+ |
|                          | | ``ip``   | string, the ip address of the peer. This is   | |
|                          | |          | not a binary representation of the ip         | |
|                          | |          | address, but the string representation. It    | |
|                          | |          | may be an IPv6 string or an IPv4 string.      | |
|                          | +----------+-----------------------------------------------+ |
|                          | | ``port`` | integer, the listen port of the peer          | |
|                          | +----------+-----------------------------------------------+ |
|                          |                                                              |
|                          | These are the local peers we were connected to when this     |
|                          | fast-resume data was saved.                                  |
|                          |                                                              |
+--------------------------+--------------------------------------------------------------+
| ``unfinished``           | list of dictionaries. Each dictionary represents an          |
|                          | piece, and has the following layout:                         |
|                          |                                                              |
|                          | +-------------+--------------------------------------------+ |
|                          | | ``piece``   | integer, the index of the piece this entry | |
|                          | |             | refers to.                                 | |
|                          | +-------------+--------------------------------------------+ |
|                          | | ``bitmask`` | string, a binary bitmask representing the  | |
|                          | |             | blocks that have been downloaded in this   | |
|                          | |             | piece.                                     | |
|                          | +-------------+--------------------------------------------+ |
|                          | | ``adler32`` | The adler32 checksum of the data in the    | |
|                          | |             | blocks specified by ``bitmask``.           | |
|                          | |             |                                            | |
|                          | +-------------+--------------------------------------------+ |
|                          |                                                              |
+--------------------------+--------------------------------------------------------------+
| ``file sizes``           | list where each entry corresponds to a file in the file list |
|                          | in the metadata. Each entry has a list of two values, the    |
|                          | first value is the size of the file in bytes, the second     |
|                          | is the time stamp when the last time someone wrote to it.    |
|                          | This information is used to compare with the files on disk.  |
|                          | All the files must match exactly this information in order   |
|                          | to consider the resume data as current. Otherwise a full     |
|                          | re-check is issued.                                          |
+--------------------------+--------------------------------------------------------------+
| ``allocation``           | The allocation mode for the storage. Can be either ``full``  |
|                          | or ``compact``. If this is full, the file sizes and          |
|                          | timestamps are disregarded. Pieces are assumed not to have   |
|                          | moved around even if the files have been modified after the  |
|                          | last resume data checkpoint.                                 |
+--------------------------+--------------------------------------------------------------+

storage allocation
==================

There are two modes in which storage (files on disk) are allocated in libtorrent.

1. The traditional *full allocation* mode, where the entire files are filled up with
   zeros before anything is downloaded. libtorrent will look for sparse files support
   in the filesystem that is used for storage, and use sparse files or file system
   zero fill support if present. This means that on NTFS, full allocation mode will
   only allocate storage for the downloaded pieces.

2. The *sparse allocation*, sparse files are used, and pieces are downloaded directly
   to where they belong. This is the recommended (and default) mode.

In previous versions of libtorrent, a 3rd mode was supported, *compact allocation*.
Support for this is deprecated and will be removed in future versions of libtorrent.
It's still described in here for completeness.

The allocation mode is selected when a torrent is started. It is passed as an
argument to ``session::add_torrent()`` (see `async_add_torrent() add_torrent()`_).

The decision to use full allocation or compact allocation typically depends on whether
any files have priority 0 and if the filesystem supports sparse files.

sparse allocation
-----------------

On filesystems that supports sparse files, this allocation mode will only use
as much space as has been downloaded.

 * It does not require an allocation pass on startup.

 * It supports skipping files (setting prioirty to 0 to not download).

 * Fast resume data will remain valid even when file time stamps are out of date.


full allocation
---------------

When a torrent is started in full allocation mode, the disk-io thread
will make sure that the entire storage is allocated, and fill any gaps with zeros.
This will be skipped if the filesystem supports sparse files or automatic zero filling.
It will of course still check for existing pieces and fast resume data. The main
drawbacks of this mode are:

 * It may take longer to start the torrent, since it will need to fill the files
   with zeros on some systems. This delay is linearly dependent on the size of
   the download.

 * The download may occupy unnecessary disk space between download sessions. In case
   sparse files are not supported.

 * Disk caches usually perform extremely poorly with random access to large files
   and may slow down a download considerably.

The benefits of this mode are:

 * Downloaded pieces are written directly to their final place in the files and the
   total number of disk operations will be fewer and may also play nicer to
   filesystems' file allocation, and reduce fragmentation.

 * No risk of a download failing because of a full disk during download. Unless
   sparse files are being used.

 * The fast resume data will be more likely to be usable, regardless of crashes or
   out of date data, since pieces won't move around.

 * Can be used with prioritizing files to 0.

compact allocation
------------------

.. note::
	Note that support for compact allocation is deprecated in libttorrent, and will
	be removed in future versions.

The compact allocation will only allocate as much storage as it needs to keep the
pieces downloaded so far. This means that pieces will be moved around to be placed
at their final position in the files while downloading (to make sure the completed
download has all its pieces in the correct place). So, the main drawbacks are:

 * More disk operations while downloading since pieces are moved around.

 * Potentially more fragmentation in the filesystem.

 * Cannot be used while having files with priority 0.

The benefits though, are:

 * No startup delay, since the files don't need allocating.

 * The download will not use unnecessary disk space.

 * Disk caches perform much better than in full allocation and raises the download
   speed limit imposed by the disk.

 * Works well on filesystems that don't support sparse files.

The algorithm that is used when allocating pieces and slots isn't very complicated.
For the interested, a description follows.

storing a piece:

1. let **A** be a newly downloaded piece, with index **n**.
2. let **s** be the number of slots allocated in the file we're
   downloading to. (the number of pieces it has room for).
3. if **n** >= **s** then allocate a new slot and put the piece there.
4. if **n** < **s** then allocate a new slot, move the data at
   slot **n** to the new slot and put **A** in slot **n**.

allocating a new slot:

1. if there's an unassigned slot (a slot that doesn't
   contain any piece), return that slot index.
2. append the new slot at the end of the file (or find an unused slot).
3. let **i** be the index of newly allocated slot
4. if we have downloaded piece index **i** already (to slot **j**) then

   1. move the data at slot **j** to slot **i**.
   2. return slot index **j** as the newly allocated free slot.

5. return **i** as the newly allocated slot.
                              
 
extensions
==========

These extensions all operates within the `extension protocol`__. The
name of the extension is the name used in the extension-list packets,
and the payload is the data in the extended message (not counting the
length-prefix, message-id nor extension-id).

__ extension_protocol.html

Note that since this protocol relies on one of the reserved bits in the
handshake, it may be incompatible with future versions of the mainline
bittorrent client.

These are the extensions that are currently implemented.

metadata from peers
-------------------

Extension name: "LT_metadata"

This extension is deprecated in favor of the more widely supported ``ut_metadata``
extension, see `BEP 9`_.
The point with this extension is that you don't have to distribute the
metadata (.torrent-file) separately. The metadata can be distributed
through the bittorrent swarm. The only thing you need to download such
a torrent is the tracker url and the info-hash of the torrent.

It works by assuming that the initial seeder has the metadata and that
the metadata will propagate through the network as more peers join.

There are three kinds of messages in the metadata extension. These packets
are put as payload to the extension message. The three packets are:

	* request metadata
	* metadata
	* don't have metadata

request metadata:

+-----------+---------------+----------------------------------------+
| size      | name          | description                            |
+===========+===============+========================================+
| uint8_t   | msg_type      | Determines the kind of message this is |
|           |               | 0 means 'request metadata'             |
+-----------+---------------+----------------------------------------+
| uint8_t   | start         | The start of the metadata block that   |
|           |               | is requested. It is given in 256:ths   |
|           |               | of the total size of the metadata,     |
|           |               | since the requesting client don't know |
|           |               | the size of the metadata.              |
+-----------+---------------+----------------------------------------+
| uint8_t   | size          | The size of the metadata block that is |
|           |               | requested. This is also given in       |
|           |               | 256:ths of the total size of the       |
|           |               | metadata. The size is given as size-1. |
|           |               | That means that if this field is set   |
|           |               | 0, the request wants one 256:th of the |
|           |               | metadata.                              |
+-----------+---------------+----------------------------------------+

metadata:

+-----------+---------------+----------------------------------------+
| size      | name          | description                            |
+===========+===============+========================================+
| uint8_t   | msg_type      | 1 means 'metadata'                     |
+-----------+---------------+----------------------------------------+
| int32_t   | total_size    | The total size of the metadata, given  |
|           |               | in number of bytes.                    |
+-----------+---------------+----------------------------------------+
| int32_t   | offset        | The offset of where the metadata block |
|           |               | in this message belongs in the final   |
|           |               | metadata. This is given in bytes.      |
+-----------+---------------+----------------------------------------+
| uint8_t[] | metadata      | The actual metadata block. The size of |
|           |               | this part is given implicit by the     |
|           |               | length prefix in the bittorrent        |
|           |               | protocol packet.                       |
+-----------+---------------+----------------------------------------+

Don't have metadata:

+-----------+---------------+----------------------------------------+
| size      | name          | description                            |
+===========+===============+========================================+
| uint8_t   | msg_type      | 2 means 'I don't have metadata'.       |
|           |               | This message is sent as a reply to a   |
|           |               | metadata request if the the client     |
|           |               | doesn't have any metadata.             |
+-----------+---------------+----------------------------------------+

.. _`BEP 9`: http://bittorrent.org/beps/bep_0009.html

dont_have
---------

Extension name: "lt_dont_have"

The ``dont_have`` extension message is used to tell peers that the client no longer
has a specific piece. The extension message should be advertised in the ``m`` dictionary
as ``lt_dont_have``. The message format mimics the regular ``HAVE`` bittorrent message.

Just like all extension messages, the first 2 bytes in the mssage itself are 20 (the
bittorrent extension message) and the message ID assigned to this extension in the ``m``
dictionary in the handshake.

+-----------+---------------+----------------------------------------+
| size      | name          | description                            |
+===========+===============+========================================+
| uint32_t  | piece         | index of the piece the peer no longer  |
|           |               | has.                                   |
+-----------+---------------+----------------------------------------+

The length of this message (including the extension message prefix) is
6 bytes, i.e. one byte longer than the normal ``HAVE`` message, because
of the extension message wrapping.

HTTP seeding
------------

There are two kinds of HTTP seeding. One with that assumes a smart
(and polite) client and one that assumes a smart server. These
are specified in `BEP 19`_ and `BEP 17`_ respectively.

libtorrent supports both. In the libtorrent source code and API,
BEP 19 urls are typically referred to as *url seeds* and BEP 17
urls are typically referred to as *HTTP seeds*.

The libtorrent implementation of `BEP 19`_ assumes that, if the URL ends with a slash
('/'), the filename should be appended to it in order to request pieces from
that file. The way this works is that if the torrent is a single-file torrent,
only that filename is appended. If the torrent is a multi-file torrent, the
torrent's name '/' the file name is appended. This is the same directory
structure that libtorrent will download torrents into.

.. _`BEP 17`: http://bittorrent.org/beps/bep_0017.html
.. _`BEP 19`: http://bittorrent.org/beps/bep_0019.html

piece picker
============

The piece picker in libtorrent has the following features:

* rarest first
* sequential download
* random pick
* reverse order picking
* parole mode
* prioritize partial pieces
* prefer whole pieces
* piece affinity by speed category
* piece priorities

internal representation
-----------------------

It is optimized by, at all times, keeping a list of pieces ordered
by rarity, randomly shuffled within each rarity class. This list
is organized as a single vector of contigous memory in RAM, for
optimal memory locality and to eliminate heap allocations and frees
when updating rarity of pieces.

Expensive events, like a peer joining or leaving, are evaluated
lazily, since it's cheaper to rebuild the whole list rather than
updating every single piece in it. This means as long as no blocks
are picked, peers joining and leaving is no more costly than a single
peer joining or leaving. Of course the special cases of peers that have
all or no pieces are optimized to not require rebuilding the list.

picker strategy
---------------

The normal mode of the picker is of course *rarest first*, meaning
pieces that few peers have are preferred to be downloaded over pieces
that more peers have. This is a fundamental algorithm that is the
basis of the performance of bittorrent. However, the user may set the
piece picker into sequential download mode. This mode simply picks
pieces sequentially, always preferring lower piece indices.

When a torrent starts out, picking the rarest pieces means increased
risk that pieces won't be completed early (since there are only a few
peers they can be downloaded from), leading to a delay of having any
piece to offer to other peers. This lack of pieces to trade, delays
the client from getting started into the normal tit-for-tat mode of
bittorrent, and will result in a long ramp-up time. The heuristic to
mitigate this problem is to, for the first few pieces, pick random pieces
rather than rare pieces. The threshold for when to leave this initial
picker mode is determined by ``session_settings::initial_picker_threshold``.

reverse order
-------------

An orthogonal setting is *reverse order*, which is used for *snubbed*
peers. Snubbed peers are peers that appear very slow, and might have timed
out a piece request. The idea behind this is to make all snubbed peers
more likely to be able to do download blocks from the same piece,
concentrating slow peers on as few pieces as possible. The reverse order
means that the most common pieces are picked, instead of the rarest pieces
(or in the case of sequential download, the last pieces, intead of the first).

parole mode
-----------

Peers that have participated in a piece that failed the hash check, may be
put in *parole mode*. This means we prefer downloading a full piece  from this
peer, in order to distinguish which peer is sending corrupt data. Whether to
do this is or not is controlled by ``session_settings::use_parole_mode``.

In parole mode, the piece picker prefers picking one whole piece at a time for
a given peer, avoiding picking any blocks from a piece any other peer has
contributed to (since that would defeat the purpose of parole mode).

prioritize partial pieces
-------------------------

This setting determines if partially downloaded or requested pieces should always
be preferred over other pieces. The benefit of doing this is that the number of
partial pieces is minimized (and hence the turn-around time for downloading a block
until it can be uploaded to others is minimized). It also puts less stress on the
disk cache, since fewer partial pieces need to be kept in the cache. Whether or
not to enable this is controlled by ``session_settings::prioritize_partial_pieces``.

The main benefit of not prioritizing partial pieces is that the rarest first
algorithm gets to have more influence on which pieces are picked. The picker is
more likely to truly pick the rarest piece, and hence improving the performance
of the swarm.

This setting is turned on automatically whenever the number of partial pieces
in the piece picker exceeds the number of peers we're connected to times 1.5.
This is in order to keep the waste of partial pieces to a minimum, but still
prefer rarest pieces.

prefer whole pieces
-------------------

The *prefer whole pieces* setting makes the piece picker prefer picking entire
pieces at a time. This is used by web connections (both http seeding
standards), in order to be able to coalesce the small bittorrent requests
to larger HTTP requests. This significantly improves performance when
downloading over HTTP.

It is also used by peers that are downloading faster than a certain
threshold. The main advantage is that these peers will better utilize the
other peer's disk cache, by requesting all blocks in a single piece, from
the same peer.

This threshold is controlled by ``session_settings::whole_pieces_threshold``.

*TODO: piece affinity by speed category*
*TODO: piece priorities*

SSL torrents
============

Torrents may have an SSL root (CA) certificate embedded in them. Such torrents
are called *SSL torrents*. An SSL torrent talks to all bittorrent peers over SSL.
The protocols are layered like this::

	+-----------------------+
	| BitTorrent protocol   |
	+-----------------------+
	| SSL                   |
	+-----------+-----------+
	| TCP       | uTP       |
	|           +-----------+
	|           | UDP       |
	+-----------+-----------+

During the SSL handshake, both peers need to authenticate by providing a certificate
that is signed by the CA certificate found in the .torrent file. These peer
certificates are expected to be privided to peers through some other means than 
bittorrent. Typically by a peer generating a certificate request which is sent to
the publisher of the torrent, and the publisher returning a signed certificate.

In libtorrent, `set_ssl_certificate()`_ in torrent_handle_ is used to tell libtorrent where
to find the peer certificate and the private key for it. When an SSL torrent is loaded,
the torrent_need_cert_alert_ is posted to remind the user to provide a certificate.

A peer connecting to an SSL torrent MUST provide the *SNI* TLS extension (server name
indication). The server name is the hex encoded info-hash of the torrent to connect to.
This is required for the client accepting the connection to know which certificate to
present.

SSL connections are accepted on a separate socket from normal bittorrent connections. To
pick which port the SSL socket should bind to, set ``session_settings::ssl_listen`` to a
different port. It defaults to port 4433. This setting is only taken into account when the
normal listen socket is opened (i.e. just changing this setting won't necessarily close
and re-open the SSL socket). To not listen on an SSL socket at all, set ``ssl_listen`` to 0.

This feature is only available if libtorrent is build with openssl support (``TORRENT_USE_OPENSSL``)
and requires at least openSSL version 1.0, since it needs SNI support.

Peer certificates must have at least one *SubjectAltName* field of type dNSName. At least
one of the fields must *exactly* match the name of the torrent. This is a byte-by-byte comparison,
the UTF-8 encoding must be identical (i.e. there's no unicode normalization going on). This is
the recommended way of verifying certificates for HTTPS servers according to `RFC 2818`_. Note
the difference that for torrents only *dNSName* fields are taken into account (not IP address fields).
The most specific (i.e. last) *Common Name* field is also taken into account if no *SubjectAltName*
did not match.

If any of these fields contain a single asterisk ("*"), the certificate is considered covering
any torrent, allowing it to be reused for any torrent.

The purpose of matching the torrent name with the fields in the peer certificate is to allow
a publisher to have a single root certificate for all torrents it distributes, and issue
separate peer certificates for each torrent. A peer receiving a certificate will not necessarily
be able to access all torrents published by this root certificate (only if it has a "star cert").

.. _`RFC 2818`: http://www.ietf.org/rfc/rfc2818.txt

testing
-------

To test incoming SSL connections to an SSL torrent, one can use the following *openssl* command::

	openssl s_client -cert <peer-certificate>.pem -key <peer-private-key>.pem -CAfile <torrent-cert>.pem -debug -connect 127.0.0.1:4433 -tls1 -servername <info-hash>

To create a root certificate, the Distinguished Name (*DN*) is not taken into account
by bittorrent peers. You still need to specify something, but from libtorrent's point of
view, it doesn't matter what it is. libtorrent only makes sure the peer certificates are
signed by the correct root certificate.

One way to create the certificates is to use the ``CA.sh`` script that comes with openssl, like thisi (don't forget to enter a common Name for the certificate)::

	CA.sh -newca
	CA.sh -newreq
	CA.sh -sign

The torrent certificate is located in ``./demoCA/private/demoCA/cacert.pem``, this is
the pem file to include in the .torrent file.

The peer's certificate is located in ``./newcert.pem`` and the certificate's
private key in ``./newkey.pem``.

