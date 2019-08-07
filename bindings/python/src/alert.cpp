// Copyright Daniel Wallin 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "boost_python.hpp"
#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/piece_picker.hpp> // for piece_block
#include <libtorrent/session_stats.hpp>
#include <libtorrent/operations.hpp>
#include <memory>
#include "bytes.hpp"

#include <boost/type_traits/is_polymorphic.hpp>

using namespace boost::python;
using namespace lt;

#ifdef _MSC_VER
#pragma warning(push)
// warning c4996: x: was declared deprecated
#pragma warning( disable : 4996 )
#endif

bytes get_buffer(read_piece_alert const& rpa)
{
    return rpa.buffer ? bytes(rpa.buffer.get(), rpa.size)
       : bytes();
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

list dht_stats_active_requests(dht_stats_alert const& a)
{
   list result;

   for (std::vector<dht_lookup>::const_iterator i = a.active_requests.begin();
		i != a.active_requests.end(); ++i)
   {
		dict d;

		d["type"] = i->type;
		d["outstanding_requests"] = i->outstanding_requests;
		d["timeouts"] = i->timeouts;
		d["responses"] = i->responses;
		d["branch_factor"] = i->branch_factor;
		d["nodes_left"] = i->nodes_left;
		d["last_sent"] = i->last_sent;
		d["first_timeout"] = i->first_timeout;

      result.append(d);
   }
   return result;
}

list dht_stats_routing_table(dht_stats_alert const& a)
{
   list result;

   for (std::vector<dht_routing_bucket>::const_iterator i = a.routing_table.begin();
		i != a.routing_table.end(); ++i)
   {
		dict d;

		d["num_nodes"] = i->num_nodes;
		d["num_replacements"] = i->num_replacements;

      result.append(d);
   }
   return result;
}

dict dht_immutable_item(dht_immutable_item_alert const& alert)
{
    dict d;
    d["key"] = alert.target;
    d["value"] = bytes(alert.item.to_string());
    return d;
}

dict dht_mutable_item(dht_mutable_item_alert const& alert)
{
    dict d;
    d["key"] = bytes(alert.key.data(), alert.key.size());
    d["value"] = bytes(alert.item.to_string());
    d["signature"] = bytes(alert.signature.data(), alert.signature.size());
    d["seq"] = alert.seq;
    d["salt"] = bytes(alert.salt);
    d["authoritative"] = alert.authoritative;
    return d;
}

dict dht_put_item(dht_put_alert const& alert)
{
    dict d;
    if (alert.target.is_all_zeros()) {
        d["public_key"] = bytes(alert.public_key.data(), alert.public_key.size());
        d["signature"] = bytes(alert.signature.data(), alert.signature.size());
        d["seq"] = alert.seq;
        d["salt"] = bytes(alert.salt);
    } else {
        d["target"] = alert.target;
    }
    return d;
}

dict session_stats_values(session_stats_alert const& alert)
{
    std::vector<stats_metric> map = session_stats_metrics();
    dict d;
    auto counters = alert.counters();

    for (stats_metric const& m : map)
    {
        d[m.name] = counters[m.value_index];
    }
    return d;
}

list dht_live_nodes_nodes(dht_live_nodes_alert const& alert)
{
    list result;
    std::vector<std::pair<sha1_hash, udp::endpoint>> const nodes = alert.nodes();
    for (std::pair<sha1_hash, udp::endpoint> const& node : nodes)
    {
        dict d;
        d["nid"] = node.first;
        d["endpoint"] = node.second;
        result.append(d);
    }
    return result;
}

list dht_sample_infohashes_nodes(dht_sample_infohashes_alert const& alert)
{
    list result;
    std::vector<std::pair<sha1_hash, udp::endpoint>> const nodes = alert.nodes();
    for (std::pair<sha1_hash, udp::endpoint> const& node : nodes)
    {
        dict d;
        d["nid"] = node.first;
        d["endpoint"] = node.second;
        result.append(d);
    }
    return result;
}

#if TORRENT_ABI_VERSION == 1
entry const& get_resume_data_entry(save_resume_data_alert const& self)
{
	return *self.resume_data;
}
#endif

namespace boost
{
	// some older compilers (like msvc-12.0) end up using
	// boost::is_polymorphic inside boost.python applied
	// to alert types. This is problematic, since it appears
	// to be implemented by deriving from the type, which
	// yields a compiler error since most alerts are final.
	// this just short-cuts the query to say that all these
	// types are indeed polymorphic, no need to derive from
	// them.
#define POLY(x) template<> \
	struct is_polymorphic<lt:: x > : boost::mpl::true_ {};

	POLY(torrent_alert)
	POLY(tracker_alert)
	POLY(torrent_removed_alert)
	POLY(read_piece_alert)
	POLY(peer_alert)
	POLY(tracker_error_alert)
	POLY(tracker_warning_alert)
	POLY(tracker_reply_alert)
	POLY(tracker_announce_alert)
	POLY(hash_failed_alert)
	POLY(peer_ban_alert)
	POLY(peer_error_alert)
	POLY(invalid_request_alert)
	POLY(torrent_error_alert)
	POLY(torrent_finished_alert)
	POLY(piece_finished_alert)
	POLY(block_finished_alert)
	POLY(block_downloading_alert)
	POLY(storage_moved_alert)
	POLY(storage_moved_failed_alert)
	POLY(torrent_deleted_alert)
	POLY(torrent_paused_alert)
	POLY(torrent_checked_alert)
	POLY(url_seed_alert)
	POLY(file_error_alert)
	POLY(metadata_failed_alert)
	POLY(metadata_received_alert)
	POLY(listen_failed_alert)
	POLY(listen_succeeded_alert)
	POLY(portmap_error_alert)
	POLY(portmap_alert)
	POLY(fastresume_rejected_alert)
	POLY(peer_blocked_alert)
	POLY(scrape_reply_alert)
	POLY(scrape_failed_alert)
	POLY(udp_error_alert)
	POLY(external_ip_alert)
	POLY(save_resume_data_alert)
	POLY(file_completed_alert)
	POLY(file_renamed_alert)
	POLY(file_rename_failed_alert)
	POLY(torrent_resumed_alert)
	POLY(state_changed_alert)
	POLY(state_update_alert)
	POLY(i2p_alert)
	POLY(dht_immutable_item_alert)
	POLY(dht_mutable_item_alert)
	POLY(dht_put_alert)
	POLY(dht_reply_alert)
	POLY(dht_announce_alert)
	POLY(dht_get_peers_alert)
	POLY(peer_unsnubbed_alert)
	POLY(peer_snubbed_alert)
	POLY(peer_connect_alert)
	POLY(peer_disconnected_alert)
	POLY(request_dropped_alert)
	POLY(block_timeout_alert)
	POLY(unwanted_block_alert)
	POLY(torrent_delete_failed_alert)
	POLY(save_resume_data_failed_alert)
	POLY(performance_alert)
	POLY(stats_alert)
	POLY(cache_flushed_alert)
	POLY(incoming_connection_alert)
	POLY(torrent_need_cert_alert)
	POLY(add_torrent_alert)
	POLY(dht_outgoing_get_peers_alert)
	POLY(lsd_error_alert)
	POLY(dht_stats_alert)
	POLY(incoming_request_alert)
	POLY(dht_log_alert)
	POLY(dht_pkt_alert)
	POLY(dht_get_peers_reply_alert)
	POLY(dht_direct_response_alert)
	POLY(session_error_alert)
	POLY(dht_live_nodes_alert)
	POLY(session_stats_header_alert)
	POLY(dht_sample_infohashes_alert)
	POLY(block_uploaded_alert)
	POLY(alerts_dropped_alert)
	POLY(session_stats_alert)
	POLY(socks5_alert)

#if TORRENT_ABI_VERSION == 1
	POLY(anonymous_mode_alert)
	POLY(torrent_added_alert)
	POLY(torrent_update_alert)
#endif

#ifndef TORRENT_DISABLE_LOGGING
	POLY(portmap_log_alert)
	POLY(log_alert)
	POLY(torrent_log_alert)
	POLY(peer_log_alert)
	POLY(picker_log_alert)
#endif // TORRENT_DISABLE_LOGGING

#undef POLY
}

struct dummy3 {};
struct dummy12 {};

bytes get_pkt_buf(dht_pkt_alert const& alert)
{
    return {alert.pkt_buf().data(), static_cast<std::size_t>(alert.pkt_buf().size())};
}

list get_dropped_alerts(alerts_dropped_alert const& alert)
{
    list ret;
    for (int i = 0; i < int(alert.dropped_alerts.size()); ++i)
        ret.append(bool(alert.dropped_alerts[i]));
    return ret;
}

void bind_alert()
{
    using boost::noncopyable;

    using by_value = return_value_policy<return_by_value>;

    {
        scope alert_scope = class_<alert, noncopyable >("alert", no_init)
            .def("message", &alert::message)
            .def("what", &alert::what)
            .def("category", &alert::category)
#if TORRENT_ABI_VERSION == 1
            .def("severity", &alert::severity)
#endif
            .def("__str__", &alert::message)
            ;

#if TORRENT_ABI_VERSION == 1
        enum_<alert::severity_t>("severity_levels")
            .value("debug", alert::debug)
            .value("info", alert::info)
            .value("warning", alert::warning)
            .value("critical", alert::critical)
            .value("fatal", alert::fatal)
            .value("none", alert::none)
            ;
#endif

        scope s = class_<dummy3>("category_t");
        s.attr("error_notification") = alert::error_notification;
        s.attr("peer_notification") = alert::peer_notification;
        s.attr("port_mapping_notification") = alert::port_mapping_notification;
        s.attr("storage_notification") = alert::storage_notification;
        s.attr("tracker_notification") = alert::tracker_notification;
        s.attr("connect_notification") = alert::connect_notification;
        s.attr("status_notification") = alert::status_notification;
#if TORRENT_ABI_VERSION == 1
        s.attr("debug_notification") = alert::debug_notification;
        s.attr("progress_notification") = alert::progress_notification;
#endif
        s.attr("ip_block_notification") = alert::ip_block_notification;
        s.attr("performance_warning") = alert::performance_warning;
        s.attr("dht_notification") = alert::dht_notification;
        s.attr("stats_notification") = alert::stats_notification;
        s.attr("session_log_notification") = alert::session_log_notification;
        s.attr("torrent_log_notification") = alert::torrent_log_notification;
        s.attr("peer_log_notification") = alert::peer_log_notification;
        s.attr("incoming_request_notification") = alert::incoming_request_notification;
        s.attr("dht_log_notification") = alert::dht_log_notification;
        s.attr("dht_operation_notification") = alert::dht_operation_notification;
        s.attr("port_mapping_log_notification") = alert::port_mapping_log_notification;
        s.attr("picker_log_notification") = alert::picker_log_notification;
        s.attr("file_progress_notification") = alert::file_progress_notification;
        s.attr("piece_progress_notification") = alert::piece_progress_notification;
        s.attr("upload_notification") = alert::upload_notification;
        s.attr("block_progress_notification") = alert::block_progress_notification;
        s.attr("all_categories") = alert::all_categories;
    }

    {
        scope s = class_<dummy12>("alert_category");
        s.attr("error") = alert_category::error;
        s.attr("peer") = alert_category::peer;
        s.attr("port_mapping") = alert_category::port_mapping;
        s.attr("storage") = alert_category::storage;
        s.attr("tracker") = alert_category::tracker;
        s.attr("connect") = alert_category::connect;
        s.attr("status") = alert_category::status;
        s.attr("ip_block") = alert_category::ip_block;
        s.attr("performance_warning") = alert_category::performance_warning;
        s.attr("dht") = alert_category::dht;
        s.attr("stats") = alert_category::stats;
        s.attr("session_log") = alert_category::session_log;
        s.attr("torrent_log") = alert_category::torrent_log;
        s.attr("peer_log") = alert_category::peer_log;
        s.attr("incoming_request") = alert_category::incoming_request;
        s.attr("dht_log") = alert_category::dht_log;
        s.attr("dht_operation") = alert_category::dht_operation;
        s.attr("port_mapping_log") = alert_category::port_mapping_log;
        s.attr("picker_log") = alert_category::picker_log;
        s.attr("file_progress") = alert_category::file_progress;
        s.attr("piece_progress") = alert_category::piece_progress;
        s.attr("upload") = alert_category::upload;
        s.attr("block_progress") = alert_category::block_progress;
        s.attr("all") = alert_category::all;
    }

    enum_<operation_t>("operation_t")
       .value("unknown", operation_t::unknown)
       .value("bittorrent", operation_t::bittorrent)
       .value("iocontrol", operation_t::iocontrol)
       .value("getpeername", operation_t::getpeername)
       .value("getname", operation_t::getname)
       .value("alloc_recvbuf", operation_t::alloc_recvbuf)
       .value("alloc_sndbuf", operation_t::alloc_sndbuf)
       .value("file_write", operation_t::file_write)
       .value("file_read", operation_t::file_read)
       .value("file", operation_t::file)
       .value("sock_write", operation_t::sock_write)
       .value("sock_read", operation_t::sock_read)
       .value("sock_open", operation_t::sock_open)
       .value("sock_bind", operation_t::sock_bind)
       .value("available", operation_t::available)
       .value("encryption", operation_t::encryption)
       .value("connect", operation_t::connect)
       .value("ssl_handshake", operation_t::ssl_handshake)
       .value("get_interface", operation_t::get_interface)
       .value("sock_listen", operation_t::sock_listen)
       .value("sock_bind_to_device", operation_t::sock_bind_to_device)
       .value("sock_accept", operation_t::sock_accept)
       .value("parse_address", operation_t::parse_address)
       .value("enum_if", operation_t::enum_if)
       .value("file_stat", operation_t::file_stat)
       .value("file_copy", operation_t::file_copy)
       .value("file_fallocate", operation_t::file_fallocate)
       .value("file_hard_link", operation_t::file_hard_link)
       .value("file_remove", operation_t::file_remove)
       .value("file_rename", operation_t::file_rename)
       .value("file_open", operation_t::file_open)
       .value("mkdir", operation_t::mkdir)
       .value("check_resume", operation_t::check_resume)
       .value("exception", operation_t::exception)
       .value("alloc_cache_piece", operation_t::alloc_cache_piece)
       .value("partfile_move", operation_t::partfile_move)
       .value("partfile_read", operation_t::partfile_read)
       .value("partfile_write", operation_t::partfile_write)
       .value("hostname_lookup", operation_t::hostname_lookup)
       .value("symlink", operation_t::symlink)
       .value("handshake", operation_t::handshake)
       .value("sock_option", operation_t::sock_option)
       ;

    def("operation_name", static_cast<char const*(*)(operation_t)>(&lt::operation_name));

    class_<torrent_alert, bases<alert>, noncopyable>(
        "torrent_alert", no_init)
        .add_property("handle", make_getter(&torrent_alert::handle, by_value()))
        .add_property("torrent_name", &torrent_alert::torrent_name)
        ;

    class_<tracker_alert, bases<torrent_alert>, noncopyable>(
        "tracker_alert", no_init)
#if TORRENT_ABI_VERSION == 1
        .def_readonly("url", &tracker_alert::url)
#endif
        .add_property("local_endpoint", make_getter(&tracker_alert::local_endpoint, by_value()))
        .def("tracker_url", &tracker_alert::tracker_url)
        ;

#if TORRENT_ABI_VERSION == 1
    class_<torrent_added_alert, bases<torrent_alert>, noncopyable>(
        "torrent_added_alert", no_init)
        ;
#endif

    class_<torrent_removed_alert, bases<torrent_alert>, noncopyable>(
        "torrent_removed_alert", no_init)
        .def_readonly("info_hash", &torrent_removed_alert::info_hash)
        ;

    class_<read_piece_alert, bases<torrent_alert>, noncopyable>(
        "read_piece_alert", nullptr, no_init)
        .def_readonly("error", &read_piece_alert::error)
#if TORRENT_ABI_VERSION == 1
        .def_readonly("ec", &read_piece_alert::ec)
#endif
        .add_property("buffer", get_buffer)
        .add_property("piece", make_getter(&read_piece_alert::piece, by_value()))
        .def_readonly("size", &read_piece_alert::size)
        ;

    class_<peer_alert, bases<torrent_alert>, noncopyable>(
        "peer_alert", no_init)
#if TORRENT_ABI_VERSION == 1
        .add_property("ip", make_getter(&peer_alert::ip, by_value()))
#endif
        .add_property("endpoint", make_getter(&peer_alert::endpoint, by_value()))
        .def_readonly("pid", &peer_alert::pid)
    ;
    class_<tracker_error_alert, bases<tracker_alert>, noncopyable>(
        "tracker_error_alert", no_init)
#if TORRENT_ABI_VERSION == 1
        .def_readonly("msg", &tracker_error_alert::msg)
        .def_readonly("status_code", &tracker_error_alert::status_code)
#endif
        .def("error_message", &tracker_error_alert::error_message)
        .def_readonly("times_in_row", &tracker_error_alert::times_in_row)
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
        .add_property("piece_index", make_getter(&hash_failed_alert::piece_index, by_value()))
        ;

    class_<peer_ban_alert, bases<peer_alert>, noncopyable>(
        "peer_ban_alert", no_init);

    class_<peer_error_alert, bases<peer_alert>, noncopyable>(
        "peer_error_alert", no_init)
        .def_readonly("error", &peer_error_alert::error)
        .def_readonly("op", &peer_error_alert::op)
        ;

    class_<invalid_request_alert, bases<peer_alert>, noncopyable>(
        "invalid_request_alert", no_init)
        .def_readonly("request", &invalid_request_alert::request)
        ;

    class_<peer_request>("peer_request")
        .add_property("piece", make_getter(&peer_request::piece, by_value()))
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
        .add_property("piece_index", make_getter(&piece_finished_alert::piece_index, by_value()))
        ;

    class_<block_finished_alert, bases<peer_alert>, noncopyable>(
        "block_finished_alert", no_init)
        .add_property("block_index", make_getter(&block_finished_alert::block_index, by_value()))
        .add_property("piece_index", make_getter(&block_finished_alert::piece_index, by_value()))
        ;

    class_<block_downloading_alert, bases<peer_alert>, noncopyable>(
        "block_downloading_alert", no_init)
#if TORRENT_ABI_VERSION == 1
        .def_readonly("peer_speedmsg", &block_downloading_alert::peer_speedmsg)
#endif
        .add_property("block_index", make_getter(&block_downloading_alert::block_index, by_value()))
        .add_property("piece_index", make_getter(&block_downloading_alert::piece_index, by_value()))
        ;

    class_<storage_moved_alert, bases<torrent_alert>, noncopyable>(
        "storage_moved_alert", no_init)
#if TORRENT_ABI_VERSION == 1
        .def_readonly("path", &storage_moved_alert::path)
#endif
        .def("storage_path", &storage_moved_alert::storage_path)
        ;

    class_<storage_moved_failed_alert, bases<torrent_alert>, noncopyable>(
        "storage_moved_failed_alert", no_init)
        .def_readonly("error", &storage_moved_failed_alert::error)
        .def("file_path", &storage_moved_failed_alert::file_path)
        .def_readonly("op", &storage_moved_failed_alert::op)
#if TORRENT_ABI_VERSION == 1
        .def_readonly("operation", &storage_moved_failed_alert::operation)
#endif
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
#if TORRENT_ABI_VERSION == 1
        .def_readonly("url", &url_seed_alert::url)
        .def_readonly("msg", &url_seed_alert::msg)
#endif
        .def_readonly("error", &url_seed_alert::error)
        .def("server_url", &url_seed_alert::server_url)
        .def("error_message", &url_seed_alert::error_message)
        ;

    class_<file_error_alert, bases<torrent_alert>, noncopyable>(
        "file_error_alert", no_init)
        .def_readonly("error", &file_error_alert::error)
        .def("filename", &file_error_alert::filename)
#if TORRENT_ABI_VERSION == 1
        .def_readonly("file", &file_error_alert::file)
        .def_readonly("msg", &file_error_alert::msg)
#endif
        ;

    class_<metadata_failed_alert, bases<torrent_alert>, noncopyable>(
        "metadata_failed_alert", no_init)
        .def_readonly("error", &metadata_failed_alert::error)
        ;

    class_<metadata_received_alert, bases<torrent_alert>, noncopyable>(
        "metadata_received_alert", no_init);

    class_<listen_failed_alert, bases<alert>, noncopyable>(
        "listen_failed_alert", no_init)
#if TORRENT_ABI_VERSION == 1
        .add_property("endpoint", make_getter(&listen_failed_alert::endpoint, by_value()))
#endif
        .add_property("address", make_getter(&listen_failed_alert::address, by_value()))
        .def_readonly("port", &listen_failed_alert::port)
        .def("listen_interface", &listen_failed_alert::listen_interface)
        .def_readonly("error", &listen_failed_alert::error)
        .def_readonly("op", &listen_failed_alert::op)
#if TORRENT_ABI_VERSION == 1
        .def_readonly("operation", &listen_failed_alert::operation)
        .def_readonly("sock_type", &listen_failed_alert::sock_type)
#endif
        .def_readonly("socket_type", &listen_failed_alert::socket_type)
        ;

    class_<listen_succeeded_alert, bases<alert>, noncopyable>(
        "listen_succeeded_alert", no_init)
#if TORRENT_ABI_VERSION == 1
        .add_property("endpoint", make_getter(&listen_succeeded_alert::endpoint, by_value()))
#endif
        .add_property("address", make_getter(&listen_succeeded_alert::address, by_value()))
        .def_readonly("port", &listen_succeeded_alert::port)
#if TORRENT_ABI_VERSION == 1
        .def_readonly("sock_type", &listen_succeeded_alert::sock_type)
#endif
        .def_readonly("socket_type", &listen_succeeded_alert::socket_type)
        ;

#if TORRENT_ABI_VERSION == 1
    enum_<listen_succeeded_alert::socket_type_t>("listen_succeded_alert_socket_type_t")
       .value("tcp", listen_succeeded_alert::socket_type_t::tcp)
       .value("tcp_ssl", listen_succeeded_alert::socket_type_t::tcp_ssl)
       .value("udp", listen_succeeded_alert::socket_type_t::udp)
       .value("i2p", listen_succeeded_alert::socket_type_t::i2p)
       .value("socks5", listen_succeeded_alert::socket_type_t::socks5)
       .value("utp_ssl", listen_succeeded_alert::socket_type_t::utp_ssl)
       ;

    enum_<listen_failed_alert::socket_type_t>("listen_failed_alert_socket_type_t")
       .value("tcp", listen_failed_alert::socket_type_t::tcp)
       .value("tcp_ssl", listen_failed_alert::socket_type_t::tcp_ssl)
       .value("udp", listen_failed_alert::socket_type_t::udp)
       .value("i2p", listen_failed_alert::socket_type_t::i2p)
       .value("socks5", listen_failed_alert::socket_type_t::socks5)
       .value("utp_ssl", listen_failed_alert::socket_type_t::utp_ssl)
       ;
#endif

    enum_<socket_type_t>("socket_type_t")
       .value("tcp", socket_type_t::tcp)
       .value("tcp_ssl", socket_type_t::tcp_ssl)
       .value("udp", socket_type_t::udp)
       .value("i2p", socket_type_t::i2p)
       .value("socks5", socket_type_t::socks5)
       .value("utp_ssl", socket_type_t::utp_ssl)
       ;

    class_<portmap_error_alert, bases<alert>, noncopyable>(
        "portmap_error_alert", no_init)
        .add_property("mapping", make_getter(&portmap_error_alert::mapping, by_value()))
        .def_readonly("error", &portmap_error_alert::error)
        .def_readonly("map_transport", &portmap_error_alert::map_transport)
#if TORRENT_ABI_VERSION == 1
        .def_readonly("map_type", &portmap_error_alert::map_type)
        .def_readonly("type", &portmap_error_alert::map_type)
        .def_readonly("msg", &portmap_error_alert::msg)
#endif
        ;

    class_<portmap_alert, bases<alert>, noncopyable>("portmap_alert", no_init)
        .add_property("mapping", make_getter(&portmap_alert::mapping, by_value()))
        .def_readonly("external_port", &portmap_alert::external_port)
        .def_readonly("map_protocol", &portmap_alert::map_protocol)
        .def_readonly("map_transport", &portmap_alert::map_transport)
#if TORRENT_ABI_VERSION == 1
        .def_readonly("type", &portmap_alert::map_type)
        .def_readonly("map_type", &portmap_alert::map_type)
#endif
        ;

#ifndef TORRENT_DISABLE_LOGGING

    class_<portmap_log_alert, bases<alert>, noncopyable>("portmap_log_alert", no_init)
        .def_readonly("map_transport", &portmap_log_alert::map_transport)
#if TORRENT_ABI_VERSION == 1
        .def_readonly("type", &portmap_log_alert::map_type)
        .def_readonly("msg", &portmap_log_alert::msg)
        .def_readonly("map_type", &portmap_log_alert::map_type)
#endif
        ;

#endif // TORRENT_DISABLE_LOGGING

    class_<fastresume_rejected_alert, bases<torrent_alert>, noncopyable>(
        "fastresume_rejected_alert", no_init)
        .def_readonly("error", &fastresume_rejected_alert::error)
        .def("file_path", &fastresume_rejected_alert::file_path)
        .def_readonly("op", &fastresume_rejected_alert::op)
#if TORRENT_ABI_VERSION == 1
        .def_readonly("operation", &fastresume_rejected_alert::operation)
        .def_readonly("msg", &fastresume_rejected_alert::msg)
#endif
        ;

    class_<peer_blocked_alert, bases<peer_alert>, noncopyable>(
        "peer_blocked_alert", no_init)
#if TORRENT_ABI_VERSION == 1
        .add_property("ip", make_getter(&peer_blocked_alert::ip, by_value()))
#endif
        .add_property("reason", &peer_blocked_alert::reason)
        ;

    enum_<peer_blocked_alert::reason_t>("reason_t")
        .value("ip_filter", peer_blocked_alert::reason_t::ip_filter)
        .value("port_filter", peer_blocked_alert::reason_t::port_filter)
        .value("i2p_mixed", peer_blocked_alert::reason_t::i2p_mixed)
        .value("privileged_ports", peer_blocked_alert::reason_t::privileged_ports)
        .value("utp_disabled", peer_blocked_alert::reason_t::utp_disabled)
        .value("tcp_disabled", peer_blocked_alert::reason_t::tcp_disabled)
        .value("invalid_local_interface", peer_blocked_alert::reason_t::invalid_local_interface)
        ;

    class_<scrape_reply_alert, bases<tracker_alert>, noncopyable>(
        "scrape_reply_alert", no_init)
        .def_readonly("incomplete", &scrape_reply_alert::incomplete)
        .def_readonly("complete", &scrape_reply_alert::complete)
        ;

    class_<scrape_failed_alert, bases<tracker_alert>, noncopyable>(
        "scrape_failed_alert", no_init)
#if TORRENT_ABI_VERSION == 1
        .def_readonly("msg", &scrape_failed_alert::msg)
#endif
        .def("error_message", &scrape_failed_alert::error_message)
        .def_readonly("error", &scrape_failed_alert::error)
        ;

    class_<udp_error_alert, bases<alert>, noncopyable>(
        "udp_error_alert", no_init)
        .add_property("endpoint", make_getter(&udp_error_alert::endpoint, by_value()))
        .def_readonly("error", &udp_error_alert::error)
        ;

    class_<external_ip_alert, bases<alert>, noncopyable>(
        "external_ip_alert", no_init)
        .add_property("external_address", make_getter(&external_ip_alert::external_address, by_value()))
        ;

    class_<save_resume_data_alert, bases<torrent_alert>, noncopyable>(
        "save_resume_data_alert", no_init)
        .def_readonly("params", &save_resume_data_alert::params)
#if TORRENT_ABI_VERSION == 1
        .add_property("resume_data", make_function(get_resume_data_entry, by_value()))
#endif
        ;

    class_<file_completed_alert, bases<torrent_alert>, noncopyable>(
        "file_completed_alert", no_init)
        .add_property("index", make_getter(&file_completed_alert::index, by_value()))
        ;

    class_<file_renamed_alert, bases<torrent_alert>, noncopyable>(
        "file_renamed_alert", no_init)
        .add_property("index", make_getter(&file_renamed_alert::index, by_value()))
#if TORRENT_ABI_VERSION == 1
        .def_readonly("name", &file_renamed_alert::name)
#endif
        .def("new_name", &file_renamed_alert::new_name)
        ;

    class_<file_rename_failed_alert, bases<torrent_alert>, noncopyable>(
        "file_rename_failed_alert", no_init)
        .add_property("index", make_getter(&file_rename_failed_alert::index, by_value()))
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
        .add_property("ip", make_getter(&dht_announce_alert::ip, by_value()))
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
        .def_readonly("socket_type", &peer_disconnected_alert::socket_type)
        .def_readonly("op", &peer_disconnected_alert::op)
        .def_readonly("error", &peer_disconnected_alert::error)
        .def_readonly("reason", &peer_disconnected_alert::reason)
#if TORRENT_ABI_VERSION == 1
        .def_readonly("msg", &peer_disconnected_alert::msg)
#endif
        ;

    class_<request_dropped_alert, bases<peer_alert>, noncopyable>(
        "request_dropped_alert", no_init)
        .add_property("block_index", make_getter(&request_dropped_alert::block_index, by_value()))
        .add_property("piece_index", make_getter(&request_dropped_alert::piece_index, by_value()))
    ;

    class_<block_timeout_alert, bases<peer_alert>, noncopyable>(
        "block_timeout_alert", no_init)
        .add_property("block_index", make_getter(&block_timeout_alert::block_index, by_value()))
        .add_property("piece_index", make_getter(&block_timeout_alert::piece_index, by_value()))
    ;

    class_<unwanted_block_alert, bases<peer_alert>, noncopyable>(
        "unwanted_block_alert", no_init)
        .add_property("block_index", make_getter(&unwanted_block_alert::block_index, by_value()))
        .add_property("piece_index", make_getter(&unwanted_block_alert::piece_index, by_value()))
    ;

    class_<torrent_delete_failed_alert, bases<torrent_alert>, noncopyable>(
        "torrent_delete_failed_alert", no_init)
#if TORRENT_ABI_VERSION == 1
        .def_readonly("msg", &torrent_delete_failed_alert::msg)
#endif
        .def_readonly("error", &torrent_delete_failed_alert::error)
        .def_readonly("info_hash", &torrent_delete_failed_alert::info_hash)
        ;

    class_<save_resume_data_failed_alert, bases<torrent_alert>, noncopyable>(
        "save_resume_data_failed_alert", no_init)
#if TORRENT_ABI_VERSION == 1
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
#if TORRENT_ABI_VERSION == 1
        .value("bittyrant_with_no_uplimit", performance_alert::bittyrant_with_no_uplimit)
#endif
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
#if TORRENT_ABI_VERSION == 1
        .value("upload_dht_protocol", stats_alert::upload_dht_protocol)
        .value("upload_tracker_protocol", stats_alert::upload_tracker_protocol)
#endif
        .value("download_payload", stats_alert::download_payload)
        .value("download_protocol", stats_alert::download_protocol)
        .value("download_ip_protocol", stats_alert::download_ip_protocol)
#if TORRENT_ABI_VERSION == 1
        .value("download_dht_protocol", stats_alert::download_dht_protocol)
        .value("download_tracker_protocol", stats_alert::download_tracker_protocol)
#endif
    ;

    class_<cache_flushed_alert, bases<torrent_alert>, noncopyable>(
        "cache_flushed_alert", no_init)
    ;

#if TORRENT_ABI_VERSION == 1
    class_<anonymous_mode_alert, bases<torrent_alert>, noncopyable>(
        "anonymous_mode_alert", no_init)
        .def_readonly("kind", &anonymous_mode_alert::kind)
        .def_readonly("str", &anonymous_mode_alert::str)
    ;

    enum_<anonymous_mode_alert::kind_t>("kind")
        .value("tracker_no_anonymous", anonymous_mode_alert::tracker_not_anonymous)
    ;
#endif // TORRENT_ABI_VERSION

    class_<incoming_connection_alert, bases<alert>, noncopyable>(
        "incoming_connection_alert", no_init)
        .def_readonly("socket_type", &incoming_connection_alert::socket_type)
#if TORRENT_ABI_VERSION == 1
        .add_property("ip", make_getter(&incoming_connection_alert::ip, by_value()))
#endif
        .add_property("endpoint", make_getter(&incoming_connection_alert::endpoint, by_value()))
        ;
    class_<torrent_need_cert_alert, bases<torrent_alert>, noncopyable>(
        "torrent_need_cert_alert", no_init)
#if TORRENT_ABI_VERSION == 1
        .def_readonly("error", &torrent_need_cert_alert::error)
#endif
        ;

    class_<add_torrent_alert, bases<torrent_alert>, noncopyable>(
       "add_torrent_alert", no_init)
       .def_readonly("error", &add_torrent_alert::error)
       .add_property("params", &add_torrent_alert::params)
       ;

#if TORRENT_ABI_VERSION == 1
    class_<torrent_update_alert, bases<torrent_alert>, noncopyable>(
       "torrent_update_alert", no_init)
        .def_readonly("old_ih", &torrent_update_alert::old_ih)
        .def_readonly("new_ih", &torrent_update_alert::new_ih)
        ;
#endif

    class_<dht_outgoing_get_peers_alert, bases<alert>, noncopyable>(
       "dht_outgoing_get_peers_alert", no_init)
        .def_readonly("info_hash", &dht_outgoing_get_peers_alert::info_hash)
        .def_readonly("obfuscated_info_hash", &dht_outgoing_get_peers_alert::obfuscated_info_hash)
#if TORRENT_ABI_VERSION == 1
        .add_property("ip", make_getter(&dht_outgoing_get_peers_alert::ip, by_value()))
#endif
        .add_property("endpoint", make_getter(&dht_outgoing_get_peers_alert::endpoint, by_value()))
        ;

    class_<log_alert, bases<alert>, noncopyable>(
       "log_alert", no_init)
#if TORRENT_ABI_VERSION == 1
        .def("msg", &log_alert::msg)
#endif
        .def("log_message", &log_alert::log_message)
        ;

    class_<torrent_log_alert, bases<torrent_alert>, noncopyable>(
       "torrent_log_alert", no_init)
#if TORRENT_ABI_VERSION == 1
        .def("msg", &torrent_log_alert::msg)
#endif
        .def("log_message", &torrent_log_alert::log_message)
        ;

    class_<peer_log_alert, bases<peer_alert>, noncopyable>(
       "peer_log_alert", no_init)
#if TORRENT_ABI_VERSION == 1
        .def("msg", &peer_log_alert::msg)
#endif
        .def("log_message", &peer_log_alert::log_message)
        ;

    class_<picker_log_alert, bases<peer_alert>, noncopyable>(
       "picker_log_alert", no_init)
        .add_property("picker_flags", &picker_log_alert::picker_flags)
        .def("blocks", &picker_log_alert::blocks)
        ;

    class_<lsd_error_alert, bases<alert>, noncopyable>(
       "lsd_error_alert", no_init)
        .def_readonly("error", &lsd_error_alert::error)
        ;

    class_<dht_stats_alert, bases<alert>, noncopyable>(
       "dht_stats_alert", no_init)
       .add_property("active_requests", &dht_stats_active_requests)
       .add_property("routing_table", &dht_stats_routing_table)
        ;

    class_<dht_log_alert, bases<alert>, noncopyable>("dht_log_alert", no_init)
        .def_readonly("module", &dht_log_alert::module)
        .def("log_message", &dht_log_alert::log_message)
    ;

    class_<dht_pkt_alert, bases<alert>, noncopyable>(
        "dht_pkt_alert", no_init)
        .add_property("pkt_buf", &get_pkt_buf)
        ;

    class_<dht_immutable_item_alert, bases<alert>, noncopyable>(
       "dht_immutable_item_alert", no_init)
        .def_readonly("target", &dht_immutable_item_alert::target)
        .add_property("item", &dht_immutable_item)
        ;

    class_<dht_mutable_item_alert, bases<alert>, noncopyable>(
       "dht_mutable_item_alert", no_init)
        .def_readonly("key", &dht_mutable_item_alert::key)
        .def_readonly("signature", &dht_mutable_item_alert::signature)
        .def_readonly("seq", &dht_mutable_item_alert::seq)
        .def_readonly("salt", &dht_mutable_item_alert::salt)
        .add_property("item", &dht_mutable_item)
        .def_readonly("authoritative", &dht_mutable_item_alert::authoritative)
        ;

    class_<dht_put_alert, bases<alert>, noncopyable>(
       "dht_put_alert", no_init)
        .def_readonly("target", &dht_put_alert::target)
        .def_readonly("public_key", &dht_put_alert::public_key)
        .def_readonly("signature", &dht_put_alert::signature)
        .def_readonly("salt", &dht_put_alert::salt)
        .def_readonly("seq", &dht_put_alert::seq)
        .def_readonly("num_success", &dht_put_alert::num_success)
        ;

    class_<session_stats_alert, bases<alert>, noncopyable>(
        "session_stats_alert", no_init)
        .add_property("values", &session_stats_values)
        ;

    class_<session_stats_header_alert, bases<alert>, noncopyable>(
        "session_stats_header_alert", no_init)
        ;

    std::vector<tcp::endpoint> (dht_get_peers_reply_alert::*peers)() const = &dht_get_peers_reply_alert::peers;

    class_<dht_get_peers_reply_alert, bases<alert>, noncopyable>(
        "dht_get_peers_reply_alert", no_init)
        .def_readonly("info_hash", &dht_get_peers_reply_alert::info_hash)
        .def("num_peers", &dht_get_peers_reply_alert::num_peers)
        .def("peers", peers)
        ;

    class_<block_uploaded_alert, bases<peer_alert>, noncopyable>(
       "block_uploaded_alert", no_init)
        .add_property("block_index", &block_uploaded_alert::block_index)
        .add_property("piece_index", make_getter((&block_uploaded_alert::piece_index), by_value()))
        ;

    class_<alerts_dropped_alert, bases<alert>, noncopyable>(
       "alerts_dropped_alert", no_init)
        .add_property("dropped_alerts", &get_dropped_alerts)
        ;

    class_<socks5_alert, bases<alert>, noncopyable>(
       "socks5_alert", no_init)
        .def_readonly("error", &socks5_alert::error)
        .def_readonly("op", &socks5_alert::op)
        .add_property("ip", make_getter(&socks5_alert::ip, by_value()))
        ;

    class_<dht_live_nodes_alert, bases<alert>, noncopyable>(
       "dht_live_nodes_alert", no_init)
        .add_property("node_id", &dht_live_nodes_alert::node_id)
        .add_property("num_nodes", &dht_live_nodes_alert::num_nodes)
        .add_property("nodes", &dht_live_nodes_nodes)
        ;

    std::vector<sha1_hash> (dht_sample_infohashes_alert::*samples)() const = &dht_sample_infohashes_alert::samples;

    class_<dht_sample_infohashes_alert, bases<alert>, noncopyable>(
       "dht_sample_infohashes_alert", no_init)
        .add_property("endpoint", make_getter(&dht_sample_infohashes_alert::endpoint, by_value()))
        .add_property("interval", make_getter(&dht_sample_infohashes_alert::interval, by_value()))
        .add_property("num_infohashes", &dht_sample_infohashes_alert::num_infohashes)
        .add_property("num_samples", &dht_sample_infohashes_alert::num_samples)
        .add_property("samples", samples)
        .add_property("num_nodes", &dht_sample_infohashes_alert::num_nodes)
        .add_property("nodes", &dht_sample_infohashes_nodes)
        ;

    class_<dht_bootstrap_alert, bases<alert>, noncopyable>(
        "dht_bootstrap_alert", no_init)
        ;

}

#ifdef _MSC_VER
#pragma warning(pop)
#endif
