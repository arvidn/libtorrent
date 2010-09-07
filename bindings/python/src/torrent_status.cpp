// Copyright Daniel Wallin 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <boost/python.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/bitfield.hpp>

using namespace boost::python;
using namespace libtorrent;

object pieces(torrent_status const& s)
{
    list result;

    for (bitfield::const_iterator i(s.pieces.begin()), e(s.pieces.end()); i != e; ++i)
        result.append(*i);

    return result;
}

void bind_torrent_status()
{
    scope status = class_<torrent_status>("torrent_status")
        .def_readonly("state", &torrent_status::state)
        .def_readonly("paused", &torrent_status::paused)
        .def_readonly("progress", &torrent_status::progress)
        .def_readonly("progress_ppm", &torrent_status::progress_ppm)
        .add_property(
            "next_announce"
          , make_getter(
                &torrent_status::next_announce, return_value_policy<return_by_value>()
            )
        )
        .add_property(
            "announce_interval"
          , make_getter(
                &torrent_status::announce_interval, return_value_policy<return_by_value>()
            )
        )
        .def_readonly("current_tracker", &torrent_status::current_tracker)
        .def_readonly("total_download", &torrent_status::total_download)
        .def_readonly("total_upload", &torrent_status::total_upload)
        .def_readonly("total_payload_download", &torrent_status::total_payload_download)
        .def_readonly("total_payload_upload", &torrent_status::total_payload_upload)
        .def_readonly("total_failed_bytes", &torrent_status::total_failed_bytes)
        .def_readonly("total_redundant_bytes", &torrent_status::total_redundant_bytes)
        .def_readonly("download_rate", &torrent_status::download_rate)
        .def_readonly("upload_rate", &torrent_status::upload_rate)
        .def_readonly("download_payload_rate", &torrent_status::download_payload_rate)
        .def_readonly("upload_payload_rate", &torrent_status::upload_payload_rate)
        .def_readonly("num_seeds", &torrent_status::num_seeds)
        .def_readonly("num_peers", &torrent_status::num_peers)
        .def_readonly("num_complete", &torrent_status::num_complete)
        .def_readonly("num_incomplete", &torrent_status::num_incomplete)
        .def_readonly("list_seeds", &torrent_status::list_seeds)
        .def_readonly("list_peers", &torrent_status::list_peers)
        .add_property("pieces", pieces)
        .def_readonly("num_pieces", &torrent_status::num_pieces)
        .def_readonly("total_done", &torrent_status::total_done)
        .def_readonly("total_wanted_done", &torrent_status::total_wanted_done)
        .def_readonly("total_wanted", &torrent_status::total_wanted)
        .def_readonly("distributed_full_copies", &torrent_status::distributed_full_copies)
        .def_readonly("distributed_fraction", &torrent_status::distributed_fraction)
        .def_readonly("distributed_copies", &torrent_status::distributed_copies)
        .def_readonly("block_size", &torrent_status::block_size)
        .def_readonly("num_uploads", &torrent_status::num_uploads)
        .def_readonly("num_connections", &torrent_status::num_connections)
        .def_readonly("uploads_limit", &torrent_status::uploads_limit)
        .def_readonly("connections_limit", &torrent_status::connections_limit)
        .def_readonly("storage_mode", &torrent_status::storage_mode)
        .def_readonly("up_bandwidth_queue", &torrent_status::up_bandwidth_queue)
        .def_readonly("down_bandwidth_queue", &torrent_status::down_bandwidth_queue)
        .def_readonly("all_time_upload", &torrent_status::all_time_upload)
        .def_readonly("all_time_download", &torrent_status::all_time_download)
        .def_readonly("active_time", &torrent_status::active_time)
        .def_readonly("finished_time", &torrent_status::finished_time)
        .def_readonly("seeding_time", &torrent_status::seeding_time)
        .def_readonly("seed_rank", &torrent_status::seed_rank)
        .def_readonly("last_scrape", &torrent_status::last_scrape)
        .def_readonly("error", &torrent_status::error)
        .def_readonly("priority", &torrent_status::priority)
        ;

    enum_<torrent_status::state_t>("states")
        .value("queued_for_checking", torrent_status::queued_for_checking)
        .value("checking_files", torrent_status::checking_files)
        .value("downloading", torrent_status::downloading)
        .value("finished", torrent_status::finished)
        .value("seeding", torrent_status::seeding)
        .value("allocating", torrent_status::allocating)
        .export_values()
        ;
}

