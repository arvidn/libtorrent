.. _user_agent:

+------------+--------+---------------------------------+
| name       | type   | default                         |
+============+========+=================================+
| user_agent | string | "libtorrent/"LIBTORRENT_VERSION |
+------------+--------+---------------------------------+

this is the client identification to the tracker.
The recommended format of this string is:
"ClientName/ClientVersion libtorrent/libtorrentVersion".
This name will not only be used when making HTTP requests, but also when
sending extended headers to peers that support that extension.
It may not contain \r or \n

.. _announce_ip:

+-------------+--------+---------+
| name        | type   | default |
+=============+========+=========+
| announce_ip | string | 0       |
+-------------+--------+---------+

``announce_ip`` is the ip address passed along to trackers as the ``&ip=`` parameter.
If left as the default, that parameter is omitted.

.. _mmap_cache:

+------------+--------+---------+
| name       | type   | default |
+============+========+=========+
| mmap_cache | string | 0       |
+------------+--------+---------+

``mmap_cache`` may be set to a filename where the disk cache will be mmapped
to. This could be useful, for instance, to map the disk cache from regular
rotating hard drives onto an SSD drive. Doing that effectively introduces
a second layer of caching, allowing the disk cache to be as big as can
fit on an SSD drive (probably about one order of magnitude more than the
available RAM). The intention of this setting is to set it up once at the
start up and not change it while running. The setting may not be changed
as long as there are any disk buffers in use. This default to the empty
string, which means use regular RAM allocations for the disk cache. The file
specified will be created and truncated to the disk cache size (``cache_size``).
Any existing file with the same name will be replaced.

Since this setting sets a hard upper limit on cache usage, it cannot be combined
with ``session_settings::contiguous_recv_buffer``, since that feature treats the
``cache_size`` setting as a soft (but still pretty hard) limit. The result of combining
the two is peers being disconnected after failing to allocate more disk buffers.

This feature requires the ``mmap`` system call, on systems that don't have ``mmap``
this setting is ignored.

.. _allow_multiple_connections_per_ip:

+-----------------------------------+------+---------+
| name                              | type | default |
+===================================+======+=========+
| allow_multiple_connections_per_ip | bool | false   |
+-----------------------------------+------+---------+

determines if connections from the same IP address as
existing connections should be rejected or not. Multiple
connections from the same IP address is not allowed by
default, to prevent abusive behavior by peers. It may
be useful to allow such connections in cases where
simulations are run on the same machie, and all peers
in a swarm has the same IP address.

.. _tracker_completion_timeout:

+----------------------------+------+---------+
| name                       | type | default |
+============================+======+=========+
| tracker_completion_timeout | int  | 60      |
+----------------------------+------+---------+

if set to true, upload, download and unchoke limits
are ignored for peers on the local network.
This option is *DEPRECATED*, please use `set_peer_class_filter()`_ instead.
``tracker_completion_timeout`` is the number of seconds the tracker
connection will wait from when it sent the request until it considers the
tracker to have timed-out. Default value is 60 seconds.

.. _tracker_receive_timeout:

+-------------------------+------+---------+
| name                    | type | default |
+=========================+======+=========+
| tracker_receive_timeout | int  | 40      |
+-------------------------+------+---------+

``tracker_receive_timeout`` is the number of seconds to wait to receive
any data from the tracker. If no data is received for this number of
seconds, the tracker will be considered as having timed out. If a tracker
is down, this is the kind of timeout that will occur.

.. _stop_tracker_timeout:

+----------------------+------+---------+
| name                 | type | default |
+======================+======+=========+
| stop_tracker_timeout | int  | 5       |
+----------------------+------+---------+

the time to wait when sending a stopped message
before considering a tracker to have timed out.
this is usually shorter, to make the client quit
faster

.. _tracker_maximum_response_length:

+---------------------------------+------+-----------+
| name                            | type | default   |
+=================================+======+===========+
| tracker_maximum_response_length | int  | 1024*1024 |
+---------------------------------+------+-----------+

this is the maximum number of bytes in a tracker
response. If a response size passes this number
of bytes it will be rejected and the connection
will be closed. On gzipped responses this size is
measured on the uncompressed data. So, if you get
20 bytes of gzip response that'll expand to 2 megabytes,
it will be interrupted before the entire response
has been uncompressed (assuming the limit is lower
than 2 megs).

.. _piece_timeout:

+---------------+------+---------+
| name          | type | default |
+===============+======+=========+
| piece_timeout | int  | 20      |
+---------------+------+---------+

the number of seconds from a request is sent until
it times out if no piece response is returned.

.. _request_timeout:

+-----------------+------+---------+
| name            | type | default |
+=================+======+=========+
| request_timeout | int  | 50      |
+-----------------+------+---------+

the number of seconds one block (16kB) is expected
to be received within. If it's not, the block is
requested from a different peer

.. _request_queue_time:

+--------------------+------+---------+
| name               | type | default |
+====================+======+=========+
| request_queue_time | int  | 3       |
+--------------------+------+---------+

the length of the request queue given in the number
of seconds it should take for the other end to send
all the pieces. i.e. the actual number of requests
depends on the download rate and this number.

.. _max_allowed_in_request_queue:

+------------------------------+------+---------+
| name                         | type | default |
+==============================+======+=========+
| max_allowed_in_request_queue | int  | 250     |
+------------------------------+------+---------+

the number of outstanding block requests a peer is
allowed to queue up in the client. If a peer sends
more requests than this (before the first one has
been sent) the last request will be dropped.
the higher this is, the faster upload speeds the
client can get to a single peer.

.. _max_out_request_queue:

+-----------------------+------+---------+
| name                  | type | default |
+=======================+======+=========+
| max_out_request_queue | int  | 200     |
+-----------------------+------+---------+

``max_out_request_queue`` is the maximum number of outstanding requests to
send to a peer. This limit takes precedence over ``request_queue_time``. i.e.
no matter the download speed, the number of outstanding requests will never
exceed this limit.

.. _whole_pieces_threshold:

+------------------------+------+---------+
| name                   | type | default |
+========================+======+=========+
| whole_pieces_threshold | int  | 20      |
+------------------------+------+---------+

if a whole piece can be downloaded in this number
of seconds, or less, the peer_connection will prefer
to request whole pieces at a time from this peer.
The benefit of this is to better utilize disk caches by
doing localized accesses and also to make it easier
to identify bad peers if a piece fails the hash check.

.. _peer_timeout:

+--------------+------+---------+
| name         | type | default |
+==============+======+=========+
| peer_timeout | int  | 120     |
+--------------+------+---------+

``peer_timeout`` is the number of seconds the peer connection should
wait (for any activity on the peer connection) before closing it due
to time out. This defaults to 120 seconds, since that's what's specified
in the protocol specification. After half the time out, a keep alive message
is sent.

.. _urlseed_timeout:

+-----------------+------+---------+
| name            | type | default |
+=================+======+=========+
| urlseed_timeout | int  | 20      |
+-----------------+------+---------+

same as peer_timeout, but only applies to url-seeds.
this is usually set lower, because web servers are
expected to be more reliable.

.. _urlseed_pipeline_size:

+-----------------------+------+---------+
| name                  | type | default |
+=======================+======+=========+
| urlseed_pipeline_size | int  | 5       |
+-----------------------+------+---------+

controls the pipelining size of url-seeds. i.e. the number
of HTTP request to keep outstanding before waiting for
the first one to complete. It's common for web servers
to limit this to a relatively low number, like 5

.. _urlseed_wait_retry:

+--------------------+------+---------+
| name               | type | default |
+====================+======+=========+
| urlseed_wait_retry | int  | 30      |
+--------------------+------+---------+

time to wait until a new retry of a web seed takes place

.. _file_pool_size:

+----------------+------+---------+
| name           | type | default |
+================+======+=========+
| file_pool_size | int  | 40      |
+----------------+------+---------+

sets the upper limit on the total number of files this
session will keep open. The reason why files are
left open at all is that some anti virus software
hooks on every file close, and scans the file for
viruses. deferring the closing of the files will
be the difference between a usable system and
a completely hogged down system. Most operating
systems also has a limit on the total number of
file descriptors a process may have open. It is
usually a good idea to find this limit and set the
number of connections and the number of files
limits so their sum is slightly below it.

.. _max_failcount:

+---------------+------+---------+
| name          | type | default |
+===============+======+=========+
| max_failcount | int  | 3       |
+---------------+------+---------+

``max_failcount`` is the maximum times we try to connect to a peer before
stop connecting again. If a peer succeeds, the failcounter is reset. If
a peer is retrieved from a peer source (other than DHT) the failcount is
decremented by one, allowing another try.

.. _min_reconnect_time:

+--------------------+------+---------+
| name               | type | default |
+====================+======+=========+
| min_reconnect_time | int  | 60      |
+--------------------+------+---------+

the number of seconds to wait to reconnect to a peer.
this time is multiplied with the failcount.

.. _peer_connect_timeout:

+----------------------+------+---------+
| name                 | type | default |
+======================+======+=========+
| peer_connect_timeout | int  | 15      |
+----------------------+------+---------+

``peer_connect_timeout`` the number of seconds to wait after a connection
attempt is initiated to a peer until it is considered as having timed out.
This setting is especially important in case the number of half-open
connections are limited, since stale half-open
connection may delay the connection of other peers considerably.

.. _connection_speed:

+------------------+------+---------+
| name             | type | default |
+==================+======+=========+
| connection_speed | int  | 6       |
+------------------+------+---------+

``connection_speed`` is the number of connection attempts that
are made per second. If a number < 0 is specified, it will default to
200 connections per second. If 0 is specified, it means don't make
outgoing connections at all.

.. _inactivity_timeout:

+--------------------+------+---------+
| name               | type | default |
+====================+======+=========+
| inactivity_timeout | int  | 600     |
+--------------------+------+---------+

if a peer is uninteresting and uninterested for longer
than this number of seconds, it will be disconnected.
default is 10 minutes

.. _unchoke_interval:

+------------------+------+---------+
| name             | type | default |
+==================+======+=========+
| unchoke_interval | int  | 15      |
+------------------+------+---------+

``unchoke_interval`` is the number of seconds between chokes/unchokes.
On this interval, peers are re-evaluated for being choked/unchoked. This
is defined as 30 seconds in the protocol, and it should be significantly
longer than what it takes for TCP to ramp up to it's max rate.

.. _optimistic_unchoke_interval:

+-----------------------------+------+---------+
| name                        | type | default |
+=============================+======+=========+
| optimistic_unchoke_interval | int  | 30      |
+-----------------------------+------+---------+

``optimistic_unchoke_interval`` is the number of seconds between
each *optimistic* unchoke. On this timer, the currently optimistically
unchoked peer will change.

.. _num_want:

+----------+------+---------+
| name     | type | default |
+==========+======+=========+
| num_want | int  | 200     |
+----------+------+---------+

``num_want`` is the number of peers we want from each tracker request. It defines
what is sent as the ``&num_want=`` parameter to the tracker.

.. _initial_picker_threshold:

+--------------------------+------+---------+
| name                     | type | default |
+==========================+======+=========+
| initial_picker_threshold | int  | 4       |
+--------------------------+------+---------+

``initial_picker_threshold`` specifies the number of pieces we need before we
switch to rarest first picking. This defaults to 4, which means the 4 first
pieces in any torrent are picked at random, the following pieces are picked
in rarest first order.

.. _allowed_fast_set_size:

+-----------------------+------+---------+
| name                  | type | default |
+=======================+======+=========+
| allowed_fast_set_size | int  | 10      |
+-----------------------+------+---------+

the number of allowed pieces to send to peers
that supports the fast extensions

.. _suggest_mode:

+--------------+------+-------------------------------------+
| name         | type | default                             |
+==============+======+=====================================+
| suggest_mode | int  | settings_pack::no_piece_suggestions |
+--------------+------+-------------------------------------+

``suggest_mode`` controls whether or not libtorrent will send out suggest
messages to create a bias of its peers to request certain pieces. The modes
are:

* ``no_piece_suggestsions`` which is the default and will not send out suggest
  messages.
* ``suggest_read_cache`` which will send out suggest messages for the most
  recent pieces that are in the read cache.

.. _max_queued_disk_bytes:

+-----------------------+------+-------------+
| name                  | type | default     |
+=======================+======+=============+
| max_queued_disk_bytes | int  | 1024 * 1024 |
+-----------------------+------+-------------+

``max_queued_disk_bytes`` is the number maximum number of bytes, to be
written to disk, that can wait in the disk I/O thread queue. This queue
is only for waiting for the disk I/O thread to receive the job and either
write it to disk or insert it in the write cache. When this limit is reached,
the peer connections will stop reading data from their sockets, until the disk
thread catches up. Setting this too low will severly limit your download rate.

.. _handshake_timeout:

+-------------------+------+---------+
| name              | type | default |
+===================+======+=========+
| handshake_timeout | int  | 10      |
+-------------------+------+---------+

the number of seconds to wait for a handshake
response from a peer. If no response is received
within this time, the peer is disconnected.

.. _send_buffer_low_watermark:

.. _send_buffer_watermark:

.. _send_buffer_watermark_factor:

+------------------------------+------+------------+
| name                         | type | default    |
+==============================+======+============+
| send_buffer_low_watermark    | int  | 512        |
+------------------------------+------+------------+
| send_buffer_watermark        | int  | 500 * 1024 |
+------------------------------+------+------------+
| send_buffer_watermark_factor | int  | 50         |
+------------------------------+------+------------+

``send_buffer_low_watermark`` the minimum send buffer target
size (send buffer includes bytes pending being read from disk).
For good and snappy seeding performance, set this fairly high, to
at least fit a few blocks. This is essentially the initial
window size which will determine how fast we can ramp up
the send rate

if the send buffer has fewer bytes than ``send_buffer_watermark``,
we'll read another 16kB block onto it. If set too small,
upload rate capacity will suffer. If set too high,
memory will be wasted.
The actual watermark may be lower than this in case
the upload rate is low, this is the upper limit.

the current upload rate to a peer is multiplied by
this factor to get the send buffer watermark. The
factor is specified as a percentage. i.e. 50 -> 0.5
This product is clamped to the ``send_buffer_watermark``
setting to not exceed the max. For high speed
upload, this should be set to a greater value than
100. For high capacity connections, setting this
higher can improve upload performance and disk throughput. Setting it too
high may waste RAM and create a bias towards read jobs over write jobs.

.. _choking_algorithm:

.. _seed_choking_algorithm:

+------------------------+------+-----------------------------------+
| name                   | type | default                           |
+========================+======+===================================+
| choking_algorithm      | int  | settings_pack::fixed_slots_choker |
+------------------------+------+-----------------------------------+
| seed_choking_algorithm | int  | settings_pack::round_robin        |
+------------------------+------+-----------------------------------+

``choking_algorithm`` specifies which algorithm to use to determine which peers
to unchoke.

The options for choking algorithms are:

* ``fixed_slots_choker`` is the traditional choker with a fixed number of unchoke
  slots (as specified by ``session::set_max_uploads()``).

* ``auto_expand_choker`` opens at least the number of slots as specified by
  ``session::set_max_uploads()`` but opens up more slots if the upload capacity
  is not saturated. This unchoker will work just like the ``fixed_slots_choker``
  if there's no global upload rate limit set.

* ``rate_based_choker`` opens up unchoke slots based on the upload rate
  achieved to peers. The more slots that are opened, the marginal upload
  rate required to open up another slot increases.

* ``bittyrant_choker`` attempts to optimize download rate by finding the
  reciprocation rate of each peer individually and prefers peers that gives
  the highest *return on investment*. It still allocates all upload capacity,
  but shuffles it around to the best peers first. For this choker to be
  efficient, you need to set a global upload rate limit
  (``session::set_upload_rate_limit()``). For more information about this
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

.. _cache_size:

.. _cache_buffer_chunk_size:

.. _cache_expiry:

+-------------------------+------+---------+
| name                    | type | default |
+=========================+======+=========+
| cache_size              | int  | 1024    |
+-------------------------+------+---------+
| cache_buffer_chunk_size | int  | 0       |
+-------------------------+------+---------+
| cache_expiry            | int  | 300     |
+-------------------------+------+---------+

``cache_size`` is the disk write and read  cache. It is specified in units of
16 KiB blocks. Buffers that are part of a peer's send or receive buffer also
count against this limit. Send and receive buffers will never be denied to be
allocated, but they will cause the actual cached blocks to be flushed or evicted.
If this is set to -1, the cache size is automatically set to the amount
of physical RAM available in the machine divided by 8. If the amount of physical
RAM cannot be determined, it's set to 1024 (= 16 MiB).

Disk buffers are allocated using a pool allocator, the number of blocks that
are allocated at a time when the pool needs to grow can be specified in
``cache_buffer_chunk_size``. Lower numbers saves memory at the expense of more
heap allocations. If it is set to 0, the effective chunk size is proportional
to the total cache size, attempting to strike a good balance between performance
and memory usage. It defaults to 0.
``cache_expiry`` is the number of seconds from the last cached write to a piece
in the write cache, to when it's forcefully flushed to disk. Default is 60 second.

.. _explicit_cache_interval:

+-------------------------+------+---------+
| name                    | type | default |
+=========================+======+=========+
| explicit_cache_interval | int  | 30      |
+-------------------------+------+---------+

``explicit_cache_interval`` is the number of seconds in between each refresh of
a part of the explicit read cache. Torrents take turns in refreshing and this
is the time in between each torrent refresh. Refreshing a torrent's explicit
read cache means scanning all pieces and picking a random set of the rarest ones.
There is an affinity to pick pieces that are already in the cache, so that
subsequent refreshes only swaps in pieces that are rarer than whatever is in
the cache at the time.

.. _disk_io_write_mode:

.. _disk_io_read_mode:

+--------------------+------+--------------------------------+
| name               | type | default                        |
+====================+======+================================+
| disk_io_write_mode | int  | settings_pack::enable_os_cache |
+--------------------+------+--------------------------------+
| disk_io_read_mode  | int  | settings_pack::enable_os_cache |
+--------------------+------+--------------------------------+

determines how files are opened when they're in read only mode versus
read and write mode. The options are:

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

.. _outgoing_port:

.. _num_outgoing_ports:

+--------------------+------+---------+
| name               | type | default |
+====================+======+=========+
| outgoing_port      | int  | 0       |
+--------------------+------+---------+
| num_outgoing_ports | int  | 0       |
+--------------------+------+---------+

this is the first port to use for binding
outgoing connections to. This is useful
for users that have routers that
allow QoS settings based on local port.
when binding outgoing connections to specific
ports, ``num_outgoing_ports`` is the size of
the range. It should be more than a few

.. warning:: setting outgoing ports will limit the ability to keep multiple
	connections to the same client, even for different torrents. It is not
	recommended to change this setting. Its main purpose is to use as an
	escape hatch for cheap routers with QoS capability but can only classify
	flows based on port numbers.

It is a range instead of a single port because of the problems with failing to reconnect
to peers if a previous socket to that peer and port is in ``TIME_WAIT`` state.

.. _peer_tos:

+----------+------+---------+
| name     | type | default |
+==========+======+=========+
| peer_tos | int  | 0       |
+----------+------+---------+

``peer_tos`` determines the TOS byte set in the IP header of every packet
sent to peers (including web seeds). The default value for this is ``0x0``
(no marking). One potentially useful TOS mark is ``0x20``, this represents
the *QBone scavenger service*. For more details, see QBSS_.

.. _`QBSS`: http://qbone.internet2.edu/qbss/

.. _active_downloads:

.. _active_seeds:

.. _active_dht_limit:

.. _active_tracker_limit:

.. _active_lsd_limit:

.. _active_limit:

+----------------------+------+---------+
| name                 | type | default |
+======================+======+=========+
| active_downloads     | int  | 3       |
+----------------------+------+---------+
| active_seeds         | int  | 5       |
+----------------------+------+---------+
| active_dht_limit     | int  | 88      |
+----------------------+------+---------+
| active_tracker_limit | int  | 360     |
+----------------------+------+---------+
| active_lsd_limit     | int  | 60      |
+----------------------+------+---------+
| active_limit         | int  | 15      |
+----------------------+------+---------+

for auto managed torrents, these are the limits
they are subject to. If there are too many torrents
some of the auto managed ones will be paused until
some slots free up.
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

.. _auto_manage_interval:

+----------------------+------+---------+
| name                 | type | default |
+======================+======+=========+
| auto_manage_interval | int  | 30      |
+----------------------+------+---------+

``auto_manage_interval`` is the number of seconds between the torrent queue
is updated, and rotated.

.. _seed_time_limit:

+-----------------+------+--------------+
| name            | type | default      |
+=================+======+==============+
| seed_time_limit | int  | 24 * 60 * 60 |
+-----------------+------+--------------+

this is the limit on the time a torrent has been an active seed
(specified in seconds) before it is considered having met the seed limit criteria.
See queuing_.

.. _auto_scrape_interval:

.. _auto_scrape_min_interval:

+--------------------------+------+---------+
| name                     | type | default |
+==========================+======+=========+
| auto_scrape_interval     | int  | 1800    |
+--------------------------+------+---------+
| auto_scrape_min_interval | int  | 300     |
+--------------------------+------+---------+

``auto_scrape_interval`` is the number of seconds between scrapes of
queued torrents (auto managed and paused torrents). Auto managed
torrents that are paused, are scraped regularly in order to keep
track of their downloader/seed ratio. This ratio is used to determine
which torrents to seed and which to pause.

``auto_scrape_min_interval`` is the minimum number of seconds between any
automatic scrape (regardless of torrent). In case there are a large number
of paused auto managed torrents, this puts a limit on how often a scrape
request is sent.

.. _max_peerlist_size:

.. _max_paused_peerlist_size:

+--------------------------+------+---------+
| name                     | type | default |
+==========================+======+=========+
| max_peerlist_size        | int  | 3000    |
+--------------------------+------+---------+
| max_paused_peerlist_size | int  | 1000    |
+--------------------------+------+---------+

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

.. _min_announce_interval:

+-----------------------+------+---------+
| name                  | type | default |
+=======================+======+=========+
| min_announce_interval | int  | 5 * 60  |
+-----------------------+------+---------+

this is the minimum allowed announce interval for a tracker. This
is specified in seconds and is used as a sanity check on what is
returned from a tracker. It mitigates hammering misconfigured trackers.

.. _auto_manage_startup:

+---------------------+------+---------+
| name                | type | default |
+=====================+======+=========+
| auto_manage_startup | int  | 120     |
+---------------------+------+---------+

this is the number of seconds a torrent is considered
active after it was started, regardless of upload and download speed. This
is so that newly started torrents are not considered inactive until they
have a fair chance to start downloading.

.. _seeding_piece_quota:

+---------------------+------+---------+
| name                | type | default |
+=====================+======+=========+
| seeding_piece_quota | int  | 20      |
+---------------------+------+---------+

``seeding_piece_quota`` is the number of pieces to send to a peer,
when seeding, before rotating in another peer to the unchoke set.
It defaults to 3 pieces, which means that when seeding, any peer we've
sent more than this number of pieces to will be unchoked in favour of
a choked peer.

.. _max_sparse_regions:

+--------------------+------+---------+
| name               | type | default |
+====================+======+=========+
| max_sparse_regions | int  | 0       |
+--------------------+------+---------+

``max_sparse_regions`` is a limit of the number of *sparse regions* in
a torrent. A sparse region is defined as a hole of pieces we have not
yet downloaded, in between pieces that have been downloaded. This is
used as a hack for windows vista which has a bug where you cannot
write files with more than a certain number of sparse regions. This
limit is not hard, it will be exceeded. Once it's exceeded, pieces
that will maintain or decrease the number of sparse regions are
prioritized. To disable this functionality, set this to 0. It defaults
to 0 on all platforms except windows.

.. _max_rejects:

+-------------+------+---------+
| name        | type | default |
+=============+======+=========+
| max_rejects | int  | 50      |
+-------------+------+---------+

``max_rejects`` is the number of piece requests we will reject in a row
while a peer is choked before the peer is considered abusive and is
disconnected.

.. _recv_socket_buffer_size:

.. _send_socket_buffer_size:

+-------------------------+------+---------+
| name                    | type | default |
+=========================+======+=========+
| recv_socket_buffer_size | int  | 0       |
+-------------------------+------+---------+
| send_socket_buffer_size | int  | 0       |
+-------------------------+------+---------+

``recv_socket_buffer_size`` and ``send_socket_buffer_size`` specifies
the buffer sizes set on peer sockets. 0 (which is the default) means
the OS default (i.e. don't change the buffer sizes). The socket buffer
sizes are changed using setsockopt() with SOL_SOCKET/SO_RCVBUF and
SO_SNDBUFFER.

.. _file_checks_delay_per_block:

+-----------------------------+------+---------+
| name                        | type | default |
+=============================+======+=========+
| file_checks_delay_per_block | int  | 0       |
+-----------------------------+------+---------+

``file_checks_delay_per_block`` is the number of milliseconds to sleep
in between disk read operations when checking torrents. This defaults
to 0, but can be set to higher numbers to slow down the rate at which
data is read from the disk while checking. This may be useful for
background tasks that doesn't matter if they take a bit longer, as long
as they leave disk I/O time for other processes.

.. _read_cache_line_size:

.. _write_cache_line_size:

+-----------------------+------+---------+
| name                  | type | default |
+=======================+======+=========+
| read_cache_line_size  | int  | 32      |
+-----------------------+------+---------+
| write_cache_line_size | int  | 16      |
+-----------------------+------+---------+

``read_cache_line_size`` is the number of blocks to read into the read
cache when a read cache miss occurs. Setting this to 0 is essentially
the same thing as disabling read cache. The number of blocks read
into the read cache is always capped by the piece boundry.

When a piece in the write cache has ``write_cache_line_size`` contiguous
blocks in it, they will be flushed. Setting this to 1 effectively
disables the write cache.

.. _optimistic_disk_retry:

+-----------------------+------+---------+
| name                  | type | default |
+=======================+======+=========+
| optimistic_disk_retry | int  | 10 * 60 |
+-----------------------+------+---------+

``optimistic_disk_retry`` is the number of seconds from a disk write
errors occur on a torrent until libtorrent will take it out of the
upload mode, to test if the error condition has been fixed.

libtorrent will only do this automatically for auto managed torrents.

You can explicitly take a torrent out of upload only mode using
`set_upload_mode()`_.

.. _max_suggest_pieces:

+--------------------+------+---------+
| name               | type | default |
+====================+======+=========+
| max_suggest_pieces | int  | 10      |
+--------------------+------+---------+

``max_suggest_pieces`` is the max number of suggested piece indices received
from a peer that's remembered. If a peer floods suggest messages, this limit
prevents libtorrent from using too much RAM. It defaults to 10.

.. _local_service_announce_interval:

+---------------------------------+------+---------+
| name                            | type | default |
+=================================+======+=========+
| local_service_announce_interval | int  | 5 * 60  |
+---------------------------------+------+---------+

``local_service_announce_interval`` is the time between local
network announces for a torrent. By default, when local service
discovery is enabled a torrent announces itself every 5 minutes.
This interval is specified in seconds.

.. _dht_announce_interval:

+-----------------------+------+---------+
| name                  | type | default |
+=======================+======+=========+
| dht_announce_interval | int  | 15 * 60 |
+-----------------------+------+---------+

``dht_announce_interval`` is the number of seconds between announcing
torrents to the distributed hash table (DHT).

.. _udp_tracker_token_expiry:

+--------------------------+------+---------+
| name                     | type | default |
+==========================+======+=========+
| udp_tracker_token_expiry | int  | 60      |
+--------------------------+------+---------+

``udp_tracker_token_expiry`` is the number of seconds libtorrent
will keep UDP tracker connection tokens around for. This is specified
to be 60 seconds, and defaults to that. The higher this value is, the
fewer packets have to be sent to the UDP tracker. In order for higher
values to work, the tracker needs to be configured to match the
expiration time for tokens.

.. _default_cache_min_age:

+-----------------------+------+---------+
| name                  | type | default |
+=======================+======+=========+
| default_cache_min_age | int  | 1       |
+-----------------------+------+---------+

``default_cache_min_age`` is the minimum number of seconds any read
cache line is kept in the cache. This defaults to one second but
may be greater if ``guided_read_cache`` is enabled. Having a lower
bound on the time a cache line stays in the cache is an attempt
to avoid swapping the same pieces in and out of the cache in case
there is a shortage of spare cache space.

.. _num_optimistic_unchoke_slots:

+------------------------------+------+---------+
| name                         | type | default |
+==============================+======+=========+
| num_optimistic_unchoke_slots | int  | 0       |
+------------------------------+------+---------+

``num_optimistic_unchoke_slots`` is the number of optimistic unchoke
slots to use. It defaults to 0, which means automatic. Having a higher
number of optimistic unchoke slots mean you will find the good peers
faster but with the trade-off to use up more bandwidth. When this is
set to 0, libtorrent opens up 20% of your allowed upload slots as
optimistic unchoke slots.

.. _default_est_reciprocation_rate:

.. _increase_est_reciprocation_rate:

.. _decrease_est_reciprocation_rate:

+---------------------------------+------+---------+
| name                            | type | default |
+=================================+======+=========+
| default_est_reciprocation_rate  | int  | 16000   |
+---------------------------------+------+---------+
| increase_est_reciprocation_rate | int  | 20      |
+---------------------------------+------+---------+
| decrease_est_reciprocation_rate | int  | 3       |
+---------------------------------+------+---------+

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

.. _max_pex_peers:

+---------------+------+---------+
| name          | type | default |
+===============+======+=========+
| max_pex_peers | int  | 50      |
+---------------+------+---------+

the max number of peers we accept from pex messages from a single peer.
this limits the number of concurrent peers any of our peers claims to
be connected to. If they clain to be connected to more than this, we'll
ignore any peer that exceeds this limit

.. _tick_interval:

+---------------+------+---------+
| name          | type | default |
+===============+======+=========+
| tick_interval | int  | 100     |
+---------------+------+---------+

``tick_interval`` specifies the number of milliseconds between internal
ticks. This is the frequency with which bandwidth quota is distributed to
peers. It should not be more than one second (i.e. 1000 ms). Setting this
to a low value (around 100) means higher resolution bandwidth quota distribution,
setting it to a higher value saves CPU cycles.

.. _share_mode_target:

+-------------------+------+---------+
| name              | type | default |
+===================+======+=========+
| share_mode_target | int  | 3       |
+-------------------+------+---------+

``share_mode_target`` specifies the target share ratio for share mode torrents.
This defaults to 3, meaning we'll try to upload 3 times as much as we download.
Setting this very high, will make it very conservative and you might end up
not downloading anything ever (and not affecting your share ratio). It does
not make any sense to set this any lower than 2. For instance, if only 3 peers
need to download the rarest piece, it's impossible to download a single piece
and upload it more than 3 times. If the share_mode_target is set to more than 3,
nothing is downloaded.

