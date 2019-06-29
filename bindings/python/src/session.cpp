// Copyright Daniel Wallin, Arvid Norberg 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <list>
#include <string>
#include <libtorrent/session.hpp>
#include <libtorrent/storage.hpp>
#include <libtorrent/error_code.hpp>
#include <libtorrent/ip_filter.hpp>
#include <libtorrent/disk_io_thread.hpp>
#include <libtorrent/extensions.hpp>
#include <libtorrent/bdecode.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/kademlia/item.hpp> // for sign_mutable_item
#include <libtorrent/time.hpp>
#include <libtorrent/session_stats.hpp>
#include <libtorrent/session_status.hpp>
#include <libtorrent/peer_class_type_filter.hpp>
#include <libtorrent/torrent_status.hpp>

#ifndef TORRENT_NO_DEPRECATE
#include <libtorrent/extensions/lt_trackers.hpp>
#include <libtorrent/extensions/metadata_transfer.hpp>
#endif
#include <libtorrent/extensions/smart_ban.hpp>
#include <libtorrent/extensions/ut_metadata.hpp>
#include <libtorrent/extensions/ut_pex.hpp>

#include "gil.hpp"
#include "bytes.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include "boost_python.hpp"

#include "libtorrent/aux_/disable_warnings_pop.hpp"

using namespace boost::python;
using namespace libtorrent;
namespace lt = libtorrent;

namespace
{
#ifndef TORRENT_NO_DEPRECATE
    void listen_on(lt::session& s, int min_, int max_, char const* interface, int flags)
    {
        allow_threading_guard guard;
        error_code ec;
        s.listen_on(std::make_pair(min_, max_), ec, interface, flags);
#ifndef BOOST_NO_EXCEPTIONS
        if (ec) throw libtorrent_exception(ec);
#endif
    }

    void outgoing_ports(lt::session& s, int _min, int _max)
    {
        allow_threading_guard guard;
        settings_pack p;
        p.set_int(settings_pack::outgoing_port, _min);
        p.set_int(settings_pack::num_outgoing_ports, _max - _min);
        s.apply_settings(p);
        return;
    }
#endif // TORRENT_NO_DEPRECATE

#ifndef TORRENT_DISABLE_DHT
    void add_dht_node(lt::session& s, tuple n)
    {
        std::string ip = extract<std::string>(n[0]);
        int port = extract<int>(n[1]);
        allow_threading_guard guard;
        s.add_dht_node(std::make_pair(ip, port));
    }

#ifndef TORRENT_NO_DEPRECATE
    void add_dht_router(lt::session& s, std::string router_, int port_)
    {
        allow_threading_guard guard;
        return s.add_dht_router(std::make_pair(router_, port_));
    }
#endif

#endif // TORRENT_DISABLE_DHT

    void add_extension(lt::session& s, object const& e)
    {
#ifndef TORRENT_DISABLE_EXTENSIONS
       if (!extract<std::string>(e).check()) return;

       std::string name = extract<std::string>(e);
       if (name == "ut_metadata")
            s.add_extension(create_ut_metadata_plugin);
       else if (name == "ut_pex")
            s.add_extension(create_ut_pex_plugin);
       else if (name == "smart_ban")
            s.add_extension(create_smart_ban_plugin);
#ifndef TORRENT_NO_DEPRECATE
       else if (name == "lt_trackers")
            s.add_extension(create_lt_trackers_plugin);
       else if (name == "metadata_transfer")
            s.add_extension(create_metadata_plugin);
#endif // TORRENT_NO_DEPRECATE

#endif // TORRENT_DISABLE_EXTENSIONS
    }

	void make_settings_pack(lt::settings_pack& p, dict const& sett_dict)
	{
		stl_input_iterator<std::string> i(sett_dict.keys()), end;
		for (; i != end; ++i)
		{
			std::string const key = *i;

			int sett = setting_by_name(key);
			if (sett < 0)
			{
				PyErr_SetString(PyExc_KeyError, ("unknown name in settings_pack: " + key).c_str());
				throw_error_already_set();
			}

			TORRENT_TRY
			{
				object const value = sett_dict[key];
				switch (sett & settings_pack::type_mask)
				{
					case settings_pack::string_type_base:
						p.set_str(sett, extract<std::string>(value));
						break;
					case settings_pack::int_type_base:
						p.set_int(sett, extract<int>(value));
						break;
					case settings_pack::bool_type_base:
						p.set_bool(sett, extract<bool>(value));
						break;
				}
			}
			TORRENT_CATCH(...) {}
		}
	}

	dict make_dict(lt::settings_pack const& sett)
	{
		dict ret;
		for (int i = settings_pack::string_type_base;
			i < settings_pack::max_string_setting_internal; ++i)
		{
			ret[name_for_setting(i)] = sett.get_str(i);
		}

		for (int i = settings_pack::int_type_base;
			i < settings_pack::max_int_setting_internal; ++i)
		{
			ret[name_for_setting(i)] = sett.get_int(i);
		}

		for (int i = settings_pack::bool_type_base;
			i < settings_pack::max_bool_setting_internal; ++i)
		{
			ret[name_for_setting(i)] = sett.get_bool(i);
		}
		return ret;
	}

	boost::shared_ptr<lt::session> make_session(boost::python::dict sett, int flags)
	{
		settings_pack p;
		make_settings_pack(p, sett);
		return boost::make_shared<lt::session>(p, flags);
	}

#ifndef TORRENT_NO_DEPRECATE
	void session_set_settings(lt::session& ses, object const& sett)
	{
		extract<session_settings> old_settings(sett);
		if (old_settings.check())
		{
			allow_threading_guard guard;
			ses.set_settings(old_settings);
		}
		else
		{
			settings_pack p;
			make_settings_pack(p, extract<dict>(sett));
			allow_threading_guard guard;
			ses.apply_settings(p);
		}
	}
#endif

	void session_apply_settings(lt::session& ses, dict const& sett_dict)
	{
		settings_pack p;
		make_settings_pack(p, sett_dict);
		allow_threading_guard guard;
		ses.apply_settings(p);
	}

	dict session_get_settings(lt::session const& ses)
	{
		settings_pack sett;
		{
			allow_threading_guard guard;
			sett = ses.get_settings();
		}
		return make_dict(sett);
	}

	dict min_memory_usage_wrapper()
	{
		settings_pack ret;
		min_memory_usage(ret);
		return make_dict(ret);
	}

	dict default_settings_wrapper()
	{
		return make_dict(default_settings());
	}

	dict high_performance_seed_wrapper()
	{
		settings_pack ret;
		high_performance_seed(ret);
		return make_dict(ret);
	}

#ifndef BOOST_NO_EXCEPTIONS
#ifndef TORRENT_NO_DEPRECATE
    torrent_handle add_torrent_depr(lt::session& s, torrent_info const& ti
        , std::string const& save, entry const& resume
        , storage_mode_t storage_mode, bool paused)
    {
        allow_threading_guard guard;
        return s.add_torrent(ti, save, resume, storage_mode, paused, default_storage_constructor);
    }
#endif
#endif
}

    void dict_to_add_torrent_params(dict params, add_torrent_params& p)
    {
        // torrent_info objects are always held by a shared_ptr in the python binding
        if (params.has_key("ti") && params.get("ti") != boost::python::object())
        {
           // make a copy here. We don't want to end up holding a python-owned
           // object inside libtorrent. If the last reference goes out of scope
           // on the C++ side, it will end up freeing the python object
           // without holding the GIL and likely crash.
           // https://mail.python.org/pipermail/cplusplus-sig/2007-June/012130.html
           p.ti = boost::make_shared<torrent_info>(
              extract<torrent_info const&>(params["ti"]));
        }


        if (params.has_key("info_hash"))
            p.info_hash = sha1_hash(bytes(extract<bytes>(params["info_hash"])).arr);
        if (params.has_key("name"))
            p.name = extract<std::string>(params["name"]);
        p.save_path = extract<std::string>(params["save_path"]);

        if (params.has_key("resume_data"))
        {
            std::string resume = extract<std::string>(params["resume_data"]);
            p.resume_data.assign(resume.begin(), resume.end());
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

        if (params.has_key("file_priorities"))
        {
            list l = extract<list>(params["file_priorities"]);
            int n = boost::python::len(l);
            p.file_priorities.clear();
            for(int i = 0; i < n; i++)
                p.file_priorities.push_back(extract<boost::uint8_t>(l[i]));
        }
    }

namespace
{

    torrent_handle add_torrent(lt::session& s, dict params)
    {
        add_torrent_params p;
        dict_to_add_torrent_params(params, p);

        allow_threading_guard guard;

#ifndef BOOST_NO_EXCEPTIONS
        return s.add_torrent(p);
#else
        error_code ec;
        return s.add_torrent(p, ec);
#endif
    }

    void async_add_torrent(lt::session& s, dict params)
    {
        add_torrent_params p;
        dict_to_add_torrent_params(params, p);

        allow_threading_guard guard;

        s.async_add_torrent(p);
    }

#ifndef TORRENT_NO_DEPRECATE
    void dict_to_feed_settings(dict params, feed_settings& feed)
    {
        if (params.has_key("auto_download"))
            feed.auto_download = extract<bool>(params["auto_download"]);
        if (params.has_key("default_ttl"))
            feed.default_ttl = extract<int>(params["default_ttl"]);
        if (params.has_key("url"))
            feed.url = extract<std::string>(params["url"]);
        if (params.has_key("add_args"))
            dict_to_add_torrent_params(dict(params["add_args"]), feed.add_args);
    }

    feed_handle add_feed(lt::session& s, dict params)
    {
        feed_settings feed;
        // this static here is a bit of a hack. It will
        // probably work for the most part
        dict_to_feed_settings(params, feed);

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
        dict_to_feed_settings(sett, feed);
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

    void start_natpmp(lt::session& s)
    {
        allow_threading_guard guard;
        s.start_natpmp();
    }

    void start_upnp(lt::session& s)
    {
        allow_threading_guard guard;
        s.start_upnp();
    }
#endif // TORRENT_NO_DEPRECATE

#ifndef TORRENT_NO_DEPRECATE
    boost::shared_ptr<alert>
#else
    alert const*
#endif
    wait_for_alert(lt::session& s, int ms)
    {
        allow_threading_guard guard;
        alert const* a = s.wait_for_alert(milliseconds(ms));
#ifndef TORRENT_NO_DEPRECATE
        if (a == NULL) return boost::shared_ptr<alert>();
        return boost::shared_ptr<alert>(a->clone().release());
#else
        return a;
#endif
    }

    list get_torrents(lt::session& s)
    {
        std::vector<torrent_handle> torrents;
        {
           allow_threading_guard guard;
           torrents = s.get_torrents();
        }

        list ret;
        for (std::vector<torrent_handle>::iterator i = torrents.begin(); i != torrents.end(); ++i)
            ret.append(*i);
        return ret;
    }

    bool wrap_pred(object pred, torrent_status const& st)
    {
        return pred(st);
    }

    list get_torrent_status(lt::session& s, object pred, int const flags)
    {
        std::vector<torrent_status> torrents;
        s.get_torrent_status(&torrents, boost::bind(&wrap_pred, pred, _1), flags);

        list ret;
        for (std::vector<torrent_status>::iterator i = torrents.begin(); i != torrents.end(); ++i)
            ret.append(*i);
        return ret;
    }

    list refresh_torrent_status(lt::session& s, list in_torrents, int const flags)
    {
        std::vector<torrent_status> torrents;
        int const n = boost::python::len(in_torrents);
        for (int i = 0; i < n; ++i)
           torrents.push_back(extract<torrent_status>(in_torrents[i]));

        {
           allow_threading_guard guard;
           s.refresh_torrent_status(&torrents, flags);
        }

        list ret;
        for (std::vector<torrent_status>::iterator i = torrents.begin(); i != torrents.end(); ++i)
            ret.append(*i);
        return ret;
    }

    cache_status get_cache_info1(lt::session& s, torrent_handle h, int flags)
    {
       cache_status ret;
       s.get_cache_info(&ret, h, flags);
       return ret;
    }

    list cached_piece_info_list(std::vector<cached_piece_info> const& v)
    {
       list pieces;
       lt::time_point now = lt::clock_type::now();
       for (std::vector<cached_piece_info>::const_iterator i = v.begin()
          , end(v.end()); i != end; ++i)
       {
          dict d;
          d["piece"] = i->piece;
          d["last_use"] = total_milliseconds(now - i->last_use) / 1000.f;
          d["next_to_hash"] = i->next_to_hash;
          d["kind"] = static_cast<int>(i->kind);
          pieces.append(d);
       }
       return pieces;
    }

    list cache_status_pieces(cache_status const& cs)
    {
        return cached_piece_info_list(cs.pieces);
    }

#ifndef TORRENT_NO_DEPRECATE
    cache_status get_cache_status(lt::session& s)
    {
       cache_status ret;
       s.get_cache_info(&ret);
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

    list get_cache_info2(lt::session& ses, sha1_hash ih)
    {
       std::vector<cached_piece_info> ret;

       {
          allow_threading_guard guard;
          ses.get_cache_info(ih, ret);
       }

       return cached_piece_info_list(ret);
    }
#endif

    entry save_state(lt::session const& s, boost::uint32_t flags)
    {
        allow_threading_guard guard;
        entry e;
        s.save_state(e, flags);
        return e;
    }

#ifndef TORRENT_NO_DEPRECATE
    object pop_alert(lt::session& ses)
    {
        std::auto_ptr<alert> a;
        {
            allow_threading_guard guard;
            a = ses.pop_alert();
        }

        return object(boost::shared_ptr<alert>(a.release()));
    }

    list pop_alerts(lt::session& ses)
    {
        std::vector<alert*> alerts;
        {
            allow_threading_guard guard;
            ses.pop_alerts(&alerts);
        }

        list ret;
        for (std::vector<alert*>::iterator i = alerts.begin()
            , end(alerts.end()); i != end; ++i)
        {
            ret.append(boost::shared_ptr<alert>((*i)->clone().release()));
        }
        return ret;
    }
#else
    list pop_alerts(lt::session& ses)
    {
        std::vector<alert*> alerts;
        {
            allow_threading_guard guard;
            ses.pop_alerts(&alerts);
        }

        list ret;
        for (std::vector<alert*>::iterator i = alerts.begin()
            , end(alerts.end()); i != end; ++i)
        {
            ret.append(boost::python::ptr(*i));
        }
        return ret;
    }
#endif

	void load_state(lt::session& ses, entry const& st, boost::uint32_t flags)
	{
		allow_threading_guard guard;

		std::vector<char> buf;
		bencode(std::back_inserter(buf), st);
		bdecode_node e;
		error_code ec;
		bdecode(&buf[0], &buf[0] + buf.size(), e, ec);
		TORRENT_ASSERT(!ec);
		ses.load_state(e, flags);
	}

	dict get_peer_class(lt::session& ses, int const pc)
	{
		lt::peer_class_info pci;
		{
			allow_threading_guard guard;
			pci = ses.get_peer_class(pc);
		}
		dict ret;
		ret["ignore_unchoke_slots"] = pci.ignore_unchoke_slots;
		ret["connection_limit_factor"] = pci.connection_limit_factor;
		ret["label"] = pci.label;
		ret["upload_limit"] = pci.upload_limit;
		ret["download_limit"] = pci.download_limit;
		ret["upload_priority"] = pci.upload_priority;
		ret["download_priority"] = pci.download_priority;
		return ret;
	}

	void set_peer_class(lt::session& ses, int const pc, dict info)
	{
		lt::peer_class_info pci;
		stl_input_iterator<std::string> i(info.keys()), end;
		for (; i != end; ++i)
		{
			std::string const key = *i;

			object const value = info[key];
			if (key == "ignore_unchoke_slots")
			{
				pci.ignore_unchoke_slots = extract<bool>(value);
			}
			else if (key == "connection_limit_factor")
			{
				pci.connection_limit_factor = extract<int>(value);
			}
			else if (key == "label")
			{
				pci.label = extract<std::string>(value);
			}
			else if (key == "upload_limit")
			{
				pci.upload_limit = extract<int>(value);
			}
			else if (key == "download_limit")
			{
				pci.download_limit = extract<int>(value);
			}
			else if (key == "upload_priority")
			{
				pci.upload_priority = extract<int>(value);
			}
			else if (key == "download_priority")
			{
				pci.download_priority = extract<int>(value);
			}
			else
			{
				PyErr_SetString(PyExc_KeyError, ("unknown name in peer_class_info: " + key).c_str());
				throw_error_already_set();
			}
		}

		allow_threading_guard guard;
		ses.set_peer_class(pc, pci);
	}

#ifndef TORRENT_DISABLE_DHT
    void dht_get_mutable_item(lt::session& ses, std::string key, std::string salt)
    {
        TORRENT_ASSERT(key.size() == 32);
        boost::array<char, 32> public_key;
        std::copy(key.begin(), key.end(), public_key.begin());
        ses.dht_get_item(public_key, salt);
    }

    void put_string(entry& e, boost::array<char, 64>& sig, boost::uint64_t& seq,
                    std::string const& salt, std::string public_key, std::string private_key,
                    std::string data)
    {
        using libtorrent::dht::sign_mutable_item;

        e = data;
        std::vector<char> buf;
        bencode(std::back_inserter(buf), e);
        ++seq;
        sign_mutable_item(std::pair<char const*, int>(&buf[0], buf.size())
                          , std::pair<char const*, int>(&salt[0], salt.size())
                          , seq, public_key.c_str(), private_key.c_str(), sig.data());
    }

    void dht_put_mutable_item(lt::session& ses, std::string private_key, std::string public_key,
                              std::string data, std::string salt)
    {
        TORRENT_ASSERT(private_key.size() == 64);
        TORRENT_ASSERT(public_key.size() == 32);
        boost::array<char, 32> key;
        std::copy(public_key.begin(), public_key.end(), key.begin());
        ses.dht_put_item(key, boost::bind(&put_string, _1, _2, _3, _4
                                          , public_key, private_key, data)
                         , salt);
    }
#endif
} // anonymous namespace


void bind_session()
{
#ifndef TORRENT_DISABLE_DHT
    void (lt::session::*dht_get_immutable_item)(sha1_hash const&) = &lt::session::dht_get_item;
    sha1_hash (lt::session::*dht_put_immutable_item)(entry data) = &lt::session::dht_put_item;
#endif // TORRENT_DISABLE_DHT

#ifndef TORRENT_NO_DEPRECATE
#ifndef TORRENT_DISABLE_DHT
    void (lt::session::*start_dht0)() = &lt::session::start_dht;
    void (lt::session::*start_dht1)(entry const&) = &lt::session::start_dht;
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
#endif // TORRENT_DISABLE_DHT
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
#endif // TORRENT_DISABLE_DHT
#endif // TORRENT_NO_DEPRECATE

    enum_<storage_mode_t>("storage_mode_t")
        .value("storage_mode_allocate", storage_mode_allocate)
        .value("storage_mode_sparse", storage_mode_sparse)
    ;

    enum_<lt::session::options_t>("options_t")
        .value("delete_files", lt::session::delete_files)
    ;

    enum_<lt::session::session_flags_t>("session_flags_t")
        .value("add_default_plugins", lt::session::add_default_plugins)
        .value("start_default_features", lt::session::start_default_features)
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
        .value("flag_super_seeding", add_torrent_params::flag_super_seeding)
        .value("flag_sequential_download", add_torrent_params::flag_sequential_download)
        .value("flag_use_resume_save_path", add_torrent_params::flag_use_resume_save_path)
        .value("flag_merge_resume_http_seeds", add_torrent_params::flag_merge_resume_http_seeds)
        .value("flag_stop_when_ready", add_torrent_params::flag_stop_when_ready)
        .value("default_flags", add_torrent_params::default_flags)
    ;
    class_<cache_status>("cache_status")
        .add_property("pieces", cache_status_pieces)
#ifndef TORRENT_NO_DEPRECATE
        .def_readonly("blocks_written", &cache_status::blocks_written)
        .def_readonly("writes", &cache_status::writes)
        .def_readonly("blocks_read", &cache_status::blocks_read)
        .def_readonly("blocks_read_hit", &cache_status::blocks_read_hit)
        .def_readonly("reads", &cache_status::reads)
        .def_readonly("queued_bytes", &cache_status::queued_bytes)
        .def_readonly("cache_size", &cache_status::cache_size)
        .def_readonly("write_cache_size", &cache_status::write_cache_size)
        .def_readonly("read_cache_size", &cache_status::read_cache_size)
        .def_readonly("pinned_blocks", &cache_status::pinned_blocks)
        .def_readonly("total_used_buffers", &cache_status::total_used_buffers)
        .def_readonly("average_read_time", &cache_status::average_read_time)
        .def_readonly("average_write_time", &cache_status::average_write_time)
        .def_readonly("average_hash_time", &cache_status::average_hash_time)
        .def_readonly("average_job_time", &cache_status::average_job_time)
        .def_readonly("cumulative_job_time", &cache_status::cumulative_job_time)
        .def_readonly("cumulative_read_time", &cache_status::cumulative_read_time)
        .def_readonly("cumulative_write_time", &cache_status::cumulative_write_time)
        .def_readonly("cumulative_hash_time", &cache_status::cumulative_hash_time)
        .def_readonly("total_read_back", &cache_status::total_read_back)
        .def_readonly("read_queue_size", &cache_status::read_queue_size)
        .def_readonly("blocked_jobs", &cache_status::blocked_jobs)
        .def_readonly("queued_jobs", &cache_status::queued_jobs)
        .def_readonly("peak_queued", &cache_status::peak_queued)
        .def_readonly("pending_jobs", &cache_status::pending_jobs)
        .def_readonly("num_jobs", &cache_status::num_jobs)
        .def_readonly("num_read_jobs", &cache_status::num_read_jobs)
        .def_readonly("num_write_jobs", &cache_status::num_write_jobs)
        .def_readonly("arc_mru_size", &cache_status::arc_mru_size)
        .def_readonly("arc_mru_ghost_size", &cache_status::arc_mru_ghost_size)
        .def_readonly("arc_mfu_size", &cache_status::arc_mfu_size)
        .def_readonly("arc_mfu_ghost_size", &cache_status::arc_mfu_ghost_size)
#endif
    ;

    class_<lt::peer_class_type_filter>("peer_class_type_filter")
        .def(init<>())
        .def("add", &lt::peer_class_type_filter::add)
        .def("remove", &lt::peer_class_type_filter::remove)
        .def("disallow", &lt::peer_class_type_filter::disallow)
        .def("allow", &lt::peer_class_type_filter::allow)
        .def("apply", &lt::peer_class_type_filter::apply)
        ;

    enum_<lt::peer_class_type_filter::socket_type_t>("socket_type_t")
        .value("tcp_socket", peer_class_type_filter::tcp_socket)
        .value("utp_socket", peer_class_type_filter::utp_socket)
        .value("ssl_tcp_socket", peer_class_type_filter::ssl_tcp_socket)
        .value("ssl_utp_socket", peer_class_type_filter::ssl_utp_socket)
        .value("i2p_socket", peer_class_type_filter::i2p_socket)
        ;

    {
    scope ses = class_<lt::session, boost::noncopyable>("session", no_init)
        .def("__init__", boost::python::make_constructor(&make_session
                , default_call_policies()
                , (arg("settings")
                , arg("flags")=lt::session::start_default_features
                    | lt::session::add_default_plugins))
        )
#ifndef TORRENT_NO_DEPRECATE
        .def(
            init<fingerprint, int, boost::uint32_t>((
                arg("fingerprint")=fingerprint("LT",0,1,0,0)
                , arg("flags")=lt::session::start_default_features | lt::session::add_default_plugins
                , arg("alert_mask")=int(alert::error_notification)))
        )
        .def("outgoing_ports", &outgoing_ports)
#endif
        .def("post_torrent_updates", allow_threads(&lt::session::post_torrent_updates), arg("flags") = 0xffffffff)
        .def("post_dht_stats", allow_threads(&lt::session::post_dht_stats))
        .def("post_session_stats", allow_threads(&lt::session::post_session_stats))
        .def("is_listening", allow_threads(&lt::session::is_listening))
        .def("listen_port", allow_threads(&lt::session::listen_port))
#ifndef TORRENT_DISABLE_DHT
        .def("add_dht_node", &add_dht_node)
#ifndef TORRENT_NO_DEPRECATE
        .def(
            "add_dht_router", &add_dht_router
          , (arg("router"), "port")
        )
#endif // TORRENT_NO_DEPRECATE
        .def("is_dht_running", allow_threads(&lt::session::is_dht_running))
        .def("set_dht_settings", allow_threads(&lt::session::set_dht_settings))
        .def("get_dht_settings", allow_threads(&lt::session::get_dht_settings))
        .def("dht_get_immutable_item", allow_threads(dht_get_immutable_item))
        .def("dht_get_mutable_item", &dht_get_mutable_item)
        .def("dht_put_immutable_item", allow_threads(dht_put_immutable_item))
        .def("dht_put_mutable_item", &dht_put_mutable_item)
        .def("dht_get_peers", allow_threads(&lt::session::dht_get_peers))
        .def("dht_announce", allow_threads(&lt::session::dht_announce))
#endif // TORRENT_DISABLE_DHT
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
#endif // TORRENT_NO_DEPRECATE
#endif // BOOST_NO_EXCEPTIONS
        .def("remove_torrent", allow_threads(&lt::session::remove_torrent), arg("option") = 0)
#ifndef TORRENT_NO_DEPRECATE
        .def("add_feed", &add_feed)
        .def("status", allow_threads(&lt::session::status))
        .def("settings", &lt::session::settings)
        .def("set_settings", &session_set_settings)
#endif
        .def("get_settings", &session_get_settings)
        .def("apply_settings", &session_apply_settings)
#ifndef TORRENT_NO_DEPRECATE
#ifndef TORRENT_DISABLE_ENCRYPTION
        .def("set_pe_settings", allow_threads(&lt::session::set_pe_settings))
        .def("get_pe_settings", allow_threads(&lt::session::get_pe_settings))
#endif
#endif
        .def("load_state", &load_state, (arg("entry"), arg("flags") = 0xffffffff))
        .def("save_state", &save_state, (arg("entry"), arg("flags") = 0xffffffff))
        .def("pop_alerts", &pop_alerts)
        .def("wait_for_alert", &wait_for_alert
#ifdef TORRENT_NO_DEPRECATE
            , return_internal_reference<>()
#endif
        )
        .def("add_extension", &add_extension)
#ifndef TORRENT_NO_DEPRECATE
        .def("pop_alert", &pop_alert)
#if TORRENT_USE_I2P
        .def("set_i2p_proxy", allow_threads(&lt::session::set_i2p_proxy))
        .def("i2p_proxy", allow_threads(&lt::session::i2p_proxy))
#endif
#endif
        .def("set_ip_filter", allow_threads(&lt::session::set_ip_filter))
        .def("get_ip_filter", allow_threads(&lt::session::get_ip_filter))
        .def("find_torrent", allow_threads(&lt::session::find_torrent))
        .def("get_torrents", &get_torrents)
        .def("get_torrent_status", &get_torrent_status, (arg("session"), arg("pred"), arg("flags") = 0))
        .def("refresh_torrent_status", &refresh_torrent_status, (arg("session"), arg("torrents"), arg("flags") = 0))
        .def("pause", allow_threads(&lt::session::pause))
        .def("resume", allow_threads(&lt::session::resume))
        .def("is_paused", allow_threads(&lt::session::is_paused))
        .def("get_cache_info", &get_cache_info1, (arg("handle") = torrent_handle(), arg("flags") = 0))
        .def("add_port_mapping", allow_threads(&lt::session::add_port_mapping))
        .def("delete_port_mapping", allow_threads(&lt::session::delete_port_mapping))
        .def("set_peer_class_filter", &lt::session::set_peer_class_filter)
        .def("set_peer_class_type_filter", &lt::session::set_peer_class_type_filter)
        .def("create_peer_class", &lt::session::create_peer_class)
        .def("delete_peer_class", &lt::session::delete_peer_class)
        .def("get_peer_class", &get_peer_class)
        .def("set_peer_class", &set_peer_class)

#ifndef TORRENT_NO_DEPRECATE
        .def("id", allow_threads(&lt::session::id))
        .def(
            "listen_on", &listen_on
          , (arg("min"), "max", arg("interface") = (char const*)0, arg("flags") = 0)
        )
#ifndef TORRENT_DISABLE_DHT
        .def("start_dht", allow_threads(start_dht0))
        .def("stop_dht", allow_threads(&lt::session::stop_dht))
        .def("start_dht", allow_threads(start_dht1))
        .def("dht_state", allow_threads(&lt::session::dht_state))
        .def("set_dht_proxy", allow_threads(&lt::session::set_dht_proxy))
        .def("dht_proxy", allow_threads(&lt::session::dht_proxy))
#endif
        .def("set_local_download_rate_limit", allow_threads(&lt::session::set_local_download_rate_limit))
        .def("local_download_rate_limit", allow_threads(&lt::session::local_download_rate_limit))
        .def("set_local_upload_rate_limit", allow_threads(&lt::session::set_local_upload_rate_limit))
        .def("local_upload_rate_limit", allow_threads(&lt::session::local_upload_rate_limit))
        .def("set_download_rate_limit", allow_threads(&lt::session::set_download_rate_limit))
        .def("download_rate_limit", allow_threads(&lt::session::download_rate_limit))
        .def("set_upload_rate_limit", allow_threads(&lt::session::set_upload_rate_limit))
        .def("upload_rate_limit", allow_threads(&lt::session::upload_rate_limit))
        .def("set_max_uploads", allow_threads(&lt::session::set_max_uploads))
        .def("set_max_connections", allow_threads(&lt::session::set_max_connections))
        .def("max_connections", allow_threads(&lt::session::max_connections))
        .def("num_connections", allow_threads(&lt::session::num_connections))
        .def("set_max_half_open_connections", allow_threads(&lt::session::set_max_half_open_connections))
        .def("set_severity_level", allow_threads(&lt::session::set_severity_level))
        .def("set_alert_queue_size_limit", allow_threads(&lt::session::set_alert_queue_size_limit))
        .def("set_alert_mask", allow_threads(&lt::session::set_alert_mask))
        .def("set_peer_proxy", allow_threads(&lt::session::set_peer_proxy))
        .def("set_tracker_proxy", allow_threads(&lt::session::set_tracker_proxy))
        .def("set_web_seed_proxy", allow_threads(&lt::session::set_web_seed_proxy))
        .def("peer_proxy", allow_threads(&lt::session::peer_proxy))
        .def("tracker_proxy", allow_threads(&lt::session::tracker_proxy))
        .def("web_seed_proxy", allow_threads(&lt::session::web_seed_proxy))
        .def("set_proxy", allow_threads(&lt::session::set_proxy))
        .def("proxy", allow_threads(&lt::session::proxy))
        .def("start_upnp", &start_upnp)
        .def("stop_upnp", allow_threads(&lt::session::stop_upnp))
        .def("start_lsd", allow_threads(&lt::session::start_lsd))
        .def("stop_lsd", allow_threads(&lt::session::stop_lsd))
        .def("start_natpmp", &start_natpmp)
        .def("stop_natpmp", allow_threads(&lt::session::stop_natpmp))
        .def("get_cache_status", &get_cache_status)
        .def("get_cache_info", &get_cache_info2)
        .def("set_peer_id", allow_threads(&lt::session::set_peer_id))
#endif // TORRENT_NO_DEPRECATE
        ;

    ses.attr("global_peer_class_id") = int(session::global_peer_class_id);
    ses.attr("tcp_peer_class_id") = int(session::tcp_peer_class_id);
    ses.attr("local_peer_class_id") = int(session::local_peer_class_id);
    }

    enum_<lt::session::protocol_type>("protocol_type")
        .value("udp", lt::session::udp)
        .value("tcp", lt::session::tcp)
    ;

    enum_<lt::session::save_state_flags_t>("save_state_flags_t")
        .value("save_settings", lt::session::save_settings)
        .value("save_dht_settings", lt::session::save_dht_settings)
        .value("save_dht_state", lt::session::save_dht_state)
#ifndef TORRENT_NO_DEPRECATE
        .value("save_encryption_settings", lt::session:: save_encryption_settings)
        .value("save_as_map", lt::session::save_as_map)
        .value("save_i2p_proxy", lt::session::save_i2p_proxy)
        .value("save_proxy", lt::session::save_proxy)
        .value("save_dht_proxy", lt::session::save_dht_proxy)
        .value("save_peer_proxy", lt::session::save_peer_proxy)
        .value("save_web_proxy", lt::session::save_web_proxy)
        .value("save_tracker_proxy", lt::session::save_tracker_proxy)
#endif
    ;

#ifndef TORRENT_NO_DEPRECATE
    enum_<lt::session::listen_on_flags_t>("listen_on_flags_t")
        .value("listen_reuse_address", lt::session::listen_reuse_address)
        .value("listen_no_system_port", lt::session::listen_no_system_port)
    ;

    class_<feed_handle>("feed_handle")
        .def("update_feed", &feed_handle::update_feed)
        .def("get_feed_status", &get_feed_status)
        .def("set_settings", &set_feed_settings)
        .def("settings", &get_feed_settings)
    ;

    typedef session_settings (*mem_preset1)();
    typedef session_settings (*perf_preset1)();

    def("high_performance_seed", (perf_preset1)high_performance_seed);
    def("min_memory_usage", (mem_preset1)min_memory_usage);
    scope().attr("create_metadata_plugin") = "metadata_transfer";
#endif

    def("high_performance_seed", high_performance_seed_wrapper);
    def("min_memory_usage", min_memory_usage_wrapper);
    def("default_settings", default_settings_wrapper);

	class_<stats_metric>("stats_metric")
		.def_readonly("name", &stats_metric::name)
		.def_readonly("value_index", &stats_metric::value_index)
		.def_readonly("type", &stats_metric::type)
	;

	enum_<stats_metric::metric_type_t>("metric_type_t")
		.value("counter", stats_metric::type_counter)
		.value("gauge", stats_metric::type_gauge)
		;

    def("session_stats_metrics", session_stats_metrics);
    def("find_metric_idx", find_metric_idx);

    scope().attr("create_ut_metadata_plugin") = "ut_metadata";
    scope().attr("create_ut_pex_plugin") = "ut_pex";
    scope().attr("create_smart_ban_plugin") = "smart_ban";
}

