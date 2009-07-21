// Copyright Daniel Wallin, Arvid Norberg 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <libtorrent/session.hpp>
#include <libtorrent/torrent.hpp>
#include <libtorrent/storage.hpp>
#include <libtorrent/ip_filter.hpp>
#include <libtorrent/disk_io_thread.hpp>
#include <boost/python.hpp>
#include "gil.hpp"

using namespace boost::python;
using namespace libtorrent;

namespace
{
    bool listen_on(session& s, int min_, int max_, char const* interface)
    {
        allow_threading_guard guard;
        return s.listen_on(std::make_pair(min_, max_), interface);
    }

    void outgoing_ports(session& s, int _min, int _max)
    {
        allow_threading_guard guard;
        session_settings settings = s.settings();
        settings.outgoing_ports = std::make_pair(_min, _max);
        s.set_settings(settings);
        return;
    }
#ifndef TORRENT_DISABLE_DHT
    void add_dht_router(session& s, std::string router_, int port_)
    {
        allow_threading_guard guard;
        return s.add_dht_router(std::make_pair(router_, port_));
    }
#endif

    struct invoke_extension_factory
    {
        invoke_extension_factory(object const& callback)
            : cb(callback)
        {}

        boost::shared_ptr<torrent_plugin> operator()(torrent* t, void*)
        {
           lock_gil lock;
           return extract<boost::shared_ptr<torrent_plugin> >(cb(ptr(t)))();
        }

        object cb;
    };

    void add_extension(session& s, object const& e)
    {
        allow_threading_guard guard;
        s.add_extension(invoke_extension_factory(e));
    }

#ifndef TORRENT_NO_DEPRECATE
    torrent_handle add_torrent_depr(session& s, torrent_info const& ti
        , boost::filesystem::path const& save, entry const& resume
        , storage_mode_t storage_mode, bool paused)
    {
        allow_threading_guard guard;
        return s.add_torrent(ti, save, resume, storage_mode, paused, default_storage_constructor);
    }
#endif

    torrent_handle add_torrent(session& s, dict params)
    {
        add_torrent_params p;

        if (params.has_key("ti"))
            p.ti = new torrent_info(extract<torrent_info const&>(params["ti"]));

        std::string url;
        if (params.has_key("tracker_url"))
        {
            url = extract<std::string>(params["tracker_url"]);
            p.tracker_url = url.c_str();
        }
        if (params.has_key("info_hash"))
            p.info_hash = extract<sha1_hash>(params["info_hash"]);
        std::string name;
        if (params.has_key("name"))
        {
            name = extract<std::string>(params["name"]);
            p.name = name.c_str();
        }
        p.save_path = fs::path(extract<std::string>(params["save_path"]));

        std::vector<char> resume_buf;
        if (params.has_key("resume_data"))
        {
            std::string resume = extract<std::string>(params["resume_data"]);
            resume_buf.resize(resume.size());
            std::memcpy(&resume_buf[0], &resume[0], resume.size());
            p.resume_data = &resume_buf;
        }
        if (params.has_key("storage_mode"))
            p.storage_mode = extract<storage_mode_t>(params["storage_mode"]);
        if (params.has_key("paused"))
            p.paused = params["paused"];
        if (params.has_key("auto_managed"))
            p.auto_managed = params["auto_managed"];
        if (params.has_key("duplicate_is_error"))
            p.duplicate_is_error = params["duplicate_is_error"];

        return s.add_torrent(p);
    }

    void start_natpmp(session& s)
    {
        allow_threading_guard guard;
        s.start_natpmp();
        return;
    }

    void start_upnp(session& s)
    {
        allow_threading_guard guard;
        s.start_upnp();
        return;
    }

    list get_torrents(session& s)
    {
        list ret;
        std::vector<torrent_handle> torrents = s.get_torrents();

        for (std::vector<torrent_handle>::iterator i = torrents.begin(); i != torrents.end(); ++i)
        {
            ret.append(*i);
        }
        return ret;
    }

#ifndef TORRENT_DISABLE_GEO_IP
    bool load_asnum_db(session& s, std::string file)
    {
        allow_threading_guard guard;
        return s.load_asnum_db(file.c_str());
    }

    bool load_country_db(session& s, std::string file)
    {
        allow_threading_guard guard;
        return s.load_country_db(file.c_str());
    }
#endif
} // namespace unnamed

void bind_session()
{
    class_<session_status>("session_status")
        .def_readonly("has_incoming_connections", &session_status::has_incoming_connections)
        .def_readonly("upload_rate", &session_status::upload_rate)
        .def_readonly("download_rate", &session_status::download_rate)
        .def_readonly("payload_upload_rate", &session_status::payload_upload_rate)
        .def_readonly("payload_download_rate", &session_status::payload_download_rate)
        .def_readonly("total_download", &session_status::total_download)
        .def_readonly("total_upload", &session_status::total_upload)
        .def_readonly("total_payload_download", &session_status::total_payload_download)
        .def_readonly("total_payload_upload", &session_status::total_payload_upload)
        .def_readonly("total_redundant_bytes", &session_status::total_redundant_bytes)
        .def_readonly("total_failed_bytes", &session_status::total_failed_bytes)
        .def_readonly("num_peers", &session_status::num_peers)
        .def_readonly("num_unchoked", &session_status::num_unchoked)
        .def_readonly("allowed_upload_slots", &session_status::allowed_upload_slots)
        .def_readonly("up_bandwidth_queue", &session_status::up_bandwidth_queue)
        .def_readonly("down_bandwidth_queue", &session_status::down_bandwidth_queue)
#ifndef TORRENT_DISABLE_DHT
        .def_readonly("dht_nodes", &session_status::dht_nodes)
        .def_readonly("dht_cache_nodes", &session_status::dht_node_cache)
        .def_readonly("dht_torrents", &session_status::dht_torrents)
        .def_readonly("dht_global_nodes", &session_status::dht_global_nodes)
#endif
        ;

    enum_<storage_mode_t>("storage_mode_t")
        .value("storage_mode_allocate", storage_mode_allocate)
        .value("storage_mode_sparse", storage_mode_sparse)
        .value("storage_mode_compact", storage_mode_compact)
    ;

    enum_<session::options_t>("options_t")
        .value("none", session::none)
        .value("delete_files", session::delete_files)
    ;

    enum_<session::session_flags_t>("session_flags_t")
        .value("add_default_plugins", session::add_default_plugins)
        .value("start_default_features", session::start_default_features)
    ;

     class_<cache_status>("cache_status")
        .def_readonly("blocks_written", &cache_status::blocks_written)
        .def_readonly("writes", &cache_status::writes)
        .def_readonly("blocks_read", &cache_status::blocks_read)
        .def_readonly("blocks_read_hit", &cache_status::blocks_read_hit)
        .def_readonly("reads", &cache_status::reads)
        .def_readonly("cache_size", &cache_status::cache_size)
        .def_readonly("read_cache_size", &cache_status::read_cache_size)
    ;

    class_<session, boost::noncopyable>("session", no_init)
        .def(
            init<fingerprint, int>((
                arg("fingerprint")=fingerprint("LT",0,1,0,0)
                , arg("flags")=session::start_default_features | session::add_default_plugins)
                )
        )
        .def("listen_on", &listen_on, (arg("min"), "max", arg("interface") = (char const*)0))
        .def("outgoing_ports", &outgoing_ports)
        .def("is_listening", allow_threads(&session::is_listening))
        .def("listen_port", allow_threads(&session::listen_port))
        .def("status", allow_threads(&session::status))
#ifndef TORRENT_DISABLE_DHT
        .def("add_dht_router", &add_dht_router, (arg("router"), "port"))
        .def("start_dht", allow_threads(&session::start_dht))
        .def("stop_dht", allow_threads(&session::stop_dht))
        .def("dht_state", allow_threads(&session::dht_state))
        .def("set_dht_proxy", allow_threads(&session::set_dht_proxy))
        .def("dht_proxy", allow_threads(&session::dht_proxy), return_value_policy<copy_const_reference>())
#endif
        .def("add_torrent", &add_torrent)
#ifndef TORRENT_NO_DEPRECATE
        .def(
            "add_torrent", &add_torrent_depr
          , (
                arg("resume_data") = entry(), arg("storage_mode") = storage_mode_sparse,
                arg("paused") = false
            )
        )
#endif
        .def("remove_torrent", allow_threads(&session::remove_torrent), arg("option") = session::none)
        .def("set_download_rate_limit", allow_threads(&session::set_download_rate_limit))
        .def("download_rate_limit", allow_threads(&session::download_rate_limit))
        .def("set_upload_rate_limit", allow_threads(&session::set_upload_rate_limit))
        .def("upload_rate_limit", allow_threads(&session::upload_rate_limit))
        .def("set_max_uploads", allow_threads(&session::set_max_uploads))
        .def("set_max_connections", allow_threads(&session::set_max_connections))
        .def("set_max_half_open_connections", allow_threads(&session::set_max_half_open_connections))
        .def("num_connections", allow_threads(&session::num_connections))
        .def("set_settings", allow_threads(&session::set_settings))
        .def("settings", allow_threads(&session::settings), return_value_policy<copy_const_reference>())
#ifndef TORRENT_DISABLE_ENCRYPTION
        .def("set_pe_settings", allow_threads(&session::set_pe_settings))
        .def("get_pe_settings", allow_threads(&session::get_pe_settings), return_value_policy<copy_const_reference>())
#endif
#ifndef TORRENT_DISABLE_GEO_IP
        .def("load_asnum_db", &load_asnum_db)
        .def("load_country_db", &load_country_db)
#endif
        .def("load_state", allow_threads(&session::load_state))
        .def("state", allow_threads(&session::state))
#ifndef TORRENT_NO_DEPRECATE
        .def("set_severity_level", allow_threads(&session::set_severity_level))
#endif
        .def("set_alert_mask", allow_threads(&session::set_alert_mask))
        .def("pop_alert", allow_threads(&session::pop_alert))
        .def("add_extension", &add_extension)
        .def("set_peer_proxy", allow_threads(&session::set_peer_proxy))
        .def("set_tracker_proxy", allow_threads(&session::set_tracker_proxy))
        .def("set_web_seed_proxy", allow_threads(&session::set_web_seed_proxy))
        .def("peer_proxy", allow_threads(&session::peer_proxy), return_value_policy<copy_const_reference>())
        .def("tracker_proxy", allow_threads(&session::tracker_proxy), return_value_policy<copy_const_reference>())
        .def("web_seed_proxy", allow_threads(&session::web_seed_proxy), return_value_policy<copy_const_reference>())
        .def("start_upnp", &start_upnp)
        .def("stop_upnp", allow_threads(&session::stop_upnp))
        .def("start_lsd", allow_threads(&session::start_lsd))
        .def("stop_lsd", allow_threads(&session::stop_lsd))
        .def("start_natpmp", &start_natpmp)
        .def("stop_natpmp", allow_threads(&session::stop_natpmp))
        .def("set_ip_filter", allow_threads(&session::set_ip_filter))
        .def("get_ip_filter", allow_threads(&session::get_ip_filter), return_value_policy<copy_const_reference>())
        .def("find_torrent", allow_threads(&session::find_torrent))
        .def("get_torrents", &get_torrents)
        .def("pause", allow_threads(&session::pause))
        .def("resume", allow_threads(&session::resume))
        .def("is_paused", allow_threads(&session::is_paused))
        .def("id", allow_threads(&session::id))
        .def("get_cache_status", allow_threads(&session::get_cache_status))
        ;

    register_ptr_to_python<std::auto_ptr<alert> >();
}
