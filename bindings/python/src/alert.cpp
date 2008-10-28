// Copyright Daniel Wallin 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>
#include <boost/python.hpp>

using namespace boost::python;
using namespace libtorrent;

extern char const* alert_doc;
extern char const* alert_msg_doc;
extern char const* alert_severity_doc;
extern char const* torrent_alert_doc;
extern char const* tracker_alert_doc;
extern char const* tracker_error_alert_doc;
extern char const* tracker_warning_alert_doc;
extern char const* tracker_reply_alert_doc;
extern char const* tracker_announce_alert_doc;
extern char const* hash_failed_alert_doc;
extern char const* peer_ban_alert_doc;
extern char const* peer_error_alert_doc;
extern char const* invalid_request_alert_doc;
extern char const* peer_request_doc;
extern char const* torrent_finished_alert_doc;
extern char const* piece_finished_alert_doc;
extern char const* block_finished_alert_doc;
extern char const* block_downloading_alert_doc;
extern char const* storage_moved_alert_doc;
extern char const* torrent_deleted_alert_doc;
extern char const* torrent_paused_alert_doc;
extern char const* torrent_checked_alert_doc;
extern char const* url_seed_alert_doc;
extern char const* file_error_alert_doc;
extern char const* metadata_failed_alert_doc;
extern char const* metadata_received_alert_doc;
extern char const* listen_failed_alert_doc;
extern char const* listen_succeeded_alert_doc;
extern char const* portmap_error_alert_doc;
extern char const* portmap_alert_doc;
extern char const* fastresume_rejected_alert_doc;
extern char const* peer_blocked_alert_doc;
extern char const* scrape_reply_alert_doc;
extern char const* scrape_failed_alert_doc;
extern char const* udp_error_alert_doc;
extern char const* external_ip_alert_doc;
extern char const* save_resume_data_alert_doc;

void bind_alert()
{
    using boost::noncopyable;

    {
        scope alert_scope = class_<alert, noncopyable>("alert", alert_doc, no_init)
            .def("message", &alert::message, alert_msg_doc)
            .def("what", &alert::what)
            .def("category", &alert::category)
#ifndef TORRENT_NO_DEPRECATE
            .def("severity", &alert::severity, alert_severity_doc)
#endif
            .def("__str__", &alert::message, alert_msg_doc)
            ;

        enum_<alert::severity_t>("severity_levels")
            .value("debug", alert::debug)
            .value("info", alert::info)
            .value("warning", alert::warning)
            .value("critical", alert::critical)
            .value("fatal", alert::fatal)
            .value("none", alert::none)
            ;

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
            .value("all_categories", alert::all_categories)
            ;

    }

    class_<torrent_alert, bases<alert>, noncopyable>(
        "torrent_alert", torrent_alert_doc, no_init
    )
        .def_readonly("handle", &torrent_alert::handle)
        ;

    class_<tracker_alert, bases<torrent_alert>, noncopyable>(
        "tracker_alert", tracker_alert_doc, no_init
    )
        .def_readonly("url", &tracker_alert::url)
        ;

    class_<peer_alert, bases<torrent_alert>, noncopyable>(
        "peer_alert", no_init
    )
        .def_readonly("ip", &peer_alert::ip)
        .def_readonly("pid", &peer_alert::pid)
    ;
    class_<tracker_error_alert, bases<tracker_alert>, noncopyable>(
        "tracker_error_alert", tracker_error_alert_doc, no_init
    )
        .def_readonly("msg", &tracker_error_alert::msg)
        .def_readonly("times_in_row", &tracker_error_alert::times_in_row)
        .def_readonly("status_code", &tracker_error_alert::status_code)
        ;

    class_<tracker_warning_alert, bases<tracker_alert>, noncopyable>(
        "tracker_warning_alert", tracker_warning_alert_doc, no_init
    );

    class_<tracker_reply_alert, bases<tracker_alert>, noncopyable>(
        "tracker_reply_alert", tracker_reply_alert_doc, no_init
    )
        .def_readonly("num_peers", &tracker_reply_alert::num_peers)
        ;

    class_<tracker_announce_alert, bases<tracker_alert>, noncopyable>(
        "tracker_announce_alert", tracker_announce_alert_doc, no_init
    );

    class_<hash_failed_alert, bases<torrent_alert>, noncopyable>(
        "hash_failed_alert", hash_failed_alert_doc, no_init
    )
        .def_readonly("piece_index", &hash_failed_alert::piece_index)
        ;

    class_<peer_ban_alert, bases<peer_alert>, noncopyable>(
        "peer_ban_alert", peer_ban_alert_doc, no_init
    );

    class_<peer_error_alert, bases<peer_alert>, noncopyable>(
        "peer_error_alert", peer_error_alert_doc, no_init
    );

    class_<invalid_request_alert, bases<peer_alert>, noncopyable>(
        "invalid_request_alert", invalid_request_alert_doc, no_init
    )
        .def_readonly("request", &invalid_request_alert::request)
        ;

    class_<peer_request>("peer_request", peer_request_doc)
        .def_readonly("piece", &peer_request::piece)
        .def_readonly("start", &peer_request::start)
        .def_readonly("length", &peer_request::length)
        .def(self == self)
        ;

    class_<torrent_finished_alert, bases<torrent_alert>, noncopyable>(
        "torrent_finished_alert", torrent_finished_alert_doc, no_init
    );

    class_<piece_finished_alert, bases<torrent_alert>, noncopyable>(
        "piece_finished_alert", piece_finished_alert_doc, no_init
    )
        .def_readonly("piece_index", &piece_finished_alert::piece_index)
        ;

    class_<block_finished_alert, bases<peer_alert>, noncopyable>(
        "block_finished_alert", block_finished_alert_doc, no_init
    )
        .def_readonly("block_index", &block_finished_alert::block_index)
        .def_readonly("piece_index", &block_finished_alert::piece_index)
        ;

    class_<block_downloading_alert, bases<peer_alert>, noncopyable>(
        "block_downloading_alert", block_downloading_alert_doc, no_init
    )
        .def_readonly("peer_speedmsg", &block_downloading_alert::peer_speedmsg)
        .def_readonly("block_index", &block_downloading_alert::block_index)
        .def_readonly("piece_index", &block_downloading_alert::piece_index)
        ;

    class_<storage_moved_alert, bases<torrent_alert>, noncopyable>(
        "storage_moved_alert", storage_moved_alert_doc, no_init
    );

    class_<torrent_deleted_alert, bases<torrent_alert>, noncopyable>(
        "torrent_deleted_alert", torrent_deleted_alert_doc, no_init
    );

    class_<torrent_paused_alert, bases<torrent_alert>, noncopyable>(
        "torrent_paused_alert", torrent_paused_alert_doc, no_init
    );

    class_<torrent_checked_alert, bases<torrent_alert>, noncopyable>(
        "torrent_checked_alert", torrent_checked_alert_doc, no_init
    );

    class_<url_seed_alert, bases<torrent_alert>, noncopyable>(
        "url_seed_alert", url_seed_alert_doc, no_init
    )
        .def_readonly("url", &url_seed_alert::url)
        ;

    class_<file_error_alert, bases<torrent_alert>, noncopyable>(
        "file_error_alert", file_error_alert_doc, no_init
    )
        .def_readonly("file", &file_error_alert::file)
        ;

    class_<metadata_failed_alert, bases<torrent_alert>, noncopyable>(
        "metadata_failed_alert", metadata_failed_alert_doc, no_init
    );

    class_<metadata_received_alert, bases<torrent_alert>, noncopyable>(
        "metadata_received_alert", metadata_received_alert_doc, no_init
    );

    class_<listen_failed_alert, bases<alert>, noncopyable>(
        "listen_failed_alert", listen_failed_alert_doc, no_init
    );

    class_<listen_succeeded_alert, bases<alert>, noncopyable>(
        "listen_succeeded_alert", listen_succeeded_alert_doc, no_init
    )
        .def_readonly("endpoint", &listen_succeeded_alert::endpoint)
        ;

    class_<portmap_error_alert, bases<alert>, noncopyable>(
        "portmap_error_alert", portmap_error_alert_doc, no_init
    )
        .def_readonly("mapping", &portmap_error_alert::mapping)
        .def_readonly("type", &portmap_error_alert::type)
        ;

    class_<portmap_alert, bases<alert>, noncopyable>(
        "portmap_alert", portmap_alert_doc, no_init
    )
        .def_readonly("mapping", &portmap_alert::mapping)
        .def_readonly("external_port", &portmap_alert::external_port)
        .def_readonly("type", &portmap_alert::type)
        ;

    class_<fastresume_rejected_alert, bases<torrent_alert>, noncopyable>(
        "fastresume_rejected_alert", fastresume_rejected_alert_doc, no_init
    );

    class_<peer_blocked_alert, bases<alert>, noncopyable>(
        "peer_blocked_alert", peer_blocked_alert_doc, no_init
    )
        .def_readonly("ip", &peer_blocked_alert::ip)
        ;

    class_<scrape_reply_alert, bases<tracker_alert>, noncopyable>(
        "scrape_reply_alert", scrape_reply_alert_doc, no_init
    )
        .def_readonly("incomplete", &scrape_reply_alert::incomplete)
        .def_readonly("complete", &scrape_reply_alert::complete)
        ;

    class_<scrape_failed_alert, bases<tracker_alert>, noncopyable>(
        "scrape_failed_alert", scrape_failed_alert_doc, no_init
    );

    class_<udp_error_alert, bases<alert>, noncopyable>(
        "udp_error_alert", udp_error_alert_doc, no_init
    )
        .def_readonly("endpoint", &udp_error_alert::endpoint)
        ;

    class_<external_ip_alert, bases<alert>, noncopyable>(
        "external_ip_alert", external_ip_alert_doc, no_init
    )
        .def_readonly("external_address", &external_ip_alert::external_address)
        ;

    class_<save_resume_data_alert, bases<torrent_alert>, noncopyable>(
        "save_resume_data_alert", save_resume_data_alert_doc, no_init
    )
        .def_readonly("resume_data", &save_resume_data_alert::resume_data)
        ;

    class_<file_renamed_alert, bases<torrent_alert>, noncopyable>(
        "file_renamed_alert", no_init
    )
        .def_readonly("index", &file_renamed_alert::index)
        .def_readonly("name", &file_renamed_alert::name)
        ;

    class_<file_rename_failed_alert, bases<torrent_alert>, noncopyable>(
        "file_rename_failed_alert", no_init
    )
        .def_readonly("index", &file_rename_failed_alert::index)
        .def_readonly("msg", &file_rename_failed_alert::msg)
        ;

    class_<torrent_resumed_alert, bases<torrent_alert>, noncopyable>(
        "torrent_resumed_alert", no_init
	);

	class_<state_changed_alert, bases<torrent_alert>, noncopyable>(
	    "state_changed_alert", no_init
	)
	    .def_readonly("state", &state_changed_alert::state)
	    ;

	class_<dht_reply_alert, bases<tracker_alert>, noncopyable>(
	    "dht_reply_alert", no_init
	)
	    .def_readonly("num_peers", &dht_reply_alert::num_peers)
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
        "peer_disconnected_alert", no_init
    );

    class_<request_dropped_alert, bases<peer_alert>, noncopyable>(
        "request_dropped_alert", no_init
    )
        .def_readonly("block_index", &request_dropped_alert::block_index)
        .def_readonly("piece_index", &request_dropped_alert::piece_index)
    ;

    class_<block_timeout_alert, bases<peer_alert>, noncopyable>(
        "block_timeout_alert", no_init
    )
        .def_readonly("block_index", &block_timeout_alert::block_index)
        .def_readonly("piece_index", &block_timeout_alert::piece_index)
    ;

    class_<unwanted_block_alert, bases<peer_alert>, noncopyable>(
        "unwanted_block_alert", no_init
    )
        .def_readonly("block_index", &unwanted_block_alert::block_index)
        .def_readonly("piece_index", &unwanted_block_alert::piece_index)
    ;

    class_<torrent_delete_failed_alert, bases<torrent_alert>, noncopyable>(
        "torrent_delete_failed_alert", no_init
    );

    class_<save_resume_data_failed_alert, bases<torrent_alert>, noncopyable>(
        "save_resume_data_failed_alert", no_init
    );

    class_<performance_alert, bases<torrent_alert>, noncopyable>(
        "performance_alert", no_init
    )
        .def_readonly("warning_code", &performance_alert::warning_code)
    ;
    enum_<performance_alert::performance_warning_t>("performance_warning_t")
        .value("outstanding_disk_buffer_limit_reached", performance_alert::outstanding_disk_buffer_limit_reached)
        .value("outstanding_request_limit_reached", performance_alert::outstanding_request_limit_reached)
    ;



}
