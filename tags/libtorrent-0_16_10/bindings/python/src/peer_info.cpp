// Copyright Daniel Wallin 2007. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <libtorrent/peer_info.hpp>
#include <libtorrent/bitfield.hpp>
#include <boost/python.hpp>
#include <boost/python/iterator.hpp>

using namespace boost::python;
using namespace libtorrent;

int get_last_active(peer_info const& pi)
{
    return total_seconds(pi.last_active);
}

int get_last_request(peer_info const& pi)
{
    return total_seconds(pi.last_request);
}

int get_download_queue_time(peer_info const& pi)
{
    return total_seconds(pi.download_queue_time);
}

#ifndef TORRENT_DISABLE_RESOLVE_COUNTRIES
str get_country(peer_info const& pi)
{
    return str(pi.country, 2);
}
#endif

tuple get_local_endpoint(peer_info const& pi)
{
    return make_tuple(pi.local_endpoint.address().to_string(), pi.local_endpoint.port());
}

tuple get_ip(peer_info const& pi)
{
    return make_tuple(pi.ip.address().to_string(), pi.ip.port());
}

list get_pieces(peer_info const& pi)
{
    list ret;

    for (bitfield::const_iterator i = pi.pieces.begin()
        , end(pi.pieces.end()); i != end; ++i)
    {
        ret.append(*i);
    }
    return ret;
}

void bind_peer_info()
{
    scope pi = class_<peer_info>("peer_info")
        .def_readonly("flags", &peer_info::flags)
        .def_readonly("source", &peer_info::source)
        .def_readonly("read_state", &peer_info::read_state)
        .def_readonly("write_state", &peer_info::write_state)
        .add_property("ip", get_ip)
        .def_readonly("up_speed", &peer_info::up_speed)
        .def_readonly("down_speed", &peer_info::down_speed)
        .def_readonly("payload_up_speed", &peer_info::payload_up_speed)
        .def_readonly("payload_down_speed", &peer_info::payload_down_speed)
        .def_readonly("total_download", &peer_info::total_download)
        .def_readonly("total_upload", &peer_info::total_upload)
        .def_readonly("pid", &peer_info::pid)
        .add_property("pieces", get_pieces)
        .def_readonly("upload_limit", &peer_info::upload_limit)
        .def_readonly("download_limit", &peer_info::download_limit)
        .add_property("last_request", get_last_request)
        .add_property("last_active", get_last_active)
        .add_property("download_queue_time", get_download_queue_time)
        .def_readonly("queue_bytes", &peer_info::queue_bytes)
        .def_readonly("request_timeout", &peer_info::request_timeout)
        .def_readonly("send_buffer_size", &peer_info::send_buffer_size)
        .def_readonly("used_send_buffer", &peer_info::used_send_buffer)
        .def_readonly("receive_buffer_size", &peer_info::receive_buffer_size)
        .def_readonly("used_receive_buffer", &peer_info::used_receive_buffer)
        .def_readonly("num_hashfails", &peer_info::num_hashfails)
#ifndef TORRENT_DISABLE_RESOLVE_COUNTRIES
        .add_property("country", get_country)
#endif
#ifndef TORRENT_DISABLE_GEO_IP
        .def_readonly("inet_as_name", &peer_info::inet_as_name)
        .def_readonly("inet_as", &peer_info::inet_as)
#endif
        .def_readonly("load_balancing", &peer_info::load_balancing)
        .def_readonly("download_queue_length", &peer_info::download_queue_length)
        .def_readonly("upload_queue_length", &peer_info::upload_queue_length)
        .def_readonly("failcount", &peer_info::failcount)
        .def_readonly("downloading_piece_index", &peer_info::downloading_piece_index)
        .def_readonly("downloading_block_index", &peer_info::downloading_block_index)
        .def_readonly("downloading_progress", &peer_info::downloading_progress)
        .def_readonly("downloading_total", &peer_info::downloading_total)
        .def_readonly("client", &peer_info::client)
        .def_readonly("connection_type", &peer_info::connection_type)
        .def_readonly("remote_dl_rate", &peer_info::remote_dl_rate)
        .def_readonly("pending_disk_bytes", &peer_info::pending_disk_bytes)
        .def_readonly("send_quota", &peer_info::send_quota)
        .def_readonly("receive_quota", &peer_info::receive_quota)
        .def_readonly("rtt", &peer_info::rtt)
        .def_readonly("num_pieces", &peer_info::num_pieces)
        .def_readonly("download_rate_peak", &peer_info::download_rate_peak)
        .def_readonly("upload_rate_peak", &peer_info::upload_rate_peak)
        .def_readonly("progress", &peer_info::progress)
        .def_readonly("progress_ppm", &peer_info::progress_ppm)
        .def_readonly("estimated_reciprocation_rate", &peer_info::estimated_reciprocation_rate)
        .add_property("local_endpoint", get_local_endpoint)
        ;

    // flags
    pi.attr("interesting") = (int)peer_info::interesting;
    pi.attr("choked") = (int)peer_info::choked;
    pi.attr("remote_interested") = (int)peer_info::remote_interested;
    pi.attr("remote_choked") = (int)peer_info::remote_choked;
    pi.attr("supports_extensions") = (int)peer_info::supports_extensions;
    pi.attr("local_connection") = (int)peer_info::local_connection;
    pi.attr("handshake") = (int)peer_info::handshake;
    pi.attr("connecting") = (int)peer_info::connecting;
    pi.attr("queued") = (int)peer_info::queued;
    pi.attr("on_parole") = (int)peer_info::on_parole;
    pi.attr("seed") = (int)peer_info::seed;
    pi.attr("optimistic_unchoke") = (int)peer_info::optimistic_unchoke;
    pi.attr("snubbed") = (int)peer_info::snubbed;
    pi.attr("upload_only") = (int)peer_info::upload_only;
    pi.attr("endgame_mode") = (int)peer_info::endgame_mode;
    pi.attr("holepunched") = (int)peer_info::holepunched;
#ifndef TORRENT_DISABLE_ENCRYPTION
    pi.attr("rc4_encrypted") = (int)peer_info::rc4_encrypted;
    pi.attr("plaintext_encrypted") = (int)peer_info::plaintext_encrypted;
#endif

    // connection_type
    pi.attr("standard_bittorrent") = (int)peer_info::standard_bittorrent;
    pi.attr("web_seed") = (int)peer_info::web_seed;

    // source
    pi.attr("tracker") = (int)peer_info::tracker;
    pi.attr("dht") = (int)peer_info::dht;
    pi.attr("pex") = (int)peer_info::pex;
    pi.attr("lsd") = (int)peer_info::lsd;
    pi.attr("resume_data") = (int)peer_info::resume_data;

    // read/write state
    pi.attr("bw_idle") = (int)peer_info::bw_idle;
#ifndef TORRENT_NO_DEPRECATE
    pi.attr("bw_torrent") = (int)peer_info::bw_torrent;
    pi.attr("bw_global") = (int)peer_info::bw_global;
#endif
    pi.attr("bw_limit") = (int)peer_info::bw_limit;
    pi.attr("bw_network") = (int)peer_info::bw_network;
    pi.attr("bw_disk") = (int)peer_info::bw_disk;
}

