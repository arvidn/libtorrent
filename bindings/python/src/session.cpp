// Copyright Daniel Wallin 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <libtorrent/session.hpp>
#include <libtorrent/torrent.hpp>
#include <boost/python.hpp>
#include "gil.hpp"

using namespace boost::python;
using namespace libtorrent;

extern char const* session_status_doc;
extern char const* session_status_has_incoming_connections_doc;
extern char const* session_status_upload_rate_doc;
extern char const* session_status_download_rate_doc;
extern char const* session_status_payload_upload_rate_doc;
extern char const* session_status_payload_download_rate_doc;
extern char const* session_status_total_download_doc;
extern char const* session_status_total_upload_doc;
extern char const* session_status_total_payload_download_doc;
extern char const* session_status_total_payload_upload_doc;
extern char const* session_status_num_peers_doc;
extern char const* session_status_dht_nodes_doc;
extern char const* session_status_cache_nodes_doc;
extern char const* session_status_dht_torrents_doc;

extern char const* session_doc;
extern char const* session_init_doc;
extern char const* session_listen_on_doc;
extern char const* session_is_listening_doc;
extern char const* session_listen_port_doc;
extern char const* session_status_m_doc;
extern char const* session_start_dht_doc;
extern char const* session_stop_dht_doc;
extern char const* session_dht_state_doc;
extern char const* session_add_torrent_doc;
extern char const* session_remove_torrent_doc;
extern char const* session_set_download_rate_limit_doc;
extern char const* session_set_upload_rate_limit_doc;
extern char const* session_set_max_uploads_doc;
extern char const* session_set_max_connections_doc;
extern char const* session_set_max_half_open_connections_doc;
extern char const* session_set_settings_doc;
extern char const* session_set_severity_level_doc;
extern char const* session_pop_alert_doc;

namespace
{

  bool listen_on(session& s, int min_, int max_, char const* interface)
  {
      allow_threading_guard guard;
      return s.listen_on(std::make_pair(min_, max_), interface);
  }

  struct invoke_extension_factory
  {
      invoke_extension_factory(object const& callback)
        : cb(callback)
      {}

      boost::shared_ptr<torrent_plugin> operator()(torrent* t)
      {
          lock_gil lock;
          return extract<boost::shared_ptr<torrent_plugin> >(cb(ptr(t)))();
      }

      object cb;
  };

  void add_extension(session& s, object const& e)
  {
//      allow_threading_guard guard;
      s.add_extension(invoke_extension_factory(e));
  }

} // namespace unnamed

void bind_session()
{
    class_<session_status>("session_status", session_status_doc)
        .def_readonly(
            "has_incoming_connections", &session_status::has_incoming_connections
          , session_status_has_incoming_connections_doc
        )
        .def_readonly(
            "upload_rate", &session_status::upload_rate
          , session_status_upload_rate_doc
        )
        .def_readonly(
            "download_rate", &session_status::download_rate
          , session_status_download_rate_doc
        )
        .def_readonly(
            "payload_upload_rate", &session_status::payload_upload_rate
          , session_status_payload_upload_rate_doc
        )
        .def_readonly(
            "payload_download_rate", &session_status::payload_download_rate
          , session_status_payload_download_rate_doc
        )
        .def_readonly(
            "total_download", &session_status::total_download
          , session_status_total_download_doc
        )
        .def_readonly(
            "total_upload", &session_status::total_upload
          , session_status_total_upload_doc
        )
        .def_readonly(
            "total_payload_download", &session_status::total_payload_download
          , session_status_total_payload_download_doc
        )
        .def_readonly(
            "total_payload_upload", &session_status::total_payload_upload
          , session_status_total_payload_upload_doc
        )
        .def_readonly(
            "num_peers", &session_status::num_peers
          , session_status_num_peers_doc
        )
#ifndef TORRENT_DISABLE_DHT
        .def_readonly(
            "dht_nodes", &session_status::dht_nodes
          , session_status_dht_nodes_doc
        )
        .def_readonly(
            "dht_cache_nodes", &session_status::dht_node_cache
          , session_status_cache_nodes_doc
        )
        .def_readonly(
            "dht_torrents", &session_status::dht_torrents
          , session_status_dht_torrents_doc
        )
#endif
        ;

    torrent_handle (session::*add_torrent0)(
        torrent_info const&
      , boost::filesystem::path const&
      , entry const&
      , bool
      , int
    ) = &session::add_torrent;

    class_<session, boost::noncopyable>("session", session_doc, no_init)
        .def(
            init<fingerprint>(arg("fingerprint")=fingerprint("LT",0,1,0,0), session_init_doc)
        )
        .def(
            "listen_on", &listen_on
          , (arg("min"), "max", arg("interface") = (char const*)0)
          , session_listen_on_doc
        )
        .def("is_listening", allow_threads(&session::is_listening), session_is_listening_doc)
        .def("listen_port", allow_threads(&session::listen_port), session_listen_port_doc)
        .def("status", allow_threads(&session::status), session_status_m_doc)
#ifndef TORRENT_DISABLE_DHT
        .def("start_dht", allow_threads(&session::start_dht), session_start_dht_doc)
        .def("stop_dht", allow_threads(&session::stop_dht), session_stop_dht_doc)
        .def("dht_state", allow_threads(&session::dht_state), session_dht_state_doc)
#endif
        .def(
            "add_torrent", allow_threads(add_torrent0)
          , (
                arg("torrent_info"), "save_path", arg("resume_data") = entry()
              , arg("compact_mode") = true, arg("block_size") = 16 * 1024
            )
          , session_add_torrent_doc
        )
        .def("remove_torrent", allow_threads(&session::remove_torrent), session_remove_torrent_doc)
        .def(
            "set_download_rate_limit", allow_threads(&session::set_download_rate_limit)
          , session_set_download_rate_limit_doc
        )
        .def(
            "set_upload_rate_limit", allow_threads(&session::set_upload_rate_limit)
          , session_set_upload_rate_limit_doc
        )
        .def(
            "set_max_uploads", allow_threads(&session::set_max_uploads)
          , session_set_max_uploads_doc
        )
        .def(
            "set_max_connections", allow_threads(&session::set_max_connections)
          , session_set_max_connections_doc
        )
        .def(
            "set_max_half_open_connections", allow_threads(&session::set_max_half_open_connections)
          , session_set_max_half_open_connections_doc
        )
        .def("set_settings", allow_threads(&session::set_settings), session_set_settings_doc)
        .def(
            "set_severity_level", allow_threads(&session::set_severity_level)
          , session_set_severity_level_doc
        )
        .def("pop_alert", allow_threads(&session::pop_alert), session_pop_alert_doc)
        .def("add_extension", &add_extension)
        ;

    register_ptr_to_python<std::auto_ptr<alert> >();
}

