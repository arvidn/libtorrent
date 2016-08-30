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
#include <libtorrent/aux_/session_settings.hpp>
#include <libtorrent/extensions.hpp>
#include <libtorrent/bdecode.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/aux_/session_impl.hpp> // for settings_map()
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/kademlia/item.hpp> // for sign_mutable_item
#include <libtorrent/alert.hpp>
#include <libtorrent/extensions/smart_ban.hpp>
#include <libtorrent/extensions/ut_metadata.hpp>
#include <libtorrent/extensions/ut_pex.hpp>

namespace boost
{
	// this fixes mysterious link error on msvc
	libtorrent::alert const volatile*
	get_pointer(libtorrent::alert const volatile* p)
	{
		return p;
	}
}

#include "gil.hpp"
#include "bytes.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include "boost_python.hpp"

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#ifdef _MSC_VER
#pragma warning(push)
// warning C4996: X: was declared deprecated
#pragma warning( disable : 4996 )
#endif

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
        if (ec) throw system_error(ec);
#endif
    }
#endif

    void outgoing_ports(lt::session& s, int _min, int _max)
    {
        allow_threading_guard guard;
        settings_pack p;
        p.set_int(settings_pack::outgoing_port, _min);
        p.set_int(settings_pack::num_outgoing_ports, _max - _min);
        s.apply_settings(p);
        return;
    }
#ifndef TORRENT_DISABLE_DHT
    void add_dht_node(lt::session& s, tuple n)
    {
        std::string ip = extract<std::string>(n[0]);
        int port = extract<int>(n[1]);
        allow_threading_guard guard;
        s.add_dht_node(std::make_pair(ip, port));
    }

    void add_dht_router(lt::session& s, std::string router_, int port_)
    {
        allow_threading_guard guard;
        return s.add_dht_router(std::make_pair(router_, port_));
    }

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

#endif // TORRENT_DISABLE_EXTENSIONS
    }

	void make_settings_pack(lt::settings_pack& p, dict const& sett_dict)
	{
		list iterkeys = (list)sett_dict.keys();
		int const len = int(boost::python::len(iterkeys));
		for (int i = 0; i < len; i++)
		{
			std::string const key = extract<std::string>(iterkeys[i]);

			int sett = setting_by_name(key);
			if (sett < 0) continue;

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
           p.ti = std::make_shared<torrent_info>(
              extract<torrent_info const&>(params["ti"]));
        }


        if (params.has_key("info_hash"))
            p.info_hash = sha1_hash(bytes(extract<bytes>(params["info_hash"])).arr.data());
        if (params.has_key("name"))
            p.name = extract<std::string>(params["name"]);
        p.save_path = extract<std::string>(params["save_path"]);

#ifndef TORRENT_NO_DEPRECATE
        if (params.has_key("resume_data"))
        {
            std::string resume = extract<std::string>(params["resume_data"]);
            p.resume_data.assign(resume.begin(), resume.end());
        }
#endif
        if (params.has_key("storage_mode"))
            p.storage_mode = extract<storage_mode_t>(params["storage_mode"]);

        if (params.has_key("trackers"))
        {
            list l = extract<list>(params["trackers"]);
            int const n = int(boost::python::len(l));
            for(int i = 0; i < n; i++)
                p.trackers.push_back(extract<std::string>(l[i]));
        }

        if (params.has_key("dht_nodes"))
        {
            list l = extract<list>(params["dht_nodes"]);
            int const n = int(boost::python::len(l));
            for(int i = 0; i < n; i++)
                p.dht_nodes.push_back(extract<std::pair<std::string, int> >(l[i]));
        }
        if (params.has_key("flags"))
            p.flags = extract<std::uint64_t>(params["flags"]);
        if (params.has_key("trackerid"))
            p.trackerid = extract<std::string>(params["trackerid"]);
        if (params.has_key("url"))
            p.url = extract<std::string>(params["url"]);
#ifndef TORRENT_NO_DEPRECATE
        if (params.has_key("uuid"))
            p.uuid = extract<std::string>(params["uuid"]);
#endif

        if (params.has_key("file_priorities"))
        {
            list l = extract<list>(params["file_priorities"]);
            int const n = int(boost::python::len(l));
            for(int i = 0; i < n; i++)
                p.file_priorities.push_back(extract<std::uint8_t>(l[i]));
            p.file_priorities.clear();
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

    alert const*
    wait_for_alert(lt::session& s, int ms)
    {
        alert const* a;
        {
            allow_threading_guard guard;
            a = s.wait_for_alert(milliseconds(ms));
        }
        return a;
    }

    list get_torrents(lt::session& s)
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

    cache_status get_cache_info1(lt::session& s, torrent_handle h, int flags)
    {
       cache_status ret;
       s.get_cache_info(&ret, h, flags);
       return ret;
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

       list pieces;
       time_point now = clock_type::now();
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
#endif

    entry save_state(lt::session const& s, std::uint32_t flags)
    {
        allow_threading_guard guard;
        entry e;
        s.save_state(e, flags);
        return e;
    }

    list pop_alerts(lt::session& ses)
    {
        std::vector<alert*> alerts;
        {
            allow_threading_guard guard;
            ses.pop_alerts(&alerts);
        }

        list ret;
        for (alert* a : alerts)
        {
            ret.append(boost::python::ptr(a));
        }
        return ret;
    }

	void load_state(lt::session& ses, entry const& st, std::uint32_t flags)
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

#ifndef TORRENT_DISABLE_DHT
    void dht_get_mutable_item(lt::session& ses, std::string key, std::string salt)
    {
        TORRENT_ASSERT(key.size() == 32);
        std::array<char, 32> public_key;
        std::copy(key.begin(), key.end(), public_key.begin());
        ses.dht_get_item(public_key, salt);
    }

    void put_string(entry& e, std::array<char, 64>& sig, std::uint64_t& seq
        , std::string const& salt, std::string pk, std::string sk
        , std::string data)
    {
        using libtorrent::dht::sign_mutable_item;

        e = data;
        std::vector<char> buf;
        bencode(std::back_inserter(buf), e);
        ++seq;
        dht::signature sign = sign_mutable_item(buf, salt
            , dht::sequence_number(seq)
            , dht::public_key(pk.data())
            , dht::secret_key(sk.data()));
        sig = sign.bytes;
    }

    void dht_put_mutable_item(lt::session& ses, std::string private_key, std::string public_key,
        std::string data, std::string salt)
    {
        TORRENT_ASSERT(private_key.size() == 64);
        TORRENT_ASSERT(public_key.size() == 32);
        std::array<char, 32> key;
        std::copy(public_key.begin(), public_key.end(), key.begin());
        ses.dht_put_item(key, boost::bind(&put_string, _1, _2, _3, _4
            , public_key, private_key, data)
            , salt);
    }
#endif

    add_torrent_params read_resume_data_wrapper(bytes const& b)
    {
        error_code ec;
        add_torrent_params p = read_resume_data(&b.arr[0], int(b.arr.size()), ec);
#ifndef BOOST_NO_EXCEPTIONS
        if (ec) throw system_error(ec);
#endif
        return p;
    }

} // namespace unnamed


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

#define PROP(val) \
    make_getter(val, return_value_policy<return_by_value>()), \
    make_setter(val, return_value_policy<return_by_value>())

    class_<add_torrent_params>("add_torrent_params")
        .def_readwrite("version", &add_torrent_params::version)
        .def_readwrite("ti", &add_torrent_params::ti)
        .add_property("trackers", PROP(&add_torrent_params::trackers))
        .add_property("tracker_tiers", PROP(&add_torrent_params::tracker_tiers))
        .add_property("dht_nodes", PROP(&add_torrent_params::dht_nodes))
        .def_readwrite("name", &add_torrent_params::name)
        .def_readwrite("save_path", &add_torrent_params::save_path)
        .def_readwrite("storage_mode", &add_torrent_params::storage_mode)
//        .def_readwrite("storage", &add_torrent_params::storage)
        .add_property("file_priorities", PROP(&add_torrent_params::file_priorities))
        .def_readwrite("trackerid", &add_torrent_params::trackerid)
        .def_readwrite("url", &add_torrent_params::url)
        .def_readwrite("flags", &add_torrent_params::flags)
        .def_readwrite("info_hash", &add_torrent_params::info_hash)
        .def_readwrite("max_uploads", &add_torrent_params::max_uploads)
        .def_readwrite("max_connections", &add_torrent_params::max_connections)
        .def_readwrite("upload_limit", &add_torrent_params::upload_limit)
        .def_readwrite("download_limit", &add_torrent_params::download_limit)
        .def_readwrite("total_uploaded", &add_torrent_params::total_uploaded)
        .def_readwrite("total_downloaded", &add_torrent_params::total_downloaded)
        .def_readwrite("active_time", &add_torrent_params::active_time)
        .def_readwrite("finished_time", &add_torrent_params::finished_time)
        .def_readwrite("seeding_time", &add_torrent_params::seeding_time)
        .def_readwrite("added_time", &add_torrent_params::added_time)
        .def_readwrite("completed_time", &add_torrent_params::completed_time)
        .def_readwrite("last_seen_complete", &add_torrent_params::last_seen_complete)
        .def_readwrite("num_complete", &add_torrent_params::num_complete)
        .def_readwrite("num_incomplete", &add_torrent_params::num_incomplete)
        .def_readwrite("num_downloaded", &add_torrent_params::num_downloaded)
        .add_property("http_seeds", PROP(&add_torrent_params::http_seeds))
        .add_property("url_seeds", PROP(&add_torrent_params::url_seeds))
        .add_property("peers", PROP(&add_torrent_params::peers))
        .add_property("banned_peers", PROP(&add_torrent_params::banned_peers))
        .add_property("unfinished_pieces", PROP(&add_torrent_params::unfinished_pieces))
        .add_property("have_pieces", PROP(&add_torrent_params::have_pieces))
        .add_property("verified_pieces", PROP(&add_torrent_params::verified_pieces))
        .add_property("piece_priorities", PROP(&add_torrent_params::piece_priorities))
        .add_property("merkle_tree", PROP(&add_torrent_params::merkle_tree))
        .add_property("renamed_files", PROP(&add_torrent_params::renamed_files))

#ifndef TORRENT_NO_DEPRECATE
        .def_readwrite("uuid", &add_torrent_params::uuid)
        .def_readwrite("resume_data", &add_torrent_params::resume_data)
#endif
      ;


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
        .value("flag_upload_mode", add_torrent_params::flag_upload_mode)
        .value("flag_share_mode", add_torrent_params::flag_share_mode)
        .value("flag_apply_ip_filter", add_torrent_params::flag_apply_ip_filter)
        .value("flag_paused", add_torrent_params::flag_paused)
        .value("flag_auto_managed", add_torrent_params::flag_auto_managed)
        .value("flag_duplicate_is_error", add_torrent_params::flag_duplicate_is_error)
        .value("flag_update_subscribe", add_torrent_params::flag_update_subscribe)
        .value("flag_super_seeding", add_torrent_params::flag_super_seeding)
        .value("flag_sequential_download", add_torrent_params::flag_sequential_download)
        .value("flag_pinned", add_torrent_params::flag_pinned)
        .value("flag_stop_when_ready", add_torrent_params::flag_stop_when_ready)
        .value("flag_override_trackers", add_torrent_params::flag_override_trackers)
        .value("flag_override_web_seeds", add_torrent_params::flag_override_web_seeds)
#ifndef TORRENT_NO_DEPRECATE
        .value("flag_override_resume_data", add_torrent_params::flag_override_resume_data)
        .value("flag_merge_resume_trackers", add_torrent_params::flag_merge_resume_trackers)
        .value("flag_use_resume_save_path", add_torrent_params::flag_use_resume_save_path)
        .value("flag_merge_resume_http_seeds", add_torrent_params::flag_merge_resume_http_seeds)
#endif
    ;
    class_<cache_status>("cache_status")
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

    class_<lt::session, boost::noncopyable>("session", no_init)
        .def("__init__", boost::python::make_constructor(&make_session
                , default_call_policies()
                , (arg("settings")
                , arg("flags")=lt::session::start_default_features
                    | lt::session::add_default_plugins))
        )
#ifndef TORRENT_NO_DEPRECATE
        .def(
            init<fingerprint, int, std::uint32_t>((
                arg("fingerprint")=fingerprint("LT",0,1,0,0)
                , arg("flags")=lt::session::start_default_features | lt::session::add_default_plugins
                , arg("alert_mask")=int(alert::error_notification)))
        )
#endif
        .def("post_torrent_updates", allow_threads(&lt::session::post_torrent_updates), arg("flags") = 0xffffffff)
        .def("post_session_stats", allow_threads(&lt::session::post_session_stats))
        .def("outgoing_ports", &outgoing_ports)
        .def("is_listening", allow_threads(&lt::session::is_listening))
        .def("listen_port", allow_threads(&lt::session::listen_port))
#ifndef TORRENT_DISABLE_DHT
        .def("add_dht_node", &add_dht_node)
        .def(
            "add_dht_router", &add_dht_router
          , (arg("router"), "port")
        )
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
        .def("async_add_torrent", &lt::session::async_add_torrent)
        .def("add_torrent", allow_threads((lt::torrent_handle (session_handle::*)(add_torrent_params const&))&lt::session::add_torrent))
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
        .def("wait_for_alert", &wait_for_alert, return_internal_reference<>())
        .def("add_extension", &add_extension)
#ifndef TORRENT_NO_DEPRECATE
#if TORRENT_USE_I2P
        .def("set_i2p_proxy", allow_threads(&lt::session::set_i2p_proxy))
        .def("i2p_proxy", allow_threads(&lt::session::i2p_proxy))
#endif
#endif
        .def("set_ip_filter", allow_threads(&lt::session::set_ip_filter))
        .def("get_ip_filter", allow_threads(&lt::session::get_ip_filter))
        .def("find_torrent", allow_threads(&lt::session::find_torrent))
        .def("get_torrents", &get_torrents)
        .def("pause", allow_threads(&lt::session::pause))
        .def("resume", allow_threads(&lt::session::resume))
        .def("is_paused", allow_threads(&lt::session::is_paused))
        .def("id", allow_threads(&lt::session::id))
        .def("get_cache_info", &get_cache_info1, (arg("handle") = torrent_handle(), arg("flags") = 0))
        .def("add_port_mapping", allow_threads(&lt::session::add_port_mapping))
        .def("delete_port_mapping", allow_threads(&lt::session::delete_port_mapping))

#ifndef TORRENT_NO_DEPRECATE
        .def(
            "listen_on", &listen_on
          , (arg("min"), "max", arg("interface") = (char const*)nullptr, arg("flags") = 0)
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

    enum_<lt::session::protocol_type>("protocol_type")
        .value("udp", lt::session::udp)
        .value("tcp", lt::session::tcp)
    ;

    enum_<lt::session::save_state_flags_t>("save_state_flags_t")
        .value("save_settings", lt::session::save_settings)
        .value("save_dht_settings", lt::session::save_dht_settings)
        .value("save_dht_state", lt::session::save_dht_state)
        .value("save_encryption_settings", lt::session:: save_encryption_settings)
#ifndef TORRENT_NO_DEPRECATE
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
#endif

    typedef void (*mem_preset2)(settings_pack& s);
    typedef void (*perf_preset2)(settings_pack& s);

#ifndef TORRENT_NO_DEPRECATE

    typedef session_settings (*mem_preset1)();
    typedef session_settings (*perf_preset1)();

    def("high_performance_seed", (perf_preset1)high_performance_seed);
    def("min_memory_usage", (mem_preset1)min_memory_usage);
#endif

    def("high_performance_seed", (perf_preset2)high_performance_seed);
    def("min_memory_usage", (mem_preset2)min_memory_usage);
    def("read_resume_data", read_resume_data_wrapper);

    scope().attr("create_ut_metadata_plugin") = "ut_metadata";
    scope().attr("create_ut_pex_plugin") = "ut_pex";
    scope().attr("create_smart_ban_plugin") = "smart_ban";
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif

