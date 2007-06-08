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
#ifndef TORRENT_DISABLE_DHT
        .def_readwrite("use_dht_as_fallback", &session_settings::use_dht_as_fallback)
#endif
        ;
    
    scope ps = class_<proxy_settings>("proxy_settings")
        .def_readwrite("hostname", &proxy_settings::hostname)
        .def_readwrite("port", &proxy_settings::port)
        .def_readwrite("password", &proxy_settings::password)
        .def_readwrite("username", &proxy_settings::username)
        .def_readwrite("type", &proxy_settings::type)
    ;

    ps.attr("none") = (int)proxy_settings::none;
    ps.attr("socks5") = (int)proxy_settings::socks5;
    ps.attr("socks5_pw") = (int)proxy_settings::socks5_pw;
    ps.attr("http") = (int)proxy_settings::http;
    ps.attr("http_pw") = (int)proxy_settings::http_pw;

    scope pes = class_<pe_settings>("pe_settings")
        .def_readwrite("out_enc_policy", &pe_settings::out_enc_policy)
        .def_readwrite("in_enc_policy", &pe_settings::in_enc_policy)
        .def_readwrite("allowed_enc_level", &pe_settings::allowed_enc_level)
        .def_readwrite("prefer_rc4", &pe_settings::prefer_rc4)
    ;

    pes.attr("forced") = pe_settings::forced;
    pes.attr("enabled") = pe_settings::enabled;
    pes.attr("disabled") = pe_settings::disabled;
    pes.attr("plaintext") = pe_settings::plaintext;
    pes.attr("rc4") = pe_settings::rc4;
}

