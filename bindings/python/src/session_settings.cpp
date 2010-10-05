// Copyright Daniel Wallin 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <boost/python.hpp>
#include <libtorrent/session.hpp>

using namespace boost::python;
using namespace libtorrent;

void bind_session_settings()
{
    class_<session_settings>("session_settings")
        .def_readwrite("user_agent", &session_settings::user_agent)
        .def_readwrite("tracker_completion_timeout", &session_settings::tracker_completion_timeout)
        .def_readwrite("tracker_receive_timeout", &session_settings::tracker_receive_timeout)
        .def_readwrite("stop_tracker_timeout", &session_settings::stop_tracker_timeout)
        .def_readwrite("tracker_maximum_response_length", &session_settings::tracker_maximum_response_length)
        .def_readwrite("piece_timeout", &session_settings::piece_timeout)
        .def_readwrite("request_timeout", &session_settings::request_timeout)
        .def_readwrite("request_queue_time", &session_settings::request_queue_time)
        .def_readwrite("max_allowed_in_request_queue", &session_settings::max_allowed_in_request_queue)
        .def_readwrite("max_out_request_queue", &session_settings::max_out_request_queue)
        .def_readwrite("whole_pieces_threshold", &session_settings::whole_pieces_threshold)
        .def_readwrite("peer_timeout", &session_settings::peer_timeout)
        .def_readwrite("urlseed_timeout", &session_settings::urlseed_timeout)
        .def_readwrite("urlseed_pipeline_size", &session_settings::urlseed_pipeline_size)
        .def_readwrite("urlseed_wait_retry", &session_settings::urlseed_wait_retry)
        .def_readwrite("file_pool_size", &session_settings::file_pool_size)
        .def_readwrite("allow_multiple_connections_per_ip", &session_settings::allow_multiple_connections_per_ip)
        .def_readwrite("max_failcount", &session_settings::max_failcount)
        .def_readwrite("min_reconnect_time", &session_settings::min_reconnect_time)
        .def_readwrite("peer_connect_timeout", &session_settings::peer_connect_timeout)
        .def_readwrite("ignore_limits_on_local_network", &session_settings::ignore_limits_on_local_network)
        .def_readwrite("connection_speed", &session_settings::connection_speed)
        .def_readwrite("send_redundant_have", &session_settings::send_redundant_have)
        .def_readwrite("lazy_bitfields", &session_settings::lazy_bitfields)
        .def_readwrite("inactivity_timeout", &session_settings::inactivity_timeout)
        .def_readwrite("unchoke_interval", &session_settings::unchoke_interval)
        .def_readwrite("optimistic_unchoke_interval", &session_settings::optimistic_unchoke_interval)
        .def_readwrite("num_want", &session_settings::num_want)
        .def_readwrite("initial_picker_threshold", &session_settings::initial_picker_threshold)
        .def_readwrite("allowed_fast_set_size", &session_settings::allowed_fast_set_size)
        .def_readwrite("max_queued_disk_bytes", &session_settings::max_queued_disk_bytes)
        .def_readwrite("handshake_timeout", &session_settings::handshake_timeout)
#ifndef TORRENT_DISABLE_DHT
        .def_readwrite("use_dht_as_fallback", &session_settings::use_dht_as_fallback)
#endif
        .def_readwrite("free_torrent_hashes", &session_settings::free_torrent_hashes)
        .def_readwrite("upnp_ignore_nonrouters", &session_settings::upnp_ignore_nonrouters)
        .def_readwrite("send_buffer_watermark", &session_settings::send_buffer_watermark)
        .def_readwrite("auto_upload_slots", &session_settings::auto_upload_slots)
        .def_readwrite("auto_upload_slots_rate_based", &session_settings::auto_upload_slots_rate_based)
        .def_readwrite("use_parole_mode", &session_settings::use_parole_mode)
        .def_readwrite("cache_size", &session_settings::cache_size)
        .def_readwrite("cache_buffer_chunk_size", &session_settings::cache_buffer_chunk_size)
        .def_readwrite("cache_expiry", &session_settings::cache_expiry)
        .def_readwrite("use_read_cache", &session_settings::use_read_cache)
        .def_readwrite("disk_io_write_mode", &session_settings::disk_io_write_mode)
        .def_readwrite("disk_io_read_mode", &session_settings::disk_io_read_mode)
        .def_readwrite("coalesce_reads", &session_settings::coalesce_reads)
        .def_readwrite("coalesce_writes", &session_settings::coalesce_writes)
        .def_readwrite("outgoing_ports", &session_settings::outgoing_ports)
        .def_readwrite("peer_tos", &session_settings::peer_tos)
        .def_readwrite("active_downloads", &session_settings::active_downloads)
        .def_readwrite("active_seeds", &session_settings::active_seeds)
        .def_readwrite("active_limit", &session_settings::active_limit)
        .def_readwrite("auto_manage_prefer_seeds", &session_settings::auto_manage_prefer_seeds)
        .def_readwrite("dont_count_slow_torrents", &session_settings::dont_count_slow_torrents)
        .def_readwrite("auto_manage_interval", &session_settings::auto_manage_interval)
        .def_readwrite("share_ratio_limit", &session_settings::share_ratio_limit)
        .def_readwrite("seed_time_ratio_limit", &session_settings::seed_time_ratio_limit)
        .def_readwrite("seed_time_limit", &session_settings::seed_time_limit)
        .def_readwrite("peer_turnover", &session_settings::peer_turnover)
        .def_readwrite("peer_turnover_cutoff", &session_settings::peer_turnover_cutoff)
        .def_readwrite("close_redundant_connections", &session_settings::close_redundant_connections)
        .def_readwrite("auto_scrape_interval", &session_settings::auto_scrape_interval)
        .def_readwrite("auto_scrape_min_interval", &session_settings::auto_scrape_min_interval)
        .def_readwrite("max_peerlist_size", &session_settings::max_peerlist_size)
        .def_readwrite("max_paused_peerlist_size", &session_settings::max_paused_peerlist_size)
        .def_readwrite("min_announce_interval", &session_settings::min_announce_interval)
        .def_readwrite("prioritize_partial_pieces", &session_settings::prioritize_partial_pieces)
        .def_readwrite("auto_manage_startup", &session_settings::auto_manage_startup)
        .def_readwrite("rate_limit_ip_overhead", &session_settings::rate_limit_ip_overhead)
        .def_readwrite("announce_to_all_trackers", &session_settings::announce_to_all_trackers)
        .def_readwrite("announce_to_all_tiers", &session_settings::announce_to_all_tiers)
        .def_readwrite("prefer_udp_trackers", &session_settings::prefer_udp_trackers)
        .def_readwrite("strict_super_seeding", &session_settings::strict_super_seeding)
        .def_readwrite("seeding_piece_quota", &session_settings::seeding_piece_quota)
        .def_readwrite("max_sparse_regions", &session_settings::max_sparse_regions)
#ifndef TORRENT_DISABLE_MLOCK
        .def_readwrite("lock_disk_cache", &session_settings::lock_disk_cache)
#endif
        .def_readwrite("max_rejects", &session_settings::max_rejects)
        .def_readwrite("recv_socket_buffer_size", &session_settings::recv_socket_buffer_size)
        .def_readwrite("send_socket_buffer_size", &session_settings::send_socket_buffer_size)
        .def_readwrite("optimize_hashing_for_speed", &session_settings::optimize_hashing_for_speed)
        .def_readwrite("file_checks_delay_per_block", &session_settings::file_checks_delay_per_block)
        .def_readwrite("disk_cache_algorithm", &session_settings::disk_cache_algorithm)
        .def_readwrite("read_cache_line_size", &session_settings::read_cache_line_size)
        .def_readwrite("write_cache_line_size", &session_settings::write_cache_line_size)
        .def_readwrite("optimistic_disk_retry", &session_settings::optimistic_disk_retry)
        .def_readwrite("disable_hash_checks", &session_settings::disable_hash_checks)
        .def_readwrite("allow_reordered_disk_operations", &session_settings::allow_reordered_disk_operations)
        .def_readwrite("max_suggest_pieces", &session_settings::max_suggest_pieces)
        .def_readwrite("drop_skipped_requests", &session_settings::drop_skipped_requests)
        .def_readwrite("low_prio_disk", &session_settings::low_prio_disk)
        .def_readwrite("local_service_announce_interval", &session_settings::local_service_announce_interval)
        .def_readwrite("udp_tracker_token_expiry", &session_settings::udp_tracker_token_expiry)
        .def_readwrite("report_true_downoaded", &session_settings::report_true_downloaded)
        .def_readwrite("strict_end_game_mode", &session_settings::strict_end_game_mode)
    ;

    enum_<proxy_settings::proxy_type>("proxy_type")
        .value("none", proxy_settings::none)
        .value("socks4", proxy_settings::socks4)
        .value("socks5", proxy_settings::socks5)
        .value("socks5_pw", proxy_settings::socks5_pw)
        .value("http", proxy_settings::http)
        .value("http_pw", proxy_settings::http_pw)
    ;

    enum_<session_settings::disk_cache_algo_t>("disk_cache_algo_t")
        .value("lru", session_settings::lru)
        .value("largest_contiguous", session_settings::largest_contiguous)
    ;

    enum_<session_settings::io_buffer_mode_t>("io_buffer_mode_t")
        .value("enable_os_cache", session_settings::enable_os_cache)
        .value("disable_os_cache_for_aligned_files", session_settings::disable_os_cache_for_aligned_files)
        .value("disable_os_cache", session_settings::disable_os_cache)
    ;

    class_<proxy_settings>("proxy_settings")
        .def_readwrite("hostname", &proxy_settings::hostname)
        .def_readwrite("port", &proxy_settings::port)
        .def_readwrite("password", &proxy_settings::password)
        .def_readwrite("username", &proxy_settings::username)
        .def_readwrite("type", &proxy_settings::type)
    ;

#ifndef TORRENT_DISABLE_DHT
    class_<dht_settings>("dht_settings")
        .def_readwrite("max_peers_reply", &dht_settings::max_peers_reply)
        .def_readwrite("search_branching", &dht_settings::search_branching)
        .def_readwrite("service_port", &dht_settings::service_port)
        .def_readwrite("max_fail_count", &dht_settings::max_fail_count)
    ;
#endif

#ifndef TORRENT_DISABLE_ENCRYPTION
    enum_<pe_settings::enc_policy>("enc_policy")
        .value("forced", pe_settings::forced)
        .value("enabled", pe_settings::enabled)
        .value("disabled", pe_settings::disabled)
    ;

    enum_<pe_settings::enc_level>("enc_level")
        .value("rc4", pe_settings::rc4)
        .value("plaintext", pe_settings::plaintext)
        .value("both", pe_settings::both)
    ;

    class_<pe_settings>("pe_settings")
        .def_readwrite("out_enc_policy", &pe_settings::out_enc_policy)
        .def_readwrite("in_enc_policy", &pe_settings::in_enc_policy)
        .def_readwrite("allowed_enc_level", &pe_settings::allowed_enc_level)
        .def_readwrite("prefer_rc4", &pe_settings::prefer_rc4)
    ;
#endif

}
