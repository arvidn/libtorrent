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
extern char const* torrent_paused_alert_doc;
extern char const* torrent_resumed_alert_doc;
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

void bind_alert()
{
    using boost::noncopyable;

    {
        scope alert_scope = class_<alert, noncopyable>("alert", alert_doc, no_init)
            .def(
                "msg", &alert::msg, return_value_policy<copy_const_reference>()
              , alert_msg_doc
            )
            .def("severity", &alert::severity, alert_severity_doc)
            .def(
                "__str__", &alert::msg, return_value_policy<copy_const_reference>()
              , alert_msg_doc
            )
            ;

        enum_<alert::severity_t>("severity_levels")
            .value("debug", alert::debug)
            .value("info", alert::info)
            .value("warning", alert::warning)
            .value("critical", alert::critical)
            .value("fatal", alert::fatal)
            .value("none", alert::none)
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
        .def_readonly("times_in_row", &tracker_alert::times_in_row)
        .def_readonly("status_code", &tracker_alert::status_code)
        ;

    class_<tracker_warning_alert, bases<torrent_alert>, noncopyable>(
        "tracker_warning_alert", tracker_warning_alert_doc, no_init
    );

    class_<tracker_reply_alert, bases<torrent_alert>, noncopyable>(
        "tracker_reply_alert", tracker_reply_alert_doc, no_init
    )
        .def_readonly("num_peers", &tracker_reply_alert::num_peers)
        ;

    class_<tracker_announce_alert, bases<torrent_alert>, noncopyable>(
        "tracker_announce_alert", tracker_announce_alert_doc, no_init
    );

    class_<hash_failed_alert, bases<torrent_alert>, noncopyable>(
        "hash_failed_alert", hash_failed_alert_doc, no_init
    )
        .def_readonly("piece_index", &hash_failed_alert::piece_index)
        ;

    class_<peer_ban_alert, bases<torrent_alert>, noncopyable>(
        "peer_ban_alert", peer_ban_alert_doc, no_init
    )
        .def_readonly("ip", &peer_ban_alert::ip)
        ;

    class_<peer_error_alert, bases<alert>, noncopyable>(
        "peer_error_alert", peer_error_alert_doc, no_init
    )
        .def_readonly("ip", &peer_error_alert::ip)
        .def_readonly("pid", &peer_error_alert::pid)
        ;

    class_<invalid_request_alert, bases<torrent_alert>, noncopyable>(
        "invalid_request_alert", invalid_request_alert_doc, no_init
    )
        .def_readonly("ip", &invalid_request_alert::ip)
        .def_readonly("request", &invalid_request_alert::request)
        .def_readonly("pid", &invalid_request_alert::pid)
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
    
    class_<block_finished_alert, bases<torrent_alert>, noncopyable>(
        "block_finished_alert", block_finished_alert_doc, no_init
    )
        .def_readonly("block_index", &block_finished_alert::block_index)
        .def_readonly("piece_index", &block_finished_alert::piece_index)
        ;
    
    class_<block_downloading_alert, bases<torrent_alert>, noncopyable>(
        "block_downloading_alert", block_downloading_alert_doc, no_init
    )
        .def_readonly("peer_speedmsg", &block_downloading_alert::peer_speedmsg)
        .def_readonly("block_index", &block_downloading_alert::block_index)
        .def_readonly("piece_index", &block_downloading_alert::piece_index)
        ;
        
    class_<storage_moved_alert, bases<torrent_alert>, noncopyable>(
        "storage_moved_alert", storage_moved_alert_doc, no_init
    );

    class_<torrent_paused_alert, bases<torrent_alert>, noncopyable>(
        "torrent_paused_alert", torrent_paused_alert_doc, no_init
    );

    class_<torrent_resumed_alert, bases<torrent_alert>, noncopyable>(
        "torrent_resumed_alert", torrent_resumed_alert_doc, no_init
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
    );
    
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
    );

    class_<portmap_alert, bases<alert>, noncopyable>(
        "portmap_alert", portmap_alert_doc, no_init
    );
            
    class_<fastresume_rejected_alert, bases<torrent_alert>, noncopyable>(
        "fastresume_rejected_alert", fastresume_rejected_alert_doc, no_init
    );
    
    class_<peer_blocked_alert, bases<alert>, noncopyable>(
        "peer_blocked_alert", peer_blocked_alert_doc, no_init
    )
        .def_readonly("ip", &peer_blocked_alert::ip)
        ;
        
    class_<scrape_reply_alert, bases<torrent_alert>, noncopyable>(
        "scrape_reply_alert", scrape_reply_alert_doc, no_init
    )
        .def_readonly("incomplete", &scrape_reply_alert::incomplete)
        .def_readonly("complete", &scrape_reply_alert::complete)
        ;
    
    class_<scrape_failed_alert, bases<torrent_alert>, noncopyable>(
        "scrape_failed_alert", scrape_failed_alert_doc, no_init
    );
}
