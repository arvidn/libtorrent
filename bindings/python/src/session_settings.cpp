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
}

