// Copyright Daniel Wallin 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "boost_python.hpp"
#include <libtorrent/torrent_status.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/bitfield.hpp>

using namespace boost::python;
using namespace lt;

using by_value = return_value_policy<return_by_value>;
std::shared_ptr<const torrent_info> get_torrent_file(torrent_status const& st)
{
	return st.torrent_file.lock();
}

void bind_torrent_status()
{
    scope status = class_<torrent_status>("torrent_status")
        .def(self == self)
        .def_readonly("handle", &torrent_status::handle)
        .def_readonly("info_hash", &torrent_status::info_hash)
        .add_property("torrent_file", &get_torrent_file)
        .def_readonly("state", &torrent_status::state)
#if TORRENT_ABI_VERSION == 1
        .def_readonly("paused", &torrent_status::paused)
        .def_readonly("stop_when_ready", &torrent_status::stop_when_ready)
        .def_readonly("auto_managed", &torrent_status::auto_managed)
        .def_readonly("sequential_download", &torrent_status::sequential_download)
#endif
        .def_readonly("is_seeding", &torrent_status::is_seeding)
        .def_readonly("is_finished", &torrent_status::is_finished)
        .def_readonly("has_metadata", &torrent_status::has_metadata)
        .def_readonly("progress", &torrent_status::progress)
        .def_readonly("progress_ppm", &torrent_status::progress_ppm)
        .add_property("next_announce", make_getter(&torrent_status::next_announce, by_value()))
#if TORRENT_ABI_VERSION == 1
        .add_property("announce_interval", make_getter(&torrent_status::announce_interval, by_value()))
#endif
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
        .def_readonly("connect_candidates", &torrent_status::connect_candidates)
        .add_property("pieces", make_getter(&torrent_status::pieces, by_value()))
        .add_property("verified_pieces", make_getter(&torrent_status::verified_pieces, by_value()))
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
        .def_readonly("seed_rank", &torrent_status::seed_rank)
        .def_readonly("has_incoming", &torrent_status::has_incoming)
#if TORRENT_ABI_VERSION == 1
        .def_readonly("seed_mode", &torrent_status::seed_mode)
        .def_readonly("upload_mode", &torrent_status::upload_mode)
        .def_readonly("share_mode", &torrent_status::share_mode)
        .def_readonly("super_seeding", &torrent_status::super_seeding)
        .def_readonly("active_time", &torrent_status::active_time)
        .def_readonly("finished_time", &torrent_status::finished_time)
        .def_readonly("seeding_time", &torrent_status::seeding_time)
        .def_readonly("last_scrape", &torrent_status::last_scrape)
        .def_readonly("error", &torrent_status::error)
        .def_readonly("priority", &torrent_status::priority)
        .def_readonly("time_since_upload", &torrent_status::time_since_upload)
        .def_readonly("time_since_download", &torrent_status::time_since_download)
#endif
        .def_readonly("errc", &torrent_status::errc)
        .add_property("error_file", make_getter(&torrent_status::error_file, by_value()))
        .def_readonly("name", &torrent_status::name)
        .def_readonly("save_path", &torrent_status::save_path)
        .def_readonly("added_time", &torrent_status::added_time)
        .def_readonly("completed_time", &torrent_status::completed_time)
        .def_readonly("last_seen_complete", &torrent_status::last_seen_complete)
        .add_property("queue_position", make_getter(&torrent_status::queue_position, by_value()))
        .def_readonly("need_save_resume", &torrent_status::need_save_resume)
#if TORRENT_ABI_VERSION == 1
        .def_readonly("ip_filter_applies", &torrent_status::ip_filter_applies)
#endif
        .def_readonly("moving_storage", &torrent_status::moving_storage)
#if TORRENT_ABI_VERSION == 1
        .def_readonly("is_loaded", &torrent_status::is_loaded)
#endif
        .def_readonly("announcing_to_trackers", &torrent_status::announcing_to_trackers)
        .def_readonly("announcing_to_lsd", &torrent_status::announcing_to_lsd)
        .def_readonly("announcing_to_dht", &torrent_status::announcing_to_dht)
        .def_readonly("info_hash", &torrent_status::info_hash)
        .add_property("last_upload", make_getter(&torrent_status::last_upload, by_value()))
        .add_property("last_download", make_getter(&torrent_status::last_download, by_value()))
        .add_property("active_duration", make_getter(&torrent_status::active_duration, by_value()))
        .add_property("finished_duration", make_getter(&torrent_status::finished_duration, by_value()))
        .add_property("seeding_duration", make_getter(&torrent_status::seeding_duration, by_value()))
        .add_property("flags", make_getter(&torrent_status::flags, by_value()))
        ;

    enum_<torrent_status::state_t>("states")
#if TORRENT_ABI_VERSION == 1
        .value("queued_for_checking", torrent_status::queued_for_checking)
#endif
        .value("checking_files", torrent_status::checking_files)
        .value("downloading_metadata", torrent_status::downloading_metadata)
        .value("downloading", torrent_status::downloading)
        .value("finished", torrent_status::finished)
        .value("seeding", torrent_status::seeding)
        .value("allocating", torrent_status::allocating)
        .value("checking_resume_data", torrent_status::checking_resume_data)
        .export_values()
        ;
}

