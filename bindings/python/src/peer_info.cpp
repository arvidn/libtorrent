// Copyright Daniel Wallin 2007. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "boost_python.hpp"
#include "bytes.hpp"
#include <libtorrent/peer_info.hpp>
#include <libtorrent/bitfield.hpp>
#include <boost/python/iterator.hpp>

using namespace boost::python;
using namespace lt;

std::int64_t get_last_active(peer_info const& pi)
{
    return total_seconds(pi.last_active);
}

std::int64_t get_last_request(peer_info const& pi)
{
    return total_seconds(pi.last_request);
}

std::int64_t get_download_queue_time(peer_info const& pi)
{
    return total_seconds(pi.download_queue_time);
}

tuple get_local_endpoint(peer_info const& pi)
{
    return boost::python::make_tuple(pi.local_endpoint.address().to_string(), pi.local_endpoint.port());
}

tuple get_ip(peer_info const& pi)
{
    return boost::python::make_tuple(pi.ip.address().to_string(), pi.ip.port());
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

bytes get_peer_info_client(peer_info const& pi)
{
	return pi.client;
}

using by_value = return_value_policy<return_by_value>;
void bind_peer_info()
{
    scope pi = class_<peer_info>("peer_info")
        .add_property("flags", make_getter(&peer_info::flags, by_value()))
        .add_property("source", make_getter(&peer_info::source, by_value()))
        .add_property("read_state", make_getter(&peer_info::read_state, by_value()))
        .add_property("write_state", make_getter(&peer_info::write_state, by_value()))
        .add_property("ip", get_ip)
        .def_readonly("up_speed", &peer_info::up_speed)
        .def_readonly("down_speed", &peer_info::down_speed)
        .def_readonly("payload_up_speed", &peer_info::payload_up_speed)
        .def_readonly("payload_down_speed", &peer_info::payload_down_speed)
        .def_readonly("total_download", &peer_info::total_download)
        .def_readonly("total_upload", &peer_info::total_upload)
        .def_readonly("pid", &peer_info::pid)
        .add_property("pieces", get_pieces)
#if TORRENT_ABI_VERSION == 1
        .def_readonly("upload_limit", &peer_info::upload_limit)
        .def_readonly("download_limit", &peer_info::download_limit)
        .def_readonly("load_balancing", &peer_info::load_balancing)
        .def_readonly("remote_dl_rate", &peer_info::remote_dl_rate)
#endif
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
        .def_readonly("download_queue_length", &peer_info::download_queue_length)
        .def_readonly("upload_queue_length", &peer_info::upload_queue_length)
        .def_readonly("failcount", &peer_info::failcount)
        .add_property("downloading_piece_index", make_getter(&peer_info::downloading_piece_index, by_value()))
        .add_property("downloading_block_index", make_getter(&peer_info::downloading_block_index, by_value()))
        .def_readonly("downloading_progress", &peer_info::downloading_progress)
        .def_readonly("downloading_total", &peer_info::downloading_total)
        .add_property("client", get_peer_info_client)
        .def_readonly("connection_type", &peer_info::connection_type)
        .def_readonly("pending_disk_bytes", &peer_info::pending_disk_bytes)
        .def_readonly("send_quota", &peer_info::send_quota)
        .def_readonly("receive_quota", &peer_info::receive_quota)
        .def_readonly("rtt", &peer_info::rtt)
        .def_readonly("num_pieces", &peer_info::num_pieces)
        .def_readonly("download_rate_peak", &peer_info::download_rate_peak)
        .def_readonly("upload_rate_peak", &peer_info::upload_rate_peak)
        .def_readonly("progress", &peer_info::progress)
        .def_readonly("progress_ppm", &peer_info::progress_ppm)
#if TORRENT_ABI_VERSION == 1
        .def_readonly("estimated_reciprocation_rate", &peer_info::estimated_reciprocation_rate)
#endif
        .add_property("local_endpoint", get_local_endpoint)
        ;

    // flags
    pi.attr("interesting") = peer_info::interesting;
    pi.attr("choked") = peer_info::choked;
    pi.attr("remote_interested") = peer_info::remote_interested;
    pi.attr("remote_choked") = peer_info::remote_choked;
    pi.attr("supports_extensions") = peer_info::supports_extensions;
    pi.attr("local_connection") = peer_info::local_connection;
    pi.attr("outgoing_connection") = peer_info::outgoing_connection;
    pi.attr("handshake") = peer_info::handshake;
    pi.attr("connecting") = peer_info::connecting;
#if TORRENT_ABI_VERSION == 1
    pi.attr("queued") = peer_info::queued;
#endif
    pi.attr("on_parole") = peer_info::on_parole;
    pi.attr("seed") = peer_info::seed;
    pi.attr("optimistic_unchoke") = peer_info::optimistic_unchoke;
    pi.attr("snubbed") = peer_info::snubbed;
    pi.attr("upload_only") = peer_info::upload_only;
    pi.attr("endgame_mode") = peer_info::endgame_mode;
    pi.attr("holepunched") = peer_info::holepunched;
#ifndef TORRENT_DISABLE_ENCRYPTION
    pi.attr("rc4_encrypted") = peer_info::rc4_encrypted;
    pi.attr("plaintext_encrypted") = peer_info::plaintext_encrypted;
#endif

    // connection_type
    pi.attr("standard_bittorrent") = peer_info::standard_bittorrent;
    pi.attr("web_seed") = peer_info::web_seed;
    pi.attr("http_seed") = peer_info::http_seed;

    // source
    pi.attr("tracker") = peer_info::tracker;
    pi.attr("dht") = peer_info::dht;
    pi.attr("pex") = peer_info::pex;
    pi.attr("lsd") = peer_info::lsd;
    pi.attr("resume_data") = peer_info::resume_data;

    // read/write state
    pi.attr("bw_idle") = peer_info::bw_idle;
#if TORRENT_ABI_VERSION == 1
    pi.attr("bw_torrent") = peer_info::bw_torrent;
    pi.attr("bw_global") = peer_info::bw_global;
#endif
    pi.attr("bw_limit") = peer_info::bw_limit;
    pi.attr("bw_network") = peer_info::bw_network;
    pi.attr("bw_disk") = peer_info::bw_disk;
}

