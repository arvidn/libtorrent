// Copyright Daniel Wallin 2007. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <libtorrent/peer_info.hpp>
#include <boost/python.hpp>

using namespace boost::python;
using namespace libtorrent;

void bind_peer_info()
{
    scope pi = class_<peer_info>("peer_info")
        .def_readonly("flags", &peer_info::flags)
        .def_readonly("ip", &peer_info::ip)
        .def_readonly("up_speed", &peer_info::up_speed)
        .def_readonly("down_speed", &peer_info::down_speed)
        .def_readonly("payload_up_speed", &peer_info::payload_up_speed)
        .def_readonly("payload_down_speed", &peer_info::payload_down_speed)
        .def_readonly("total_download", &peer_info::total_download)
        .def_readonly("total_upload", &peer_info::total_upload)
        .def_readonly("pid", &peer_info::pid)
        .def_readonly("pieces", &peer_info::pieces)
        .def_readonly("seed", &peer_info::seed)
        .def_readonly("upload_limit", &peer_info::upload_limit)
        .def_readonly("download_limit", &peer_info::download_limit)
        .def_readonly("load_balancing", &peer_info::load_balancing)
        .def_readonly("download_queue_length", &peer_info::download_queue_length)
        .def_readonly("upload_queue_length", &peer_info::upload_queue_length)
        .def_readonly("downloading_piece_index", &peer_info::downloading_piece_index)
        .def_readonly("downloading_block_index", &peer_info::downloading_block_index)
        .def_readonly("downloading_progress", &peer_info::downloading_progress)
        .def_readonly("downloading_total", &peer_info::downloading_total)
        .def_readonly("client", &peer_info::client)
        .def_readonly("connection_type", &peer_info::connection_type)
        ;

    pi.attr("interesting") = (int)peer_info::interesting;
    pi.attr("choked") = (int)peer_info::choked;
    pi.attr("remote_interested") = (int)peer_info::remote_interested;
    pi.attr("remote_choked") = (int)peer_info::remote_choked;
    pi.attr("supports_extensions") = (int)peer_info::supports_extensions;
    pi.attr("local_connection") = (int)peer_info::local_connection;
    pi.attr("handshake") = (int)peer_info::handshake;
    pi.attr("connecting") = (int)peer_info::connecting;
    pi.attr("queued") = (int)peer_info::queued;

    pi.attr("standard_bittorrent") = 0;
    pi.attr("web_seed") = 1;
}

