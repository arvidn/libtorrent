// Copyright Daniel Wallin, Arvid Norberg 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <boost/python.hpp>
#include <list>
#include <string>
#include <libtorrent/session.hpp>
#include <libtorrent/settings.hpp> // for bencode_map_entry
#include <libtorrent/torrent.hpp>
#include <libtorrent/storage.hpp>
#include <libtorrent/ip_filter.hpp>
#include <libtorrent/disk_io_thread.hpp>
#include <libtorrent/extensions.hpp>

#include <libtorrent/extensions/lt_trackers.hpp>
#include <libtorrent/extensions/metadata_transfer.hpp>
#include <libtorrent/extensions/smart_ban.hpp>
#include <libtorrent/extensions/ut_metadata.hpp>
#include <libtorrent/extensions/ut_pex.hpp>

#include "gil.hpp"

using namespace boost::python;
using namespace libtorrent;

namespace
{
    void listen_on(session& s, int min_, int max_, char const* interface, int flags)
    {
        allow_threading_guard guard;
        error_code ec;
        s.listen_on(std::make_pair(min_, max_), ec, interface, flags);
        if (ec) throw libtorrent_exception(ec);
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
    void add_dht_node(session& s, tuple n)
    {
        std::string ip = extract<std::string>(n[0]);
        int port = extract<int>(n[1]);
        s.add_dht_node(std::make_pair(ip, port));
    }

    void add_dht_router(session& s, std::string router_, int port_)
    {
        allow_threading_guard guard;
        return s.add_dht_router(std::make_pair(router_, port_));
    }
#endif

    void add_extension(session& s, object const& e)
    {
       if (!extract<std::string>(e).check()) return;

       std::string name = extract<std::string>(e);
       if (name == "ut_metadata")
            s.add_extension(create_ut_metadata_plugin);
       else if (name == "ut_pex")
            s.add_extension(create_ut_pex_plugin);
       else if (name == "smart_ban")
            s.add_extension(create_smart_ban_plugin);
       else if (name == "lt_trackers")
            s.add_extension(create_lt_trackers_plugin);
       else if (name == "metadata_transfer")
            s.add_extension(create_metadata_plugin);
    }

#ifndef TORRENT_NO_DEPRECATE

    boost::shared_ptr<torrent_plugin> dummy_plugin_wrapper(torrent* t) {
        return boost::shared_ptr<torrent_plugin>();
    }

#endif

	void session_set_settings(session& ses, dict const& sett_dict)
	{
		bencode_map_entry* map;
		int len;
		boost::tie(map, len) = aux::settings_map();
	 
		session_settings sett;
		for (int i = 0; i < len; ++i)
		{
			if (!sett_dict.has_key(map[i].name)) continue;

			void* dest = ((char*)&sett) + map[i].offset;
			char const* name = map[i].name;
			switch (map[i].type)
			{
				case std_string:
					*((std::string*)dest) = extract<std::string>(sett_dict[name]);
					break;
				case character:
					*((char*)dest) = extract<char>(sett_dict[name]);
					break;
				case boolean:
					*((bool*)dest) = extract<bool>(sett_dict[name]);
					break;
				case integer:
					*((int*)dest) = extract<int>(sett_dict[name]);
					break;
				case floating_point:
					*((float*)dest) = extract<float>(sett_dict[name]);
					break;
			}
		}

		if (!sett_dict.has_key("outgoing_port"))
			sett.outgoing_ports.first = extract<int>(sett_dict["outgoing_port"]);
		if (!sett_dict.has_key("num_outgoing_ports"))
			sett.outgoing_ports.second = sett.outgoing_ports.first + extract<int>(sett_dict["num_outgoing_ports"]);

		ses.set_settings(sett);
	}

	dict session_get_settings(session const& ses)
	{
		session_settings sett;
		{
			allow_threading_guard guard;
			sett = ses.settings();
		}
		dict sett_dict;
		bencode_map_entry* map;
		int len;
		boost::tie(map, len) = aux::settings_map();
		for (int i = 0; i < len; ++i)
		{
			void const* dest = ((char const*)&sett) + map[i].offset;
			char const* name = map[i].name;
			switch (map[i].type)
			{
				case std_string:
					sett_dict[name] = *((std::string const*)dest);
					break;
				case character:
					sett_dict[name] = *((char const*)dest);
					break;
				case boolean:
					sett_dict[name] = *((bool const*)dest);
					break;
				case integer:
					sett_dict[name] = *((int const*)dest);
					break;
				case floating_point:
					sett_dict[name] = *((float const*)dest);
					break;
			}
		}
		sett_dict["outgoing_port"] = sett.outgoing_ports.first;
		sett_dict["num_outgoing_ports"] = sett.outgoing_ports.second - sett.outgoing_ports.first + 1;
		return sett_dict;
	}

#ifndef BOOST_NO_EXCEPTIONS
#ifndef TORRENT_NO_DEPRECATE
    torrent_handle add_torrent_depr(session& s, torrent_info const& ti
        , std::string const& save, entry const& resume
        , storage_mode_t storage_mode, bool paused)
    {
        allow_threading_guard guard;
        return s.add_torrent(ti, save, resume, storage_mode, paused, default_storage_constructor);
    }
#endif
#endif
}

    void dict_to_add_torrent_params(dict params, add_torrent_params& p
        , std::vector<char>& rd, std::vector<boost::uint8_t>& fp)
    {
        // torrent_info objects are always held by an intrusive_ptr in the python binding
        if (params.has_key("ti") && params.get("ti") != boost::python::object())
            p.ti = extract<intrusive_ptr<torrent_info> >(params["ti"]);

        if (params.has_key("info_hash"))
            p.info_hash = extract<sha1_hash>(params["info_hash"]);
        if (params.has_key("name"))
            p.name = extract<std::string>(params["name"]);
        p.save_path = extract<std::string>(params["save_path"]);

        if (params.has_key("resume_data"))
        {
            std::string resume = extract<std::string>(params["resume_data"]);
            rd.resize(resume.size());
            std::memcpy(&rd[0], &resume[0], rd.size());
            p.resume_data = &rd;
        }
        if (params.has_key("storage_mode"))
            p.storage_mode = extract<storage_mode_t>(params["storage_mode"]);

        if (params.has_key("trackers"))
        {
            list l = extract<list>(params["trackers"]);
            int n = boost::python::len(l);
            for(int i = 0; i < n; i++)
                p.trackers.push_back(extract<std::string>(l[i]));
        }

        if (params.has_key("dht_nodes"))
        {
            list l = extract<list>(params["dht_nodes"]);
            int n = boost::python::len(l);
            for(int i = 0; i < n; i++)
                p.dht_nodes.push_back(extract<std::pair<std::string, int> >(l[i]));
        }
#ifndef TORRENT_NO_DEPRECATE
        std::string url;
        if (params.has_key("tracker_url"))
            p.trackers.push_back(extract<std::string>(params["tracker_url"]));
        if (params.has_key("seed_mode"))
            p.seed_mode = params["seed_mode"];
        if (params.has_key("upload_mode"))
            p.upload_mode = params["upload_mode"];
        if (params.has_key("share_mode"))
            p.upload_mode = params["share_mode"];
        if (params.has_key("override_resume_data"))
            p.override_resume_data = params["override_resume_data"];
        if (params.has_key("apply_ip_filter"))
            p.apply_ip_filter = params["apply_ip_filter"];
        if (params.has_key("paused"))
            p.paused = params["paused"];
        if (params.has_key("auto_managed"))
            p.auto_managed = params["auto_managed"];
        if (params.has_key("duplicate_is_error"))
            p.duplicate_is_error = params["duplicate_is_error"];
        if (params.has_key("merge_resume_trackers"))
            p.merge_resume_trackers = params["merge_resume_trackers"];
#endif
        if (params.has_key("flags"))
            p.flags = extract<boost::uint64_t>(params["flags"]);

        if (params.has_key("trackerid"))
            p.trackerid = extract<std::string>(params["trackerid"]);
        if (params.has_key("url"))
            p.url = extract<std::string>(params["url"]);
        if (params.has_key("source_feed_url"))
            p.source_feed_url = extract<std::string>(params["source_feed_url"]);
        if (params.has_key("uuid"))
            p.uuid = extract<std::string>(params["uuid"]);

        fp.clear();
        if (params.has_key("file_priorities"))
        {
            list l = extract<list>(params["file_priorities"]);
            int n = boost::python::len(l);
            for(int i = 0; i < n; i++)
                fp.push_back(extract<boost::uint8_t>(l[i]));
            p.file_priorities = &fp;
        }
    }

namespace
{

    torrent_handle add_torrent(session& s, dict params)
    {
        add_torrent_params p;
        std::vector<char> resume_buf;
        std::vector<boost::uint8_t> files_buf;
        dict_to_add_torrent_params(params, p, resume_buf, files_buf);

        allow_threading_guard guard;

#ifndef BOOST_NO_EXCEPTIONS
        return s.add_torrent(p);
#else
        error_code ec;
        return s.add_torrent(p, ec);
#endif
    }

    void async_add_torrent(session& s, dict params)
    {
        add_torrent_params p;
        std::vector<char> resume_buf;
        std::vector<boost::uint8_t> files_buf;
        dict_to_add_torrent_params(params, p, resume_buf, files_buf);

        allow_threading_guard guard;

#ifndef BOOST_NO_EXCEPTIONS
        s.async_add_torrent(p);
#else
        error_code ec;
        s.async_add_torrent(p, ec);
#endif
    }

    void dict_to_feed_settings(dict params, feed_settings& feed
        , std::vector<char>& resume_buf
        , std::vector<boost::uint8_t> files_buf)
    {
        if (params.has_key("auto_download"))
            feed.auto_download = extract<bool>(params["auto_download"]);
        if (params.has_key("default_ttl"))
            feed.default_ttl = extract<int>(params["default_ttl"]);
        if (params.has_key("url"))
            feed.url = extract<std::string>(params["url"]);
        if (params.has_key("add_args"))
            dict_to_add_torrent_params(dict(params["add_args"]), feed.add_args
                , resume_buf, files_buf);
    }

    feed_handle add_feed(session& s, dict params)
    {
        feed_settings feed;
        // this static here is a bit of a hack. It will
        // probably work for the most part
        static std::vector<char> resume_buf;
        static std::vector<boost::uint8_t> files_buf;
        dict_to_feed_settings(params, feed, resume_buf, files_buf);

        allow_threading_guard guard;
        return s.add_feed(feed);
    }

    dict get_feed_status(feed_handle const& h)
    {
        feed_status s;
        {
            allow_threading_guard guard;
            s = h.get_feed_status();
        }
        dict ret;
        ret["url"] = s.url;
        ret["title"] = s.title;
        ret["description"] = s.description;
        ret["last_update"] = s.last_update;
        ret["next_update"] = s.next_update;
        ret["updating"] = s.updating;
        ret["error"] = s.error ? s.error.message() : "";
        ret["ttl"] = s.ttl;

        list items;
        for (std::vector<feed_item>::iterator i = s.items.begin()
            , end(s.items.end()); i != end; ++i)
        {
            dict item;
            item["url"] = i->url;
            item["uuid"] = i->uuid;
            item["title"] = i->title;
            item["description"] = i->description;
            item["comment"] = i->comment;
            item["category"] = i->category;
            item["size"] = i->size;
            item["handle"] = i->handle;
            item["info_hash"] = i->info_hash.to_string();
            items.append(item);
        }
        ret["items"] = items;
        return ret;
    }

    void set_feed_settings(feed_handle& h, dict sett)
    {
        feed_settings feed;
        static std::vector<char> resume_buf;
        static std::vector<boost::uint8_t> files_buf;
        dict_to_feed_settings(sett, feed, resume_buf, files_buf);
        h.set_settings(feed);
    }

    dict get_feed_settings(feed_handle& h)
    {
        feed_settings s;
        {
            allow_threading_guard guard;
            s = h.settings();
        }
        dict ret;
        ret["url"] = s.url;
        ret["auto_download"] = s.auto_download;
        ret["default_ttl"] = s.default_ttl;
        return ret;
    }

    void start_natpmp(session& s)
    {
        allow_threading_guard guard;
        s.start_natpmp();
    }

    void start_upnp(session& s)
    {
        allow_threading_guard guard;
        s.start_upnp();
    }

    alert const* wait_for_alert(session& s, int ms)
    {
        allow_threading_guard guard;
        return s.wait_for_alert(milliseconds(ms));
    }

    list get_torrents(session& s)
    {
        list ret;
        std::vector<torrent_handle> torrents;
        {
           allow_threading_guard guard;
           torrents = s.get_torrents();
        }

        for (std::vector<torrent_handle>::iterator i = torrents.begin(); i != torrents.end(); ++i)
        {
            ret.append(*i);
        }
        return ret;
    }

    dict get_utp_stats(session_status const& st)
    {
        dict ret;
        ret["num_idle"] = st.utp_stats.num_idle;
        ret["num_syn_sent"] = st.utp_stats.num_syn_sent;
        ret["num_connected"] = st.utp_stats.num_connected;
        ret["num_fin_sent"] = st.utp_stats.num_fin_sent;
        ret["num_close_wait"] = st.utp_stats.num_close_wait;
        return ret;
    }

    list get_cache_info(session& ses, sha1_hash ih)
    {
        std::vector<cached_piece_info> ret;

        {
           allow_threading_guard guard;
           ses.get_cache_info(ih, ret);
        }

        list pieces;
        ptime now = time_now();
        for (std::vector<cached_piece_info>::iterator i = ret.begin()
           , end(ret.end()); i != end; ++i)
        {
            dict d;
            d["piece"] = i->piece;
            d["last_use"] = total_milliseconds(now - i->last_use) / 1000.f;
            d["next_to_hash"] = i->next_to_hash;
            d["kind"] = i->kind;
            pieces.append(d);
        }
        return pieces;
    }

#ifndef TORRENT_DISABLE_GEO_IP
    void load_asnum_db(session& s, std::string file)
    {
        allow_threading_guard guard;
        s.load_asnum_db(file.c_str());
    }

    void load_country_db(session& s, std::string file)
    {
        allow_threading_guard guard;
        s.load_country_db(file.c_str());
    }
#endif

    entry save_state(session const& s, boost::uint32_t flags)
    {
        allow_threading_guard guard;
        entry e;
        s.save_state(e, flags);
        return e;
    }

    list pop_alerts(session& ses)
    {
        std::deque<alert*> alerts;
        {
            allow_threading_guard guard;
            ses.pop_alerts(&alerts);
        }

        list ret;
        for (std::deque<alert*>::iterator i = alerts.begin()
            , end(alerts.end()); i != end; ++i)
        {
				std::auto_ptr<alert> ptr(*i);
            ret.append(ptr);
        }
        return ret;
    }

} // namespace unnamed


void bind_session()
{
#ifndef TORRENT_DISABLE_DHT
    void (session::*start_dht0)() = &session::start_dht;
#ifndef TORRENT_NO_DEPRECATE
    void (session::*start_dht1)(entry const&) = &session::start_dht;
#endif
#endif

    void (session::*load_state0)(lazy_entry const&) = &session::load_state;
#ifndef TORRENT_NO_DEPRECATE
    void (session::*load_state1)(entry const&) = &session::load_state;
#endif

    class_<session_status>("session_status")
        .def_readonly("has_incoming_connections", &session_status::has_incoming_connections)

        .def_readonly("upload_rate", &session_status::upload_rate)
        .def_readonly("download_rate", &session_status::download_rate)
        .def_readonly("total_download", &session_status::total_download)
        .def_readonly("total_upload", &session_status::total_upload)

        .def_readonly("payload_upload_rate", &session_status::payload_upload_rate)
        .def_readonly("payload_download_rate", &session_status::payload_download_rate)
        .def_readonly("total_payload_download", &session_status::total_payload_download)
        .def_readonly("total_payload_upload", &session_status::total_payload_upload)

        .def_readonly("ip_overhead_upload_rate", &session_status::ip_overhead_upload_rate)
        .def_readonly("ip_overhead_download_rate", &session_status::ip_overhead_download_rate)
        .def_readonly("total_ip_overhead_download", &session_status::total_ip_overhead_download)
        .def_readonly("total_ip_overhead_upload", &session_status::total_ip_overhead_upload)

        .def_readonly("dht_upload_rate", &session_status::dht_upload_rate)
        .def_readonly("dht_download_rate", &session_status::dht_download_rate)
        .def_readonly("total_dht_download", &session_status::total_dht_download)
        .def_readonly("total_dht_upload", &session_status::total_dht_upload)

        .def_readonly("tracker_upload_rate", &session_status::tracker_upload_rate)
        .def_readonly("tracker_download_rate", &session_status::tracker_download_rate)
        .def_readonly("total_tracker_download", &session_status::total_tracker_download)
        .def_readonly("total_tracker_upload", &session_status::total_tracker_upload)

        .def_readonly("total_redundant_bytes", &session_status::total_redundant_bytes)
        .def_readonly("total_failed_bytes", &session_status::total_failed_bytes)

        .def_readonly("num_peers", &session_status::num_peers)
        .def_readonly("num_unchoked", &session_status::num_unchoked)
        .def_readonly("allowed_upload_slots", &session_status::allowed_upload_slots)

        .def_readonly("up_bandwidth_queue", &session_status::up_bandwidth_queue)
        .def_readonly("down_bandwidth_queue", &session_status::down_bandwidth_queue)

        .def_readonly("up_bandwidth_bytes_queue", &session_status::up_bandwidth_bytes_queue)
        .def_readonly("down_bandwidth_bytes_queue", &session_status::down_bandwidth_bytes_queue)

        .def_readonly("optimistic_unchoke_counter", &session_status::optimistic_unchoke_counter)
        .def_readonly("unchoke_counter", &session_status::unchoke_counter)

#ifndef TORRENT_DISABLE_DHT
        .def_readonly("dht_nodes", &session_status::dht_nodes)
        .def_readonly("dht_node_cache", &session_status::dht_node_cache)
        .def_readonly("dht_torrents", &session_status::dht_torrents)
        .def_readonly("dht_global_nodes", &session_status::dht_global_nodes)
        .def_readonly("active_requests", &session_status::active_requests)
        .def_readonly("dht_total_allocations", &session_status::dht_total_allocations)
#endif
        .add_property("utp_stats", &get_utp_stats)
        ;

#ifndef TORRENT_DISABLE_DHT
    class_<dht_lookup>("dht_lookup")
        .def_readonly("type", &dht_lookup::type)
        .def_readonly("outstanding_requests", &dht_lookup::outstanding_requests)
        .def_readonly("timeouts", &dht_lookup::timeouts)
        .def_readonly("response", &dht_lookup::responses)
        .def_readonly("branch_factor", &dht_lookup::branch_factor)
    ;
#endif

    enum_<storage_mode_t>("storage_mode_t")
        .value("storage_mode_allocate", storage_mode_allocate)
        .value("storage_mode_sparse", storage_mode_sparse)
#ifndef TORRENT_NO_DEPRECATE
        .value("storage_mode_compact", storage_mode_compact)
#endif
    ;

    enum_<session::options_t>("options_t")
        .value("none", session::none)
        .value("delete_files", session::delete_files)
    ;

    enum_<session::session_flags_t>("session_flags_t")
        .value("add_default_plugins", session::add_default_plugins)
        .value("start_default_features", session::start_default_features)
    ;

    enum_<add_torrent_params::flags_t>("add_torrent_params_flags_t")
        .value("flag_seed_mode", add_torrent_params::flag_seed_mode)
        .value("flag_override_resume_data", add_torrent_params::flag_override_resume_data)
        .value("flag_upload_mode", add_torrent_params::flag_upload_mode)
        .value("flag_share_mode", add_torrent_params::flag_share_mode)
        .value("flag_apply_ip_filter", add_torrent_params::flag_apply_ip_filter)
        .value("flag_paused", add_torrent_params::flag_paused)
        .value("flag_auto_managed", add_torrent_params::flag_auto_managed)
        .value("flag_duplicate_is_error", add_torrent_params::flag_duplicate_is_error)
        .value("flag_merge_resume_trackers", add_torrent_params::flag_merge_resume_trackers)
        .value("flag_update_subscribe", add_torrent_params::flag_update_subscribe)
    ;
    class_<cache_status>("cache_status")
        .def_readonly("blocks_written", &cache_status::blocks_written)
        .def_readonly("writes", &cache_status::writes)
        .def_readonly("blocks_read", &cache_status::blocks_read)
        .def_readonly("blocks_read_hit", &cache_status::blocks_read_hit)
        .def_readonly("reads", &cache_status::reads)
        .def_readonly("queued_bytes", &cache_status::queued_bytes)
        .def_readonly("cache_size", &cache_status::cache_size)
        .def_readonly("read_cache_size", &cache_status::read_cache_size)
        .def_readonly("total_used_buffers", &cache_status::total_used_buffers)
        .def_readonly("average_queue_time", &cache_status::average_queue_time)
        .def_readonly("average_read_time", &cache_status::average_read_time)
        .def_readonly("average_write_time", &cache_status::average_write_time)
        .def_readonly("average_hash_time", &cache_status::average_hash_time)
        .def_readonly("average_job_time", &cache_status::average_job_time)
        .def_readonly("average_sort_time", &cache_status::average_sort_time)
        .def_readonly("job_queue_length", &cache_status::job_queue_length)
        .def_readonly("cumulative_job_time", &cache_status::cumulative_job_time)
        .def_readonly("cumulative_read_time", &cache_status::cumulative_read_time)
        .def_readonly("cumulative_write_time", &cache_status::cumulative_write_time)
        .def_readonly("cumulative_hash_time", &cache_status::cumulative_hash_time)
        .def_readonly("cumulative_sort_time", &cache_status::cumulative_sort_time)
        .def_readonly("total_read_back", &cache_status::total_read_back)
        .def_readonly("read_queue_size", &cache_status::read_queue_size)
    ;

    class_<session, boost::noncopyable>("session", no_init)
        .def(
            init<fingerprint, int>((
                arg("fingerprint")=fingerprint("LT",0,1,0,0)
                , arg("flags")=session::start_default_features | session::add_default_plugins))
        )
        .def("post_torrent_updates", allow_threads(&session::post_torrent_updates))
        .def(
            "listen_on", &listen_on
          , (arg("min"), "max", arg("interface") = (char const*)0, arg("flags") = 0)
        )
        .def("outgoing_ports", &outgoing_ports)
        .def("is_listening", allow_threads(&session::is_listening))
        .def("listen_port", allow_threads(&session::listen_port))
        .def("status", allow_threads(&session::status))
#ifndef TORRENT_DISABLE_DHT
        .def("add_dht_node", add_dht_node)
        .def(
            "add_dht_router", &add_dht_router
          , (arg("router"), "port")
        )
        .def("is_dht_running", allow_threads(&session::is_dht_running))
        .def("set_dht_settings", allow_threads(&session::set_dht_settings))
        .def("start_dht", allow_threads(start_dht0))
        .def("stop_dht", allow_threads(&session::stop_dht))
#ifndef TORRENT_NO_DEPRECATE
        .def("start_dht", allow_threads(start_dht1))
        .def("dht_state", allow_threads(&session::dht_state))
        .def("set_dht_proxy", allow_threads(&session::set_dht_proxy))
        .def("dht_proxy", allow_threads(&session::dht_proxy))
#endif
#endif
        .def("add_torrent", &add_torrent)
        .def("async_add_torrent", &async_add_torrent)
#ifndef BOOST_NO_EXCEPTIONS
#ifndef TORRENT_NO_DEPRECATE
        .def(
            "add_torrent", &add_torrent_depr
          , (
                arg("resume_data") = entry(),
                arg("storage_mode") = storage_mode_sparse,
                arg("paused") = false
            )
        )
#endif
#endif
        .def("add_feed", &add_feed)
        .def("remove_torrent", allow_threads(&session::remove_torrent), arg("option") = session::none
)
#ifndef TORRENT_NO_DEPRECATE
        .def("set_local_download_rate_limit", allow_threads(&session::set_local_download_rate_limit))
        .def("local_download_rate_limit", allow_threads(&session::local_download_rate_limit))

        .def("set_local_upload_rate_limit", allow_threads(&session::set_local_upload_rate_limit))
        .def("local_upload_rate_limit", allow_threads(&session::local_upload_rate_limit))

        .def("set_download_rate_limit", allow_threads(&session::set_download_rate_limit))
        .def("download_rate_limit", allow_threads(&session::download_rate_limit))

        .def("set_upload_rate_limit", allow_threads(&session::set_upload_rate_limit))
        .def("upload_rate_limit", allow_threads(&session::upload_rate_limit))

        .def("set_max_uploads", allow_threads(&session::set_max_uploads))
        .def("set_max_connections", allow_threads(&session::set_max_connections))
        .def("max_connections", allow_threads(&session::max_connections))
        .def("set_max_half_open_connections", allow_threads(&session::set_max_half_open_connections))
        .def("num_connections", allow_threads(&session::num_connections))
        .def("set_settings", &session::set_settings)
        .def("settings", &session::settings)
        .def("get_settings", &session_get_settings)
#else
        .def("settings", &session_get_settings)
        .def("get_settings", &session_get_settings)
#endif
        .def("set_settings", &session_set_settings)
#ifndef TORRENT_DISABLE_ENCRYPTION
        .def("set_pe_settings", allow_threads(&session::set_pe_settings))
        .def("get_pe_settings", allow_threads(&session::get_pe_settings))
#endif
#ifndef TORRENT_DISABLE_GEO_IP
        .def("load_asnum_db", &load_asnum_db)
        .def("load_country_db", &load_country_db)
#endif
        .def("load_state", load_state0)
        .def("save_state", &save_state, (arg("entry"), arg("flags") = 0xffffffff))
#ifndef TORRENT_NO_DEPRECATE
        .def("load_state", load_state1)
        .def("set_severity_level", allow_threads(&session::set_severity_level))
        .def("set_alert_queue_size_limit", allow_threads(&session::set_alert_queue_size_limit))
#endif
        .def("set_alert_mask", allow_threads(&session::set_alert_mask))
        .def("pop_alert", allow_threads(&session::pop_alert))
        .def("pop_alerts", &pop_alerts)
        .def("wait_for_alert", &wait_for_alert, return_internal_reference<>())
        .def("add_extension", &add_extension)
#ifndef TORRENT_NO_DEPRECATE
        .def("set_peer_proxy", allow_threads(&session::set_peer_proxy))
        .def("set_tracker_proxy", allow_threads(&session::set_tracker_proxy))
        .def("set_web_seed_proxy", allow_threads(&session::set_web_seed_proxy))
        .def("peer_proxy", allow_threads(&session::peer_proxy))
        .def("tracker_proxy", allow_threads(&session::tracker_proxy))
        .def("web_seed_proxy", allow_threads(&session::web_seed_proxy))
#endif
#if TORRENT_USE_I2P
        .def("set_i2p_proxy", allow_threads(&session::set_i2p_proxy))
        .def("i2p_proxy", allow_threads(&session::i2p_proxy))
#endif
        .def("set_proxy", allow_threads(&session::set_proxy))
        .def("proxy", allow_threads(&session::proxy))
        .def("start_upnp", &start_upnp)
        .def("stop_upnp", allow_threads(&session::stop_upnp))
        .def("start_lsd", allow_threads(&session::start_lsd))
        .def("stop_lsd", allow_threads(&session::stop_lsd))
        .def("start_natpmp", &start_natpmp)
        .def("stop_natpmp", allow_threads(&session::stop_natpmp))
        .def("set_ip_filter", allow_threads(&session::set_ip_filter))
        .def("get_ip_filter", allow_threads(&session::get_ip_filter))
        .def("find_torrent", allow_threads(&session::find_torrent))
        .def("get_torrents", &get_torrents)
        .def("pause", allow_threads(&session::pause))
        .def("resume", allow_threads(&session::resume))
        .def("is_paused", allow_threads(&session::is_paused))
        .def("id", allow_threads(&session::id))
        .def("get_cache_status", allow_threads(&session::get_cache_status))
		  .def("get_cache_info", get_cache_info)
        .def("set_peer_id", allow_threads(&session::set_peer_id))
        ;

    enum_<session::save_state_flags_t>("save_state_flags_t")
        .value("save_settings", session::save_settings)
        .value("save_dht_settings", session::save_dht_settings)
        .value("save_dht_state", session::save_dht_state)
        .value("save_i2p_proxy", session::save_i2p_proxy)
        .value("save_encryption_settings", session:: save_encryption_settings)
        .value("save_as_map", session::save_as_map)
        .value("save_proxy", session::save_proxy)
#ifndef TORRENT_NO_DEPRECATE
        .value("save_dht_proxy", session::save_dht_proxy)
        .value("save_peer_proxy", session::save_peer_proxy)
        .value("save_web_proxy", session::save_web_proxy)
        .value("save_tracker_proxy", session::save_tracker_proxy)
#endif
    ;

    enum_<session::listen_on_flags_t>("listen_on_flags_t")
#ifndef TORRENT_NO_DEPRECATE
        .value("listen_reuse_address", session::listen_reuse_address)
#endif
        .value("listen_no_system_port", session::listen_no_system_port)
    ;

    class_<feed_handle>("feed_handle")
        .def("update_feed", &feed_handle::update_feed)
        .def("get_feed_status", &get_feed_status)
        .def("set_settings", &set_feed_settings)
        .def("settings", &get_feed_settings)
    ;

#ifndef TORRENT_NO_DEPRECATE
    def("create_ut_pex_plugin", dummy_plugin_wrapper);
    def("create_metadata_plugin", dummy_plugin_wrapper);
    def("create_ut_metadata_plugin", dummy_plugin_wrapper);
    def("create_smart_ban_plugin", dummy_plugin_wrapper);
#endif

    register_ptr_to_python<std::auto_ptr<alert> >();

	 def("high_performance_seed", high_performance_seed);
	 def("min_memory_usage", min_memory_usage);

	 scope().attr("create_metadata_plugin") = "metadata_transfer";
	 scope().attr("create_ut_metadata_plugin") = "ut_metadata";
	 scope().attr("create_ut_pex_plugin") = "ut_pex";
	 scope().attr("create_smart_ban_plugin") = "smart_ban";
}
