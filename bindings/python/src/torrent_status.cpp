// Copyright Daniel Wallin 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <libtorrent/torrent_handle.hpp>
#include <boost/python.hpp>

using namespace boost::python;
using namespace libtorrent;

object pieces(torrent_status const& s)
{
    list result;

    for (std::vector<bool>::const_iterator i(s.pieces->begin()), e(s.pieces->end()); i != e; ++i)
        result.append(*i);

    return result;
}

extern char const* torrent_status_doc;
extern char const* torrent_status_state_doc;
extern char const* torrent_status_paused_doc;
extern char const* torrent_status_progress_doc;
extern char const* torrent_status_next_announce_doc;
extern char const* torrent_status_announce_interval_doc;
extern char const* torrent_status_current_tracker_doc;
extern char const* torrent_status_total_download_doc;
extern char const* torrent_status_total_upload_doc;
extern char const* torrent_status_total_payload_download_doc;
extern char const* torrent_status_total_payload_upload_doc;
extern char const* torrent_status_total_failed_bytes_doc;

void bind_torrent_status()
{
    scope status = class_<torrent_status>("torrent_status", torrent_status_doc)
        .def_readonly("state", &torrent_status::state, torrent_status_state_doc)
        .def_readonly("paused", &torrent_status::paused, torrent_status_paused_doc)
        .def_readonly("progress", &torrent_status::progress, torrent_status_progress_doc)
        .add_property(
            "next_announce"
          , make_getter(
                &torrent_status::next_announce, return_value_policy<return_by_value>()
            )
          , torrent_status_next_announce_doc
        )
        .add_property(
            "announce_interval"
          , make_getter(
                &torrent_status::announce_interval, return_value_policy<return_by_value>()
            )
          , torrent_status_announce_interval_doc
        )
        .def_readonly(
            "current_tracker", &torrent_status::current_tracker
          , torrent_status_current_tracker_doc
        )
        .def_readonly(
            "total_download", &torrent_status::total_download
          , torrent_status_total_download_doc
        )
        .def_readonly(
            "total_upload", &torrent_status::total_upload
          , torrent_status_total_upload_doc
        )
        .def_readonly(
            "total_payload_download", &torrent_status::total_payload_download
          , torrent_status_total_payload_download_doc
        )
        .def_readonly(
            "total_payload_upload", &torrent_status::total_payload_upload
          , torrent_status_total_payload_upload_doc
        )
        .def_readonly(
            "total_failed_bytes", &torrent_status::total_failed_bytes
          , torrent_status_total_failed_bytes_doc
        )
        .def_readonly("total_redundant_bytes", &torrent_status::total_redundant_bytes)
        .def_readonly("download_rate", &torrent_status::download_rate)
        .def_readonly("upload_rate", &torrent_status::upload_rate)
        .def_readonly("download_payload_rate", &torrent_status::download_payload_rate)
        .def_readonly("upload_payload_rate", &torrent_status::upload_payload_rate)
        .def_readonly("num_peers", &torrent_status::num_peers)
        .def_readonly("num_complete", &torrent_status::num_complete)
        .def_readonly("num_incomplete", &torrent_status::num_incomplete)
        .add_property("pieces", pieces)
        .def_readonly("num_pieces", &torrent_status::num_pieces)
        .def_readonly("total_done", &torrent_status::total_done)
        .def_readonly("total_wanted_done", &torrent_status::total_wanted_done)
        .def_readonly("total_wanted", &torrent_status::total_wanted)
        .def_readonly("num_seeds", &torrent_status::num_seeds)
        .def_readonly("distributed_copies", &torrent_status::distributed_copies)
        .def_readonly("block_size", &torrent_status::block_size)
        ;

    enum_<torrent_status::state_t>("states")
        .value("queued_for_checking", torrent_status::queued_for_checking)
        .value("checking_files", torrent_status::checking_files)
        .value("connecting_to_tracker", torrent_status::connecting_to_tracker)
        .value("downloading", torrent_status::downloading)
        .value("finished", torrent_status::finished)
        .value("seeding", torrent_status::seeding)
        .value("allocating", torrent_status::allocating)
        .export_values()
        ;
}

