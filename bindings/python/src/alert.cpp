// Copyright Daniel Wallin 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <boost/python.hpp>
#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>
#include <memory>
#include "bytes.hpp"

using namespace boost::python;
using namespace libtorrent;

bytes get_buffer(read_piece_alert const& rpa)
{
    return rpa.buffer ? bytes(rpa.buffer.get(), rpa.size)
       : bytes();
}

tuple endpoint_to_tuple(tcp::endpoint const& ep)
{
    return boost::python::make_tuple(ep.address().to_string(), ep.port());
}

tuple peer_alert_ip(peer_alert const& pa)
{
    return endpoint_to_tuple(pa.ip);
}

std::string peer_blocked_alert_ip(peer_blocked_alert const& pa)
{
    error_code ec;
    return pa.ip.to_string(ec);
}

std::string dht_announce_alert_ip(dht_announce_alert const& pa)
{
    error_code ec;
    return pa.ip.to_string(ec);
}

tuple incoming_connection_alert_ip(incoming_connection_alert const& ica)
{
    return endpoint_to_tuple(ica.ip);
}

std::string external_ip_alert_ip(external_ip_alert const& eia)
{
    return eia.external_address.to_string();
}

list stats_alert_transferred(stats_alert const& alert)
{
   list result;
   for (int i = 0; i < alert.num_channels; ++i) {
      result.append(alert.transferred[i]);
   }
   return result;
}

list get_status_from_update_alert(state_update_alert const& alert)
{
   list result;

   for (std::vector<torrent_status>::const_iterator i = alert.status.begin(); i != alert.status.end(); ++i)
   {
      result.append(*i);
   }
   return result;
}

dict get_params(add_torrent_alert const& alert)
{
    add_torrent_params const& p = alert.params;
    dict ret;
    ret["ti"] = p.ti;
    ret["info_hash"] = p.info_hash;
    ret["name"] = p.name;
    ret["save_path"] = p.save_path;
    ret["storage_mode"] = p.storage_mode;
    list trackers;
    for (std::vector<std::string>::const_iterator i = p.trackers.begin();
       i != p.trackers.end(); ++i)
    {
        trackers.append(*i);
    }
    ret["trackers"] = trackers;
    // TODO: dht_nodes
    ret["flags"] = p.flags;
    ret["trackerid"] = p.trackerid;
    ret["url"] = p.url;
    ret["source_feed_url"] = p.source_feed_url;
    ret["uuid"] = p.uuid;
    return ret;
}


void bind_alert()
{
    using boost::noncopyable;

#if BOOST_VERSION >= 106000
    if (boost::python::converter::registry::query(
        boost::python::type_id <boost::shared_ptr<alert> >()) == NULL)
    {
        register_ptr_to_python<boost::shared_ptr<alert> >();
    }
#endif

    {
        scope alert_scope = class_<alert, boost::shared_ptr<alert>, noncopyable >("alert", no_init)
            .def("message", &alert::message)
            .def("what", &alert::what)
            .def("category", &alert::category)
#ifndef TORRENT_NO_DEPRECATE
            .def("severity", &alert::severity)
#endif
            .def("__str__", &alert::message)
            ;

#ifndef TORRENT_NO_DEPRECATE
        enum_<alert::severity_t>("severity_levels")
            .value("debug", alert::debug)
            .value("info", alert::info)
            .value("warning", alert::warning)
            .value("critical", alert::critical)
            .value("fatal", alert::fatal)
            .value("none", alert::none)
            ;
#endif

        enum_<alert::category_t>("category_t")
            .value("error_notification", alert::error_notification)
            .value("peer_notification", alert::peer_notification)
            .value("port_mapping_notification", alert::port_mapping_notification)
            .value("storage_notification", alert::storage_notification)
            .value("tracker_notification", alert::tracker_notification)
            .value("debug_notification", alert::debug_notification)
            .value("status_notification", alert::status_notification)
            .value("progress_notification", alert::progress_notification)
            .value("ip_block_notification", alert::ip_block_notification)
            .value("performance_warning", alert::performance_warning)
            .value("stats_notification", alert::stats_notification)
            // deliberately not INT_MAX. Arch linux crash while throwing an exception
            .value("all_categories", (alert::category_t)0xfffffff)
            ;

    }

    class_<torrent_alert, bases<alert>, noncopyable>(
        "torrent_alert", no_init)
        .def_readonly("handle", &torrent_alert::handle)
        ;

    class_<tracker_alert, bases<torrent_alert>, noncopyable>(
        "tracker_alert", no_init)
        .def_readonly("url", &tracker_alert::url)
        ;

    class_<torrent_added_alert, bases<torrent_alert>, noncopyable>(
        "torrent_added_alert", no_init)
        ;

    class_<torrent_removed_alert, bases<torrent_alert>, noncopyable>(
        "torrent_removed_alert", no_init)
        .def_readonly("info_hash", &torrent_removed_alert::info_hash) 
        ;

    class_<read_piece_alert, bases<torrent_alert>, noncopyable>(
        "read_piece_alert", 0, no_init)
        .add_property("buffer", get_buffer)
        .def_readonly("piece", &read_piece_alert::piece)
        .def_readonly("size", &read_piece_alert::size)
        ;

    class_<peer_alert, bases<torrent_alert>, noncopyable>(
        "peer_alert", no_init)
        .add_property("ip", &peer_alert_ip)
        .def_readonly("pid", &peer_alert::pid)
    ;
    class_<tracker_error_alert, bases<tracker_alert>, noncopyable>(
        "tracker_error_alert", no_init)
        .def_readonly("msg", &tracker_error_alert::msg)
        .def_readonly("times_in_row", &tracker_error_alert::times_in_row)
        .def_readonly("status_code", &tracker_error_alert::status_code)
        .def_readonly("error", &tracker_error_alert::error)
        ;

    class_<tracker_warning_alert, bases<tracker_alert>, noncopyable>(
        "tracker_warning_alert", no_init);

    class_<tracker_reply_alert, bases<tracker_alert>, noncopyable>(
        "tracker_reply_alert", no_init)
        .def_readonly("num_peers", &tracker_reply_alert::num_peers)
        ;

    class_<tracker_announce_alert, bases<tracker_alert>, noncopyable>(
        "tracker_announce_alert", no_init)
        .def_readonly("event", &tracker_announce_alert::event)
        ;

    class_<hash_failed_alert, bases<torrent_alert>, noncopyable>(
        "hash_failed_alert", no_init)
        .def_readonly("piece_index", &hash_failed_alert::piece_index)
        ;

    class_<peer_ban_alert, bases<peer_alert>, noncopyable>(
        "peer_ban_alert", no_init);

    class_<peer_error_alert, bases<peer_alert>, noncopyable>(
        "peer_error_alert", no_init)
        .def_readonly("error", &peer_error_alert::error)
        ;

    class_<invalid_request_alert, bases<peer_alert>, noncopyable>(
        "invalid_request_alert", no_init)
        .def_readonly("request", &invalid_request_alert::request)
        ;

    class_<peer_request>("peer_request")
        .def_readonly("piece", &peer_request::piece)
        .def_readonly("start", &peer_request::start)
        .def_readonly("length", &peer_request::length)
        .def(self == self)
        ;

    class_<torrent_error_alert, bases<torrent_alert>, noncopyable>(
        "torrent_error_alert", no_init)
        .def_readonly("error", &torrent_error_alert::error)
        ;

    class_<torrent_finished_alert, bases<torrent_alert>, noncopyable>(
        "torrent_finished_alert", no_init);

    class_<piece_finished_alert, bases<torrent_alert>, noncopyable>(
        "piece_finished_alert", no_init)
        .def_readonly("piece_index", &piece_finished_alert::piece_index)
        ;

    class_<block_finished_alert, bases<peer_alert>, noncopyable>(
        "block_finished_alert", no_init)
        .def_readonly("block_index", &block_finished_alert::block_index)
        .def_readonly("piece_index", &block_finished_alert::piece_index)
        ;

    class_<block_downloading_alert, bases<peer_alert>, noncopyable>(
        "block_downloading_alert", no_init)
        .def_readonly("peer_speedmsg", &block_downloading_alert::peer_speedmsg)
        .def_readonly("block_index", &block_downloading_alert::block_index)
        .def_readonly("piece_index", &block_downloading_alert::piece_index)
        ;

    class_<storage_moved_alert, bases<torrent_alert>, noncopyable>(
        "storage_moved_alert", no_init)
        .def_readonly("path", &storage_moved_alert::path)
        ;

    class_<storage_moved_failed_alert, bases<torrent_alert>, noncopyable>(
        "storage_moved_failed_alert", no_init)
        .def_readonly("error", &storage_moved_failed_alert::error)
        ;

    class_<torrent_deleted_alert, bases<torrent_alert>, noncopyable>(
        "torrent_deleted_alert", no_init)
        .def_readonly("info_hash", &torrent_deleted_alert::info_hash)
    ;

    class_<torrent_paused_alert, bases<torrent_alert>, noncopyable>(
        "torrent_paused_alert", no_init);

    class_<torrent_checked_alert, bases<torrent_alert>, noncopyable>(
        "torrent_checked_alert", no_init);

    class_<url_seed_alert, bases<torrent_alert>, noncopyable>(
        "url_seed_alert", no_init)
        .def_readonly("url", &url_seed_alert::url)
        .def_readonly("msg", &url_seed_alert::msg)
        ;

    class_<file_error_alert, bases<torrent_alert>, noncopyable>(
        "file_error_alert", no_init)
        .def_readonly("file", &file_error_alert::file)
        .def_readonly("error", &file_error_alert::error)
#ifndef TORRENT_NO_DEPRECATE
        .def_readonly("msg", &file_error_alert::msg)
#endif
        ;

    class_<metadata_failed_alert, bases<torrent_alert>, noncopyable>(
        "metadata_failed_alert", no_init);

    class_<metadata_received_alert, bases<torrent_alert>, noncopyable>(
        "metadata_received_alert", no_init);

    class_<listen_failed_alert, bases<alert>, noncopyable>(
        "listen_failed_alert", no_init)
        .def_readonly("endpoint", &listen_failed_alert::endpoint)
        .def_readonly("error", &listen_failed_alert::error)
        ;

    class_<listen_succeeded_alert, bases<alert>, noncopyable>(
        "listen_succeeded_alert", no_init)
        .def_readonly("endpoint", &listen_succeeded_alert::endpoint)
        ;

    class_<portmap_error_alert, bases<alert>, noncopyable>(
        "portmap_error_alert", no_init)
        .def_readonly("mapping", &portmap_error_alert::mapping)
        .def_readonly("map_type", &portmap_error_alert::map_type)
        .def_readonly("error", &portmap_error_alert::error)
#ifndef TORRENT_NO_DEPRECATE
        .def_readonly("type", &portmap_error_alert::map_type)
        .def_readonly("msg", &portmap_error_alert::msg)
#endif
        ;

    class_<portmap_alert, bases<alert>, noncopyable>(
        "portmap_alert", no_init)
        .def_readonly("mapping", &portmap_alert::mapping)
        .def_readonly("external_port", &portmap_alert::external_port)
#ifndef TORRENT_NO_DEPRECATE
        .def_readonly("type", &portmap_alert::map_type)
#endif
        .def_readonly("map_type", &portmap_alert::map_type)
        ;

    class_<portmap_log_alert, bases<alert>, noncopyable>(
        "portmap_log_alert", no_init)
        .def_readonly("map_type", &portmap_log_alert::map_type)
#ifndef TORRENT_NO_DEPRECATE
        .def_readonly("type", &portmap_log_alert::map_type)
        .def_readonly("msg", &portmap_log_alert::msg)
#endif
        ;

    class_<fastresume_rejected_alert, bases<torrent_alert>, noncopyable>(
        "fastresume_rejected_alert", no_init)
        .def_readonly("error", &fastresume_rejected_alert::error)
#ifndef TORRENT_NO_DEPRECATE
        .def_readonly("msg", &fastresume_rejected_alert::msg)
#endif
        ;

    class_<peer_blocked_alert, bases<alert>, noncopyable>(
        "peer_blocked_alert", no_init)
        .add_property("ip", &peer_blocked_alert_ip)
        ;

    class_<scrape_reply_alert, bases<tracker_alert>, noncopyable>(
        "scrape_reply_alert", no_init)
        .def_readonly("incomplete", &scrape_reply_alert::incomplete)
        .def_readonly("complete", &scrape_reply_alert::complete)
        ;

    class_<scrape_failed_alert, bases<tracker_alert>, noncopyable>(
        "scrape_failed_alert", no_init)
        .def_readonly("msg", &scrape_failed_alert::msg)
        ;

    class_<udp_error_alert, bases<alert>, noncopyable>(
        "udp_error_alert", no_init)
        .def_readonly("endpoint", &udp_error_alert::endpoint)
        .def_readonly("error", &udp_error_alert::error)
        ;

    class_<external_ip_alert, bases<alert>, noncopyable>(
        "external_ip_alert", no_init)
        .add_property("external_address", &external_ip_alert_ip)
        ;

    class_<save_resume_data_alert, bases<torrent_alert>, noncopyable>(
        "save_resume_data_alert", no_init)
        .def_readonly("resume_data", &save_resume_data_alert::resume_data)
        ;

    class_<file_completed_alert, bases<torrent_alert>, noncopyable>(
        "file_completed_alert", no_init)
        .def_readonly("index", &file_completed_alert::index)
        ;

    class_<file_renamed_alert, bases<torrent_alert>, noncopyable>(
        "file_renamed_alert", no_init)
        .def_readonly("index", &file_renamed_alert::index)
        .def_readonly("name", &file_renamed_alert::name)
        ;

    class_<file_rename_failed_alert, bases<torrent_alert>, noncopyable>(
        "file_rename_failed_alert", no_init)
        .def_readonly("index", &file_rename_failed_alert::index)
        .def_readonly("error", &file_rename_failed_alert::error)
        ;

    class_<torrent_resumed_alert, bases<torrent_alert>, noncopyable>(
        "torrent_resumed_alert", no_init
    );

    class_<state_changed_alert, bases<torrent_alert>, noncopyable>(
        "state_changed_alert", no_init)
        .def_readonly("state", &state_changed_alert::state)
        .def_readonly("prev_state", &state_changed_alert::prev_state)
        ;

    class_<state_update_alert, bases<alert>, noncopyable>(
        "state_update_alert", no_init)
        .add_property("status", &get_status_from_update_alert)
        ;

    class_<i2p_alert, bases<alert>, noncopyable>(
        "i2p_alert", no_init)
        .add_property("error", &i2p_alert::error)
        ;

    class_<dht_reply_alert, bases<tracker_alert>, noncopyable>(
        "dht_reply_alert", no_init)
        .def_readonly("num_peers", &dht_reply_alert::num_peers)
        ;

    class_<dht_announce_alert, bases<alert>, noncopyable>(
        "dht_announce_alert", no_init)
        .add_property("ip", &dht_announce_alert_ip)
        .def_readonly("port", &dht_announce_alert::port)
        .def_readonly("info_hash", &dht_announce_alert::info_hash)
    ;

    class_<dht_get_peers_alert, bases<alert>, noncopyable>(
        "dht_get_peers_alert", no_init
    )
        .def_readonly("info_hash", &dht_get_peers_alert::info_hash)
    ;

    class_<peer_unsnubbed_alert, bases<peer_alert>, noncopyable>(
        "peer_unsnubbed_alert", no_init
    );

    class_<peer_snubbed_alert, bases<peer_alert>, noncopyable>(
        "peer_snubbed_alert", no_init
    );

    class_<peer_connect_alert, bases<peer_alert>, noncopyable>(
        "peer_connect_alert", no_init
    );

    class_<peer_disconnected_alert, bases<peer_alert>, noncopyable>(
        "peer_disconnected_alert", no_init)
        .def_readonly("error", &peer_disconnected_alert::error)
#ifndef TORRENT_NO_DEPRECATE
        .def_readonly("msg", &peer_disconnected_alert::msg)
#endif
        ;

    class_<request_dropped_alert, bases<peer_alert>, noncopyable>(
        "request_dropped_alert", no_init)
        .def_readonly("block_index", &request_dropped_alert::block_index)
        .def_readonly("piece_index", &request_dropped_alert::piece_index)
    ;

    class_<block_timeout_alert, bases<peer_alert>, noncopyable>(
        "block_timeout_alert", no_init)
        .def_readonly("block_index", &block_timeout_alert::block_index)
        .def_readonly("piece_index", &block_timeout_alert::piece_index)
    ;

    class_<unwanted_block_alert, bases<peer_alert>, noncopyable>(
        "unwanted_block_alert", no_init)
        .def_readonly("block_index", &unwanted_block_alert::block_index)
        .def_readonly("piece_index", &unwanted_block_alert::piece_index)
    ;

    class_<torrent_delete_failed_alert, bases<torrent_alert>, noncopyable>(
        "torrent_delete_failed_alert", no_init)
#ifndef TORRENT_NO_DEPRECATE
        .def_readonly("msg", &torrent_delete_failed_alert::msg)
#endif
        .def_readonly("error", &torrent_delete_failed_alert::error)
        ;

    class_<save_resume_data_failed_alert, bases<torrent_alert>, noncopyable>(
        "save_resume_data_failed_alert", no_init)
#ifndef TORRENT_NO_DEPRECATE
        .def_readonly("msg", &save_resume_data_failed_alert::msg)
#endif
        .def_readonly("error", &save_resume_data_failed_alert::error)
        ;

    class_<performance_alert, bases<torrent_alert>, noncopyable>(
        "performance_alert", no_init)
        .def_readonly("warning_code", &performance_alert::warning_code)
    ;
    enum_<performance_alert::performance_warning_t>("performance_warning_t")
        .value("outstanding_disk_buffer_limit_reached", performance_alert::outstanding_disk_buffer_limit_reached)
        .value("outstanding_request_limit_reached", performance_alert::outstanding_request_limit_reached)
        .value("upload_limit_too_low", performance_alert::upload_limit_too_low)
        .value("download_limit_too_low", performance_alert::download_limit_too_low)
        .value("send_buffer_watermark_too_low", performance_alert::send_buffer_watermark_too_low)
        .value("too_many_optimistic_unchoke_slots", performance_alert::too_many_optimistic_unchoke_slots)
        .value("bittyrant_with_no_uplimit", performance_alert::bittyrant_with_no_uplimit)
        .value("too_high_disk_queue_limit", performance_alert::too_high_disk_queue_limit)
        .value("too_few_outgoing_ports", performance_alert::too_few_outgoing_ports)
        .value("too_few_file_descriptors", performance_alert::too_few_file_descriptors)
    ;

    class_<stats_alert, bases<torrent_alert>, noncopyable>(
        "stats_alert", no_init)
        .add_property("transferred", &stats_alert_transferred)
        .def_readonly("interval", &stats_alert::interval)
    ;

    enum_<stats_alert::stats_channel>("stats_channel")
        .value("upload_payload", stats_alert::upload_payload)
        .value("upload_protocol", stats_alert::upload_protocol)
        .value("upload_ip_protocol", stats_alert::upload_ip_protocol)
        .value("upload_dht_protocol", stats_alert::upload_dht_protocol)
        .value("upload_tracker_protocol", stats_alert::upload_tracker_protocol)
        .value("download_payload", stats_alert::download_payload)
        .value("download_protocol", stats_alert::download_protocol)
        .value("download_ip_protocol", stats_alert::download_ip_protocol)
        .value("download_dht_protocol", stats_alert::download_dht_protocol)
        .value("download_tracker_protocol", stats_alert::download_tracker_protocol)
    ;

    class_<anonymous_mode_alert, bases<torrent_alert>, noncopyable>(
        "anonymous_mode_alert", no_init)
        .def_readonly("kind", &anonymous_mode_alert::kind)
        .def_readonly("str", &anonymous_mode_alert::str)
    ;

    enum_<anonymous_mode_alert::kind_t>("kind")
        .value("tracker_no_anonymous", anonymous_mode_alert::tracker_not_anonymous)
    ;

    class_<incoming_connection_alert, bases<alert>, noncopyable>(
        "incoming_connection_alert", no_init)
        .def_readonly("socket_type", &incoming_connection_alert::socket_type)
        .add_property("ip", &incoming_connection_alert_ip)
        ;
    class_<torrent_need_cert_alert, bases<torrent_alert>, noncopyable>(
        "torrent_need_cert_alert", no_init)
        .def_readonly("error", &torrent_need_cert_alert::error)
        ;

    class_<add_torrent_alert, bases<torrent_alert>, noncopyable>(
       "add_torrent_alert", no_init)
       .def_readonly("error", &add_torrent_alert::error)
       .add_property("params", &get_params)
       ;

    class_<torrent_update_alert, bases<torrent_alert>, noncopyable>(
       "torrent_update_alert", no_init)
        .def_readonly("old_ih", &torrent_update_alert::old_ih)
        .def_readonly("new_ih", &torrent_update_alert::new_ih)
        ;
}
