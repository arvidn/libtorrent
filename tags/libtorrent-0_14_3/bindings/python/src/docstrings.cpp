// Copyright Daniel Wallin 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// -- torrent_status --------------------------------------------------------

char const* torrent_status_doc = 
    "Represents the current status for a torrent.\n"
    "Returned by `torrent_handle.status()`.";

char const* torrent_status_state_doc = 
    "The torrents current task. One of `torrent_status.states`.";

char const* torrent_status_paused_doc = 
    "Indicates if this torrent is paused or not.";

char const* torrent_status_progress_doc = 
    "A value in the range [0, 1], that represents the progress of\n"
    "the torrent's current task.";

char const* torrent_status_next_announce_doc = 
    "The time until the torrent will announce itself to the\n"
    "tracker. An instance of `datetime.timedelta`.";

char const* torrent_status_announce_interval_doc = 
    "The interval at which the torrent will reannounce itself to the\n"
    "tracker. An instance of `datetime.timedelta`.";

char const* torrent_status_current_tracker_doc = 
    "The URL of the last working tracker. If no tracker request has\n"
    "been successful yet, it's set to an empty string.";

char const* torrent_status_total_download_doc = "";
char const* torrent_status_total_upload_doc = "";
char const* torrent_status_total_payload_download_doc = "";
char const* torrent_status_total_payload_upload_doc = "";
char const* torrent_status_total_failed_bytes_doc = "";

// -- session_status --------------------------------------------------------

char const* session_status_doc = 
    "";
char const* session_status_has_incoming_connections_doc = 
    "";
char const* session_status_upload_rate_doc = 
    "";
char const* session_status_download_rate_doc = 
    "";
char const* session_status_payload_upload_rate_doc = 
    "";
char const* session_status_payload_download_rate_doc = 
    "";
char const* session_status_total_download_doc = 
    "";
char const* session_status_total_upload_doc = 
    "";
char const* session_status_total_payload_download_doc = 
    "";
char const* session_status_total_payload_upload_doc = 
    "";
char const* session_status_num_peers_doc = 
    "";
char const* session_status_dht_nodes_doc = 
    "";
char const* session_status_cache_nodes_doc = 
    "";
char const* session_status_dht_torrents_doc = 
    "";

// -- session ---------------------------------------------------------------

char const* session_doc = 
    "";
char const* session_init_doc = 
    "The `fingerprint` is a short string that will be used in\n"
    "the peer-id to identify the client and the client's version.\n"
    "For more details see the `fingerprint` class.\n"
    "The constructor that only takes a fingerprint will not open\n"
    "a listen port for the session, to get it running you'll have\n"
    "to call `session.listen_on()`.";

char const* session_listen_on_doc = 
    "";
char const* session_is_listening_doc = 
    "";
char const* session_listen_port_doc = 
    "";

char const* session_status_m_doc = 
    "Returns an instance of `session_status` with session wide-statistics\n"
    "and status";

char const* session_start_dht_doc = 
    "";
char const* session_stop_dht_doc = 
    "";
char const* session_dht_state_doc = 
    "";
char const* session_add_dht_router_doc = 
    "add dht router";

char const* session_add_torrent_doc = 
    "Adds a new torrent to the session. Return a `torrent_handle`.\n"
    "\n"
    ":Parameters:\n"
    "  - `torrent_info`: `torrent_info` instance representing the torrent\n"
    "    you want to add.\n"
    "  - `save_path`: The path to the directory where files will be saved.\n"
    "  - `resume_data (optional)`: The resume data for this torrent, as decoded\n"
    "    with `bdecode()`. This can be acquired from a running torrent with\n"
    "    `torrent_handle.write_resume_data()`.\n"
    "  - `compact_mode (optional)`: If set to true (default), the storage\n"
    "    will grow as more pieces are downloaded, and pieces are rearranged\n"
    "    to finally be in their correct places once the entire torrent has\n"
    "    been downloaded. If it is false, the entire storage is allocated\n"
    "    before download begins. I.e. the files contained in the torrent\n"
    "    are filled with zeros, and each downloaded piece is put in its\n"
    "    final place directly when downloaded.\n"
    "  - `block_size (optional)`: Sets the preferred request size, i.e.\n"
    "    the number of bytes to request from a peer at a time. This block size\n"
    "    must be a divisor of the piece size, and since the piece size is an\n"
    "    even power of 2, so must the block size be. If the block size given\n"
    "    here turns out to be greater than the piece size, it will simply be\n"
    "    clamped to the piece size.\n"
    "\n"
    ":Exceptions:\n"
    "  - `duplicate_torrent`: If the torrent you are trying to add already\n"
    "    exists in the session (is either queued for checking, being checked\n"
    "    or downloading) `add_torrent()` will throw `duplicate_torrent`.\n";

char const* session_remove_torrent_doc = 
    "Close all peer connections associated with the torrent and tell the\n"
    "tracker that we've stopped participating in the swarm.";

char const* session_download_rate_limit_doc = 
    "";
char const* session_upload_rate_limit_doc = 
    "";
char const* session_set_download_rate_limit_doc = 
    "";
char const* session_set_upload_rate_limit_doc = 
    "";
char const* session_set_max_uploads_doc = 
    "";
char const* session_set_max_connections_doc = 
    "";
char const* session_set_max_half_open_connections_doc = 
    "Sets the maximum number of half-open connections libtorrent will\n"
    "have when connecting to peers. A half-open connection is one where\n"
    "connect() has been called, but the connection still hasn't been\n"
    "established (nor failed). Windows XP Service Pack 2 sets a default,\n"
    "system wide, limit of the number of half-open connections to 10. So, \n"
    "this limit can be used to work nicer together with other network\n"
    "applications on that system. The default is to have no limit, and passing\n"
    "-1 as the limit, means to have no limit. When limiting the number of\n"
    "simultaneous connection attempts, peers will be put in a queue waiting\n"
    "for their turn to get connected.";
char const* session_num_connections_doc =
    "";
char const* session_set_settings_doc = 
    "";
char const* session_set_pe_settings_doc = 
    "";
char const* session_get_pe_settings_doc = 
    "";
char const* session_set_severity_level_doc = 
    "";
char const* session_pop_alert_doc = 
    "";
char const* session_start_upnp_doc =  
    "";
char const* session_stop_upnp_doc =
    "";
char const* session_start_lsd_doc =  
    "";
char const* session_stop_lsd_doc =
    "";
char const* session_start_natpmp_doc =
    "";
char const* session_stop_natpmp_doc =
    "";
char const* session_set_ip_filter_doc =
    "";
    
// -- alert -----------------------------------------------------------------

char const* alert_doc =
    "Base class for all concrete alert classes.";

char const* alert_msg_doc =
    "Returns a string describing this alert.";

char const* alert_severity_doc =
    "Returns the severity level for this alert, one of `alert.severity_levels`.";

char const* torrent_alert_doc =
    "";

char const* tracker_alert_doc =
    "This alert is generated on tracker time outs, premature\n"
    "disconnects, invalid response or a HTTP response other than\n"
    "\"200 OK\". From the alert you can get the handle to the torrent\n"
    "the tracker belongs to. This alert is generated as severity level\n"
    "`alert.severity_levels.warning`.";

char const* tracker_error_alert_doc =
    "";
    
char const* tracker_warning_alert_doc =
    "This alert is triggered if the tracker reply contains a warning\n"
    "field. Usually this means that the tracker announce was successful\n"
    ", but the tracker has a message to the client. The message string in\n"
    "the alert will contain the warning message from the tracker. It is\n"
    "generated with severity level `alert.severity_levels.warning`.";
    
char const* tracker_reply_alert_doc =
    "This alert is only for informational purpose. It is generated when\n"
    "a tracker announce succeeds. It is generated with severity level\n"
    "`alert.severity_levels.info`.";

char const* tracker_announce_alert_doc =
    "This alert is generated each time a tracker announce is sent\n"
    "(or attempted to be sent). It is generated at severity level `alert.severity_levels.info`.";

char const* hash_failed_alert_doc =
    "This alert is generated when a finished piece fails its hash check.\n"
    "You can get the handle to the torrent which got the failed piece\n"
    "and the index of the piece itself from the alert. This alert is\n"
    "generated as severity level `alert.severity_levels.info`.";

char const* peer_ban_alert_doc =
    "This alert is generated when a peer is banned because it has sent\n"
    "too many corrupt pieces to us. It is generated at severity level\n"
    "`alert.severity_levels.info`. The handle member is a `torrent_handle` to the torrent that\n"
    "this peer was a member of.";
   
char const* peer_error_alert_doc =
    "This alert is generated when a peer sends invalid data over the\n"
    "peer-peer protocol. The peer will be disconnected, but you get its\n"
    "ip address from the alert, to identify it. This alert is generated\n"
    "is severity level `alert.severity_levels.debug`.";

char const* invalid_request_alert_doc =
    "This is a debug alert that is generated by an incoming invalid\n"
    "piece request. The handle is a handle to the torrent the peer\n"
    "is a member of. Ip is the address of the peer and the request is\n"
    "the actual incoming request from the peer. The alert is generated\n"
    "as severity level `alert.severity_levels.debug`.";

char const* peer_request_doc =
    "The `peer_request` contains the values the client sent in its\n"
    "request message. ``piece`` is the index of the piece it want data\n"
    "from, ``start`` is the offset within the piece where the data should be\n"
    "read, and ``length`` is the amount of data it wants.";

char const* torrent_finished_alert_doc =
    "This alert is generated when a torrent switches from being a\n"
    "downloader to a seed. It will only be generated once per torrent.\n"
    "It contains a `torrent_handle` to the torrent in question. This alert\n"
    "is generated as severity level `alert.severity_levels.info`.";

char const* piece_finished_alert_doc =
    "";
    
char const* block_finished_alert_doc =
    "";
    
char const* block_downloading_alert_doc =
    "";

char const* storage_moved_alert_doc =
    "This alert is generated when a torrent moves storage.\n"
    "It contains a `torrent_handle` to the torrent in question. This alert\n"
    "is generated as severity level `alert.severity_levels.warning`.";

char const* torrent_deleted_alert_doc =
    "";

char const* torrent_paused_alert_doc =
    "This alert is generated when a torrent switches from being a\n"
    "active to paused.\n"
    "It contains a `torrent_handle` to the torrent in question. This alert\n"
    "is generated as severity level `alert.severity_levels.warning`.";

char const* torrent_checked_alert_doc =
    "";   
    
char const* url_seed_alert_doc =
    "This alert is generated when a HTTP seed name lookup fails. This\n"
    "alert is generated as severity level `alert.severity_levels.warning`.";

char const* file_error_alert_doc =
    "If the storage fails to read or write files that it needs access\n"
    "to, this alert is generated and the torrent is paused. It is\n"
    "generated as severity level `alert.severity_levels.fatal`.";
    
char const* metadata_failed_alert_doc = 
    "This alert is generated when the metadata has been completely\n"
    "received and the info-hash failed to match it. i.e. the\n"
    "metadata that was received was corrupt. libtorrent will\n"
    "automatically retry to fetch it in this case. This is only\n"
    "relevant when running a torrent-less download, with the metadata\n"
    "extension provided by libtorrent. It is generated at severity\n"
    "level `alert.severity_levels.info`.";

char const* metadata_received_alert_doc =
    "This alert is generated when the metadata has been completely\n"
    "received and the torrent can start downloading. It is not generated\n"
    "on torrents that are started with metadata, but only those that\n"
    "needs to download it from peers (when utilizing the libtorrent\n"
    "extension). It is generated at severity level `alert.severity_levels.info`.";

char const* listen_failed_alert_doc =
    "This alert is generated when none of the ports, given in the\n"
    "port range, to `session` can be opened for listening. This alert\n"
    "is generated as severity level `alert.severity_levels.fatal`.";
    
char const* listen_succeeded_alert_doc =
    "";

char const* portmap_error_alert_doc =
    "";

char const* portmap_alert_doc =
    "";
    
char const* fastresume_rejected_alert_doc =
    "This alert is generated when a fastresume file has been passed\n"
    "to `session.add_torrent` but the files on disk did not match the\n"
    "fastresume file. The string explains the reason why the resume\n"
    "file was rejected. It is generated at severity level `alert.severity_levels.warning`.";

char const* peer_blocked_alert_doc =
    "";

char const* scrape_reply_alert_doc =
    "This alert is generated when a scrape request succeeds.\n"
    "incomplete and complete is the data returned in the scrape\n"
    "response. These numbers may be -1 if the reponse was malformed.";
    
char const* scrape_failed_alert_doc = 
    "If a scrape request fails, this alert is generated. This might\n"
    "be due to the tracker timing out, refusing connection or returning\n"
    "an http response code indicating an error.";

char const* udp_error_alert_doc =
    "";

char const* external_ip_alert_doc =
    "";

char const* save_resume_data_alert_doc =
    "";
    
