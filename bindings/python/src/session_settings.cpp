// Copyright Daniel Wallin 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <libtorrent/session.hpp>
#include <boost/python.hpp>

using namespace boost::python;
using namespace libtorrent;

void bind_session_settings()
{
    class_<session_settings>("session_settings")
        .def_readwrite("user_agent", &session_settings::user_agent)
        .def_readwrite("tracker_completion_timeout", &session_settings::tracker_completion_timeout)
        .def_readwrite("tracker_receive_timeout", &session_settings::tracker_receive_timeout)
        .def_readwrite("tracker_maximum_response_length", &session_settings::tracker_maximum_response_length)
        .def_readwrite("piece_timeout", &session_settings::piece_timeout)
        .def_readwrite("request_queue_time", &session_settings::request_queue_time)
        .def_readwrite("max_allowed_in_request_queue", &session_settings::max_allowed_in_request_queue)
        .def_readwrite("max_out_request_queue", &session_settings::max_out_request_queue)
        .def_readwrite("whole_pieces_threshold", &session_settings::whole_pieces_threshold)
        .def_readwrite("peer_timeout", &session_settings::peer_timeout)
        .def_readwrite("urlseed_timeout", &session_settings::urlseed_timeout)
        .def_readwrite("urlseed_pipeline_size", &session_settings::urlseed_pipeline_size)
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
        .def_readwrite("active_downloads", &session_settings::active_downloads)
        .def_readwrite("active_seeds", &session_settings::active_seeds)
        .def_readwrite("active_limit", &session_settings::active_limit)
        .def_readwrite("dont_count_slow_torrents", &session_settings::dont_count_slow_torrents)
        .def_readwrite("auto_manage_interval", &session_settings::auto_manage_interval)
        .def_readwrite("share_ratio_limit", &session_settings::share_ratio_limit)
        .def_readwrite("seed_time_ratio_limit", &session_settings::seed_time_ratio_limit)
        .def_readwrite("seed_time_limit", &session_settings::seed_time_limit)
        .def_readwrite("auto_scraped_interval", &session_settings::auto_scrape_interval)
        .def_readwrite("peer_tos", &session_settings::peer_tos)
        .def_readwrite("rate_limit_ip_overhead", &session_settings::rate_limit_ip_overhead)
        .def_readwrite("outgoing_ports", &session_settings::outgoing_ports)
#ifndef TORRENT_DISABLE_DHT
        .def_readwrite("use_dht_as_fallback", &session_settings::use_dht_as_fallback)
#endif
    ;

    enum_<proxy_settings::proxy_type>("proxy_type")
        .value("none", proxy_settings::none)
        .value("socks4", proxy_settings::socks4)
        .value("socks5", proxy_settings::socks5)
        .value("socks5_pw", proxy_settings::socks5_pw)
        .value("http", proxy_settings::http)
        .value("http_pw", proxy_settings::http_pw)
    ;
    class_<proxy_settings>("proxy_settings")
        .def_readwrite("hostname", &proxy_settings::hostname)
        .def_readwrite("port", &proxy_settings::port)
        .def_readwrite("password", &proxy_settings::password)
        .def_readwrite("username", &proxy_settings::username)
        .def_readwrite("type", &proxy_settings::type)
    ;

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
