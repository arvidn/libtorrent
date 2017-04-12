/*

Copyright (c) 2017, Arvid Norberg
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

#ifndef TORRENT_FWD_HPP
#define TORRENT_FWD_HPP

#include "libtorrent/export.hpp"

namespace libtorrent {


// include/libtorrent/add_torrent_params.hpp
struct TORRENT_EXPORT add_torrent_params;

// include/libtorrent/alert.hpp
class TORRENT_EXPORT alert;

// include/libtorrent/alert_types.hpp
struct TORRENT_EXPORT torrent_alert;
struct TORRENT_EXPORT peer_alert;
struct TORRENT_EXPORT tracker_alert;
struct TORRENT_EXPORT torrent_removed_alert;
struct TORRENT_EXPORT read_piece_alert;
struct TORRENT_EXPORT file_completed_alert;
struct TORRENT_EXPORT file_renamed_alert;
struct TORRENT_EXPORT file_rename_failed_alert;
struct TORRENT_EXPORT performance_alert;
struct TORRENT_EXPORT state_changed_alert;
struct TORRENT_EXPORT tracker_error_alert;
struct TORRENT_EXPORT tracker_warning_alert;
struct TORRENT_EXPORT scrape_reply_alert;
struct TORRENT_EXPORT scrape_failed_alert;
struct TORRENT_EXPORT tracker_reply_alert;
struct TORRENT_EXPORT dht_reply_alert;
struct TORRENT_EXPORT tracker_announce_alert;
struct TORRENT_EXPORT hash_failed_alert;
struct TORRENT_EXPORT peer_ban_alert;
struct TORRENT_EXPORT peer_unsnubbed_alert;
struct TORRENT_EXPORT peer_snubbed_alert;
struct TORRENT_EXPORT peer_error_alert;
struct TORRENT_EXPORT peer_connect_alert;
struct TORRENT_EXPORT peer_disconnected_alert;
struct TORRENT_EXPORT invalid_request_alert;
struct TORRENT_EXPORT torrent_finished_alert;
struct TORRENT_EXPORT piece_finished_alert;
struct TORRENT_EXPORT request_dropped_alert;
struct TORRENT_EXPORT block_timeout_alert;
struct TORRENT_EXPORT block_finished_alert;
struct TORRENT_EXPORT block_downloading_alert;
struct TORRENT_EXPORT unwanted_block_alert;
struct TORRENT_EXPORT storage_moved_alert;
struct TORRENT_EXPORT storage_moved_failed_alert;
struct TORRENT_EXPORT torrent_deleted_alert;
struct TORRENT_EXPORT torrent_delete_failed_alert;
struct TORRENT_EXPORT save_resume_data_alert;
struct TORRENT_EXPORT save_resume_data_failed_alert;
struct TORRENT_EXPORT torrent_paused_alert;
struct TORRENT_EXPORT torrent_resumed_alert;
struct TORRENT_EXPORT torrent_checked_alert;
struct TORRENT_EXPORT url_seed_alert;
struct TORRENT_EXPORT file_error_alert;
struct TORRENT_EXPORT metadata_failed_alert;
struct TORRENT_EXPORT metadata_received_alert;
struct TORRENT_EXPORT udp_error_alert;
struct TORRENT_EXPORT external_ip_alert;
struct TORRENT_EXPORT listen_failed_alert;
struct TORRENT_EXPORT listen_succeeded_alert;
struct TORRENT_EXPORT portmap_error_alert;
struct TORRENT_EXPORT portmap_alert;
struct TORRENT_EXPORT portmap_log_alert;
struct TORRENT_EXPORT fastresume_rejected_alert;
struct TORRENT_EXPORT peer_blocked_alert;
struct TORRENT_EXPORT dht_announce_alert;
struct TORRENT_EXPORT dht_get_peers_alert;
struct TORRENT_EXPORT stats_alert;
struct TORRENT_EXPORT cache_flushed_alert;
struct TORRENT_EXPORT anonymous_mode_alert;
struct TORRENT_EXPORT lsd_peer_alert;
struct TORRENT_EXPORT trackerid_alert;
struct TORRENT_EXPORT dht_bootstrap_alert;
struct TORRENT_EXPORT torrent_error_alert;
struct TORRENT_EXPORT torrent_need_cert_alert;
struct TORRENT_EXPORT incoming_connection_alert;
struct TORRENT_EXPORT add_torrent_alert;
struct TORRENT_EXPORT state_update_alert;
struct TORRENT_EXPORT session_stats_alert;
struct TORRENT_EXPORT dht_error_alert;
struct TORRENT_EXPORT dht_immutable_item_alert;
struct TORRENT_EXPORT dht_mutable_item_alert;
struct TORRENT_EXPORT dht_put_alert;
struct TORRENT_EXPORT i2p_alert;
struct TORRENT_EXPORT dht_outgoing_get_peers_alert;
struct TORRENT_EXPORT log_alert;
struct TORRENT_EXPORT torrent_log_alert;
struct TORRENT_EXPORT peer_log_alert;
struct TORRENT_EXPORT lsd_error_alert;
struct TORRENT_EXPORT dht_lookup;
struct TORRENT_EXPORT dht_routing_bucket;
struct TORRENT_EXPORT dht_stats_alert;
struct TORRENT_EXPORT incoming_request_alert;
struct TORRENT_EXPORT dht_log_alert;
struct TORRENT_EXPORT dht_pkt_alert;
struct TORRENT_EXPORT dht_get_peers_reply_alert;
struct TORRENT_EXPORT dht_direct_response_alert;
struct TORRENT_EXPORT picker_log_alert;
struct TORRENT_EXPORT session_error_alert;
struct TORRENT_EXPORT dht_live_nodes_alert;
struct TORRENT_EXPORT session_stats_header_alert;

// include/libtorrent/announce_entry.hpp
struct TORRENT_EXPORT announce_entry;

// include/libtorrent/bdecode.hpp
struct TORRENT_EXPORT bdecode_node;

// include/libtorrent/bitfield.hpp
struct TORRENT_EXPORT bitfield;

// include/libtorrent/create_torrent.hpp
struct TORRENT_EXPORT create_torrent;

// include/libtorrent/disk_interface.hpp
struct TORRENT_EXPORT open_file_state;

// include/libtorrent/disk_io_thread.hpp
struct TORRENT_EXPORT cache_status;

// include/libtorrent/entry.hpp
class TORRENT_EXPORT entry;

// include/libtorrent/error_code.hpp
struct TORRENT_EXPORT storage_error;

// include/libtorrent/extensions.hpp
struct TORRENT_EXPORT plugin;
struct TORRENT_EXPORT torrent_plugin;
struct TORRENT_EXPORT peer_plugin;
struct TORRENT_EXPORT crypto_plugin;

// include/libtorrent/file_pool.hpp
struct TORRENT_EXPORT file_pool;

// include/libtorrent/file_storage.hpp
struct TORRENT_EXPORT file_slice;
class TORRENT_EXPORT file_storage;

// include/libtorrent/hasher.hpp
class TORRENT_EXPORT hasher;

// include/libtorrent/hasher512.hpp
class TORRENT_EXPORT hasher512;

// include/libtorrent/ip_filter.hpp
struct TORRENT_EXPORT ip_filter;
class TORRENT_EXPORT port_filter;

// include/libtorrent/kademlia/dht_state.hpp
struct TORRENT_EXPORT dht_state;

// include/libtorrent/kademlia/dht_storage.hpp
struct TORRENT_EXPORT dht_storage_counters;
struct TORRENT_EXPORT dht_storage_interface;

// include/libtorrent/peer_connection_handle.hpp
struct TORRENT_EXPORT peer_connection_handle;
struct TORRENT_EXPORT bt_peer_connection_handle;

// include/libtorrent/peer_info.hpp
struct TORRENT_EXPORT peer_info;

// include/libtorrent/peer_request.hpp
struct TORRENT_EXPORT peer_request;

// include/libtorrent/session.hpp
class TORRENT_EXPORT session_proxy;
struct TORRENT_EXPORT session_params;
class TORRENT_EXPORT session;

// include/libtorrent/session_handle.hpp
struct TORRENT_EXPORT session_handle;

// include/libtorrent/session_settings.hpp
struct TORRENT_EXPORT dht_settings;
struct TORRENT_EXPORT pe_settings;

// include/libtorrent/session_stats.hpp
struct TORRENT_EXPORT stats_metric;

// include/libtorrent/session_status.hpp
struct TORRENT_EXPORT utp_status;
struct TORRENT_EXPORT session_status;

// include/libtorrent/settings_pack.hpp
struct TORRENT_EXPORT settings_pack;

// include/libtorrent/storage.hpp
struct TORRENT_EXPORT storage_interface;
class TORRENT_EXPORT default_storage;

// include/libtorrent/storage_defs.hpp
struct TORRENT_EXPORT storage_interface;
struct TORRENT_EXPORT storage_params;

// include/libtorrent/torrent_handle.hpp
struct TORRENT_EXPORT block_info;
struct TORRENT_EXPORT partial_piece_info;
struct TORRENT_EXPORT torrent_handle;

// include/libtorrent/torrent_info.hpp
struct TORRENT_EXPORT web_seed_entry;
class TORRENT_EXPORT torrent_info;

// include/libtorrent/torrent_status.hpp
struct TORRENT_EXPORT torrent_status;

#ifndef TORRENT_NO_DEPRECATE

// include/libtorrent/alert_types.hpp
struct TORRENT_DEPRECATED_EXPORT torrent_added_alert;
struct TORRENT_DEPRECATED_EXPORT mmap_cache_alert;
struct TORRENT_DEPRECATED_EXPORT torrent_update_alert;

// include/libtorrent/file_storage.hpp
struct TORRENT_DEPRECATED_EXPORT file_entry;
struct TORRENT_DEPRECATED_EXPORT internal_file_entry;

// include/libtorrent/lazy_entry.hpp
struct TORRENT_DEPRECATED_EXPORT pascal_string;
struct TORRENT_DEPRECATED_EXPORT lazy_entry;

#endif // TORRENT_NO_DEPRECATE

}

#endif // TORRENT_FWD_HPP
