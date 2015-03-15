.. _peer.error_peers:

.. _peer.disconnected_peers:

.. raw:: html

	<a name="peer.error_peers"></a>
	<a name="peer.disconnected_peers"></a>

+-------------------------+---------+
| name                    | type    |
+=========================+=========+
| peer.error_peers        | counter |
+-------------------------+---------+
| peer.disconnected_peers | counter |
+-------------------------+---------+


``error_peers`` is the total number of peer disconnects
caused by an error (not initiated by this client) and
disconnected initiated by this client (``disconnected_peers``).

.. _peer.eof_peers:

.. _peer.connreset_peers:

.. _peer.connrefused_peers:

.. _peer.connaborted_peers:

.. _peer.notconnected_peers:

.. _peer.perm_peers:

.. _peer.buffer_peers:

.. _peer.unreachable_peers:

.. _peer.broken_pipe_peers:

.. _peer.addrinuse_peers:

.. _peer.no_access_peers:

.. _peer.invalid_arg_peers:

.. _peer.aborted_peers:

.. raw:: html

	<a name="peer.eof_peers"></a>
	<a name="peer.connreset_peers"></a>
	<a name="peer.connrefused_peers"></a>
	<a name="peer.connaborted_peers"></a>
	<a name="peer.notconnected_peers"></a>
	<a name="peer.perm_peers"></a>
	<a name="peer.buffer_peers"></a>
	<a name="peer.unreachable_peers"></a>
	<a name="peer.broken_pipe_peers"></a>
	<a name="peer.addrinuse_peers"></a>
	<a name="peer.no_access_peers"></a>
	<a name="peer.invalid_arg_peers"></a>
	<a name="peer.aborted_peers"></a>

+-------------------------+---------+
| name                    | type    |
+=========================+=========+
| peer.eof_peers          | counter |
+-------------------------+---------+
| peer.connreset_peers    | counter |
+-------------------------+---------+
| peer.connrefused_peers  | counter |
+-------------------------+---------+
| peer.connaborted_peers  | counter |
+-------------------------+---------+
| peer.notconnected_peers | counter |
+-------------------------+---------+
| peer.perm_peers         | counter |
+-------------------------+---------+
| peer.buffer_peers       | counter |
+-------------------------+---------+
| peer.unreachable_peers  | counter |
+-------------------------+---------+
| peer.broken_pipe_peers  | counter |
+-------------------------+---------+
| peer.addrinuse_peers    | counter |
+-------------------------+---------+
| peer.no_access_peers    | counter |
+-------------------------+---------+
| peer.invalid_arg_peers  | counter |
+-------------------------+---------+
| peer.aborted_peers      | counter |
+-------------------------+---------+


these counters break down the peer errors into more specific
categories. These errors are what the underlying transport
reported (i.e. TCP or uTP)

.. _peer.piece_requests:

.. _peer.max_piece_requests:

.. _peer.invalid_piece_requests:

.. _peer.choked_piece_requests:

.. _peer.cancelled_piece_requests:

.. _peer.piece_rejects:

.. raw:: html

	<a name="peer.piece_requests"></a>
	<a name="peer.max_piece_requests"></a>
	<a name="peer.invalid_piece_requests"></a>
	<a name="peer.choked_piece_requests"></a>
	<a name="peer.cancelled_piece_requests"></a>
	<a name="peer.piece_rejects"></a>

+-------------------------------+---------+
| name                          | type    |
+===============================+=========+
| peer.piece_requests           | counter |
+-------------------------------+---------+
| peer.max_piece_requests       | counter |
+-------------------------------+---------+
| peer.invalid_piece_requests   | counter |
+-------------------------------+---------+
| peer.choked_piece_requests    | counter |
+-------------------------------+---------+
| peer.cancelled_piece_requests | counter |
+-------------------------------+---------+
| peer.piece_rejects            | counter |
+-------------------------------+---------+


the total number of incoming piece requests we've received followed
by the number of rejected piece requests for various reasons.
max_piece_requests mean we already had too many outstanding requests
from this peer, so we rejected it. cancelled_piece_requests are ones
where the other end explicitly asked for the piece to be rejected.

.. _peer.error_incoming_peers:

.. _peer.error_outgoing_peers:

.. raw:: html

	<a name="peer.error_incoming_peers"></a>
	<a name="peer.error_outgoing_peers"></a>

+---------------------------+---------+
| name                      | type    |
+===========================+=========+
| peer.error_incoming_peers | counter |
+---------------------------+---------+
| peer.error_outgoing_peers | counter |
+---------------------------+---------+


these counters break down the peer errors into
whether they happen on incoming or outgoing peers.

.. _peer.error_rc4_peers:

.. _peer.error_encrypted_peers:

.. raw:: html

	<a name="peer.error_rc4_peers"></a>
	<a name="peer.error_encrypted_peers"></a>

+----------------------------+---------+
| name                       | type    |
+============================+=========+
| peer.error_rc4_peers       | counter |
+----------------------------+---------+
| peer.error_encrypted_peers | counter |
+----------------------------+---------+


these counters break down the peer errors into
whether they happen on encrypted peers (just
encrypted handshake) and rc4 peers (full stream
encryption). These can indicate whether encrypted
peers are more or less likely to fail

.. _peer.error_tcp_peers:

.. _peer.error_utp_peers:

.. raw:: html

	<a name="peer.error_tcp_peers"></a>
	<a name="peer.error_utp_peers"></a>

+----------------------+---------+
| name                 | type    |
+======================+=========+
| peer.error_tcp_peers | counter |
+----------------------+---------+
| peer.error_utp_peers | counter |
+----------------------+---------+


these counters break down the peer errors into
whether they happen on uTP peers or TCP peers.
these may indicate whether one protocol is
more error prone

.. _peer.connect_timeouts:

.. _peer.uninteresting_peers:

.. _peer.timeout_peers:

.. _peer.no_memory_peers:

.. _peer.too_many_peers:

.. _peer.transport_timeout_peers:

.. _peer.num_banned_peers:

.. _peer.banned_for_hash_failure:

.. _peer.connection_attempts:

.. _peer.connection_attempt_loops:

.. _peer.incoming_connections:

.. raw:: html

	<a name="peer.connect_timeouts"></a>
	<a name="peer.uninteresting_peers"></a>
	<a name="peer.timeout_peers"></a>
	<a name="peer.no_memory_peers"></a>
	<a name="peer.too_many_peers"></a>
	<a name="peer.transport_timeout_peers"></a>
	<a name="peer.num_banned_peers"></a>
	<a name="peer.banned_for_hash_failure"></a>
	<a name="peer.connection_attempts"></a>
	<a name="peer.connection_attempt_loops"></a>
	<a name="peer.incoming_connections"></a>

+-------------------------------+---------+
| name                          | type    |
+===============================+=========+
| peer.connect_timeouts         | counter |
+-------------------------------+---------+
| peer.uninteresting_peers      | counter |
+-------------------------------+---------+
| peer.timeout_peers            | counter |
+-------------------------------+---------+
| peer.no_memory_peers          | counter |
+-------------------------------+---------+
| peer.too_many_peers           | counter |
+-------------------------------+---------+
| peer.transport_timeout_peers  | counter |
+-------------------------------+---------+
| peer.num_banned_peers         | counter |
+-------------------------------+---------+
| peer.banned_for_hash_failure  | counter |
+-------------------------------+---------+
| peer.connection_attempts      | counter |
+-------------------------------+---------+
| peer.connection_attempt_loops | counter |
+-------------------------------+---------+
| peer.incoming_connections     | counter |
+-------------------------------+---------+


these counters break down the reasons to
disconnect peers.

.. _peer.num_tcp_peers:

.. _peer.num_socks5_peers:

.. _peer.num_http_proxy_peers:

.. _peer.num_utp_peers:

.. _peer.num_i2p_peers:

.. _peer.num_ssl_peers:

.. _peer.num_ssl_socks5_peers:

.. _peer.num_ssl_http_proxy_peers:

.. _peer.num_ssl_utp_peers:

.. _peer.num_peers_half_open:

.. _peer.num_peers_connected:

.. _peer.num_peers_up_interested:

.. _peer.num_peers_down_interested:

.. _peer.num_peers_up_unchoked_all:

.. _peer.num_peers_up_unchoked_optimistic:

.. _peer.num_peers_up_unchoked:

.. _peer.num_peers_down_unchoked:

.. _peer.num_peers_up_requests:

.. _peer.num_peers_down_requests:

.. _peer.num_peers_end_game:

.. _peer.num_peers_up_disk:

.. _peer.num_peers_down_disk:

.. raw:: html

	<a name="peer.num_tcp_peers"></a>
	<a name="peer.num_socks5_peers"></a>
	<a name="peer.num_http_proxy_peers"></a>
	<a name="peer.num_utp_peers"></a>
	<a name="peer.num_i2p_peers"></a>
	<a name="peer.num_ssl_peers"></a>
	<a name="peer.num_ssl_socks5_peers"></a>
	<a name="peer.num_ssl_http_proxy_peers"></a>
	<a name="peer.num_ssl_utp_peers"></a>
	<a name="peer.num_peers_half_open"></a>
	<a name="peer.num_peers_connected"></a>
	<a name="peer.num_peers_up_interested"></a>
	<a name="peer.num_peers_down_interested"></a>
	<a name="peer.num_peers_up_unchoked_all"></a>
	<a name="peer.num_peers_up_unchoked_optimistic"></a>
	<a name="peer.num_peers_up_unchoked"></a>
	<a name="peer.num_peers_down_unchoked"></a>
	<a name="peer.num_peers_up_requests"></a>
	<a name="peer.num_peers_down_requests"></a>
	<a name="peer.num_peers_end_game"></a>
	<a name="peer.num_peers_up_disk"></a>
	<a name="peer.num_peers_down_disk"></a>

+---------------------------------------+-------+
| name                                  | type  |
+=======================================+=======+
| peer.num_tcp_peers                    | gauge |
+---------------------------------------+-------+
| peer.num_socks5_peers                 | gauge |
+---------------------------------------+-------+
| peer.num_http_proxy_peers             | gauge |
+---------------------------------------+-------+
| peer.num_utp_peers                    | gauge |
+---------------------------------------+-------+
| peer.num_i2p_peers                    | gauge |
+---------------------------------------+-------+
| peer.num_ssl_peers                    | gauge |
+---------------------------------------+-------+
| peer.num_ssl_socks5_peers             | gauge |
+---------------------------------------+-------+
| peer.num_ssl_http_proxy_peers         | gauge |
+---------------------------------------+-------+
| peer.num_ssl_utp_peers                | gauge |
+---------------------------------------+-------+
| peer.num_peers_half_open              | gauge |
+---------------------------------------+-------+
| peer.num_peers_connected              | gauge |
+---------------------------------------+-------+
| peer.num_peers_up_interested          | gauge |
+---------------------------------------+-------+
| peer.num_peers_down_interested        | gauge |
+---------------------------------------+-------+
| peer.num_peers_up_unchoked_all        | gauge |
+---------------------------------------+-------+
| peer.num_peers_up_unchoked_optimistic | gauge |
+---------------------------------------+-------+
| peer.num_peers_up_unchoked            | gauge |
+---------------------------------------+-------+
| peer.num_peers_down_unchoked          | gauge |
+---------------------------------------+-------+
| peer.num_peers_up_requests            | gauge |
+---------------------------------------+-------+
| peer.num_peers_down_requests          | gauge |
+---------------------------------------+-------+
| peer.num_peers_end_game               | gauge |
+---------------------------------------+-------+
| peer.num_peers_up_disk                | gauge |
+---------------------------------------+-------+
| peer.num_peers_down_disk              | gauge |
+---------------------------------------+-------+


the number of peer connections for each kind of socket.
these counts include half-open (connecting) peers.
``num_peers_up_unchoked_all`` is the total number of unchoked peers,
whereas ``num_peers_up_unchoked`` only are unchoked peers that count
against the limit (i.e. excluding peers that are unchoked because the
limit doesn't apply to them). ``num_peers_up_unchoked_optimistic`` is
the number of optimistically unchoked peers.

.. _net.on_read_counter:

.. _net.on_write_counter:

.. _net.on_tick_counter:

.. _net.on_lsd_counter:

.. _net.on_lsd_peer_counter:

.. _net.on_udp_counter:

.. _net.on_accept_counter:

.. _net.on_disk_counter:

.. raw:: html

	<a name="net.on_read_counter"></a>
	<a name="net.on_write_counter"></a>
	<a name="net.on_tick_counter"></a>
	<a name="net.on_lsd_counter"></a>
	<a name="net.on_lsd_peer_counter"></a>
	<a name="net.on_udp_counter"></a>
	<a name="net.on_accept_counter"></a>
	<a name="net.on_disk_counter"></a>

+-------------------------+---------+
| name                    | type    |
+=========================+=========+
| net.on_read_counter     | counter |
+-------------------------+---------+
| net.on_write_counter    | counter |
+-------------------------+---------+
| net.on_tick_counter     | counter |
+-------------------------+---------+
| net.on_lsd_counter      | counter |
+-------------------------+---------+
| net.on_lsd_peer_counter | counter |
+-------------------------+---------+
| net.on_udp_counter      | counter |
+-------------------------+---------+
| net.on_accept_counter   | counter |
+-------------------------+---------+
| net.on_disk_counter     | counter |
+-------------------------+---------+


These counters count the number of times the
network thread wakes up for each respective
reason. If these counters are very large, it
may indicate a performance issue, causing the
network thread to wake up too ofte, wasting CPU.
mitigate it by increasing buffers and limits
for the specific trigger that wakes up the
thread.

.. _net.sent_payload_bytes:

.. _net.sent_bytes:

.. _net.sent_ip_overhead_bytes:

.. _net.sent_tracker_bytes:

.. _net.recv_payload_bytes:

.. _net.recv_bytes:

.. _net.recv_ip_overhead_bytes:

.. _net.recv_tracker_bytes:

.. raw:: html

	<a name="net.sent_payload_bytes"></a>
	<a name="net.sent_bytes"></a>
	<a name="net.sent_ip_overhead_bytes"></a>
	<a name="net.sent_tracker_bytes"></a>
	<a name="net.recv_payload_bytes"></a>
	<a name="net.recv_bytes"></a>
	<a name="net.recv_ip_overhead_bytes"></a>
	<a name="net.recv_tracker_bytes"></a>

+----------------------------+---------+
| name                       | type    |
+============================+=========+
| net.sent_payload_bytes     | counter |
+----------------------------+---------+
| net.sent_bytes             | counter |
+----------------------------+---------+
| net.sent_ip_overhead_bytes | counter |
+----------------------------+---------+
| net.sent_tracker_bytes     | counter |
+----------------------------+---------+
| net.recv_payload_bytes     | counter |
+----------------------------+---------+
| net.recv_bytes             | counter |
+----------------------------+---------+
| net.recv_ip_overhead_bytes | counter |
+----------------------------+---------+
| net.recv_tracker_bytes     | counter |
+----------------------------+---------+


total number of bytes sent and received by the session

.. _net.limiter_up_queue:

.. _net.limiter_down_queue:

.. raw:: html

	<a name="net.limiter_up_queue"></a>
	<a name="net.limiter_down_queue"></a>

+------------------------+-------+
| name                   | type  |
+========================+=======+
| net.limiter_up_queue   | gauge |
+------------------------+-------+
| net.limiter_down_queue | gauge |
+------------------------+-------+


the number of sockets currently waiting for upload and download
bandwidht from the rate limiter.

.. _net.limiter_up_bytes:

.. _net.limiter_down_bytes:

.. raw:: html

	<a name="net.limiter_up_bytes"></a>
	<a name="net.limiter_down_bytes"></a>

+------------------------+-------+
| name                   | type  |
+========================+=======+
| net.limiter_up_bytes   | gauge |
+------------------------+-------+
| net.limiter_down_bytes | gauge |
+------------------------+-------+


the number of upload and download bytes waiting to be handed out from
the rate limiter.

.. _net.recv_failed_bytes:

.. raw:: html

	<a name="net.recv_failed_bytes"></a>

+-----------------------+---------+
| name                  | type    |
+=======================+=========+
| net.recv_failed_bytes | counter |
+-----------------------+---------+


the number of bytes downloaded that had to be discarded because they
failed the hash check

.. _net.recv_redundant_bytes:

.. raw:: html

	<a name="net.recv_redundant_bytes"></a>

+--------------------------+---------+
| name                     | type    |
+==========================+=========+
| net.recv_redundant_bytes | counter |
+--------------------------+---------+


the number of downloaded bytes that were discarded because they
were downloaded multiple times (from different peers)

.. _net.has_incoming_connections:

.. raw:: html

	<a name="net.has_incoming_connections"></a>

+------------------------------+-------+
| name                         | type  |
+==============================+=======+
| net.has_incoming_connections | gauge |
+------------------------------+-------+


is false by default and set to true when
the first incoming connection is established
this is used to know if the client is behind
NAT or not.

.. _ses.num_checking_torrents:

.. _ses.num_stopped_torrents:

.. _ses.num_upload_only_torrents:

.. _ses.num_downloading_torrents:

.. _ses.num_seeding_torrents:

.. _ses.num_queued_seeding_torrents:

.. _ses.num_queued_download_torrents:

.. _ses.num_error_torrents:

.. raw:: html

	<a name="ses.num_checking_torrents"></a>
	<a name="ses.num_stopped_torrents"></a>
	<a name="ses.num_upload_only_torrents"></a>
	<a name="ses.num_downloading_torrents"></a>
	<a name="ses.num_seeding_torrents"></a>
	<a name="ses.num_queued_seeding_torrents"></a>
	<a name="ses.num_queued_download_torrents"></a>
	<a name="ses.num_error_torrents"></a>

+----------------------------------+-------+
| name                             | type  |
+==================================+=======+
| ses.num_checking_torrents        | gauge |
+----------------------------------+-------+
| ses.num_stopped_torrents         | gauge |
+----------------------------------+-------+
| ses.num_upload_only_torrents     | gauge |
+----------------------------------+-------+
| ses.num_downloading_torrents     | gauge |
+----------------------------------+-------+
| ses.num_seeding_torrents         | gauge |
+----------------------------------+-------+
| ses.num_queued_seeding_torrents  | gauge |
+----------------------------------+-------+
| ses.num_queued_download_torrents | gauge |
+----------------------------------+-------+
| ses.num_error_torrents           | gauge |
+----------------------------------+-------+


these gauges count the number of torrents in
different states. Each torrent only belongs to
one of these states. For torrents that could
belong to multiple of these, the most prominent
in picked. For instance, a torrent with an error
counts as an error-torrent, regardless of its other
state.

.. _ses.num_loaded_torrents:

.. _ses.num_pinned_torrents:

.. raw:: html

	<a name="ses.num_loaded_torrents"></a>
	<a name="ses.num_pinned_torrents"></a>

+-------------------------+-------+
| name                    | type  |
+=========================+=======+
| ses.num_loaded_torrents | gauge |
+-------------------------+-------+
| ses.num_pinned_torrents | gauge |
+-------------------------+-------+


the number of torrents that are currently loaded

.. _ses.num_piece_passed:

.. _ses.num_piece_failed:

.. _ses.num_have_pieces:

.. _ses.num_total_pieces_added:

.. raw:: html

	<a name="ses.num_piece_passed"></a>
	<a name="ses.num_piece_failed"></a>
	<a name="ses.num_have_pieces"></a>
	<a name="ses.num_total_pieces_added"></a>

+----------------------------+---------+
| name                       | type    |
+============================+=========+
| ses.num_piece_passed       | counter |
+----------------------------+---------+
| ses.num_piece_failed       | counter |
+----------------------------+---------+
| ses.num_have_pieces        | counter |
+----------------------------+---------+
| ses.num_total_pieces_added | counter |
+----------------------------+---------+


these count the number of times a piece has passed the
hash check, the number of times a piece was successfully
written to disk and the number of total possible pieces
added by adding torrents. e.g. when adding a torrent with
1000 piece, num_total_pieces_added is incremented by 1000.

.. _ses.torrent_evicted_counter:

.. raw:: html

	<a name="ses.torrent_evicted_counter"></a>

+-----------------------------+---------+
| name                        | type    |
+=============================+=========+
| ses.torrent_evicted_counter | counter |
+-----------------------------+---------+


this counts the number of times a torrent has been
evicted (only applies when `dynamic loading of torrent files`_
is enabled).

.. _ses.num_unchoke_slots:

.. raw:: html

	<a name="ses.num_unchoke_slots"></a>

+-----------------------+-------+
| name                  | type  |
+=======================+=======+
| ses.num_unchoke_slots | gauge |
+-----------------------+-------+


the number of allowed unchoked peers

.. _ses.num_incoming_choke:

.. _ses.num_incoming_unchoke:

.. _ses.num_incoming_interested:

.. _ses.num_incoming_not_interested:

.. _ses.num_incoming_have:

.. _ses.num_incoming_bitfield:

.. _ses.num_incoming_request:

.. _ses.num_incoming_piece:

.. _ses.num_incoming_cancel:

.. _ses.num_incoming_dht_port:

.. _ses.num_incoming_suggest:

.. _ses.num_incoming_have_all:

.. _ses.num_incoming_have_none:

.. _ses.num_incoming_reject:

.. _ses.num_incoming_allowed_fast:

.. _ses.num_incoming_ext_handshake:

.. _ses.num_incoming_pex:

.. _ses.num_incoming_metadata:

.. _ses.num_incoming_extended:

.. _ses.num_outgoing_choke:

.. _ses.num_outgoing_unchoke:

.. _ses.num_outgoing_interested:

.. _ses.num_outgoing_not_interested:

.. _ses.num_outgoing_have:

.. _ses.num_outgoing_bitfield:

.. _ses.num_outgoing_request:

.. _ses.num_outgoing_piece:

.. _ses.num_outgoing_cancel:

.. _ses.num_outgoing_dht_port:

.. _ses.num_outgoing_suggest:

.. _ses.num_outgoing_have_all:

.. _ses.num_outgoing_have_none:

.. _ses.num_outgoing_reject:

.. _ses.num_outgoing_allowed_fast:

.. _ses.num_outgoing_ext_handshake:

.. _ses.num_outgoing_pex:

.. _ses.num_outgoing_metadata:

.. _ses.num_outgoing_extended:

.. raw:: html

	<a name="ses.num_incoming_choke"></a>
	<a name="ses.num_incoming_unchoke"></a>
	<a name="ses.num_incoming_interested"></a>
	<a name="ses.num_incoming_not_interested"></a>
	<a name="ses.num_incoming_have"></a>
	<a name="ses.num_incoming_bitfield"></a>
	<a name="ses.num_incoming_request"></a>
	<a name="ses.num_incoming_piece"></a>
	<a name="ses.num_incoming_cancel"></a>
	<a name="ses.num_incoming_dht_port"></a>
	<a name="ses.num_incoming_suggest"></a>
	<a name="ses.num_incoming_have_all"></a>
	<a name="ses.num_incoming_have_none"></a>
	<a name="ses.num_incoming_reject"></a>
	<a name="ses.num_incoming_allowed_fast"></a>
	<a name="ses.num_incoming_ext_handshake"></a>
	<a name="ses.num_incoming_pex"></a>
	<a name="ses.num_incoming_metadata"></a>
	<a name="ses.num_incoming_extended"></a>
	<a name="ses.num_outgoing_choke"></a>
	<a name="ses.num_outgoing_unchoke"></a>
	<a name="ses.num_outgoing_interested"></a>
	<a name="ses.num_outgoing_not_interested"></a>
	<a name="ses.num_outgoing_have"></a>
	<a name="ses.num_outgoing_bitfield"></a>
	<a name="ses.num_outgoing_request"></a>
	<a name="ses.num_outgoing_piece"></a>
	<a name="ses.num_outgoing_cancel"></a>
	<a name="ses.num_outgoing_dht_port"></a>
	<a name="ses.num_outgoing_suggest"></a>
	<a name="ses.num_outgoing_have_all"></a>
	<a name="ses.num_outgoing_have_none"></a>
	<a name="ses.num_outgoing_reject"></a>
	<a name="ses.num_outgoing_allowed_fast"></a>
	<a name="ses.num_outgoing_ext_handshake"></a>
	<a name="ses.num_outgoing_pex"></a>
	<a name="ses.num_outgoing_metadata"></a>
	<a name="ses.num_outgoing_extended"></a>

+---------------------------------+---------+
| name                            | type    |
+=================================+=========+
| ses.num_incoming_choke          | counter |
+---------------------------------+---------+
| ses.num_incoming_unchoke        | counter |
+---------------------------------+---------+
| ses.num_incoming_interested     | counter |
+---------------------------------+---------+
| ses.num_incoming_not_interested | counter |
+---------------------------------+---------+
| ses.num_incoming_have           | counter |
+---------------------------------+---------+
| ses.num_incoming_bitfield       | counter |
+---------------------------------+---------+
| ses.num_incoming_request        | counter |
+---------------------------------+---------+
| ses.num_incoming_piece          | counter |
+---------------------------------+---------+
| ses.num_incoming_cancel         | counter |
+---------------------------------+---------+
| ses.num_incoming_dht_port       | counter |
+---------------------------------+---------+
| ses.num_incoming_suggest        | counter |
+---------------------------------+---------+
| ses.num_incoming_have_all       | counter |
+---------------------------------+---------+
| ses.num_incoming_have_none      | counter |
+---------------------------------+---------+
| ses.num_incoming_reject         | counter |
+---------------------------------+---------+
| ses.num_incoming_allowed_fast   | counter |
+---------------------------------+---------+
| ses.num_incoming_ext_handshake  | counter |
+---------------------------------+---------+
| ses.num_incoming_pex            | counter |
+---------------------------------+---------+
| ses.num_incoming_metadata       | counter |
+---------------------------------+---------+
| ses.num_incoming_extended       | counter |
+---------------------------------+---------+
| ses.num_outgoing_choke          | counter |
+---------------------------------+---------+
| ses.num_outgoing_unchoke        | counter |
+---------------------------------+---------+
| ses.num_outgoing_interested     | counter |
+---------------------------------+---------+
| ses.num_outgoing_not_interested | counter |
+---------------------------------+---------+
| ses.num_outgoing_have           | counter |
+---------------------------------+---------+
| ses.num_outgoing_bitfield       | counter |
+---------------------------------+---------+
| ses.num_outgoing_request        | counter |
+---------------------------------+---------+
| ses.num_outgoing_piece          | counter |
+---------------------------------+---------+
| ses.num_outgoing_cancel         | counter |
+---------------------------------+---------+
| ses.num_outgoing_dht_port       | counter |
+---------------------------------+---------+
| ses.num_outgoing_suggest        | counter |
+---------------------------------+---------+
| ses.num_outgoing_have_all       | counter |
+---------------------------------+---------+
| ses.num_outgoing_have_none      | counter |
+---------------------------------+---------+
| ses.num_outgoing_reject         | counter |
+---------------------------------+---------+
| ses.num_outgoing_allowed_fast   | counter |
+---------------------------------+---------+
| ses.num_outgoing_ext_handshake  | counter |
+---------------------------------+---------+
| ses.num_outgoing_pex            | counter |
+---------------------------------+---------+
| ses.num_outgoing_metadata       | counter |
+---------------------------------+---------+
| ses.num_outgoing_extended       | counter |
+---------------------------------+---------+


bittorrent message counters. These counters are incremented
every time a message of the corresponding type is received from
or sent to a bittorrent peer.

.. _ses.waste_piece_timed_out:

.. _ses.waste_piece_cancelled:

.. _ses.waste_piece_unknown:

.. _ses.waste_piece_seed:

.. _ses.waste_piece_end_game:

.. _ses.waste_piece_closing:

.. raw:: html

	<a name="ses.waste_piece_timed_out"></a>
	<a name="ses.waste_piece_cancelled"></a>
	<a name="ses.waste_piece_unknown"></a>
	<a name="ses.waste_piece_seed"></a>
	<a name="ses.waste_piece_end_game"></a>
	<a name="ses.waste_piece_closing"></a>

+---------------------------+---------+
| name                      | type    |
+===========================+=========+
| ses.waste_piece_timed_out | counter |
+---------------------------+---------+
| ses.waste_piece_cancelled | counter |
+---------------------------+---------+
| ses.waste_piece_unknown   | counter |
+---------------------------+---------+
| ses.waste_piece_seed      | counter |
+---------------------------+---------+
| ses.waste_piece_end_game  | counter |
+---------------------------+---------+
| ses.waste_piece_closing   | counter |
+---------------------------+---------+


the number of wasted downloaded bytes by reason of the bytes being
wasted.

.. _picker.piece_picker_partial_loops:

.. _picker.piece_picker_suggest_loops:

.. _picker.piece_picker_sequential_loops:

.. _picker.piece_picker_reverse_rare_loops:

.. _picker.piece_picker_rare_loops:

.. _picker.piece_picker_rand_start_loops:

.. _picker.piece_picker_rand_loops:

.. _picker.piece_picker_busy_loops:

.. raw:: html

	<a name="picker.piece_picker_partial_loops"></a>
	<a name="picker.piece_picker_suggest_loops"></a>
	<a name="picker.piece_picker_sequential_loops"></a>
	<a name="picker.piece_picker_reverse_rare_loops"></a>
	<a name="picker.piece_picker_rare_loops"></a>
	<a name="picker.piece_picker_rand_start_loops"></a>
	<a name="picker.piece_picker_rand_loops"></a>
	<a name="picker.piece_picker_busy_loops"></a>

+----------------------------------------+---------+
| name                                   | type    |
+========================================+=========+
| picker.piece_picker_partial_loops      | counter |
+----------------------------------------+---------+
| picker.piece_picker_suggest_loops      | counter |
+----------------------------------------+---------+
| picker.piece_picker_sequential_loops   | counter |
+----------------------------------------+---------+
| picker.piece_picker_reverse_rare_loops | counter |
+----------------------------------------+---------+
| picker.piece_picker_rare_loops         | counter |
+----------------------------------------+---------+
| picker.piece_picker_rand_start_loops   | counter |
+----------------------------------------+---------+
| picker.piece_picker_rand_loops         | counter |
+----------------------------------------+---------+
| picker.piece_picker_busy_loops         | counter |
+----------------------------------------+---------+


the number of pieces considered while picking pieces

.. _picker.reject_piece_picks:

.. _picker.unchoke_piece_picks:

.. _picker.incoming_redundant_piece_picks:

.. _picker.incoming_piece_picks:

.. _picker.end_game_piece_picks:

.. _picker.snubbed_piece_picks:

.. _picker.interesting_piece_picks:

.. _picker.hash_fail_piece_picks:

.. _disk.write_cache_blocks:

.. _disk.read_cache_blocks:

.. raw:: html

	<a name="picker.reject_piece_picks"></a>
	<a name="picker.unchoke_piece_picks"></a>
	<a name="picker.incoming_redundant_piece_picks"></a>
	<a name="picker.incoming_piece_picks"></a>
	<a name="picker.end_game_piece_picks"></a>
	<a name="picker.snubbed_piece_picks"></a>
	<a name="picker.interesting_piece_picks"></a>
	<a name="picker.hash_fail_piece_picks"></a>
	<a name="disk.write_cache_blocks"></a>
	<a name="disk.read_cache_blocks"></a>

+---------------------------------------+---------+
| name                                  | type    |
+=======================================+=========+
| picker.reject_piece_picks             | counter |
+---------------------------------------+---------+
| picker.unchoke_piece_picks            | counter |
+---------------------------------------+---------+
| picker.incoming_redundant_piece_picks | counter |
+---------------------------------------+---------+
| picker.incoming_piece_picks           | counter |
+---------------------------------------+---------+
| picker.end_game_piece_picks           | counter |
+---------------------------------------+---------+
| picker.snubbed_piece_picks            | counter |
+---------------------------------------+---------+
| picker.interesting_piece_picks        | counter |
+---------------------------------------+---------+
| picker.hash_fail_piece_picks          | counter |
+---------------------------------------+---------+
| disk.write_cache_blocks               | gauge   |
+---------------------------------------+---------+
| disk.read_cache_blocks                | gauge   |
+---------------------------------------+---------+


This breaks down the piece picks into the event that
triggered it

.. _disk.request_latency:

.. _disk.pinned_blocks:

.. _disk.disk_blocks_in_use:

.. _disk.queued_disk_jobs:

.. _disk.num_running_disk_jobs:

.. _disk.num_read_jobs:

.. _disk.num_write_jobs:

.. _disk.num_jobs:

.. _disk.num_writing_threads:

.. _disk.num_running_threads:

.. _disk.blocked_disk_jobs:

.. raw:: html

	<a name="disk.request_latency"></a>
	<a name="disk.pinned_blocks"></a>
	<a name="disk.disk_blocks_in_use"></a>
	<a name="disk.queued_disk_jobs"></a>
	<a name="disk.num_running_disk_jobs"></a>
	<a name="disk.num_read_jobs"></a>
	<a name="disk.num_write_jobs"></a>
	<a name="disk.num_jobs"></a>
	<a name="disk.num_writing_threads"></a>
	<a name="disk.num_running_threads"></a>
	<a name="disk.blocked_disk_jobs"></a>

+----------------------------+-------+
| name                       | type  |
+============================+=======+
| disk.request_latency       | gauge |
+----------------------------+-------+
| disk.pinned_blocks         | gauge |
+----------------------------+-------+
| disk.disk_blocks_in_use    | gauge |
+----------------------------+-------+
| disk.queued_disk_jobs      | gauge |
+----------------------------+-------+
| disk.num_running_disk_jobs | gauge |
+----------------------------+-------+
| disk.num_read_jobs         | gauge |
+----------------------------+-------+
| disk.num_write_jobs        | gauge |
+----------------------------+-------+
| disk.num_jobs              | gauge |
+----------------------------+-------+
| disk.num_writing_threads   | gauge |
+----------------------------+-------+
| disk.num_running_threads   | gauge |
+----------------------------+-------+
| disk.blocked_disk_jobs     | gauge |
+----------------------------+-------+


the number of microseconds it takes from receiving a request from a
peer until we're sending the response back on the socket.

.. _disk.queued_write_bytes:

.. _disk.arc_mru_size:

.. _disk.arc_mru_ghost_size:

.. _disk.arc_mfu_size:

.. _disk.arc_mfu_ghost_size:

.. _disk.arc_write_size:

.. _disk.arc_volatile_size:

.. raw:: html

	<a name="disk.queued_write_bytes"></a>
	<a name="disk.arc_mru_size"></a>
	<a name="disk.arc_mru_ghost_size"></a>
	<a name="disk.arc_mfu_size"></a>
	<a name="disk.arc_mfu_ghost_size"></a>
	<a name="disk.arc_write_size"></a>
	<a name="disk.arc_volatile_size"></a>

+-------------------------+-------+
| name                    | type  |
+=========================+=======+
| disk.queued_write_bytes | gauge |
+-------------------------+-------+
| disk.arc_mru_size       | gauge |
+-------------------------+-------+
| disk.arc_mru_ghost_size | gauge |
+-------------------------+-------+
| disk.arc_mfu_size       | gauge |
+-------------------------+-------+
| disk.arc_mfu_ghost_size | gauge |
+-------------------------+-------+
| disk.arc_write_size     | gauge |
+-------------------------+-------+
| disk.arc_volatile_size  | gauge |
+-------------------------+-------+


the number of bytes we have sent to the disk I/O
thread for writing. Every time we hear back from
the disk I/O thread with a completed write job, this
is updated to the number of bytes the disk I/O thread
is actually waiting for to be written (as opposed to
bytes just hanging out in the cache)

.. _disk.num_blocks_written:

.. _disk.num_blocks_read:

.. raw:: html

	<a name="disk.num_blocks_written"></a>
	<a name="disk.num_blocks_read"></a>

+-------------------------+---------+
| name                    | type    |
+=========================+=========+
| disk.num_blocks_written | counter |
+-------------------------+---------+
| disk.num_blocks_read    | counter |
+-------------------------+---------+


the number of blocks written and read from disk in total. A block is
16 kiB.

.. _disk.num_blocks_hashed:

.. raw:: html

	<a name="disk.num_blocks_hashed"></a>

+------------------------+---------+
| name                   | type    |
+========================+=========+
| disk.num_blocks_hashed | counter |
+------------------------+---------+


the total number of blocks run through SHA-1 hashing

.. _disk.num_blocks_cache_hits:

.. raw:: html

	<a name="disk.num_blocks_cache_hits"></a>

+----------------------------+---------+
| name                       | type    |
+============================+=========+
| disk.num_blocks_cache_hits | counter |
+----------------------------+---------+


the number of blocks read from the disk cache

.. _disk.num_write_ops:

.. _disk.num_read_ops:

.. raw:: html

	<a name="disk.num_write_ops"></a>
	<a name="disk.num_read_ops"></a>

+--------------------+---------+
| name               | type    |
+====================+=========+
| disk.num_write_ops | counter |
+--------------------+---------+
| disk.num_read_ops  | counter |
+--------------------+---------+


the number of disk I/O operation for reads and writes. One disk
operation may transfer more then one block.

.. _disk.num_read_back:

.. raw:: html

	<a name="disk.num_read_back"></a>

+--------------------+---------+
| name               | type    |
+====================+=========+
| disk.num_read_back | counter |
+--------------------+---------+


the number of blocks that had to be read back from disk in order to
hash a piece (when verifying against the piece hash)

.. _disk.disk_read_time:

.. _disk.disk_write_time:

.. _disk.disk_hash_time:

.. _disk.disk_job_time:

.. raw:: html

	<a name="disk.disk_read_time"></a>
	<a name="disk.disk_write_time"></a>
	<a name="disk.disk_hash_time"></a>
	<a name="disk.disk_job_time"></a>

+----------------------+---------+
| name                 | type    |
+======================+=========+
| disk.disk_read_time  | counter |
+----------------------+---------+
| disk.disk_write_time | counter |
+----------------------+---------+
| disk.disk_hash_time  | counter |
+----------------------+---------+
| disk.disk_job_time   | counter |
+----------------------+---------+


cumulative time spent in various disk jobs, as well
as total for all disk jobs. Measured in microseconds

.. _disk.num_fenced_read:

.. _disk.num_fenced_write:

.. _disk.num_fenced_hash:

.. _disk.num_fenced_move_storage:

.. _disk.num_fenced_release_files:

.. _disk.num_fenced_delete_files:

.. _disk.num_fenced_check_fastresume:

.. _disk.num_fenced_save_resume_data:

.. _disk.num_fenced_rename_file:

.. _disk.num_fenced_stop_torrent:

.. _disk.num_fenced_cache_piece:

.. _disk.num_fenced_flush_piece:

.. _disk.num_fenced_flush_hashed:

.. _disk.num_fenced_flush_storage:

.. _disk.num_fenced_trim_cache:

.. _disk.num_fenced_file_priority:

.. _disk.num_fenced_load_torrent:

.. _disk.num_fenced_clear_piece:

.. _disk.num_fenced_tick_storage:

.. raw:: html

	<a name="disk.num_fenced_read"></a>
	<a name="disk.num_fenced_write"></a>
	<a name="disk.num_fenced_hash"></a>
	<a name="disk.num_fenced_move_storage"></a>
	<a name="disk.num_fenced_release_files"></a>
	<a name="disk.num_fenced_delete_files"></a>
	<a name="disk.num_fenced_check_fastresume"></a>
	<a name="disk.num_fenced_save_resume_data"></a>
	<a name="disk.num_fenced_rename_file"></a>
	<a name="disk.num_fenced_stop_torrent"></a>
	<a name="disk.num_fenced_cache_piece"></a>
	<a name="disk.num_fenced_flush_piece"></a>
	<a name="disk.num_fenced_flush_hashed"></a>
	<a name="disk.num_fenced_flush_storage"></a>
	<a name="disk.num_fenced_trim_cache"></a>
	<a name="disk.num_fenced_file_priority"></a>
	<a name="disk.num_fenced_load_torrent"></a>
	<a name="disk.num_fenced_clear_piece"></a>
	<a name="disk.num_fenced_tick_storage"></a>

+----------------------------------+-------+
| name                             | type  |
+==================================+=======+
| disk.num_fenced_read             | gauge |
+----------------------------------+-------+
| disk.num_fenced_write            | gauge |
+----------------------------------+-------+
| disk.num_fenced_hash             | gauge |
+----------------------------------+-------+
| disk.num_fenced_move_storage     | gauge |
+----------------------------------+-------+
| disk.num_fenced_release_files    | gauge |
+----------------------------------+-------+
| disk.num_fenced_delete_files     | gauge |
+----------------------------------+-------+
| disk.num_fenced_check_fastresume | gauge |
+----------------------------------+-------+
| disk.num_fenced_save_resume_data | gauge |
+----------------------------------+-------+
| disk.num_fenced_rename_file      | gauge |
+----------------------------------+-------+
| disk.num_fenced_stop_torrent     | gauge |
+----------------------------------+-------+
| disk.num_fenced_cache_piece      | gauge |
+----------------------------------+-------+
| disk.num_fenced_flush_piece      | gauge |
+----------------------------------+-------+
| disk.num_fenced_flush_hashed     | gauge |
+----------------------------------+-------+
| disk.num_fenced_flush_storage    | gauge |
+----------------------------------+-------+
| disk.num_fenced_trim_cache       | gauge |
+----------------------------------+-------+
| disk.num_fenced_file_priority    | gauge |
+----------------------------------+-------+
| disk.num_fenced_load_torrent     | gauge |
+----------------------------------+-------+
| disk.num_fenced_clear_piece      | gauge |
+----------------------------------+-------+
| disk.num_fenced_tick_storage     | gauge |
+----------------------------------+-------+


for each kind of disk job, a counter of how many jobs of that kind
are currently blocked by a disk fence

.. _dht.dht_nodes:

.. raw:: html

	<a name="dht.dht_nodes"></a>

+---------------+-------+
| name          | type  |
+===============+=======+
| dht.dht_nodes | gauge |
+---------------+-------+


The number of nodes in the DHT routing table

.. _dht.dht_node_cache:

.. raw:: html

	<a name="dht.dht_node_cache"></a>

+--------------------+-------+
| name               | type  |
+====================+=======+
| dht.dht_node_cache | gauge |
+--------------------+-------+


The number of replacement nodes in the DHT routing table

.. _dht.dht_torrents:

.. raw:: html

	<a name="dht.dht_torrents"></a>

+------------------+-------+
| name             | type  |
+==================+=======+
| dht.dht_torrents | gauge |
+------------------+-------+


the number of torrents currently tracked by our DHT node

.. _dht.dht_peers:

.. raw:: html

	<a name="dht.dht_peers"></a>

+---------------+-------+
| name          | type  |
+===============+=======+
| dht.dht_peers | gauge |
+---------------+-------+


the number of peers currently tracked by our DHT node

.. _dht.dht_immutable_data:

.. raw:: html

	<a name="dht.dht_immutable_data"></a>

+------------------------+-------+
| name                   | type  |
+========================+=======+
| dht.dht_immutable_data | gauge |
+------------------------+-------+


the number of immutable data items tracked by our DHT node

.. _dht.dht_mutable_data:

.. raw:: html

	<a name="dht.dht_mutable_data"></a>

+----------------------+-------+
| name                 | type  |
+======================+=======+
| dht.dht_mutable_data | gauge |
+----------------------+-------+


the number of mutable data items tracked by our DHT node

.. _dht.dht_allocated_observers:

.. raw:: html

	<a name="dht.dht_allocated_observers"></a>

+-----------------------------+-------+
| name                        | type  |
+=============================+=======+
| dht.dht_allocated_observers | gauge |
+-----------------------------+-------+


the number of RPC observers currently allocated

.. _dht.dht_messages_in:

.. _dht.dht_messages_out:

.. raw:: html

	<a name="dht.dht_messages_in"></a>
	<a name="dht.dht_messages_out"></a>

+----------------------+---------+
| name                 | type    |
+======================+=========+
| dht.dht_messages_in  | counter |
+----------------------+---------+
| dht.dht_messages_out | counter |
+----------------------+---------+


the total number of DHT messages sent and received

.. _dht.dht_messages_out_dropped:

.. raw:: html

	<a name="dht.dht_messages_out_dropped"></a>

+------------------------------+---------+
| name                         | type    |
+==============================+=========+
| dht.dht_messages_out_dropped | counter |
+------------------------------+---------+


the number of outgoing messages that failed to be
sent

.. _dht.dht_bytes_in:

.. _dht.dht_bytes_out:

.. raw:: html

	<a name="dht.dht_bytes_in"></a>
	<a name="dht.dht_bytes_out"></a>

+-------------------+---------+
| name              | type    |
+===================+=========+
| dht.dht_bytes_in  | counter |
+-------------------+---------+
| dht.dht_bytes_out | counter |
+-------------------+---------+


the total number of bytes sent and received by the DHT

.. _dht.dht_ping_in:

.. _dht.dht_ping_out:

.. _dht.dht_find_node_in:

.. _dht.dht_find_node_out:

.. _dht.dht_get_peers_in:

.. _dht.dht_get_peers_out:

.. _dht.dht_announce_peer_in:

.. _dht.dht_announce_peer_out:

.. _dht.dht_get_in:

.. _dht.dht_get_out:

.. _dht.dht_put_in:

.. _dht.dht_put_out:

.. raw:: html

	<a name="dht.dht_ping_in"></a>
	<a name="dht.dht_ping_out"></a>
	<a name="dht.dht_find_node_in"></a>
	<a name="dht.dht_find_node_out"></a>
	<a name="dht.dht_get_peers_in"></a>
	<a name="dht.dht_get_peers_out"></a>
	<a name="dht.dht_announce_peer_in"></a>
	<a name="dht.dht_announce_peer_out"></a>
	<a name="dht.dht_get_in"></a>
	<a name="dht.dht_get_out"></a>
	<a name="dht.dht_put_in"></a>
	<a name="dht.dht_put_out"></a>

+---------------------------+---------+
| name                      | type    |
+===========================+=========+
| dht.dht_ping_in           | counter |
+---------------------------+---------+
| dht.dht_ping_out          | counter |
+---------------------------+---------+
| dht.dht_find_node_in      | counter |
+---------------------------+---------+
| dht.dht_find_node_out     | counter |
+---------------------------+---------+
| dht.dht_get_peers_in      | counter |
+---------------------------+---------+
| dht.dht_get_peers_out     | counter |
+---------------------------+---------+
| dht.dht_announce_peer_in  | counter |
+---------------------------+---------+
| dht.dht_announce_peer_out | counter |
+---------------------------+---------+
| dht.dht_get_in            | counter |
+---------------------------+---------+
| dht.dht_get_out           | counter |
+---------------------------+---------+
| dht.dht_put_in            | counter |
+---------------------------+---------+
| dht.dht_put_out           | counter |
+---------------------------+---------+


the number of DHT messages we've sent and received
by kind.

.. _dht.dht_invalid_announce:

.. _dht.dht_invalid_get_peers:

.. _dht.dht_invalid_put:

.. _dht.dht_invalid_get:

.. raw:: html

	<a name="dht.dht_invalid_announce"></a>
	<a name="dht.dht_invalid_get_peers"></a>
	<a name="dht.dht_invalid_put"></a>
	<a name="dht.dht_invalid_get"></a>

+---------------------------+---------+
| name                      | type    |
+===========================+=========+
| dht.dht_invalid_announce  | counter |
+---------------------------+---------+
| dht.dht_invalid_get_peers | counter |
+---------------------------+---------+
| dht.dht_invalid_put       | counter |
+---------------------------+---------+
| dht.dht_invalid_get       | counter |
+---------------------------+---------+


the number of failed incoming DHT requests by kind of request

.. _utp.utp_packet_loss:

.. _utp.utp_timeout:

.. _utp.utp_packets_in:

.. _utp.utp_packets_out:

.. _utp.utp_fast_retransmit:

.. _utp.utp_packet_resend:

.. _utp.utp_samples_above_target:

.. _utp.utp_samples_below_target:

.. _utp.utp_payload_pkts_in:

.. _utp.utp_payload_pkts_out:

.. _utp.utp_invalid_pkts_in:

.. _utp.utp_redundant_pkts_in:

.. raw:: html

	<a name="utp.utp_packet_loss"></a>
	<a name="utp.utp_timeout"></a>
	<a name="utp.utp_packets_in"></a>
	<a name="utp.utp_packets_out"></a>
	<a name="utp.utp_fast_retransmit"></a>
	<a name="utp.utp_packet_resend"></a>
	<a name="utp.utp_samples_above_target"></a>
	<a name="utp.utp_samples_below_target"></a>
	<a name="utp.utp_payload_pkts_in"></a>
	<a name="utp.utp_payload_pkts_out"></a>
	<a name="utp.utp_invalid_pkts_in"></a>
	<a name="utp.utp_redundant_pkts_in"></a>

+------------------------------+---------+
| name                         | type    |
+==============================+=========+
| utp.utp_packet_loss          | counter |
+------------------------------+---------+
| utp.utp_timeout              | counter |
+------------------------------+---------+
| utp.utp_packets_in           | counter |
+------------------------------+---------+
| utp.utp_packets_out          | counter |
+------------------------------+---------+
| utp.utp_fast_retransmit      | counter |
+------------------------------+---------+
| utp.utp_packet_resend        | counter |
+------------------------------+---------+
| utp.utp_samples_above_target | counter |
+------------------------------+---------+
| utp.utp_samples_below_target | counter |
+------------------------------+---------+
| utp.utp_payload_pkts_in      | counter |
+------------------------------+---------+
| utp.utp_payload_pkts_out     | counter |
+------------------------------+---------+
| utp.utp_invalid_pkts_in      | counter |
+------------------------------+---------+
| utp.utp_redundant_pkts_in    | counter |
+------------------------------+---------+


uTP counters. Each counter represents the number of time each event
has occurred.

.. _utp.num_utp_idle:

.. _utp.num_utp_syn_sent:

.. _utp.num_utp_connected:

.. _utp.num_utp_fin_sent:

.. _utp.num_utp_close_wait:

.. raw:: html

	<a name="utp.num_utp_idle"></a>
	<a name="utp.num_utp_syn_sent"></a>
	<a name="utp.num_utp_connected"></a>
	<a name="utp.num_utp_fin_sent"></a>
	<a name="utp.num_utp_close_wait"></a>

+------------------------+-------+
| name                   | type  |
+========================+=======+
| utp.num_utp_idle       | gauge |
+------------------------+-------+
| utp.num_utp_syn_sent   | gauge |
+------------------------+-------+
| utp.num_utp_connected  | gauge |
+------------------------+-------+
| utp.num_utp_fin_sent   | gauge |
+------------------------+-------+
| utp.num_utp_close_wait | gauge |
+------------------------+-------+


the number of uTP sockets in each respective state

.. _sock_bufs.socket_send_size3:

.. _sock_bufs.socket_send_size4:

.. _sock_bufs.socket_send_size5:

.. _sock_bufs.socket_send_size6:

.. _sock_bufs.socket_send_size7:

.. _sock_bufs.socket_send_size8:

.. _sock_bufs.socket_send_size9:

.. _sock_bufs.socket_send_size10:

.. _sock_bufs.socket_send_size11:

.. _sock_bufs.socket_send_size12:

.. _sock_bufs.socket_send_size13:

.. _sock_bufs.socket_send_size14:

.. _sock_bufs.socket_send_size15:

.. _sock_bufs.socket_send_size16:

.. _sock_bufs.socket_send_size17:

.. _sock_bufs.socket_send_size18:

.. _sock_bufs.socket_send_size19:

.. _sock_bufs.socket_send_size20:

.. _sock_bufs.socket_recv_size3:

.. _sock_bufs.socket_recv_size4:

.. _sock_bufs.socket_recv_size5:

.. _sock_bufs.socket_recv_size6:

.. _sock_bufs.socket_recv_size7:

.. _sock_bufs.socket_recv_size8:

.. _sock_bufs.socket_recv_size9:

.. _sock_bufs.socket_recv_size10:

.. _sock_bufs.socket_recv_size11:

.. _sock_bufs.socket_recv_size12:

.. _sock_bufs.socket_recv_size13:

.. _sock_bufs.socket_recv_size14:

.. _sock_bufs.socket_recv_size15:

.. _sock_bufs.socket_recv_size16:

.. _sock_bufs.socket_recv_size17:

.. _sock_bufs.socket_recv_size18:

.. _sock_bufs.socket_recv_size19:

.. _sock_bufs.socket_recv_size20:

.. raw:: html

	<a name="sock_bufs.socket_send_size3"></a>
	<a name="sock_bufs.socket_send_size4"></a>
	<a name="sock_bufs.socket_send_size5"></a>
	<a name="sock_bufs.socket_send_size6"></a>
	<a name="sock_bufs.socket_send_size7"></a>
	<a name="sock_bufs.socket_send_size8"></a>
	<a name="sock_bufs.socket_send_size9"></a>
	<a name="sock_bufs.socket_send_size10"></a>
	<a name="sock_bufs.socket_send_size11"></a>
	<a name="sock_bufs.socket_send_size12"></a>
	<a name="sock_bufs.socket_send_size13"></a>
	<a name="sock_bufs.socket_send_size14"></a>
	<a name="sock_bufs.socket_send_size15"></a>
	<a name="sock_bufs.socket_send_size16"></a>
	<a name="sock_bufs.socket_send_size17"></a>
	<a name="sock_bufs.socket_send_size18"></a>
	<a name="sock_bufs.socket_send_size19"></a>
	<a name="sock_bufs.socket_send_size20"></a>
	<a name="sock_bufs.socket_recv_size3"></a>
	<a name="sock_bufs.socket_recv_size4"></a>
	<a name="sock_bufs.socket_recv_size5"></a>
	<a name="sock_bufs.socket_recv_size6"></a>
	<a name="sock_bufs.socket_recv_size7"></a>
	<a name="sock_bufs.socket_recv_size8"></a>
	<a name="sock_bufs.socket_recv_size9"></a>
	<a name="sock_bufs.socket_recv_size10"></a>
	<a name="sock_bufs.socket_recv_size11"></a>
	<a name="sock_bufs.socket_recv_size12"></a>
	<a name="sock_bufs.socket_recv_size13"></a>
	<a name="sock_bufs.socket_recv_size14"></a>
	<a name="sock_bufs.socket_recv_size15"></a>
	<a name="sock_bufs.socket_recv_size16"></a>
	<a name="sock_bufs.socket_recv_size17"></a>
	<a name="sock_bufs.socket_recv_size18"></a>
	<a name="sock_bufs.socket_recv_size19"></a>
	<a name="sock_bufs.socket_recv_size20"></a>

+------------------------------+---------+
| name                         | type    |
+==============================+=========+
| sock_bufs.socket_send_size3  | counter |
+------------------------------+---------+
| sock_bufs.socket_send_size4  | counter |
+------------------------------+---------+
| sock_bufs.socket_send_size5  | counter |
+------------------------------+---------+
| sock_bufs.socket_send_size6  | counter |
+------------------------------+---------+
| sock_bufs.socket_send_size7  | counter |
+------------------------------+---------+
| sock_bufs.socket_send_size8  | counter |
+------------------------------+---------+
| sock_bufs.socket_send_size9  | counter |
+------------------------------+---------+
| sock_bufs.socket_send_size10 | counter |
+------------------------------+---------+
| sock_bufs.socket_send_size11 | counter |
+------------------------------+---------+
| sock_bufs.socket_send_size12 | counter |
+------------------------------+---------+
| sock_bufs.socket_send_size13 | counter |
+------------------------------+---------+
| sock_bufs.socket_send_size14 | counter |
+------------------------------+---------+
| sock_bufs.socket_send_size15 | counter |
+------------------------------+---------+
| sock_bufs.socket_send_size16 | counter |
+------------------------------+---------+
| sock_bufs.socket_send_size17 | counter |
+------------------------------+---------+
| sock_bufs.socket_send_size18 | counter |
+------------------------------+---------+
| sock_bufs.socket_send_size19 | counter |
+------------------------------+---------+
| sock_bufs.socket_send_size20 | counter |
+------------------------------+---------+
| sock_bufs.socket_recv_size3  | counter |
+------------------------------+---------+
| sock_bufs.socket_recv_size4  | counter |
+------------------------------+---------+
| sock_bufs.socket_recv_size5  | counter |
+------------------------------+---------+
| sock_bufs.socket_recv_size6  | counter |
+------------------------------+---------+
| sock_bufs.socket_recv_size7  | counter |
+------------------------------+---------+
| sock_bufs.socket_recv_size8  | counter |
+------------------------------+---------+
| sock_bufs.socket_recv_size9  | counter |
+------------------------------+---------+
| sock_bufs.socket_recv_size10 | counter |
+------------------------------+---------+
| sock_bufs.socket_recv_size11 | counter |
+------------------------------+---------+
| sock_bufs.socket_recv_size12 | counter |
+------------------------------+---------+
| sock_bufs.socket_recv_size13 | counter |
+------------------------------+---------+
| sock_bufs.socket_recv_size14 | counter |
+------------------------------+---------+
| sock_bufs.socket_recv_size15 | counter |
+------------------------------+---------+
| sock_bufs.socket_recv_size16 | counter |
+------------------------------+---------+
| sock_bufs.socket_recv_size17 | counter |
+------------------------------+---------+
| sock_bufs.socket_recv_size18 | counter |
+------------------------------+---------+
| sock_bufs.socket_recv_size19 | counter |
+------------------------------+---------+
| sock_bufs.socket_recv_size20 | counter |
+------------------------------+---------+


the buffer sizes accepted by
socket send and receive calls respectively.
The larger the buffers are, the more efficient,
because it reqire fewer system calls per byte.
The size is 1 << n, where n is the number
at the end of the counter name. i.e.
8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192,
16384, 32768, 65536, 131072, 262144, 524288, 1048576
bytes

