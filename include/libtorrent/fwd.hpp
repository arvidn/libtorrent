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

#include "libtorrent/config.hpp"

namespace libtorrent {

// include/libtorrent/add_torrent_params.hpp
TORRENT_VERSION_NAMESPACE_2
struct add_torrent_params;
TORRENT_VERSION_NAMESPACE_2_END

// include/libtorrent/alert.hpp
class alert;

// include/libtorrent/alert_types.hpp
struct dht_routing_bucket;
TORRENT_VERSION_NAMESPACE_2
struct torrent_alert;
struct peer_alert;
struct tracker_alert;
struct torrent_removed_alert;
struct read_piece_alert;
struct file_completed_alert;
struct file_renamed_alert;
struct file_rename_failed_alert;
struct performance_alert;
struct state_changed_alert;
struct tracker_error_alert;
struct tracker_warning_alert;
struct scrape_reply_alert;
struct scrape_failed_alert;
struct tracker_reply_alert;
struct dht_reply_alert;
struct tracker_announce_alert;
struct hash_failed_alert;
struct peer_ban_alert;
struct peer_unsnubbed_alert;
struct peer_snubbed_alert;
struct peer_error_alert;
struct peer_connect_alert;
struct peer_disconnected_alert;
struct invalid_request_alert;
struct torrent_finished_alert;
struct piece_finished_alert;
struct request_dropped_alert;
struct block_timeout_alert;
struct block_finished_alert;
struct block_downloading_alert;
struct unwanted_block_alert;
struct storage_moved_alert;
struct storage_moved_failed_alert;
struct torrent_deleted_alert;
struct torrent_delete_failed_alert;
struct save_resume_data_alert;
struct save_resume_data_failed_alert;
struct torrent_paused_alert;
struct torrent_resumed_alert;
struct torrent_checked_alert;
struct url_seed_alert;
struct file_error_alert;
struct metadata_failed_alert;
struct metadata_received_alert;
struct udp_error_alert;
struct external_ip_alert;
struct listen_failed_alert;
struct listen_succeeded_alert;
struct portmap_error_alert;
struct portmap_alert;
struct portmap_log_alert;
struct fastresume_rejected_alert;
struct peer_blocked_alert;
struct dht_announce_alert;
struct dht_get_peers_alert;
struct stats_alert;
struct cache_flushed_alert;
struct lsd_peer_alert;
struct trackerid_alert;
struct dht_bootstrap_alert;
struct torrent_error_alert;
struct torrent_need_cert_alert;
struct incoming_connection_alert;
struct add_torrent_alert;
struct state_update_alert;
struct session_stats_alert;
struct dht_error_alert;
struct dht_immutable_item_alert;
struct dht_mutable_item_alert;
struct dht_put_alert;
struct i2p_alert;
struct dht_outgoing_get_peers_alert;
struct log_alert;
struct torrent_log_alert;
struct peer_log_alert;
struct lsd_error_alert;
struct dht_lookup;
struct dht_stats_alert;
struct incoming_request_alert;
struct dht_log_alert;
struct dht_pkt_alert;
struct dht_get_peers_reply_alert;
struct dht_direct_response_alert;
struct picker_log_alert;
struct session_error_alert;
struct dht_live_nodes_alert;
struct session_stats_header_alert;
struct dht_sample_infohashes_alert;
struct block_uploaded_alert;
struct alerts_dropped_alert;
struct socks5_alert;
TORRENT_VERSION_NAMESPACE_2_END

// include/libtorrent/announce_entry.hpp
struct announce_endpoint;
TORRENT_VERSION_NAMESPACE_2
struct announce_entry;
TORRENT_VERSION_NAMESPACE_2_END

// include/libtorrent/bdecode.hpp
struct bdecode_node;

// include/libtorrent/bitfield.hpp
struct bitfield;

// include/libtorrent/create_torrent.hpp
struct create_torrent;

// include/libtorrent/disk_interface.hpp
struct open_file_state;

// include/libtorrent/disk_io_thread.hpp
struct cache_status;

// include/libtorrent/entry.hpp
class entry;

// include/libtorrent/error_code.hpp
struct storage_error;

// include/libtorrent/extensions.hpp
struct plugin;
struct torrent_plugin;
struct peer_plugin;
struct crypto_plugin;

// include/libtorrent/file_pool.hpp
struct file_pool;

// include/libtorrent/file_storage.hpp
struct file_slice;
class file_storage;

// include/libtorrent/hasher.hpp
class hasher;

// include/libtorrent/hasher512.hpp
class hasher512;

// include/libtorrent/ip_filter.hpp
struct ip_filter;
class port_filter;

// include/libtorrent/kademlia/dht_settings.hpp
namespace dht {
struct dht_settings;
}

// include/libtorrent/kademlia/dht_state.hpp
namespace dht {
struct dht_state;
}

// include/libtorrent/kademlia/dht_storage.hpp
namespace dht {
struct dht_storage_counters;
}
namespace dht {
struct dht_storage_interface;
}

// include/libtorrent/peer_class.hpp
struct peer_class_info;

// include/libtorrent/peer_class_type_filter.hpp
struct peer_class_type_filter;

// include/libtorrent/peer_connection_handle.hpp
struct peer_connection_handle;
struct bt_peer_connection_handle;

// include/libtorrent/peer_info.hpp
TORRENT_VERSION_NAMESPACE_2
struct peer_info;
TORRENT_VERSION_NAMESPACE_2_END

// include/libtorrent/peer_request.hpp
struct peer_request;

// include/libtorrent/session.hpp
class session_proxy;
struct session_params;
class session;

// include/libtorrent/session_handle.hpp
struct session_handle;

// include/libtorrent/session_stats.hpp
struct stats_metric;

// include/libtorrent/session_status.hpp
struct utp_status;
struct session_status;

// include/libtorrent/settings_pack.hpp
struct settings_pack;

// include/libtorrent/storage.hpp
struct storage_interface;
class default_storage;

// include/libtorrent/storage_defs.hpp
struct storage_interface;
struct storage_params;

// include/libtorrent/torrent_handle.hpp
struct block_info;
struct partial_piece_info;
struct torrent_handle;

// include/libtorrent/torrent_info.hpp
struct web_seed_entry;
class torrent_info;

// include/libtorrent/torrent_status.hpp
TORRENT_VERSION_NAMESPACE_2
struct torrent_status;
TORRENT_VERSION_NAMESPACE_2_END

#if TORRENT_ABI_VERSION == 1

// include/libtorrent/alert_types.hpp
TORRENT_VERSION_NAMESPACE_2
struct torrent_added_alert;
struct anonymous_mode_alert;
struct mmap_cache_alert;
struct torrent_update_alert;
TORRENT_VERSION_NAMESPACE_2_END

// include/libtorrent/file_storage.hpp
struct file_entry;

// include/libtorrent/fingerprint.hpp
struct fingerprint;

// include/libtorrent/lazy_entry.hpp
struct pascal_string;
struct lazy_entry;

// include/libtorrent/session_settings.hpp
struct pe_settings;

#endif // TORRENT_ABI_VERSION

}

namespace lt = libtorrent;

#endif // TORRENT_FWD_HPP
