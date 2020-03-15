// Copyright Daniel Wallin 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "boost_python.hpp"
#include <libtorrent/session.hpp>
#include <libtorrent/kademlia/dht_settings.hpp>

using namespace boost::python;
using namespace lt;

void bind_session_settings()
{
    enum_<settings_pack::choking_algorithm_t>("choking_algorithm_t")
        .value("fixed_slots_choker", settings_pack::fixed_slots_choker)
#if TORRENT_ABI_VERSION == 1
        .value("auto_expand_choker", settings_pack::rate_based_choker)
#endif
        .value("rate_based_choker", settings_pack::rate_based_choker)
#if TORRENT_ABI_VERSION == 1
        .value("bittyrant_choker", settings_pack::bittyrant_choker)
#endif
    ;

    enum_<settings_pack::seed_choking_algorithm_t>("seed_choking_algorithm_t")
        .value("round_robin", settings_pack::round_robin)
        .value("fastest_upload", settings_pack::fastest_upload)
        .value("anti_leech", settings_pack::anti_leech)
    ;

    enum_<settings_pack::suggest_mode_t>("suggest_mode_t")
        .value("no_piece_suggestions", settings_pack::no_piece_suggestions)
        .value("suggest_read_cache", settings_pack::suggest_read_cache)
    ;

    enum_<settings_pack::io_buffer_mode_t>("io_buffer_mode_t")
        .value("enable_os_cache", settings_pack::enable_os_cache)
#if TORRENT_ABI_VERSION == 1
        .value("disable_os_cache_for_aligned_files", settings_pack::disable_os_cache_for_aligned_files)
#endif
        .value("disable_os_cache", settings_pack::disable_os_cache)
    ;

    enum_<settings_pack::bandwidth_mixed_algo_t>("bandwidth_mixed_algo_t")
        .value("prefer_tcp", settings_pack::prefer_tcp)
        .value("peer_proportional", settings_pack::peer_proportional)
    ;

    enum_<settings_pack::enc_policy>("enc_policy")
        .value("pe_forced", settings_pack::pe_forced)
        .value("pe_enabled", settings_pack::pe_enabled)
        .value("pe_disabled", settings_pack::pe_disabled)
#if TORRENT_ABI_VERSION == 1
        .value("forced", settings_pack::pe_forced)
        .value("enabled", settings_pack::pe_enabled)
        .value("disabled", settings_pack::pe_disabled)
#endif
    ;

    enum_<settings_pack::enc_level>("enc_level")
        .value("pe_rc4", settings_pack::pe_rc4)
        .value("pe_plaintext", settings_pack::pe_plaintext)
        .value("pe_both", settings_pack::pe_both)
#if TORRENT_ABI_VERSION == 1
        .value("rc4", settings_pack::pe_rc4)
        .value("plaintext", settings_pack::pe_plaintext)
        .value("both", settings_pack::pe_both)
#endif
    ;

    {
    scope s = enum_<settings_pack::proxy_type_t>("proxy_type_t")
        .value("none", settings_pack::none)
        .value("socks4", settings_pack::socks4)
        .value("socks5", settings_pack::socks5)
        .value("socks5_pw", settings_pack::socks5_pw)
        .value("http", settings_pack::http)
        .value("http_pw", settings_pack::http_pw)
        .value("i2p_proxy", settings_pack::i2p_proxy)
   ;

#if TORRENT_ABI_VERSION == 1
    scope().attr("proxy_type") = s;

    class_<proxy_settings>("proxy_settings")
        .def_readwrite("hostname", &proxy_settings::hostname)
        .def_readwrite("port", &proxy_settings::port)
        .def_readwrite("password", &proxy_settings::password)
        .def_readwrite("username", &proxy_settings::username)
        .def_readwrite("type", &proxy_settings::type)
        .def_readwrite("proxy_peer_connections", &proxy_settings::proxy_peer_connections)
        .def_readwrite("proxy_hostnames", &proxy_settings::proxy_hostnames)
    ;
#endif
   }

#ifndef TORRENT_DISABLE_DHT
    class_<dht::dht_settings>("dht_settings")
        .def_readwrite("max_peers_reply", &dht::dht_settings::max_peers_reply)
        .def_readwrite("search_branching", &dht::dht_settings::search_branching)
#if TORRENT_ABI_VERSION == 1
        .def_readwrite("service_port", &dht::dht_settings::service_port)
#endif
        .def_readwrite("max_fail_count", &dht::dht_settings::max_fail_count)
        .def_readwrite("max_torrents", &dht::dht_settings::max_torrents)
        .def_readwrite("max_dht_items", &dht::dht_settings::max_dht_items)
        .def_readwrite("restrict_routing_ips", &dht::dht_settings::restrict_routing_ips)
        .def_readwrite("restrict_search_ips", &dht::dht_settings::restrict_search_ips)
        .def_readwrite("max_torrent_search_reply", &dht::dht_settings::max_torrent_search_reply)
        .def_readwrite("extended_routing_table", &dht::dht_settings::extended_routing_table)
        .def_readwrite("aggressive_lookups", &dht::dht_settings::aggressive_lookups)
        .def_readwrite("privacy_lookups", &dht::dht_settings::privacy_lookups)
        .def_readwrite("enforce_node_id", &dht::dht_settings::enforce_node_id)
        .def_readwrite("ignore_dark_internet", &dht::dht_settings::ignore_dark_internet)
        .def_readwrite("block_timeout", &dht::dht_settings::block_timeout)
        .def_readwrite("block_ratelimit", &dht::dht_settings::block_ratelimit)
        .def_readwrite("read_only", &dht::dht_settings::read_only)
        .def_readwrite("item_lifetime", &dht::dht_settings::item_lifetime)
    ;
#endif

#if TORRENT_ABI_VERSION == 1
    class_<pe_settings>("pe_settings")
        .def_readwrite("out_enc_policy", &pe_settings::out_enc_policy)
        .def_readwrite("in_enc_policy", &pe_settings::in_enc_policy)
        .def_readwrite("allowed_enc_level", &pe_settings::allowed_enc_level)
        .def_readwrite("prefer_rc4", &pe_settings::prefer_rc4)
    ;
#endif

}
