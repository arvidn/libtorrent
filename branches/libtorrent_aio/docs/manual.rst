============================
libtorrent API Documentation
============================

:Author: Arvid Norberg, arvid@rasterbar.com
:Version: 1.1.0

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

torrent_status
==============

It contains the following fields::

	struct torrent_status
	{
		enum state_t
		{
			queued_for_checking, // deprecated
			checking_files,
			downloading_metadata,
			downloading,
			finished,
			seeding,
			allocating,
			checking_resume_data
		};

		torrent_handle handle;
	
		state_t state;
		bool paused;
		bool auto_managed;
		bool sequential_download;
		bool is_seeding;
		bool is_finished;
		bool is_loaded;
		float progress;
		int progress_ppm;
		std::string error;
		std::string save_path;
		std::string name;

		boost::weak_ptr<const torrent_info> torrent_file;

		boost::posix_time::time_duration next_announce;
		boost::posix_time::time_duration announce_interval;

		std::string current_tracker;

		size_type total_download;
		size_type total_upload;

		size_type total_payload_download;
		size_type total_payload_upload;

		size_type total_failed_bytes;
		size_type total_redundant_bytes;

		int download_rate;
		int upload_rate;

		int download_payload_rate;
		int upload_payload_rate;

		int num_peers;

		int num_complete;
		int num_incomplete;

		int list_seeds;
		int list_peers;

		int connect_candidates;

		bitfield pieces;
		bitfield verified_pieces;

		int num_pieces;

		size_type total_done;
		size_type total_wanted_done;
		size_type total_wanted;

		int num_seeds;

		int distributed_full_copies;
		int distributed_fraction;

		float distributed_copies;

		int block_size;

		int num_uploads;
		int num_connections;
		int uploads_limit;
		int connections_limit;

		storage_mode_t storage_mode;

		int up_bandwidth_queue;
		int down_bandwidth_queue;

		size_type all_time_upload;
		size_type all_time_download;

		int active_time;
		int finished_time;
		int seeding_time;

		int seed_rank;

		int last_scrape;

		bool has_incoming;

		int sparse_regions;

		bool seed_mode;
		bool upload_mode;
		bool share_mode;
		bool super_seeding;

		int priority;

		time_t added_time;
		time_t completed_time;
		time_t last_seen_complete;

		int time_since_upload;
		int time_since_download;

		int queue_position;
		bool need_save_resume;
		bool ip_filter_applies;

		sha1_hash info_hash;

		int listen_port;
	};

``handle`` is a handle to the torrent whose status the object represents.

``progress`` is a value in the range [0, 1], that represents the progress of the
torrent's current task. It may be checking files or downloading.

``progress_ppm`` reflects the same value as ``progress``, but instead in a range
[0, 1000000] (ppm = parts per million). When floating point operations are disabled,
this is the only alternative to the floating point value in ``progress``.

The torrent's current task is in the ``state`` member, it will be one of the following:

+--------------------------+----------------------------------------------------------+
|``checking_resume_data``  |The torrent is currently checking the fastresume data and |
|                          |comparing it to the files on disk. This is typically      |
|                          |completed in a fraction of a second, but if you add a     |
|                          |large number of torrents at once, they will queue up.     |
+--------------------------+----------------------------------------------------------+
|``queued_for_checking``   |**THIS STATE IS DEPRECATED**                              |
|                          |A torrent that is queued for checking is now in the       |
|                          |``checking_files`` state, and paused and auto-managed.    |
|                          |                                                          |
|                          |The previous semantic for this state was:                 |
|                          |The torrent is in the queue for being checked. But there  |
|                          |currently is another torrent that are being checked.      |
|                          |This torrent will wait for its turn.                      |
+--------------------------+----------------------------------------------------------+
|``checking_files``        |The torrent has not started its download yet, and is      |
|                          |currently checking existing files.                        |
+--------------------------+----------------------------------------------------------+
|``downloading_metadata``  |The torrent is trying to download metadata from peers.    |
|                          |This assumes the metadata_transfer extension is in use.   |
+--------------------------+----------------------------------------------------------+
|``downloading``           |The torrent is being downloaded. This is the state        |
|                          |most torrents will be in most of the time. The progress   |
|                          |meter will tell how much of the files that has been       |
|                          |downloaded.                                               |
+--------------------------+----------------------------------------------------------+
|``finished``              |In this state the torrent has finished downloading but    |
|                          |still doesn't have the entire torrent. i.e. some pieces   |
|                          |are filtered and won't get downloaded.                    |
+--------------------------+----------------------------------------------------------+
|``seeding``               |In this state the torrent has finished downloading and    |
|                          |is a pure seeder.                                         |
+--------------------------+----------------------------------------------------------+
|``allocating``            |If the torrent was started in full allocation mode, this  |
|                          |indicates that the (disk) storage for the torrent is      |
|                          |allocated.                                                |
+--------------------------+----------------------------------------------------------+


When downloading, the progress is ``total_wanted_done`` / ``total_wanted``. This takes
into account files whose priority have been set to 0. They are not considered.

``paused`` is set to true if the torrent is paused and false otherwise. It's only true
if the torrent itself is paused. If the torrent is not running because the session is
paused, this is still false. To know if a torrent is active or not, you need to inspect
both ``torrent_status::paused`` and ``session::is_paused()``.

``auto_managed`` is set to true if the torrent is auto managed, i.e. libtorrent is
responsible for determining whether it should be started or queued. For more info
see queuing_

``sequential_download`` is true when the torrent is in sequential download mode. In
this mode pieces are downloaded in order rather than rarest first.

``is_seeding`` is true if all pieces have been downloaded.

``is_finished`` is true if all pieces that have a priority > 0 are downloaded. There is
only a distinction between finished and seeding if some pieces or files have been
set to priority 0, i.e. are not downloaded.

``is_loaded`` is true if this torrent is loaded into RAM. A torrent can be started
and still not loaded into RAM, in case it has not had any peers interested in it
yet. Torrents are loaded on demand.

``has_metadata`` is true if this torrent has metadata (either it was started from a
.torrent file or the metadata has been downloaded). The only scenario where this can be
false is when the torrent was started torrent-less (i.e. with just an info-hash and tracker
ip, a magnet link for instance).

``error`` may be set to an error message describing why the torrent was paused, in
case it was paused by an error. If the torrent is not paused or if it's paused but
not because of an error, this string is empty.

``save_path`` is the path to the directory where this torrent's files are stored.
It's typically the path as was given to `async_add_torrent() add_torrent()`_ when this torrent
was started. This field is only included if the torrent status is queried with
``torrent_handle::query_save_path``.

``name`` is the name of the torrent. Typically this is derived from the .torrent file.
In case the torrent was started without metadata, and hasn't completely received it yet,
it returns the name given to it when added to the session. See ``session::add_torrent``.
This field is only included if the torrent status is queried with ``torrent_handle::query_name``.

``torrent_file`` is set to point to the ``torrent_info`` object for this torrent. It's
only included if the torrent status is queried with ``torrent_handle::query_torrent_file``.

``next_announce`` is the time until the torrent will announce itself to the tracker. And
``announce_interval`` is the time the tracker want us to wait until we announce ourself
again the next time.

``current_tracker`` is the URL of the last working tracker. If no tracker request has
been successful yet, it's set to an empty string.

``total_download`` and ``total_upload`` is the number of bytes downloaded and
uploaded to all peers, accumulated, *this session* only. The session is considered
to restart when a torrent is paused and restarted again. When a torrent is paused,
these counters are reset to 0. If you want complete, persistent, stats, see
``all_time_upload`` and ``all_time_download``.

``total_payload_download`` and ``total_payload_upload`` counts the amount of bytes
send and received this session, but only the actual payload data (i.e the interesting
data), these counters ignore any protocol overhead.

``total_failed_bytes`` is the number of bytes that has been downloaded and that
has failed the piece hash test. In other words, this is just how much crap that
has been downloaded.

``total_redundant_bytes`` is the number of bytes that has been downloaded even
though that data already was downloaded. The reason for this is that in some
situations the same data can be downloaded by mistake. When libtorrent sends
requests to a peer, and the peer doesn't send a response within a certain
timeout, libtorrent will re-request that block. Another situation when
libtorrent may re-request blocks is when the requests it sends out are not
replied in FIFO-order (it will re-request blocks that are skipped by an out of
order block). This is supposed to be as low as possible.

``pieces`` is the bitmask that represents which pieces we have (set to true) and
the pieces we don't have. It's a pointer and may be set to 0 if the torrent isn't
downloading or seeding.

``verified_pieces`` is a bitmask representing which pieces has had their hash
checked. This only applies to torrents in *seed mode*. If the torrent is not
in seed mode, this bitmask may be empty.

``num_pieces`` is the number of pieces that has been downloaded. It is equivalent
to: ``std::accumulate(pieces->begin(), pieces->end())``. So you don't have to
count yourself. This can be used to see if anything has updated since last time
if you want to keep a graph of the pieces up to date.

``download_rate`` and ``upload_rate`` are the total rates for all peers for this
torrent. These will usually have better precision than summing the rates from
all peers. The rates are given as the number of bytes per second. The
``download_payload_rate`` and ``upload_payload_rate`` respectively is the
total transfer rate of payload only, not counting protocol chatter. This might
be slightly smaller than the other rates, but if projected over a long time
(e.g. when calculating ETA:s) the difference may be noticeable.

``num_peers`` is the number of peers this torrent currently is connected to.
Peer connections that are in the half-open state (is attempting to connect)
or are queued for later connection attempt do not count. Although they are
visible in the peer list when you call `get_peer_info()`_.

``num_complete`` and ``num_incomplete`` are set to -1 if the tracker did not
send any scrape data in its announce reply. This data is optional and may
not be available from all trackers. If these are not -1, they are the total
number of peers that are seeding (complete) and the total number of peers
that are still downloading (incomplete) this torrent.

``list_seeds`` and ``list_peers`` are the number of seeds in our peer list
and the total number of peers (including seeds) respectively. We are not
necessarily connected to all the peers in our peer list. This is the number
of peers we know of in total, including banned peers and peers that we have
failed to connect to.

``connect_candidates`` is the number of peers in this torrent's peer list
that is a candidate to be connected to. i.e. It has fewer connect attempts
than the max fail count, it is not a seed if we are a seed, it is not banned
etc. If this is 0, it means we don't know of any more peers that we can try.

``total_done`` is the total number of bytes of the file(s) that we have. All
this does not necessarily has to be downloaded during this session (that's
``total_payload_download``).

``total_wanted_done`` is the number of bytes we have downloaded, only counting the
pieces that we actually want to download. i.e. excluding any pieces that we have but
have priority 0 (i.e. not wanted).

``total_wanted`` is the total number of bytes we want to download. This is also
excluding pieces whose priorities have been set to 0.

``num_seeds`` is the number of peers that are seeding that this client is
currently connected to.

``distributed_full_copies`` is the number of distributed copies of the torrent.
Note that one copy may be spread out among many peers. It tells how many copies
there are currently of the rarest piece(s) among the peers this client is
connected to.

``distributed_fraction`` tells the share of pieces that have more copies than
the rarest piece(s). Divide this number by 1000 to get the fraction.

For example, if ``distributed_full_copies`` is 2 and ``distrbuted_fraction``
is 500, it means that the rarest pieces have only 2 copies among the peers
this torrent is connected to, and that 50% of all the pieces have more than
two copies.

If we are a seed, the piece picker is deallocated as an optimization, and
piece availability is no longer tracked. In this case the distributed
copies members are set to -1.

``distributed_copies`` is a floating point representation of the
``distributed_full_copies`` as the integer part and ``distributed_fraction``
/ 1000 as the fraction part. If floating point operations are disabled
this value is always -1.

``block_size`` is the size of a block, in bytes. A block is a sub piece, it
is the number of bytes that each piece request asks for and the number of
bytes that each bit in the ``partial_piece_info``'s bitset represents
(see `get_download_queue()`_). This is typically 16 kB, but it may be
larger if the pieces are larger.

``num_uploads`` is the number of unchoked peers in this torrent.

``num_connections`` is the number of peer connections this torrent has, including
half-open connections that hasn't completed the bittorrent handshake yet. This is
always >= ``num_peers``.

``uploads_limit`` is the set limit of upload slots (unchoked peers) for this torrent.

``connections_limit`` is the set limit of number of connections for this torrent.

``storage_mode`` is one of ``storage_mode_allocate``, ``storage_mode_sparse`` or
``storage_mode_compact``. Identifies which storage mode this torrent is being saved
with. See `Storage allocation`_.

``up_bandwidth_queue`` and ``down_bandwidth_queue`` are the number of peers in this
torrent that are waiting for more bandwidth quota from the torrent rate limiter.
This can determine if the rate you get from this torrent is bound by the torrents
limit or not. If there is no limit set on this torrent, the peers might still be
waiting for bandwidth quota from the global limiter, but then they are counted in
the ``session_status`` object.

``all_time_upload`` and ``all_time_download`` are accumulated upload and download
payload byte counters. They are saved in and restored from resume data to keep totals
across sessions.

``active_time``, ``finished_time`` and ``seeding_time`` are second counters.
They keep track of the number of seconds this torrent has been active (not
paused) and the number of seconds it has been active while being finished and
active while being a seed. ``seeding_time`` should be <= ``finished_time`` which
should be <= ``active_time``. They are all saved in and restored from resume data,
to keep totals across sessions.

``seed_rank`` is a rank of how important it is to seed the torrent, it is used
to determine which torrents to seed and which to queue. It is based on the peer
to seed ratio from the tracker scrape. For more information, see queuing_.

``last_scrape`` is the number of seconds since this torrent acquired scrape data.
If it has never done that, this value is -1.

``has_incoming`` is true if there has ever been an incoming connection attempt
to this torrent.'

``sparse_regions`` the number of regions of non-downloaded pieces in the
torrent. This is an interesting metric on windows vista, since there is
a limit on the number of sparse regions in a single file there.

``seed_mode`` is true if the torrent is in seed_mode. If the torrent was
started in seed mode, it will leave seed mode once all pieces have been
checked or as soon as one piece fails the hash check.

``upload_mode`` is true if the torrent is blocked from downloading. This
typically happens when a disk write operation fails. If the torrent is
auto-managed, it will periodically be taken out of this state, in the
hope that the disk condition (be it disk full or permission errors) has
been resolved. If the torrent is not auto-managed, you have to explicitly
take it out of the upload mode by calling `set_upload_mode()`_ on the
torrent_handle_.

``share_mode`` is true if the torrent is currently in share-mode, i.e.
not downloading the torrent, but just helping the swarm out.

``super_seeding`` is true if the torrent is in super seeding mode.

``added_time`` is the posix-time when this torrent was added. i.e. what
``time(NULL)`` returned at the time.

``completed_time`` is the posix-time when this torrent was finished. If
the torrent is not yet finished, this is 0.

``last_seen_complete`` is the time when we, or one of our peers, last
saw a complete copy of this torrent.

``time_since_upload`` and ``time_since_download`` are the number of
seconds since any peer last uploaded from this torrent and the last
time a downloaded piece passed the hash check, respectively.

``queue_position`` is the position this torrent has in the download
queue. If the torrent is a seed or finished, this is -1.

``need_save_resume`` is true if this torrent has unsaved changes
to its download state and statistics since the last resume data
was saved.

``ip_filter_applies`` is true if the session global IP filter applies
to this torrent. This defaults to true.

``info_hash`` is the info-hash of the torrent.

``listen_port`` is the listen port this torrent is listening on for new
connections, if the torrent has its own listen socket. Only SSL torrents
have their own listen sockets. If the torrent doesn't have one, and is
accepting connections on the single listen socket, this is 0.

peer_info
=========

It contains the following fields::

	struct peer_info
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
			rc4_encrypted = 0x100000,
			plaintext_encrypted = 0x200000
		};

		unsigned int flags;

		enum peer_source_flags
		{
			tracker = 0x1,
			dht = 0x2,
			pex = 0x4,
			lsd = 0x8
		};

		int source;

		// bitmask representing socket state
		enum bw_state { bw_idle = 0, bw_limit = 1, bw_network = 2, bw_disk = 4 };

		char read_state;
		char write_state;

		asio::ip::tcp::endpoint ip;
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

		time_duration last_request;
		time_duration last_active;
		int request_timeout;

		int send_buffer_size;
		int used_send_buffer;

		int receive_buffer_size;
		int used_receive_buffer;

		int num_hashfails;

		char country[2];

		std::string inet_as_name;
		int inet_as;

		int requests_in_buffer;
		int download_queue_length;
		int upload_queue_length;

		int failcount;

		int downloading_piece_index;
		int downloading_block_index;
		int downloading_progress;
		int downloading_total;

		std::string client;

		enum
		{
			standard_bittorrent = 0,
			web_seed = 1
		};
		int connection_type;

		int remote_dl_rate;

		int pending_disk_bytes;

		int send_quota;
		int receive_quota;

		int rtt;

		int num_pieces;

		int download_rate_peak;
		int upload_rate_peak;

		float progress;
		int progress_ppm;

		tcp::endpoint local_endpoint;
	};

The ``flags`` attribute tells you in which state the peer is. It is set to
any combination of the enums above. The following table describes each flag:

+-------------------------+-------------------------------------------------------+
| ``interesting``         | **we** are interested in pieces from this peer.       |
+-------------------------+-------------------------------------------------------+
| ``choked``              | **we** have choked this peer.                         |
+-------------------------+-------------------------------------------------------+
| ``remote_interested``   | the peer is interested in **us**                      |
+-------------------------+-------------------------------------------------------+
| ``remote_choked``       | the peer has choked **us**.                           |
+-------------------------+-------------------------------------------------------+
| ``support_extensions``  | means that this peer supports the                     |
|                         | `extension protocol`__.                               |
+-------------------------+-------------------------------------------------------+
| ``local_connection``    | The connection was initiated by us, the peer has a    |
|                         | listen port open, and that port is the same as in the |
|                         | address of this peer. If this flag is not set, this   |
|                         | peer connection was opened by this peer connecting to |
|                         | us.                                                   |
+-------------------------+-------------------------------------------------------+
| ``handshake``           | The connection is opened, and waiting for the         |
|                         | handshake. Until the handshake is done, the peer      |
|                         | cannot be identified.                                 |
+-------------------------+-------------------------------------------------------+
| ``connecting``          | The connection is in a half-open state (i.e. it is    |
|                         | being connected).                                     |
+-------------------------+-------------------------------------------------------+
| ``queued``              | The connection is currently queued for a connection   |
|                         | attempt. This may happen if there is a limit set on   |
|                         | the number of half-open TCP connections.              |
+-------------------------+-------------------------------------------------------+
| ``on_parole``           | The peer has participated in a piece that failed the  |
|                         | hash check, and is now "on parole", which means we're |
|                         | only requesting whole pieces from this peer until     |
|                         | it either fails that piece or proves that it doesn't  |
|                         | send bad data.                                        |
+-------------------------+-------------------------------------------------------+
| ``seed``                | This peer is a seed (it has all the pieces).          |
+-------------------------+-------------------------------------------------------+
| ``optimistic_unchoke``  | This peer is subject to an optimistic unchoke. It has |
|                         | been unchoked for a while to see if it might unchoke  |
|                         | us in return an earn an upload/unchoke slot. If it    |
|                         | doesn't within some period of time, it will be choked |
|                         | and another peer will be optimistically unchoked.     |
+-------------------------+-------------------------------------------------------+
| ``snubbed``             | This peer has recently failed to send a block within  |
|                         | the request timeout from when the request was sent.   |
|                         | We're currently picking one block at a time from this |
|                         | peer.                                                 |
+-------------------------+-------------------------------------------------------+
| ``upload_only``         | This peer has either explicitly (with an extension)   |
|                         | or implicitly (by becoming a seed) told us that it    |
|                         | will not downloading anything more, regardless of     |
|                         | which pieces we have.                                 |
+-------------------------+-------------------------------------------------------+
| ``endgame_mode``        | This means the last time this peer picket a piece,    |
|                         | it could not pick as many as it wanted because there  |
|                         | were not enough free ones. i.e. all pieces this peer  |
|                         | has were already requested from other peers.          |
+-------------------------+-------------------------------------------------------+
| ``holepunched``         | This flag is set if the peer was in holepunch mode    |
|                         | when the connection succeeded. This typically only    |
|                         | happens if both peers are behind a NAT and the peers  |
|                         | connect via the NAT holepunch mechanism.              |
+-------------------------+-------------------------------------------------------+

__ extension_protocol.html

``source`` is a combination of flags describing from which sources this peer
was received. The flags are:

+------------------------+--------------------------------------------------------+
| ``tracker``            | The peer was received from the tracker.                |
+------------------------+--------------------------------------------------------+
| ``dht``                | The peer was received from the kademlia DHT.           |
+------------------------+--------------------------------------------------------+
| ``pex``                | The peer was received from the peer exchange           |
|                        | extension.                                             |
+------------------------+--------------------------------------------------------+
| ``lsd``                | The peer was received from the local service           |
|                        | discovery (The peer is on the local network).          |
+------------------------+--------------------------------------------------------+
| ``resume_data``        | The peer was added from the fast resume data.          |
+------------------------+--------------------------------------------------------+

``read_state`` and ``write_state`` are bitmasks indicating what state this peer
is in with regards to sending and receiving data. The states are declared in the
``bw_state`` enum and defines as follows:

+------------------------+--------------------------------------------------------+
| ``bw_idle``            | The peer is not waiting for any external events to     |
|                        | send or receive data.                                  |
|                        |                                                        |
+------------------------+--------------------------------------------------------+
| ``bw_limit``           | The peer is waiting for the rate limiter.              |
|                        |                                                        |
+------------------------+--------------------------------------------------------+
| ``bw_network``         | The peer has quota and is currently waiting for a      |
|                        | network read or write operation to complete. This is   |
|                        | the state all peers are in if there are no bandwidth   |
|                        | limits.                                                |
|                        |                                                        |
+------------------------+--------------------------------------------------------+
| ``bw_disk``            | The peer is waiting for the disk I/O thread to catch   |
|                        | up writing buffers to disk before downloading more.    |
|                        |                                                        |
+------------------------+--------------------------------------------------------+

Note that ``read_state`` and ``write_state`` are bitmasks. A peer may be waiting
on disk and on the network at the same time. ``bw_idle`` does not represent a bit,
but is simply a name for no bit being set in the bitmask.

The ``ip`` field is the IP-address to this peer. The type is an asio endpoint. For
more info, see the asio_ documentation.

.. _asio: http://asio.sf.net

``up_speed`` and ``down_speed`` contains the current upload and download speed
we have to and from this peer (including any protocol messages). The transfer rates
of payload data only are found in ``payload_up_speed`` and ``payload_down_speed``.
These figures are updated approximately once every second.

``total_download`` and ``total_upload`` are the total number of bytes downloaded
from and uploaded to this peer. These numbers do not include the protocol chatter, but only
the payload data.

``pid`` is the peer's id as used in the bit torrent protocol. This id can be used to
extract 'fingerprints' from the peer. Sometimes it can tell you which client the peer
is using. See identify_client()_

``pieces`` is a bitfield, with one bit per piece in the torrent.
Each bit tells you if the peer has that piece (if it's set to 1)
or if the peer miss that piece (set to 0).

``seed`` is true if this peer is a seed.

``upload_limit`` is the number of bytes per second we are allowed to send to this
peer every second. It may be -1 if there's no local limit on the peer. The global
limit and the torrent limit is always enforced anyway.

``download_limit`` is the number of bytes per second this peer is allowed to
receive. -1 means it's unlimited.

``last_request`` and ``last_active`` is the time since we last sent a request
to this peer and since any transfer occurred with this peer, respectively.

``request_timeout`` is the number of seconds until the current front piece request
will time out. This timeout can be adjusted through request_timeout_.
-1 means that there is not outstanding request.

``send_buffer_size`` and ``used_send_buffer`` is the number of bytes allocated
and used for the peer's send buffer, respectively.

``receive_buffer_size`` and ``used_receive_buffer`` are the number of bytes
allocated and used as receive buffer, respectively.

``num_hashfails`` is the number of pieces this peer has participated in
sending us that turned out to fail the hash check.

``country`` is the two letter `ISO 3166 country code`__ for the country the peer
is connected from. If the country hasn't been resolved yet, both chars are set
to 0. If the resolution failed for some reason, the field is set to "--". If the
resolution service returns an invalid country code, it is set to "!!".
The ``countries.nerd.dk`` service is used to look up countries. This field will
remain set to 0 unless the torrent is set to resolve countries, see `resolve_countries()`_.

__ http://www.iso.org/iso/en/prods-services/iso3166ma/02iso-3166-code-lists/list-en1.html

``inet_as_name`` is the name of the AS this peer is located in. This might be
an empty string if there is no name in the geo ip database.

``inet_as`` is the AS number the peer is located in.

``requests_in_buffer`` is the number of requests messages that are currently in the
send buffer waiting to be sent.

``download_queue_length`` is the number of piece-requests we have sent to this peer
that hasn't been answered with a piece yet.

``upload_queue_length`` is the number of piece-requests we have received from this peer
that we haven't answered with a piece yet.

``failcount`` is the number of times this peer has "failed". i.e. failed to connect
or disconnected us. The failcount is decremented when we see this peer in a tracker
response or peer exchange message.

You can know which piece, and which part of that piece, that is currently being
downloaded from a specific peer by looking at the next four members.
``downloading_piece_index`` is the index of the piece that is currently being downloaded.
This may be set to -1 if there's currently no piece downloading from this peer. If it is
>= 0, the other three members are valid. ``downloading_block_index`` is the index of the
block (or sub-piece) that is being downloaded. ``downloading_progress`` is the number
of bytes of this block we have received from the peer, and ``downloading_total`` is
the total number of bytes in this block.

``client`` is a string describing the software at the other end of the connection.
In some cases this information is not available, then it will contain a string
that may give away something about which software is running in the other end.
In the case of a web seed, the server type and version will be a part of this
string.

``connection_type`` can currently be one of:

+---------------------------------------+-------------------------------------------------------+
| type                                  | meaning                                               |
+=======================================+=======================================================+
| ``peer_info::standard_bittorrent``    | Regular bittorrent connection over TCP                |
+---------------------------------------+-------------------------------------------------------+
| ``peer_info::bittorrent_utp``         | Bittorrent connection over uTP                        |
+---------------------------------------+-------------------------------------------------------+
| ``peer_info::web_sesed``              | HTTP connection using the `BEP 19`_ protocol          |
+---------------------------------------+-------------------------------------------------------+
| ``peer_info::http_seed``              | HTTP connection using the `BEP 17`_ protocol          |
+---------------------------------------+-------------------------------------------------------+

``remote_dl_rate`` is an estimate of the rate this peer is downloading at, in
bytes per second.

``pending_disk_bytes`` is the number of bytes this peer has pending in the
disk-io thread. Downloaded and waiting to be written to disk.

``send_quota`` and ``receive_quota`` are the number of bytes this peer has been
assigned to be allowed to send and receive until it has to request more quota
from the bandwidth manager.

``rtt`` is an estimated round trip time to this peer, in milliseconds. It is
estimated by timing the the tcp ``connect()``. It may be 0 for incoming connections.

``num_pieces`` is the number of pieces this peer has.

``download_rate_peak`` and ``upload_rate_peak`` are the highest download and upload
rates seen on this connection. They are given in bytes per second. This number is
reset to 0 on reconnect.

``progress`` is the progress of the peer in the range [0, 1]. This is always 0 when
floating point operations are diabled, instead use ``progress_ppm``.

``progress_ppm`` indicates the download progress of the peer in the range [0, 1000000]
(parts per million).

``local_endpoint`` is the IP and port pair the socket is bound to locally. i.e. the IP
address of the interface it's going out over. This may be useful for multi-homed
clients with multiple interfaces to the internet.

feed_handle
===========

The ``feed_handle`` refers to a specific RSS feed which is watched by the session.
The ``feed_item`` struct is defined in ``<libtorrent/rss.hpp>``. It has the following
functions::

	struct feed_handle
	{
		feed_handle();
		void update_feed();
		feed_status get_feed_status() const;
		void set_settings(feed_settings const& s);
		feed_settings settings() const;
	};

update_feed()
-------------

	::

		void update_feed();

Forces an update/refresh of the feed. Regular updates of the feed is managed
by libtorrent, be careful to not call this too frequently since it may
overload the RSS server.

get_feed_status()
-----------------

	::

		feed_status get_feed_status() const;

Queries the RSS feed for information, including all the items in the feed.
The ``feed_status`` object has the following fields::

	struct feed_status
	{
		std::string url;
		std::string title;
		std::string description;
		time_t last_update;
		int next_update;
		bool updating;
		std::vector<feed_item> items;
		error_code error;
		int ttl;
	};

``url`` is the URL of the feed.

``title`` is the name of the feed (as specified by the feed itself). This
may be empty if we have not recevied a response from the RSS server yet,
or if the feed does not specify a title.

``description`` is the feed description (as specified by the feed itself).
This may be empty if we have not received a response from the RSS server
yet, or if the feed does not specify a description.

``last_update`` is the posix time of the last successful response from the feed.

``next_update`` is the number of seconds, from now, when the feed will be
updated again.

``updating`` is true if the feed is currently being updated (i.e. waiting for
DNS resolution, connecting to the server or waiting for the response to the
HTTP request, or receiving the response).

``items`` is a vector of all items that we have received from the feed. See
feed_item_ for more information.

``error`` is set to the appropriate error code if the feed encountered an
error.

``ttl`` is the current refresh time (in minutes). It's either the configured
default ttl, or the ttl specified by the feed.


set_settings() settings()
-------------------------

	::

		void set_settings(feed_settings const& s);
		feed_settings settings() const;

Sets and gets settings for this feed. For more information on the
available settings, see `add_feed()`_.

feed_item
=========

The ``feed_item`` struct is defined in ``<libtorrent/rss.hpp>``.

	::

		struct feed_item
		{
			feed_item();
			std::string url;
			std::string uuid;
			std::string title;
			std::string description;
			std::string comment;
			std::string category;
			size_type size;
			torrent_handle handle;
			sha1_hash info_hash;
		};

``size`` is the total size of the content the torrent refers to, or -1
if no size was specified by the feed.

``handle`` is the handle to the torrent, if the session is already downloading
this torrent.

``info_hash`` is the info-hash of the torrent, or cleared (i.e. all zeroes) if
the feed does not specify the info-hash.

All the strings are self explanatory and may be empty if the feed does not specify
those fields.

session customization
=====================

You have some control over session configuration through the ``session::apply_settings()``
member function. To change one or more configuration options, create a settings_pack_.
object and fill it with the settings to be set and pass it in to ``session::apply_settings()``.

see `apply_settings()`_.

You have control over proxy and authorization settings and also the user-agent
that will be sent to the tracker. The user-agent will also be used to identify the
client with other peers.

presets
-------

The default values of the session settings are set for a regular bittorrent client running
on a desktop system. There are functions that can set the session settings to pre set
settings for other environments. These can be used for the basis, and should be tweaked to
fit your needs better.

::

	void min_memory_usage(settings_pack& p);
	void high_performance_seed(settings_pack& p);

``min_memory_usage`` returns settings that will use the minimal amount of RAM, at the
potential expense of upload and download performance. It adjusts the socket buffer sizes,
disables the disk cache, lowers the send buffer watermarks so that each connection only has
at most one block in use at any one time. It lowers the outstanding blocks send to the disk
I/O thread so that connections only have one block waiting to be flushed to disk at any given
time. It lowers the max number of peers in the peer list for torrents. It performs multiple
smaller reads when it hashes pieces, instead of reading it all into memory before hashing.

This configuration is inteded to be the starting point for embedded devices. It will
significantly reduce memory usage.

``high_performance_seed`` returns settings optimized for a seed box, serving many peers
and that doesn't do any downloading. It has a 128 MB disk cache and has a limit of 400 files
in its file pool. It support fast upload rates by allowing large send buffers.


settings_pack
-------------

.. parsed-literal::

	struct settings_pack
	{
		void set_str(int name, std::string val);
		void set_int(int name, int val);
		void set_bool(int name, bool val);

		std::string get_str(int name);
		int get_int(int name);
		bool get_bool(int name);

		void clear();

		*all settings enums, see below*

		enum { no_piece_suggestions = 0, suggest_read_cache = 1 };

		enum choking_algorithm_t
		{
			fixed_slots_choker,
			auto_expand_choker,
			rate_based_choker,
			bittyrant_choker
		};

		enum seed_choking_algorithm_t
		{
			round_robin,
			fastest_upload,
			anti_leech
		};

		enum io_buffer_mode_t
		{
			enable_os_cache = 0,
			disable_os_cache_for_aligned_files = 1,
			disable_os_cache = 2
		};

		enum disk_cache_algo_t
		{ lru, largest_contiguous, avoid_readback };

		enum bandwidth_mixed_algo_t
		{
			prefer_tcp = 0,
			peer_proportional = 1
		};
	};

The ``settings_pack`` struct, contains the names of all settings as
enum values. These values are passed in to the ``set_str()``,
``set_int()``, ``set_bool()`` functions, to specify the setting to
change.

These are the available settings:

.. include:: settings.rst

pe_settings
===========

The ``pe_settings`` structure is used to control the settings related
to peer protocol encryption::

	struct pe_settings
	{
		pe_settings();

		enum enc_policy
		{
			forced,
			enabled,
			disabled
		};

		enum enc_level
		{
			plaintext,
			rc4, 
			both
		};

		enc_policy out_enc_policy;
		enc_policy in_enc_policy;
		enc_level allowed_enc_level;
		bool prefer_rc4;
	};


``in_enc_policy`` and ``out_enc_policy`` control the settings for incoming
and outgoing connections respectively. The settings for these are:

 * ``forced`` - Only encrypted connections are allowed. Incoming connections
   that are not encrypted are closed and if the encrypted outgoing connection
   fails, a non-encrypted retry will not be made.

 * ``enabled`` - encrypted connections are enabled, but non-encrypted
   connections are allowed. An incoming non-encrypted connection will
   be accepted, and if an outgoing encrypted connection fails, a non-
   encrypted connection will be tried.

 * ``disabled`` - only non-encrypted connections are allowed.

``allowed_enc_level`` determines the encryption level of the
connections.  This setting will adjust which encryption scheme is
offered to the other peer, as well as which encryption scheme is
selected by the client. The settings are:

 * ``plaintext`` - only the handshake is encrypted, the bulk of the traffic
   remains unchanged.

 * ``rc4`` - the entire stream is encrypted with RC4

 * ``both`` - both RC4 and plaintext connections are allowed.

``prefer_rc4`` can be set to true if you want to prefer the RC4 encrypted stream.


proxy_settings
==============

The ``proxy_settings`` structs contains the information needed to
direct certain traffic to a proxy.

	::

		struct proxy_settings
		{
			proxy_settings();

			std::string hostname;
			int port;

			std::string username;
			std::string password;

			enum proxy_type
			{
				none,
				socks4,
				socks5,
				socks5_pw,
				http,
				http_pw
			};
		
			proxy_type type;
			bool proxy_hostnames;
			bool proxy_peer_connections;
		};

``hostname`` is the name or IP of the proxy server. ``port`` is the
port number the proxy listens to. If required, ``username`` and ``password``
can be set to authenticate with the proxy.

The ``type`` tells libtorrent what kind of proxy server it is. The following
options are available:

 * ``none`` - This is the default, no proxy server is used, all other fields
   are ignored.

 * ``socks4`` - The server is assumed to be a `SOCKS4 server`_ that
   requires a username.

 * ``socks5`` - The server is assumed to be a SOCKS5 server (`RFC 1928`_) that
   does not require any authentication. The username and password are ignored.

 * ``socks5_pw`` - The server is assumed to be a SOCKS5 server that supports
   plain text username and password authentication (`RFC 1929`_). The username
   and password specified may be sent to the proxy if it requires.

 * ``http`` - The server is assumed to be an HTTP proxy. If the transport used
   for the connection is non-HTTP, the server is assumed to support the
   CONNECT_ method. i.e. for web seeds and HTTP trackers, a plain proxy will
   suffice. The proxy is assumed to not require authorization. The username
   and password will not be used.

 * ``http_pw`` - The server is assumed to be an HTTP proxy that requires
   user authorization. The username and password will be sent to the proxy.

.. _`SOCKS4 server`: http://www.ufasoft.com/doc/socks4_protocol.htm
.. _`RFC 1928`: http://www.faqs.org/rfcs/rfc1928.html
.. _`RFC 1929`: http://www.faqs.org/rfcs/rfc1929.html
.. _CONNECT: draft-luotonen-web-proxy-tunneling-01.txt

``proxy_hostnames`` defaults to true. It means that hostnames should be
attempted to be resolved through the proxy instead of using the local DNS
service. This is only supported by SOCKS5 and HTTP.

``proxy_peer_connections`` determines whether or not to excempt peer and
web seed connections from using the proxy. This defaults to true, i.e. peer
connections are proxied by default.

ip_filter
=========

The ``ip_filter`` class is a set of rules that uniquely categorizes all
ip addresses as allowed or disallowed. The default constructor creates
a single rule that allows all addresses (0.0.0.0 - 255.255.255.255 for
the IPv4 range, and the equivalent range covering all addresses for the
IPv6 range).

	::

		template <class Addr>
		struct ip_range
		{
			Addr first;
			Addr last;
			int flags;
		};

		class ip_filter
		{
		public:
			enum access_flags { blocked = 1 };

			ip_filter();
			void add_rule(address first, address last, int flags);
			int access(address const& addr) const;

			typedef boost::tuple<std::vector<ip_range<address_v4> >
				, std::vector<ip_range<address_v6> > > filter_tuple_t;

			filter_tuple_t export_filter() const;
		};


ip_filter()
-----------

	::

		ip_filter()

Creates a default filter that doesn't filter any address.

postcondition:
``access(x) == 0`` for every ``x``


add_rule()
----------

	::

		void add_rule(address first, address last, int flags);

Adds a rule to the filter. ``first`` and ``last`` defines a range of
ip addresses that will be marked with the given flags. The ``flags``
can currently be 0, which means allowed, or ``ip_filter::blocked``, which
means disallowed.

precondition:
``first.is_v4() == last.is_v4() && first.is_v6() == last.is_v6()``

postcondition:
``access(x) == flags`` for every ``x`` in the range [``first``, ``last``]

This means that in a case of overlapping ranges, the last one applied takes
precedence.


access()
--------

	::

		int access(address const& addr) const;

Returns the access permissions for the given address (``addr``). The permission
can currently be 0 or ``ip_filter::blocked``. The complexity of this operation
is O(``log`` n), where n is the minimum number of non-overlapping ranges to describe
the current filter.


export_filter()
---------------

	::

		boost::tuple<std::vector<ip_range<address_v4> >
			, std::vector<ip_range<address_v6> > > export_filter() const;

This function will return the current state of the filter in the minimum number of
ranges possible. They are sorted from ranges in low addresses to high addresses. Each
entry in the returned vector is a range with the access control specified in its
``flags`` field.

The return value is a tuple containing two range-lists. One for IPv4 addresses
and one for IPv6 addresses.

      
big_number
==========

Both the ``peer_id`` and ``sha1_hash`` types are typedefs of the class
``big_number``. It represents 20 bytes of data. Its synopsis follows::

	class big_number
	{
	public:
		bool operator==(const big_number& n) const;
		bool operator!=(const big_number& n) const;
		bool operator<(const big_number& n) const;

		const unsigned char* begin() const;
		const unsigned char* end() const;

		unsigned char* begin();
		unsigned char* end();
	};

The iterators gives you access to individual bytes.


bitfield
========

The bitfiled type stores any number of bits as a bitfield in an array.

::

	class bitfield
	{
		bitfield();
		bitfield(int bits);
		bitfield(int bits, bool val);
		bitfield(char const* bytes, int bits);
		bitfield(bitfield const& rhs);

		void borrow_bytes(char* bytes, int bits);
		~bitfield();

		void assign(char const* bytes, int bits);

		bool operator[](int index) const;

		bool get_bit(int index) const;
	
		void clear_bit(int index);
		void set_bit(int index);

		std::size_t size() const;
		bool empty() const;

		char const* bytes() const;

		bitfield& operator=(bitfield const& rhs);

		int count() const;

		typedef const_iterator;
		const_iterator begin() const;
		const_iterator end() const;

		void resize(int bits, bool val);
		void set_all();
		void clear_all();
		void resize(int bits);
	};



hasher
======

This class creates sha1-hashes. Its declaration looks like this::

	class hasher
	{
	public:
		hasher();
		hasher(char const* data, unsigned int len);

		void update(char const* data, unsigned int len);
		sha1_hash final();
		void reset();
	};


You use it by first instantiating it, then call ``update()`` to feed it
with data. i.e. you don't have to keep the entire buffer of which you want to
create the hash in memory. You can feed the hasher parts of it at a time. When
You have fed the hasher with all the data, you call ``final()`` and it
will return the sha1-hash of the data.

The constructor that takes a ``char const*`` and an integer will construct the
sha1 context and feed it the data passed in.

If you want to reuse the hasher object once you have created a hash, you have to
call ``reset()`` to reinitialize it.

The sha1-algorithm used was implemented by Steve Reid and released as public domain.
For more info, see ``src/sha1.cpp``.


fingerprint
===========

The fingerprint class represents information about a client and its version. It is used
to encode this information into the client's peer id.

This is the class declaration::

	struct fingerprint
	{
		fingerprint(const char* id_string, int major, int minor
			, int revision, int tag);

		std::string to_string() const;

		char name[2];
		char major_version;
		char minor_version;
		char revision_version;
		char tag_version;

	};

The constructor takes a ``char const*`` that should point to a string constant containing
exactly two characters. These are the characters that should be unique for your client. Make
sure not to clash with anybody else. Here are some taken id's:

+----------+-----------------------+
| id chars | client                |
+==========+=======================+
| 'AZ'     | Azureus               |
+----------+-----------------------+
| 'LT'     | libtorrent (default)  |
+----------+-----------------------+
| 'BX'     | BittorrentX           |
+----------+-----------------------+
| 'MT'     | Moonlight Torrent     |
+----------+-----------------------+
| 'TS'     | Torrent Storm         |
+----------+-----------------------+
| 'SS'     | Swarm Scope           |
+----------+-----------------------+
| 'XT'     | Xan Torrent           |
+----------+-----------------------+

There's currently an informal directory of client id's here__.

__ http://wiki.theory.org/BitTorrentSpecification#peer_id


The ``major``, ``minor``, ``revision`` and ``tag`` parameters are used to identify the
version of your client. All these numbers must be within the range [0, 9].

``to_string()`` will generate the actual string put in the peer-id, and return it.


UPnP and NAT-PMP
================

The ``upnp`` and ``natpmp`` classes contains the state for all UPnP and NAT-PMP mappings,
by default 1 or two mappings are made by libtorrent, one for the listen port and one
for the DHT port (UDP).

::

	class upnp
	{
	public:

		enum protocol_type { none = 0, udp = 1, tcp = 2 };
		int add_mapping(protocol_type p, int external_port, int local_port);
		void delete_mapping(int mapping_index);
	
		void discover_device();
		void close();
	
		std::string router_model();
	};

	class natpmp
	{
	public:
	
		enum protocol_type { none = 0, udp = 1, tcp = 2 };
		int add_mapping(protocol_type p, int external_port, int local_port);
		void delete_mapping(int mapping_index);
	
		void close();
		void rebind(address const& listen_interface);
	};

``discover_device()``, ``close()`` and ``rebind()`` are for internal uses and should
not be called directly by clients.

add_mapping()
-------------

	::

		int add_mapping(protocol_type p, int external_port, int local_port);

Attempts to add a port mapping for the specified protocol. Valid protocols are
``upnp::tcp`` and ``upnp::udp`` for the UPnP class and ``natpmp::tcp`` and
``natpmp::udp`` for the NAT-PMP class.

``external_port`` is the port on the external address that will be mapped. This
is a hint, you are not guaranteed that this port will be available, and it may
end up being something else. In the portmap_alert_ notification, the actual
external port is reported.

``local_port`` is the port in the local machine that the mapping should forward
to.

The return value is an index that identifies this port mapping. This is used
to refer to mappings that fails or succeeds in the portmap_error_alert_ and
portmap_alert_ respectively. If The mapping fails immediately, the return value
is -1, which means failure. There will not be any error alert notification for
mappings that fail with a -1 return value.

delete_mapping()
----------------

	::

		void delete_mapping(int mapping_index);

This function removes a port mapping. ``mapping_index`` is the index that refers
to the mapping you want to remove, which was returned from `add_mapping()`_.

router_model()
--------------

	::

		std::string router_model();

This is only available for UPnP routers. If the model is advertized by
the router, it can be queried through this function.


free functions
==============

identify_client()
-----------------

	::

		std::string identify_client(peer_id const& id);

This function is declared in the header ``<libtorrent/identify_client.hpp>``. It can can be used
to extract a string describing a client version from its peer-id. It will recognize most clients
that have this kind of identification in the peer-id.


client_fingerprint()
--------------------

	::

		boost::optional<fingerprint> client_fingerprint(peer_id const& p);

Returns an optional fingerprint if any can be identified from the peer id. This can be used
to automate the identification of clients. It will not be able to identify peers with non-
standard encodings. Only Azureus style, Shadow's style and Mainline style. This function is
declared in the header ``<libtorrent/identify_client.hpp>``.


lazy_bdecode()
--------------

	::

		int lazy_bdecode(char const* start, char const* end, lazy_entry& ret
			, error_code& ec, int* error_pos = 0, int depth_limit = 1000
			, int item_limit = 1000000);

This function decodes bencoded_ data.

.. _bencoded: http://wiki.theory.org/index.php/BitTorrentSpecification

Whenever possible, ``lazy_bdecode()`` should be preferred over ``bdecode()``.
It is more efficient and more secure. It supports having constraints on the
amount of memory is consumed by the parser.

*lazy* refers to the fact that it doesn't copy any actual data out of the
bencoded buffer. It builds a tree of ``lazy_entry`` which has pointers into
the bencoded buffer. This makes it very fast and efficient. On top of that,
it is not recursive, which saves a lot of stack space when parsing deeply
nested trees. However, in order to protect against potential attacks, the
``depth_limit`` and ``item_limit`` control how many levels deep the tree is
allowed to get. With recursive parser, a few thousand levels would be enough
to exhaust the threads stack and terminate the process. The ``item_limit``
protects against very large structures, not necessarily deep. Each bencoded
item in the structure causes the parser to allocate some amount of memory,
this memory is constant regardless of how much data actually is stored in
the item. One potential attack is to create a bencoded list of hundreds of
thousands empty strings, which would cause the parser to allocate a significant
amount of memory, perhaps more than is available on the machine, and effectively
provide a denial of service. The default item limit is set as a reasonable
upper limit for desktop computers. Very few torrents have more items in them.
The limit corresponds to about 25 MB, which might be a bit much for embedded
systems.

``start`` and ``end`` defines the bencoded buffer to be decoded. ``ret`` is
the ``lazy_entry`` which is filled in with the whole decoded tree. ``ec``
is a reference to an ``error_code`` which is set to describe the error encountered
in case the function fails. ``error_pos`` is an optional pointer to an int,
which will be set to the byte offset into the buffer where an error occurred,
in case the function fails.

bdecode() bencode() 
--------------------

	::

		template<class InIt> entry bdecode(InIt start, InIt end);
		template<class OutIt> void bencode(OutIt out, const entry& e);

These functions will encode data to bencoded_ or decode bencoded_ data.

If possible, `lazy_bdecode()`_ should be preferred over ``bdecode()``.

The entry_ class is the internal representation of the bencoded data
and it can be used to retrieve information, an entry_ can also be build by
the program and given to ``bencode()`` to encode it into the ``OutIt``
iterator.

The ``OutIt`` and ``InIt`` are iterators
(InputIterator_ and OutputIterator_ respectively). They
are templates and are usually instantiated as ostream_iterator_,
back_insert_iterator_ or istream_iterator_. These
functions will assume that the iterator refers to a character
(``char``). So, if you want to encode entry ``e`` into a buffer
in memory, you can do it like this::

	std::vector<char> buffer;
	bencode(std::back_inserter(buf), e);

.. _InputIterator: http://www.sgi.com/tech/stl/InputIterator.html
.. _OutputIterator: http://www.sgi.com/tech/stl/OutputIterator.html
.. _ostream_iterator: http://www.sgi.com/tech/stl/ostream_iterator.html
.. _back_insert_iterator: http://www.sgi.com/tech/stl/back_insert_iterator.html
.. _istream_iterator: http://www.sgi.com/tech/stl/istream_iterator.html

If you want to decode a torrent file from a buffer in memory, you can do it like this::

	std::vector<char> buffer;
	// ...
	entry e = bdecode(buf.begin(), buf.end());

Or, if you have a raw char buffer::

	const char* buf;
	// ...
	entry e = bdecode(buf, buf + data_size);

Now we just need to know how to retrieve information from the entry_.

If ``bdecode()`` encounters invalid encoded data in the range given to it
it will throw libtorrent_exception_.

add_magnet_uri()
----------------

*deprecated*

	::

		torrent_handle add_magnet_uri(session& ses, std::string const& uri
			add_torrent_params p);
		torrent_handle add_magnet_uri(session& ses, std::string const& uri
			add_torrent_params p, error_code& ec);

This function parses the magnet URI (``uri``) as a bittorrent magnet link,
and adds the torrent to the specified session (``ses``). It returns the
handle to the newly added torrent, or an invalid handle in case parsing
failed. To control some initial settings of the torrent, sepcify those in
the ``add_torrent_params``, ``p``. See `async_add_torrent() add_torrent()`_.

The overload that does not take an ``error_code`` throws an exception on
error and is not available when building without exception support.

A simpler way to add a magnet link to a session is to pass in the
link through ``add_torrent_params::url`` argument to ``session::add_torrent()``.

For more information about magnet links, see `magnet links`_.

parse_magnet_uri()
------------------

	::

		void parse_magnet_uri(std::string const& uri, add_torrent_params& p, error_code& ec);

This function parses out information from the magnet link and populates the
``add_torrent_params`` object.

make_magnet_uri()
-----------------

	::

		std::string make_magnet_uri(torrent_handle const& handle);

Generates a magnet URI from the specified torrent. If the torrent
handle is invalid, an empty string is returned.

For more information about magnet links, see `magnet links`_.


alerts
======

The ``pop_alert()`` function on session is the interface for retrieving
alerts, warnings, messages and errors from libtorrent. If no alerts have
been posted by libtorrent ``pop_alert()`` will return a default initialized
``auto_ptr`` object. If there is an alert in libtorrent's queue, the alert
from the front of the queue is popped and returned.
You can then use the alert object and query

By default, only errors are reported. `set_alert_mask()`_ can be
used to specify which kinds of events should be reported. The alert mask
is a bitmask with the following bits:

+--------------------------------+---------------------------------------------------------------------+
| ``error_notification``         | Enables alerts that report an error. This includes:                 |
|                                |                                                                     |
|                                | * tracker errors                                                    |
|                                | * tracker warnings                                                  |
|                                | * file errors                                                       |
|                                | * resume data failures                                              |
|                                | * web seed errors                                                   |
|                                | * .torrent files errors                                             |
|                                | * listen socket errors                                              |
|                                | * port mapping errors                                               |
+--------------------------------+---------------------------------------------------------------------+
| ``peer_notification``          | Enables alerts when peers send invalid requests, get banned or      |
|                                | snubbed.                                                            |
+--------------------------------+---------------------------------------------------------------------+
| ``port_mapping_notification``  | Enables alerts for port mapping events. For NAT-PMP and UPnP.       |
+--------------------------------+---------------------------------------------------------------------+
| ``storage_notification``       | Enables alerts for events related to the storage. File errors and   |
|                                | synchronization events for moving the storage, renaming files etc.  |
+--------------------------------+---------------------------------------------------------------------+
| ``tracker_notification``       | Enables all tracker events. Includes announcing to trackers,        |
|                                | receiving responses, warnings and errors.                           |
+--------------------------------+---------------------------------------------------------------------+
| ``debug_notification``         | Low level alerts for when peers are connected and disconnected.     |
+--------------------------------+---------------------------------------------------------------------+
| ``status_notification``        | Enables alerts for when a torrent or the session changes state.     |
+--------------------------------+---------------------------------------------------------------------+
| ``progress_notification``      | Alerts for when blocks are requested and completed. Also when       |
|                                | pieces are completed.                                               |
+--------------------------------+---------------------------------------------------------------------+
| ``ip_block_notification``      | Alerts when a peer is blocked by the ip blocker or port blocker.    |
+--------------------------------+---------------------------------------------------------------------+
| ``performance_warning``        | Alerts when some limit is reached that might limit the download     |
|                                | or upload rate.                                                     |
+--------------------------------+---------------------------------------------------------------------+
| ``stats_notification``         | If you enable these alerts, you will receive a ``stats_alert``      |
|                                | approximately once every second, for every active torrent.          |
|                                | These alerts contain all statistics counters for the interval since |
|                                | the lasts stats alert.                                              |
+--------------------------------+---------------------------------------------------------------------+
| ``dht_notification``           | Alerts on events in the DHT node. For incoming searches or          |
|                                | bootstrapping being done etc.                                       |
+--------------------------------+---------------------------------------------------------------------+
| ``rss_notification``           | Alerts on RSS related events, like feeds being updated, feed error  |
|                                | conditions and successful RSS feed updates. Enabling this categoty  |
|                                | will make you receive ``rss_alert`` alerts.                         |
+--------------------------------+---------------------------------------------------------------------+
| ``all_categories``             | The full bitmask, representing all available categories.            |
+--------------------------------+---------------------------------------------------------------------+

Every alert belongs to one or more category. There is a small cost involved in posting alerts. Only
alerts that belong to an enabled category are posted. Setting the alert bitmask to 0 will disable
all alerts

When you get an alert, you can use ``alert_cast<>`` to attempt to cast the pointer to a
more specific alert type, to be queried for more information about the alert. ``alert_cast``
has the followinf signature::

	template <T> T* alert_cast(alert* a);
	template <T> T const* alert_cast(alert const* a);

You can also use a `alert dispatcher`_ mechanism that's available in libtorrent.

All alert types are defined in the ``<libtorrent/alert_types.hpp>`` header file.

The ``alert`` class is the base class that specific messages are derived from. This
is its synopsis:

.. parsed-literal::

	class alert
	{
	public:

		enum category_t
		{
			error_notification = *implementation defined*,
			peer_notification = *implementation defined*,
			port_mapping_notification = *implementation defined*,
			storage_notification = *implementation defined*,
			tracker_notification = *implementation defined*,
			debug_notification = *implementation defined*,
			status_notification = *implementation defined*,
			progress_notification = *implementation defined*,
			ip_block_notification = *implementation defined*,
			performance_warning = *implementation defined*,
			dht_notification = *implementation defined*,
			stats_notification = *implementation defined*,
			rss_notification = *implementation defined*,

			all_categories = *implementation defined*
		};

		ptime timestamp() const;

		virtual ~alert();

		virtual int type() const = 0;
		virtual std::string message() const = 0;
		virtual char const* what() const = 0;
		virtual int category() const = 0;
		virtual bool discardable() const;
		virtual std::auto_ptr<alert> clone() const = 0;
	};

``type()`` returns an integer that is unique to this alert type. It can be
compared against a specific alert by querying a static constant called ``alert_type``
in the alert. It can be used to determine the run-time type of an alert* in
order to cast to that alert type and access specific members.

e.g::

	std::auto_ptr<alert> a = ses.pop_alert();
	switch (a->type())
	{
		case read_piece_alert::alert_type:
		{
			read_piece_alert* p = (read_piece_alert*)a.get();
			if (p->ec) {
				// read_piece failed
				break;
			}
			// use p
			break;
		}
		case file_renamed_alert::alert_type:
		{
			// etc...
		}
	}

``what()`` returns a string literal describing the type of the alert. It does
not include any information that might be bundled with the alert.

``category()`` returns a bitmask specifying which categories this alert belong to.

``clone()`` returns a pointer to a copy of the alert.

``discardable()`` determines whether or not an alert is allowed to be discarded
when the alert queue is full. There are a few alerts which may not be discared,
since they would break the user contract, such as ``save_resume_data_alert``.

``message()`` generate a string describing the alert and the information bundled
with it. This is mainly intended for debug and development use. It is not suitable
to use this for applications that may be localized. Instead, handle each alert
type individually and extract and render the information from the alert depending
on the locale.

There's another alert base class that most alerts derives from, all the
alerts that are generated for a specific torrent are derived from::

	struct torrent_alert: alert
	{
		// ...
		torrent_handle handle;
	};

There's also a base class for all alerts referring to tracker events::

	struct tracker_alert: torrent_alert
	{
		// ...
		std::string url;
	};

The specific alerts are:

torrent_added_alert
-------------------

The ``torrent_added_alert`` is posted once every time a torrent is successfully
added. It doesn't contain any members of its own, but inherits the torrent handle
from its base class.
It's posted when the ``status_notification`` bit is set in the alert mask.

::

	struct torrent_added_alert: torrent_alert
	{
		// ...
	};


add_torrent_alert
-----------------

This alert is always posted when a torrent was attempted to be added
and contains the return status of the add operation. The torrent handle of the new
torrent can be found in the base class' ``handle`` member. If adding
the torrent failed, ``error`` contains the error code.

::

	struct add_torrent_alert: torrent_alert
	{
		// ...
		add_torrent_params params;
		error_code error;
	};

``params`` is a copy of the parameters used when adding the torrent, it can be used
to identify which invocation to ``async_add_torrent()`` caused this alert.

``error`` is set to the error, if one occurred while adding the torrent.


torrent_removed_alert
---------------------

The ``torrent_removed_alert`` is posted whenever a torrent is removed. Since
the torrent handle in its baseclass will always be invalid (since the torrent
is already removed) it has the info hash as a member, to identify it.
It's posted when the ``status_notification`` bit is set in the alert mask.

Even though the ``handle`` member doesn't point to an existing torrent anymore,
it is still useful for comparing to other handles, which may also no
longer point to existing torrents, but to the same non-existing torrents.

The ``torrent_handle`` acts as a ``weak_ptr``, even though its object no
longer exists, it can still compare equal to another weak pointer which
points to the same non-existent object.

::

	struct torrent_removed_alert: torrent_alert
	{
		// ...
		sha1_hash info_hash;
	};

read_piece_alert
----------------

This alert is posted when the asynchronous read operation initiated by
a call to `read_piece()`_ is completed. If the read failed, the torrent
is paused and an error state is set and the buffer member of the alert
is 0. If successful, ``buffer`` points to a buffer containing all the data
of the piece. ``piece`` is the piece index that was read. ``size`` is the
number of bytes that was read.

If the operation fails, ec will indicat what went wrong.

::

	struct read_piece_alert: torrent_alert
	{
		// ...
		error_code ec;
		boost::shared_ptr<char> buffer;
		int piece;
		int size;
	};

external_ip_alert
-----------------

Whenever libtorrent learns about the machines external IP, this alert is
generated. The external IP address can be acquired from the tracker (if it
supports that) or from peers that supports the extension protocol.
The address can be accessed through the ``external_address`` member.

::

	struct external_ip_alert: alert
	{
		// ...
		address external_address;
	};


listen_failed_alert
-------------------

This alert is generated when none of the ports, given in the port range, to
session_ can be opened for listening. The ``endpoint`` member is the
interface and port that failed, ``error`` is the error code describing
the failure.

libtorrent may sometimes try to listen on port 0, if all other ports failed.
Port 0 asks the operating system to pick a port that's free). If that fails
you may see a listen_failed_alert_ with port 0 even if you didn't ask to
listen on it.

::

	struct listen_failed_alert: alert
	{
		// ...
		tcp::endpoint endpoint;
		error_code error;
	};

listen_succeeded_alert
----------------------

This alert is posted when the listen port succeeds to be opened on a
particular interface. ``endpoint`` is the endpoint that successfully
was opened for listening.

::

	struct listen_succeeded_alert: alert
	{
		// ...
		tcp::endpoint endpoint;
	};


portmap_error_alert
-------------------

This alert is generated when a NAT router was successfully found but some
part of the port mapping request failed. It contains a text message that
may help the user figure out what is wrong. This alert is not generated in
case it appears the client is not running on a NAT:ed network or if it
appears there is no NAT router that can be remote controlled to add port
mappings.

``mapping`` refers to the mapping index of the port map that failed, i.e.
the index returned from `add_mapping()`_.

``map_type`` is 0 for NAT-PMP and 1 for UPnP.

``error`` tells you what failed.
::

	struct portmap_error_alert: alert
	{
		// ...
		int mapping;
		int type;
		error_code error;
	};

portmap_alert
-------------

This alert is generated when a NAT router was successfully found and
a port was successfully mapped on it. On a NAT:ed network with a NAT-PMP
capable router, this is typically generated once when mapping the TCP
port and, if DHT is enabled, when the UDP port is mapped.

``mapping`` refers to the mapping index of the port map that failed, i.e.
the index returned from `add_mapping()`_.

``external_port`` is the external port allocated for the mapping.

``type`` is 0 for NAT-PMP and 1 for UPnP.

::

	struct portmap_alert: alert
	{
		// ...
		int mapping;
		int external_port;
		int map_type;
	};

portmap_log_alert
-----------------

This alert is generated to log informational events related to either
UPnP or NAT-PMP. They contain a log line and the type (0 = NAT-PMP
and 1 = UPnP). Displaying these messages to an end user is only useful
for debugging the UPnP or NAT-PMP implementation.

::

	struct portmap_log_alert: alert
	{
		//...
		int map_type;
		std::string msg;
	};

file_error_alert
----------------

If the storage fails to read or write files that it needs access to, this alert is
generated and the torrent is paused.

``file`` is the path to the file that was accessed when the error occurred.

``error`` is the error code describing the error.

``operation`` is a NULL-terminated string of the low-level operation that failed, or NULL if
there was no low level disk operation.

::

	struct file_error_alert: torrent_alert
	{
		// ...
		std::string file;
		error_code error;
		char const* operation;
	};

torrent_error_alert
-------------------

This is posted whenever a torrent is transitioned into the error state.

::

	struct torrent_error_alert: torrent_alert
	{
		// ...
		error_code error;
		std::string error_file;
	};

The ``error`` specifies which error the torrent encountered.

``error_file`` is the filename (or object) the error occurred on.

file_renamed_alert
------------------

This is posted as a response to a ``torrent_handle::rename_file`` call, if the rename
operation succeeds.

::

	struct file_renamed_alert: torrent_alert
	{
		// ...
		std::string name;
		int index;
	};

The ``index`` member refers to the index of the file that was renamed,
``name`` is the new name of the file.


file_rename_failed_alert
------------------------

This is posted as a response to a ``torrent_handle::rename_file`` call, if the rename
operation failed.

::

	struct file_rename_failed_alert: torrent_alert
	{
		// ...
		int index;
		error_code error;
	};

The ``index`` member refers to the index of the file that was supposed to be renamed,
``error`` is the error code returned from the filesystem.

tracker_announce_alert
----------------------

This alert is generated each time a tracker announce is sent (or attempted to be sent).
There are no extra data members in this alert. The url can be found in the base class
however.

::

	struct tracker_announce_alert: tracker_alert
	{
		// ...
		int event;
	};

Event specifies what event was sent to the tracker. It is defined as:

0. None
1. Completed
2. Started
3. Stopped


tracker_error_alert
-------------------

This alert is generated on tracker time outs, premature disconnects, invalid response or
a HTTP response other than "200 OK". From the alert you can get the handle to the torrent
the tracker belongs to.

The ``times_in_row`` member says how many times in a row this tracker has failed.
``status_code`` is the code returned from the HTTP server. 401 means the tracker needs
authentication, 404 means not found etc. If the tracker timed out, the code will be set
to 0.

::

	struct tracker_error_alert: tracker_alert
	{
		// ...
		int times_in_row;
		int status_code;
	};


tracker_reply_alert
-------------------

This alert is only for informational purpose. It is generated when a tracker announce
succeeds. It is generated regardless what kind of tracker was used, be it UDP, HTTP or
the DHT.

::

	struct tracker_reply_alert: tracker_alert
	{
		// ...
		int num_peers;
	};

The ``num_peers`` tells how many peers the tracker returned in this response. This is
not expected to be more thant the ``num_want`` settings. These are not necessarily
all new peers, some of them may already be connected.

tracker_warning_alert
---------------------

This alert is triggered if the tracker reply contains a warning field. Usually this
means that the tracker announce was successful, but the tracker has a message to
the client. The ``msg`` string in the alert contains the warning message from
the tracker.

::

	struct tracker_warning_alert: tracker_alert
	{
		// ...
		std::string msg;
	};

scrape_reply_alert
------------------

This alert is generated when a scrape request succeeds. ``incomplete``
and ``complete`` is the data returned in the scrape response. These numbers
may be -1 if the reponse was malformed.

::

	struct scrape_reply_alert: tracker_alert
	{
		// ...
		int incomplete;
		int complete;
	};


scrape_failed_alert
-------------------

If a scrape request fails, this alert is generated. This might be due
to the tracker timing out, refusing connection or returning an http response
code indicating an error. ``msg`` contains a message describing the error.

::

	struct scrape_failed_alert: tracker_alert
	{
		// ...
		std::string msg;
	};

url_seed_alert
--------------

This alert is generated when a HTTP seed name lookup fails.

It contains ``url`` to the HTTP seed that failed along with an error message.

::

	struct url_seed_alert: torrent_alert
	{
		// ...
		std::string url;
	};

   
hash_failed_alert
-----------------

This alert is generated when a finished piece fails its hash check. You can get the handle
to the torrent which got the failed piece and the index of the piece itself from the alert.

::

	struct hash_failed_alert: torrent_alert
	{
		// ...
		int piece_index;
	};


peer_alert
----------

The peer alert is a base class for alerts that refer to a specific peer. It includes all
the information to identify the peer. i.e. ``ip`` and ``peer-id``.

::

	struct peer_alert: torrent_alert
	{
		// ...
		tcp::endpoint ip;
		peer_id pid;
	};

peer_connect_alert
------------------

This alert is posted every time an outgoing peer connect attempts succeeds.

::

	struct peer_connect_alert: peer_alert
	{
		// ...
	};

peer_ban_alert
--------------

This alert is generated when a peer is banned because it has sent too many corrupt pieces
to us. ``ip`` is the endpoint to the peer that was banned.

::

	struct peer_ban_alert: peer_alert
	{
		// ...
	};


peer_snubbed_alert
------------------

This alert is generated when a peer is snubbed, when it stops sending data when we request
it.

::

	struct peer_snubbed_alert: peer_alert
	{
		// ...
	};


peer_unsnubbed_alert
--------------------

This alert is generated when a peer is unsnubbed. Essentially when it was snubbed for stalling
sending data, and now it started sending data again.

::

	struct peer_unsnubbed_alert: peer_alert
	{
		// ...
	};


peer_error_alert
----------------

This alert is generated when a peer sends invalid data over the peer-peer protocol. The peer
will be disconnected, but you get its ip address from the alert, to identify it.

The ``error_code`` tells you what error caused this alert.

::

	struct peer_error_alert: peer_alert
	{
		// ...
		error_code error;
	};


peer_connected_alert
--------------------

This alert is generated when a peer is connected.

::

	struct peer_connected_alert: peer_alert
	{
		// ...
	};


peer_disconnected_alert
-----------------------

This alert is generated when a peer is disconnected for any reason (other than the ones
covered by ``peer_error_alert``).

The ``error_code`` tells you what error caused peer to disconnect.

::

	struct peer_disconnected_alert: peer_alert
	{
		// ...
		error_code error;
	};


invalid_request_alert
---------------------

This is a debug alert that is generated by an incoming invalid piece request.
``ip`` is the address of the peer and the ``request`` is the actual incoming
request from the peer.

::

	struct invalid_request_alert: peer_alert
	{
		// ...
		peer_request request;
	};


	struct peer_request
	{
		int piece;
		int start;
		int length;
		bool operator==(peer_request const& r) const;
	};


The ``peer_request`` contains the values the client sent in its ``request`` message. ``piece`` is
the index of the piece it want data from, ``start`` is the offset within the piece where the data
should be read, and ``length`` is the amount of data it wants.

request_dropped_alert
---------------------

This alert is generated when a peer rejects or ignores a piece request.

::

	struct request_dropped_alert: peer_alert
	{
		// ...
		int block_index;
		int piece_index;
	};


block_timeout_alert
-------------------

This alert is generated when a block request times out.

::

	struct block_timeout_alert: peer_alert
	{
		// ...
		int block_index;
		int piece_index;
	};


block_finished_alert
--------------------

This alert is generated when a block request receives a response.

::

	struct block_finished_alert: peer_alert
	{
		// ...
		int block_index;
		int piece_index;
	};


lsd_peer_alert
--------------

This alert is generated when we receive a local service discovery message from a peer
for a torrent we're currently participating in.

::

	struct lsd_peer_alert: peer_alert
	{
		// ...
	};


file_completed_alert
--------------------

This is posted whenever an individual file completes its download. i.e.
All pieces overlapping this file have passed their hash check.

::

	struct file_completed_alert: torrent_alert
	{
		// ...
		int index;
	};

The ``index`` member refers to the index of the file that completed.


block_downloading_alert
-----------------------

This alert is generated when a block request is sent to a peer.

::

	struct block_downloading_alert: peer_alert
	{
		// ...
		int block_index;
		int piece_index;
	};


unwanted_block_alert
--------------------

This alert is generated when a block is received that was not requested or
whose request timed out.

::

	struct unwanted_block_alert: peer_alert
	{
		// ...
		int block_index;
		int piece_index;
	};


torrent_delete_failed_alert
---------------------------

This alert is generated when a request to delete the files of a torrent fails.

The ``error_code`` tells you why it failed.

::

	struct torrent_delete_failed_alert: torrent_alert
	{
		// ...
		error_code error;
	};

torrent_deleted_alert
---------------------

This alert is generated when a request to delete the files of a torrent complete.

The ``info_hash`` is the info-hash of the torrent that was just deleted. Most of
the time the torrent_handle in the ``torrent_alert`` will be invalid by the time
this alert arrives, since the torrent is being deleted. The ``info_hash`` member
is hence the main way of identifying which torrent just completed the delete.

This alert is posted in the ``storage_notification`` category, and that bit
needs to be set in the alert mask.

::

	struct torrent_deleted_alert: torrent_alert
	{
		// ...
		sha1_hash info_hash;
	};

torrent_finished_alert
----------------------

This alert is generated when a torrent switches from being a downloader to a seed.
It will only be generated once per torrent. It contains a torrent_handle to the
torrent in question.

There are no additional data members in this alert.


performance_alert
-----------------

This alert is generated when a limit is reached that might have a negative impact on
upload or download rate performance.

::

	struct performance_alert: torrent_alert
	{
		// ...

		enum performance_warning_t
		{
			outstanding_disk_buffer_limit_reached,
			outstanding_request_limit_reached,
			upload_limit_too_low,
			download_limit_too_low,
			send_buffer_watermark_too_low,
			too_many_optimistic_unchoke_slots,
			too_high_disk_queue_limit,
			too_few_outgoing_ports
		};

		performance_warning_t warning_code;
	};

outstanding_disk_buffer_limit_reached
	This warning means that the number of bytes queued to be written to disk
	exceeds the max disk byte queue setting (``settings_pack::max_queued_disk_bytes``).
	This might restrict the download rate, by not queuing up enough write jobs
	to the disk I/O thread. When this alert is posted, peer connections are
	temporarily stopped from downloading, until the queued disk bytes have fallen
	below the limit again. Unless your ``max_queued_disk_bytes`` setting is already
	high, you might want to increase it to get better performance.

outstanding_request_limit_reached
	This is posted when libtorrent would like to send more requests to a peer,
	but it's limited by max_out_request_queue_. The queue length
	libtorrent is trying to achieve is determined by the download rate and the
	assumed round-trip-time (request_queue_time_). The assumed
	rount-trip-time is not limited to just the network RTT, but also the remote disk
	access time and message handling time. It defaults to 3 seconds. The target number
	of outstanding requests is set to fill the bandwidth-delay product (assumed RTT
	times download rate divided by number of bytes per request). When this alert
	is posted, there is a risk that the number of outstanding requests is too low
	and limits the download rate. You might want to increase the ``max_out_request_queue``
	setting.

upload_limit_too_low
	This warning is posted when the amount of TCP/IP overhead is greater than the
	upload rate limit. When this happens, the TCP/IP overhead is caused by a much
	faster download rate, triggering TCP ACK packets. These packets eat into the
	rate limit specified to libtorrent. When the overhead traffic is greater than
	the rate limit, libtorrent will not be able to send any actual payload, such
	as piece requests. This means the download rate will suffer, and new requests
	can be sent again. There will be an equilibrium where the download rate, on
	average, is about 20 times the upload rate limit. If you want to maximize the
	download rate, increase the upload rate limit above 5% of your download capacity.

download_limit_too_low
	This is the same warning as ``upload_limit_too_low`` but referring to the download
	limit instead of upload. This suggests that your download rate limit is mcuh lower
	than your upload capacity. Your upload rate will suffer. To maximize upload rate,
	make sure your download rate limit is above 5% of your upload capacity.

send_buffer_watermark_too_low
	We're stalled on the disk. We want to write to the socket, and we can write
	but our send buffer is empty, waiting to be refilled from the disk.
	This either means the disk is slower than the network connection
	or that our send buffer watermark is too small, because we can
	send it all before the disk gets back to us.
	The number of bytes that we keep outstanding, requested from the disk, is calculated
	as follows::

		min(512, max(upload_rate * send_buffer_watermark_factor / 100, send_buffer_watermark))

	If you receive this alert, you migth want to either increase your ``send_buffer_watermark``
	or ``send_buffer_watermark_factor``.

too_many_optimistic_unchoke_slots
	If the half (or more) of all upload slots are set as optimistic unchoke slots, this
	warning is issued. You probably want more regular (rate based) unchoke slots.

too_high_disk_queue_limit
	If the disk write queue ever grows larger than half of the cache size, this warning
	is posted. The disk write queue eats into the total disk cache and leaves very little
	left for the actual cache. This causes the disk cache to oscillate in evicting large
	portions of the cache before allowing peers to download any more, onto the disk write
	queue. Either lower ``max_queued_disk_bytes`` or increase ``cache_size``.

too_few_outgoing_ports
	This is generated if outgoing peer connections are failing because of *address in use*
	errors, indicating that num_outgoing_ports_ is set and is too small of
	a range. Consider not using the num_outgoing_ports_ setting at all,
	or widen the range to include more ports.

state_changed_alert
-------------------

Generated whenever a torrent changes its state.

::	

	struct state_changed_alert: torrent_alert
	{
		// ...

		torrent_status::state_t state;
		torrent_status::state_t prev_state;
	};

``state`` is the new state of the torrent. ``prev_state`` is the previous state.


metadata_failed_alert
---------------------

This alert is generated when the metadata has been completely received and the info-hash
failed to match it. i.e. the metadata that was received was corrupt. libtorrent will
automatically retry to fetch it in this case. This is only relevant when running a
torrent-less download, with the metadata extension provided by libtorrent.

::	

	struct metadata_failed_alert: torrent_alert
	{
		// ...

		error_code error;
	};

The ``error`` member indicates what failed when parsing the metadata. This error is
what's returned from ``lazy_bdecode()``.


metadata_received_alert
-----------------------

This alert is generated when the metadata has been completely received and the torrent
can start downloading. It is not generated on torrents that are started with metadata, but
only those that needs to download it from peers (when utilizing the libtorrent extension).

There are no additional data members in this alert.

Typically, when receiving this alert, you would want to save the torrent file in order
to load it back up again when the session is restarted. Here's an example snippet of
code to do that::

	torrent_handle h = alert->handle();
	if (h.is_valid()) {
		boost::shared_ptr<torrent_info const> ti = h.torrent_file();
		create_torrent ct(*ti);
		entry te = ct.generate();
		std::vector<char> buffer;
		bencode(std::back_inserter(buffer), te);
		FILE* f = fopen((to_hex(ti->info_hash().to_string()) + ".torrent").c_str(), "wb+");
		if (f) {
			fwrite(&buffer[0], 1, buffer.size(), f);
			fclose(f);
		}
	}

fastresume_rejected_alert
-------------------------

This alert is generated when a fastresume file has been passed to ``add_torrent`` but the
files on disk did not match the fastresume file. The ``error_code`` explains the reason why the
resume file was rejected.

If the error happend to a specific file, ``file`` is the path to it. If the error happened
in a disk operation, ``operation`` is a NULL-terminated string of the name of that operation.
``operation`` is NULL otherwise.

::

	struct fastresume_rejected_alert: torrent_alert
	{
		// ...
		error_code error;
		std::string file;
		char const* operation;
	};


peer_blocked_alert
------------------

This alert is posted when an incoming peer connection, or a peer that's about to be added
to our peer list, is blocked for some reason. This could be any of:

* the IP filter
* i2p mixed mode restrictions (a normal peer is not allowed on an i2p swarm)
* the port filter
* the peer has a low port and ``no_connect_privileged_ports`` is enabled
* the protocol of the peer is blocked (uTP/TCP blocking)

The ``ip`` member is the address that was blocked.

::

	struct peer_blocked_alert: torrent_alert
	{
		// ...
		address ip;
	};


storage_moved_alert
-------------------

The ``storage_moved_alert`` is generated when all the disk IO has completed and the
files have been moved, as an effect of a call to ``torrent_handle::move_storage``. This
is useful to synchronize with the actual disk. The ``path`` member is the new path of
the storage.

::

	struct storage_moved_alert: torrent_alert
	{
		// ...
		std::string path;
	};


storage_moved_failed_alert
--------------------------

The ``storage_moved_failed_alert`` is generated when an attempt to move the storage
(via torrent_handle::move_storage()) fails.

If the error happened for a speific file, ``file`` is its path. If the error
happened in a specific disk operation, ``operation`` is a NULL terminated string
naming which one, otherwise it's NULL.

::

	struct storage_moved_failed_alert: torrent_alert
	{
		// ...
		error_code error;
		std::string file;
		char const* operation;
	};


torrent_paused_alert
--------------------

This alert is generated as a response to a ``torrent_handle::pause`` request. It is
generated once all disk IO is complete and the files in the torrent have been closed.
This is useful for synchronizing with the disk.

There are no additional data members in this alert.

torrent_resumed_alert
---------------------

This alert is generated as a response to a ``torrent_handle::resume`` request. It is
generated when a torrent goes from a paused state to an active state.

There are no additional data members in this alert.

save_resume_data_alert
----------------------

This alert is generated as a response to a ``torrent_handle::save_resume_data`` request.
It is generated once the disk IO thread is done writing the state for this torrent.
The ``resume_data`` member points to the resume data.

::

	struct save_resume_data_alert: torrent_alert
	{
		// ...
		boost::shared_ptr<entry> resume_data;
	};

save_resume_data_failed_alert
-----------------------------

This alert is generated instead of ``save_resume_data_alert`` if there was an error
generating the resume data. ``error`` describes what went wrong.

::

	struct save_resume_data_failed_alert: torrent_alert
	{
		// ...
		error_code error;
	};

stats_alert
-----------

This alert is posted approximately once every second, and it contains
byte counters of most statistics that's tracked for torrents. Each active
torrent posts these alerts regularly.

::

	struct stats_alert: torrent_alert
	{
		// ...
		enum stats_channel
		{
			upload_payload,
			upload_protocol,
			upload_ip_protocol,
			upload_dht_protocol,
			upload_tracker_protocol,
			download_payload,
			download_protocol,
			download_ip_protocol,
			download_dht_protocol,
			download_tracker_protocol,
			num_channels
		};

		int transferred[num_channels];
		int interval;
	};

``transferred`` this is an array of samples. The enum describes what each
sample is a measurement of. All of these are raw, and not smoothing is performed.

``interval`` the number of milliseconds during which these stats
were collected. This is typically just above 1000, but if CPU is
limited, it may be higher than that.


cache_flushed_alert
-------------------

This alert is posted when the disk cache has been flushed for a specific torrent
as a result of a call to `flush_cache()`_. This alert belongs to the
``storage_notification`` category, which must be enabled to let this alert through.
The alert is also posted when removing a torrent from the session, once the outstanding
cache flush is complete and the torrent does no longer have any files open.

::

	struct flush_cached_alert: torrent_alert
	{
		// ...
	};

torrent_need_cert_alert
-----------------------

This is always posted for SSL torrents. This is a reminder to the client that
the torrent won't work unless torrent_handle::set_ssl_certificate() is called with
a valid certificate. Valid certificates MUST be signed by the SSL certificate
in the .torrent file.

::

	struct torrent_need_cert_alert: tracker_alert
	{
		// ...
	};

dht_announce_alert
------------------

This alert is generated when a DHT node announces to an info-hash on our DHT node. It belongs
to the ``dht_notification`` category.

::

	struct dht_announce_alert: alert
	{
		// ...
		address ip;
		int port;
		sha1_hash info_hash;
	};

dht_get_peers_alert
-------------------

This alert is generated when a DHT node sends a ``get_peers`` message to our DHT node.
It belongs to the ``dht_notification`` category.

::

	struct dht_get_peers_alert: alert
	{
		// ...
		sha1_hash info_hash;
	};

dht_reply_alert
---------------

This alert is generated each time the DHT receives peers from a node. ``num_peers``
is the number of peers we received in this packet. Typically these packets are
received from multiple DHT nodes, and so the alerts are typically generated
a few at a time.

::

	struct dht_reply_alert: tracker_alert
	{
		// ...
		int num_peers;
	};

dht_bootstrap_alert
-------------------

This alert is posted when the initial DHT bootstrap is done. There's no any other
relevant information associated with this alert.

::
	
	struct dht_bootstrap_alert: alert
	{
		// ...
	};

anonymous_mode_alert
--------------------

This alert is posted when a bittorrent feature is blocked because of the
anonymous mode. For instance, if the tracker proxy is not set up, no
trackers will be used, because trackers can only be used through proxies
when in anonymous mode.

::

	struct anonymous_mode_alert: tracker_alert
	{
		// ...
		enum kind_t
		{
			tracker_not_anonymous = 1
		};
		int kind;
		std::string str;
	};

``kind`` specifies what error this is, it's one of:

``tracker_not_anonymous`` means that there's no proxy set up for tracker
communication and the tracker will not be contacted. The tracker which
this failed for is specified in the ``str`` member.

rss_alert
---------

This alert is posted on RSS feed events such as start of RSS feed updates,
successful completed updates and errors during updates.

This alert is only posted if the ``rss_notifications`` category is enabled
in the alert mask.

::

	struct rss_alert: alert
	{
		// ..
		virtual std::string message() const;

		enum state_t
		{
			state_updating, state_updated, state_error
		};

		feed_handle handle;
		std::string url;
		int state;
		error_code error;
	};

``handle`` is the handle to the feed which generated this alert.

``url`` is a short cut to access the url of the feed, without
having to call ``get_settings()``.

``state`` is one of:

``rss_alert::state_updating``
	An update of this feed was just initiated, it will either succeed
	or fail soon.

``rss_alert::state_updated``
	The feed just completed a successful update, there may be new items
	in it. If you're adding torrents manually, you may want to request
	the feed status of the feed and look through the ``items`` vector.

``rss_akert::state_error``
	An error just occurred. See the ``error`` field for information on
	what went wrong.

``error`` is an error code used for when an error occurs on the feed.

rss_item_alert
--------------

This alert is posted every time a new RSS item (i.e. torrent) is received
from an RSS feed.

It is only posted if the ``rss_notifications`` category is enabled in the
alert mask.

::

	struct rss_alert : alert
	{
		// ...
		virtual std::string message() const;

		feed_handle handle;
		feed_item item;
	};

incoming_connection_alert
-------------------------

The incoming connection alert is posted every time we successfully accept
an incoming connection, through any mean. The most straigh-forward ways
of accepting incoming connections are through the TCP listen socket and
the UDP listen socket for uTP sockets. However, connections may also be
accepted ofer a Socks5 or i2p listen socket, or via a torrent specific
listen socket for SSL torrents.

::

	struct incoming_connection_alert: alert
	{
		// ...
		virtual std::string message() const;

		int socket_type;
		tcp::endpoint ip;
	};

``socket_type`` tells you what kind of socket the connection was accepted
as:

+----------+-------------------------------------+
| value    | type                                |
+==========+=====================================+
| 0        | none (no socket instantiated)       |
+----------+-------------------------------------+
| 1        | TCP                                 |
+----------+-------------------------------------+
| 2        | Socks5                              |
+----------+-------------------------------------+
| 3        | HTTP                                |
+----------+-------------------------------------+
| 4        | uTP                                 |
+----------+-------------------------------------+
| 5        | i2p                                 |
+----------+-------------------------------------+
| 6        | SSL/TCP                             |
+----------+-------------------------------------+
| 7        | SSL/Socks5                          |
+----------+-------------------------------------+
| 8        | HTTPS (SSL/HTTP)                    |
+----------+-------------------------------------+
| 9        | SSL/uTP                             |
+----------+-------------------------------------+

``ip`` is the IP address and port the connection came from.

state_update_alert
------------------

This alert is only posted when requested by the user, by calling `post_torrent_updates()`_
on the session. It contains the torrent status of all torrents that changed
since last time this message was posted. Its category is ``status_notification``, but
it's not subject to filtering, since it's only manually posted anyway.

::

	
	struct state_update_alert: alert
	{
		// ...
		std::vector<torrent_status> status;
	};

``status`` contains the torrent status of all torrents that changed since last time
this message was posted. Note that you can map a torrent status to a specific torrent
via its ``handle`` member. The receiving end is suggested to have all torrents sorted
by the ``torrent_handle`` or hashed by it, for efficient updates.

session_stats_alert
-------------------

The session_stats_alert is posted when the user requests session statistics by
calling `post_session_stats()`_ on the session_ object. Its category is
``status_notification``, but it is not subject to filtering, since it's only
manually posted anyway.

The resulting alert contains::

	struct session_stats_alert: alert
	{
		// ...
		boost::uint64_t timestamp;
		std::vector<boost::uint64_t> values;
	};

The values in the ``values`` array are a mix of *counters* and *gauges*, which
meanings can be queries via the `session_stats_metrics()`_ function on the session.
The mapping from a specific metric to an index into this array is constant for a
specific version of libtorrent, but may differ for other versions. The intended
usage is to request the mapping (i.e. call `session_stats_metrics()`_) once
on startup, and then use that mapping to interpret these values throughout
the process' runtime.

The ``timestamp`` field is the number of microseconds since the session was
started. It represent the time when the snapshot of values was taken. When
the network thread is under heavy load, the latency between calling
`post_session_stats()`_ and receiving this alert may be significant, and
the timestamp may help provide higher accuracy in measurements.

For more information, see the `session statistics`_ section.


torrent_update_alert
--------------------

When a torrent changes its info-hash, this alert is posted. This only happens in very
specific cases. For instance, when a torrent is downloaded from a URL, the true info
hash is not known immediately. First the .torrent file must be downloaded and parsed.

Once this download completes, the ``torrent_update_alert`` is posted to notify the client
of the info-hash changing.

::

	struct torrent_update_alert: torrent_alert
	{
		// ...
		sha1_hash old_ih;
		sha1_hash new_ih;
	};

``old_ih`` and ``new_ih`` are the previous and new info-hash for the torrent, respectively.

alert dispatcher
================

The ``handle_alert`` class is defined in ``<libtorrent/alert.hpp>``.

Examples usage::

	struct my_handler
	{
		void operator()(portmap_error_alert const& a) const
		{
			std::cout << "Portmapper: " << a.msg << std::endl;
		}

		void operator()(tracker_warning_alert const& a) const
		{
			std::cout << "Tracker warning: " << a.msg << std::endl;
		}

		void operator()(torrent_finished_alert const& a) const
		{
			// write fast resume data
			// ...

			std::cout << a.handle.torrent_file()->name() << "completed"
				<< std::endl;
		}
	};

::

	std::auto_ptr<alert> a;
	a = ses.pop_alert();
	my_handler h;
	while (a.get())
	{
		handle_alert<portmap_error_alert
			, tracker_warning_alert
			, torrent_finished_alert
		>::handle_alert(h, a);
		a = ses.pop_alert();
	}

In this example 3 alert types are used. You can use any number of template
parameters to select between more types. If the number of types are more than
15, you can define ``TORRENT_MAX_ALERT_TYPES`` to a greater number before
including ``<libtorrent/alert.hpp>``.


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

	struct storage_params
	{
		file_storage const* files;
		file_storage const* mapped_files; // optional
		std::string path;
		file_pool* pool;
		storage_mode_t mode;
		std::vector<boost::uint8_t> const* priorities; // optional
		torrent_info const* info;
	};

	struct storage_interface
	{
		virtual void initialize(storage_error& ec) = 0;
		virtual int readv(file::iovec_t const* bufs, int num_bufs
			, int piece, int offset, int flags, storage_error& ec) = 0;
		virtual int writev(file::iovec_t const* bufs, int num_bufs
			, int piece, int offset, int flags, storage_error& ec) = 0;
		virtual bool has_any_file(storage_error& ec) = 0;
		virtual void move_storage(std::string const& save_path, storage_error& ec) = 0;
		virtual bool verify_resume_data(lazy_entry const& rd, storage_error& ec) = 0;
		virtual void write_resume_data(entry& rd, storage_error& ec) const = 0;
		virtual void release_files(storage_error& ec) = 0;
		virtual void rename_file(int index, std::string const& new_filenamem, storage_error& ec) = 0;
		virtual void delete_files(storage_error& ec) = 0;
		virtual void finalize_file(int, storage_error&);
		virtual ~storage_interface();

		// non virtual functions

		aux::session_settings const& settings() const { return *m_settings; }
	};

	struct storage_error
	{
		// the actual error code
		error_code ec;

		// the index of the file the error occurred on
		int32_t file:24;

		// the operation that failed
		int32_t operation:8;

		enum {
			none,
			stat,
			mkdir,
			open,
			rename,
			remove,
			copy,
			read,
			write,
			fallocate,
			alloc_cache_piece
		};

		storage_error();
		operator bool() const;

		// turn the operation ID into a string
		char const* operation_str() const;
	};

initialize()
------------

	::

		virtual void initialize(storage_error& ec) = 0;

This function is called when the storage is to be initialized. The default storage
will create directories and empty files at this point.

If an error occurs, ``storage_error`` should be set to reflect it.

has_any_file()
--------------

	::

		virtual bool has_any_file(storage_error& ec) = 0;

This function is called when first checking (or re-checking) the storage for a torrent.
It should return true if any of the files that is used in this storage exists on disk.
If so, the storage will be checked for existing pieces before starting the download.

If an error occurs, ``storage_error`` should be set to reflect it.

readv() writev()
----------------

	::

		virtual int readv(file::iovec_t const* bufs, int num_bufs
			, int piece, int offset, int flags, storage_error& ec) = 0;
		virtual int writev(file::iovec_t const* bufs, int num_bufs
			, int piece, int offset, int flags, storage_error& ec) = 0;

These functions should read and write the data in or to the given ``piece`` at
the given ``offset``. It should read or write ``num_bufs`` buffers sequentially,
where the size of each buffer is specified in the buffer array ``bufs``. The
file::iovec_t type has the following members::

	struct iovec_t
	{
		void* iov_base;
		size_t iov_len;
	};

These functions may be called simultaneously from multiple threads. Make sure they
are thread safe. The ``file`` in libtorrent is thread safe when it can fall back
to ``pread``, ``preadv`` or the windows equivalents. On targets where read operations
cannot be thread safe (i.e one has to seek first and then read), only one disk thread
is used.

Every buffer in ``bufs`` can be assumed to be page aligned and be of a page aligned size,
except for the last buffer of the torrent. The allocated buffer can be assumed to fit a
fully page aligned number of bytes though.

The ``offset`` is aligned to 16 kiB boundries  *most of the time*, but there are rare
exceptions when it's not. Specifically if the read cache is disabled/or full and a
peer requests unaligned data. Most clients request aligned data.

The number of bytes read or written should be returned, or -1 on error. If there's
an error, the ``storage_error`` must be filled out to represent the error that occurred.

move_storage()
--------------

	::

		void move_storage(std::string const& save_path, storage_error& ec) = 0;

This function should move all the files belonging to the storage to the new save_path.
The default storage moves the single file or the directory of the torrent.

Before moving the files, any open file handles may have to be closed, like
``release_files()``.

If an error occurs, ``storage_error`` should be set to reflect it.


verify_resume_data()
--------------------

	::

		bool verify_resume_data(lazy_entry const& rd, storage_error& error) = 0;

This function should verify the resume data ``rd`` with the files
on disk. If the resume data seems to be up-to-date, return true. If
not, set ``error`` to a description of what mismatched and return false.

The default storage may compare file sizes and time stamps of the files.

If an error occurs, ``storage_error`` should be set to reflect it.


write_resume_data()
-------------------

	::

		bool write_resume_data(entry& rd, storage_error& ec) const = 0;

This function should fill in resume data, the current state of the
storage, in ``rd``. The default storage adds file timestamps and
sizes.

Returning ``true`` indicates an error occurred.

If an error occurs, ``storage_error`` should be set to reflect it.


rename_file()
-------------

	::

		void rename_file(int file, std::string const& new_name, storage_error& ec) = 0;

Rename file with index ``file`` to the thame ``new_name``.

If an error occurs, ``storage_error`` should be set to reflect it.

release_files()
---------------

	::

		void release_files(storage_error& ec) = 0;

This function should release all the file handles that it keeps open to files
belonging to this storage. The default implementation just calls
``file_pool::release_files(this)``.

If an error occurs, ``storage_error`` should be set to reflect it.


delete_files()
--------------

	::

		void delete_files(storage_error& ec) = 0;

This function should delete all files and directories belonging to this storage.

If an error occurs, ``storage_error`` should be set to reflect it.

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

		virtual void finalize_file(int index, storage_error& ec);

This function is called each time a file is completely downloaded. The
storage implementation can perform last operations on a file. The file will
not be opened for writing after this.

``index`` is the index of the file that completed.

On windows the default storage implementation clears the sparse file flag
on the specified file.

If an error occurs, ``storage_error`` should be set to reflect it.

example
-------

This is an example storage implementation that stores all pieces in a ``std::map``,
i.e. in RAM. It's not necessarily very useful in practice, but illustrates the
basics of implementing a custom storage.

::

	struct temp_storage : storage_interface
	{
		temp_storage(file_storage const& fs) : m_files(fs) {}
		virtual bool initialize(storage_error& se) { return false; }
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
		virtual bool verify_resume_data(lazy_entry const& rd, storage_error& error) { return false; }
		virtual bool write_resume_data(entry& rd) const { return false; }
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

	storage_interface* temp_storage_constructor(storage_params const& params)
	{
		return new temp_storage(*params.files);
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
active_downloads_, active_seeds_ and active_limit_ settings.
These limits takes non auto managed torrents into account as well. If there are 
more non-auto managed torrents being downloaded than the active_downloads_
setting, any auto managed torrents will be queued until torrents are removed so 
that the number drops below the limit.

The default values are 8 active downloads and 5 active seeds.

At a regular interval, torrents are checked if there needs to be any re-ordering of
which torrents are active and which are queued. This interval can be controlled via
auto_manage_interval_ setting.

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

The relevant settings to control these limits are share_ratio_limit_,
seed_time_ratio_limit_ and seed_time_limit_.


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
	Support for compact allocation is deprecated in libttorrent, and will
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

.. note::
	This extension is deprecated in favor of the more widely supported
	``ut_metadata`` extension, see `BEP 9`_.

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

dynamic loading of torrent files
================================

libtorrent has a feature that can unload idle torrents from memory. The purpose
of this is to support being active on many more torrents than the RAM permits.
This is useful for both embedded devices that have limited RAM and servers
seeding tens of thousands of torrents.

The most significant parts of loaded torrents that use RAM are the piece
hashes (20 bytes per piece) and the file list. The entire info-dictionary
of the .torrent file is kept in RAM.

In order to activate the dynamic loading of torrent files, set the load
function on the session. See `set_load_function()`_.

When a load function is set on the session, the dynamic load/unload
feature is enabled. Torrents are kept in an LRU. Every time an operation
is performed, on a torrent or from a peer, that requires the metadata of
the torrent to be loaded, the torrent is bumped up in the LRU. When a torrent
is paused or queued, it is demoted to the least recently used torrent in
the LRU, since it's a good candidate for eviction.

To configure how many torrents are allowed to be loaded at the same time,
set ``settings_pack::active_loaded_limit`` on the session.

Torrents can be exempt from being unloaded by being *pinned*. Pinned torrents
still count against the limit, but are never considered for eviction.
You can either pin a torrent when adding it, in ``add_torrent_params``
(see `async_add_torrent() add_torrent()`_), or after ading it with the
`set_pinned()`_ function on torrent_handle_.

Torrents that start out without metadata (e.g. magnet links or http downloads)
are automatically pinned. This is important in order to give the client a
chance to save the metadata to disk once it's received (see metadata_received_alert_).

Once the metadata is saved to disk, it might make sense to unpin the torrent.

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
picker mode is determined by initial_picker_threshold_.

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
do this is or not is controlled by use_parole_mode_.

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
not to enable this is controlled by prioritize_partial_pieces_.

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

This threshold is controlled by the whole_pieces_threshold_ setting.

*TODO: piece affinity by speed category*
*TODO: piece priorities*

predictive piece announce
=========================

In order to improve performance, libtorrent supports a feature called
``predictive piece announce``. When enabled, it will make libtorrent announce
that we have pieces to peers, before we truly have them. The most important
case is to announce a piece as soon as it has been downloaded and passed
the hash check, but not yet been written to disk. In this case, there is
a risk the piece will fail to be written to disk, in which case we won't have
the piece anymore, even though we announced it to peers.

The other case is when we're very close to completing the download of a piece
and assume it will pass the hash check, we can announce it to peers to make
it available one round-trip sooner than otherwise. This lets libtorrent start
uploading the piece to interested peers immediately when the piece complete, instead
of waiting one round-trip for the peers to request it.

This makes for the implementation slightly more complicated, since piece will have
more states and more complicated transitions. For instance, a piece could be:

1. hashed but not fully written to disk
2. fully written to disk but not hashed
3. not fully downloaded
4. downloaded and hash checked

Once a piece is fully downloaded, the hash check could complete before any of
the write operations or it could complete after all write operations are complete.

peer classes
============

The peer classes feature in libtorrent allows a client to define custom groups of peers
and rate limit them individually. Each such group is called a *peer class*. There are a few
default peer classes that are always created:

* global - all peers belong to this class, except peers on the local network
* local peers - all peers on the local network belongs to this class
* TCP peers - all peers connected over TCP belong to this class

The TCP peers class is used by the uTP/TCP balancing logic, if it's enabled, to throttle TCP
peers. The global and local classes are used to adjust the global rate limits.

When the rate limits are adjusted for a specific torrent, a class is created implicitly for
that torrent.

The default peer class IDs are defined as enums in the ``session`` class::

	enum {
		global_peer_class_id,
		tcp_peer_class_id,
		local_peer_class_id
	};

A peer class can be considered a more general form of *lables* that some clients have. Peer
classes however are not just applied to torrents, but ultimately the peers.

Peer classes can be created with the `create_peer_class()`_ call (on the session object), and
deleted with the `delete_peer_class()`_ call.

Peer classes are configured with the `set_peer_class() get_peer_class()`_ calls.

Custom peer classes can be assigned to torrents, with the ??? call, in which case all its
peers will belong to the class. They can also be assigned based on the peer's IP address.
See `set_peer_class_filter()`_ for more information.

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
pick which port the SSL socket should bind to, set ssl_listen_ to a
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

session statistics
==================

libtorrent provides a mechanism to query performance and statistics counters from its
internals. This is primarily useful for troubleshooting of production systems and performance
tuning.

The statistics consists of two fundamental types. *counters* and *gauges*. A counter is a
monotonically increasing value, incremented every time some event occurs. For example,
every time the network thread wakes up because a socket became readable will increment a
counter. Another example is every time a socket receives *n* bytes, a counter is incremented
by *n*.

*Counters* are the most flexible of metrics. It allows the program to sample the counter at
any interval, and calculate average rates of increments to the counter. Some events may be
rare and need to be sampled over a longer period in order to get userful rates, where other
events may be more frequent and evenly distributed that sampling it frequently yields useful
values. Counters also provides accurate overall counts. For example, converting samples of
a download rate into a total transfer count is not accurate and takes more samples. Converting
an increasing counter into a rate is easy and flexible.

*Gauges* measure the instantaneous state of some kind. This is used for metrics that are not
counting events or flows, but states that can fluctuate. For example, the number of torrents
that are currenly being downloaded.

It's important to know whether a value is a counter or a gauge in order to interpret it correctly.
In order to query libtorrent for which counters and gauges are available, call
`session_stats_metrics()`_. This will return metadata about the values available for inspection
in libtorrent. It will include whether a value is a counter or a gauge. The key information
it includes is the index used to extract the actual measurements for a specific counter or
gauge.

In order to take a sample, call `post_session_stats()`_ in the session object. This will result
in a `session_stats_alert`_ being posted. In this alert object, there is an array of values,
these values make up the sample. The value index in the stats metric indicates which index the
metric's value is stored in.

The mapping between metric and value is not stable across versions of libtorrent. Always query
the metrics first, to find out the index at which the value is stored, before interpreting the
values array in the `session_stats_alert`_. The mapping will *not* change during the runtime of
your process though, it's tied to a specific libtorrent version. You only have to query the
mapping once on startup (or every time ``libtorrent.so`` is loaded, if it's done dynamically).

The available stats metrics are:

.. include:: stats_counters.rst

