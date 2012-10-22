.. _peer.error_peers:

.. _peer.disconnected_peers:

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

.. _peer.perm_peers:

.. _peer.buffer_peers:

.. _peer.unreachable_peers:

.. _peer.broken_pipe_peers:

.. _peer.addrinuse_peers:

.. _peer.no_access_peers:

.. _peer.invalid_arg_peers:

.. _peer.aborted_peers:

+------------------------+---------+
| name                   | type    |
+========================+=========+
| peer.eof_peers         | counter |
+------------------------+---------+
| peer.connreset_peers   | counter |
+------------------------+---------+
| peer.connrefused_peers | counter |
+------------------------+---------+
| peer.connaborted_peers | counter |
+------------------------+---------+
| peer.perm_peers        | counter |
+------------------------+---------+
| peer.buffer_peers      | counter |
+------------------------+---------+
| peer.unreachable_peers | counter |
+------------------------+---------+
| peer.broken_pipe_peers | counter |
+------------------------+---------+
| peer.addrinuse_peers   | counter |
+------------------------+---------+
| peer.no_access_peers   | counter |
+------------------------+---------+
| peer.invalid_arg_peers | counter |
+------------------------+---------+
| peer.aborted_peers     | counter |
+------------------------+---------+

these counters break down the peer errors into more specific
categories. These errors are what the underlying transport
reported (i.e. TCP or uTP)

.. _peer.error_incoming_peers:

.. _peer.error_outgoing_peers:

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

.. _peer.connection_attempts:

.. _peer.banned_for_hash_failure:

+------------------------------+---------+
| name                         | type    |
+==============================+=========+
| peer.connect_timeouts        | counter |
+------------------------------+---------+
| peer.uninteresting_peers     | counter |
+------------------------------+---------+
| peer.timeout_peers           | counter |
+------------------------------+---------+
| peer.no_memory_peers         | counter |
+------------------------------+---------+
| peer.too_many_peers          | counter |
+------------------------------+---------+
| peer.transport_timeout_peers | counter |
+------------------------------+---------+
| peer.num_banned_peers        | counter |
+------------------------------+---------+
| peer.connection_attempts     | counter |
+------------------------------+---------+
| peer.banned_for_hash_failure | counter |
+------------------------------+---------+

these counters break down the reasons to
disconnect peers.

.. _net.on_read_counter:

.. _net.on_write_counter:

.. _net.on_tick_counter:

.. _net.on_lsd_counter:

.. _net.on_lsd_peer_counter:

.. _net.on_udp_counter:

.. _net.on_accept_counter:

.. _net.on_disk_counter:

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

.. _ses.num_checking_torrents:

.. _ses.num_stopped_torrents:

.. _ses.num_upload_only_torrents:

.. _ses.num_downloading_torrents:

.. _ses.num_seeding_torrents:

.. _ses.num_queued_seeding_torrents:

.. _ses.num_queued_download_torrents:

.. _ses.num_error_torrents:

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

.. _ses.torrent_evicted_counter:

+-----------------------------+---------+
| name                        | type    |
+=============================+=========+
| ses.torrent_evicted_counter | counter |
+-----------------------------+---------+

this counts the number of times a torrent has been
evicted (only applies when `dynamic loading of torrent files`_
is enabled).

.. _picker.piece_picks:

+--------------------+---------+
| name               | type    |
+====================+=========+
| picker.piece_picks | counter |
+--------------------+---------+

counts the number of times the piece picker has been invoked

.. _picker.piece_picker_loops:

+---------------------------+---------+
| name                      | type    |
+===========================+=========+
| picker.piece_picker_loops | counter |
+---------------------------+---------+

the number of pieces considered while picking pieces

.. _picker.end_game_piece_picker_blocks:

.. _picker.piece_picker_blocks:

.. _picker.reject_piece_picks:

.. _picker.unchoke_piece_picks:

.. _picker.incoming_redundant_piece_picks:

.. _picker.incoming_piece_picks:

.. _picker.end_game_piece_picks:

.. _picker.snubbed_piece_picks:

+---------------------------------------+---------+
| name                                  | type    |
+=======================================+=========+
| picker.end_game_piece_picker_blocks   | counter |
+---------------------------------------+---------+
| picker.piece_picker_blocks            | counter |
+---------------------------------------+---------+
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

This breaks down the piece picks into the event that
triggered it

