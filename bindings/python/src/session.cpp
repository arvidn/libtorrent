// Copyright Daniel Wallin, Arvid Norberg 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "boost_python.hpp"
#include <list>
#include <string>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/error_code.hpp>
#include <libtorrent/ip_filter.hpp>
#include <libtorrent/extensions.hpp>
#include <libtorrent/bdecode.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/write_resume_data.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/kademlia/item.hpp> // for sign_mutable_item
#include <libtorrent/alert.hpp>
#include <libtorrent/time.hpp>
#include <libtorrent/session_stats.hpp>
#include <libtorrent/session_status.hpp>
#include <libtorrent/peer_class_type_filter.hpp>
#include <libtorrent/torrent_status.hpp>

#include <libtorrent/extensions/smart_ban.hpp>
#include <libtorrent/extensions/ut_metadata.hpp>
#include <libtorrent/extensions/ut_pex.hpp>

namespace boost
{
	// this fixes mysterious link error on msvc
	template <>
	inline lt::alert const volatile*
	get_pointer(lt::alert const volatile* p)
	{
		return p;
	}
}

#include "gil.hpp"
#include "bytes.hpp"

#ifdef _MSC_VER
#pragma warning(push)
// warning C4996: X: was declared deprecated
#pragma warning( disable : 4996 )
#endif

using namespace boost::python;
using namespace lt;

// defined in torrent_info.cpp
load_torrent_limits dict_to_limits(dict limits);

namespace
{
#if TORRENT_ABI_VERSION == 1
    struct dummy {};

    void listen_on(lt::session& s, int min_, int max_, char const* interface, int flags)
    {
        allow_threading_guard guard;
        error_code ec;
        s.listen_on(std::make_pair(min_, max_), ec, interface, flags);
#ifndef BOOST_NO_EXCEPTIONS
        if (ec) throw system_error(ec);
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
#endif // TORRENT_ABI_VERSION

#ifndef TORRENT_DISABLE_DHT
    void add_dht_node(lt::session& s, tuple n)
    {
        std::string ip = extract<std::string>(n[0]);
        int port = extract<int>(n[1]);
        allow_threading_guard guard;
        s.add_dht_node(std::make_pair(ip, port));
    }

#if TORRENT_ABI_VERSION == 1
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
				// if the dictionary doesn't contain "key", it will throw, hence
				// the try-catch here
				object const value = sett_dict[key];
				switch (sett & settings_pack::type_mask)
				{
					case settings_pack::string_type_base:
						p.set_str(sett, extract<std::string>(value));
						break;
					case settings_pack::int_type_base:
					{
						std::int64_t const val = extract<std::int64_t>(value);
						// deliberately truncate and sign-convert here. If we
						// extract an int directly, unsigned ints may throw
						// an exception otherwise, if it doesn't fit. Notably for a
						// flag-type with all bits set.
						p.set_int(sett, static_cast<int>(val));
						break;
					}
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
			// deprecated settings are still here, they just have empty names
			char const* name = name_for_setting(i);
			if (name[0] != '\0') ret[name] = sett.get_str(i);
		}

		for (int i = settings_pack::int_type_base;
			i < settings_pack::max_int_setting_internal; ++i)
		{
			char const* name = name_for_setting(i);
			if (name[0] != '\0') ret[name] = sett.get_int(i);
		}

		for (int i = settings_pack::bool_type_base;
			i < settings_pack::max_bool_setting_internal; ++i)
		{
			char const* name = name_for_setting(i);
			if (name[0] != '\0') ret[name] = sett.get_bool(i);
		}
		return ret;
	}

	std::shared_ptr<lt::session> make_session(boost::python::dict sett
		, session_flags_t const flags)
	{
		settings_pack p;
		make_settings_pack(p, sett);
#if TORRENT_ABI_VERSION <= 2
		if (flags & lt::session::add_default_plugins)
		{
#endif
			session_params params(std::move(p));
			return std::make_shared<lt::session>(std::move(params), flags);
#if TORRENT_ABI_VERSION <= 2
		}
		else
		{
			session_params params(std::move(p), {});
			return std::make_shared<lt::session>(std::move(params), flags);
		}
#endif
	}

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
		settings_pack ret = min_memory_usage();
		return make_dict(ret);
	}

	dict default_settings_wrapper()
	{
		return make_dict(default_settings());
	}

	dict high_performance_seed_wrapper()
	{
		settings_pack ret = high_performance_seed();
		return make_dict(ret);
	}

#ifndef BOOST_NO_EXCEPTIONS
#if TORRENT_ABI_VERSION == 1
    torrent_handle add_torrent_depr(lt::session& s, torrent_info const& ti
        , std::string const& save, entry const& resume
        , storage_mode_t storage_mode, bool paused)
    {
        allow_threading_guard guard;
        return s.add_torrent(ti, save, resume, storage_mode, paused);
    }
#endif
#endif
}

    void dict_to_add_torrent_params(dict params, add_torrent_params& p)
    {
        list items = params.items();
        int const len = int(boost::python::len(items));
        for (int i = 0; i < len; i++)
        {
            boost::python::api::object_item item = items[i];
            std::string const key = extract<std::string>(item[0]);
            object const value = item[1];
            // torrent_info objects are always held by a shared_ptr in the
            // python binding, skip it if it is a object
            if (key == "ti" && value != boost::python::object())
            {
                // make a copy here. We don't want to end up holding a python-owned
                // object inside libtorrent. If the last reference goes out of scope
                // on the C++ side, it will end up freeing the python object
                // without holding the GIL and likely crash.
                // https://mail.python.org/pipermail/cplusplus-sig/2007-June/012130.html
                p.ti = std::make_shared<torrent_info>(
                    extract<torrent_info const&>(value));
                continue;
            }
#if TORRENT_ABI_VERSION < 3
            else if (key == "info_hash")
            {
                if (boost::python::len(value) == sha1_hash::size())
                {
                    p.info_hash = sha1_hash(bytes(extract<bytes>(value)).arr.data());
                }
            }
#endif
            else if (key == "info_hashes")
            {
                if (boost::python::len(value) == sha1_hash::size())
                {
                    p.info_hashes = info_hash_t(sha1_hash(
                            bytes(extract<bytes>(value)).arr.data()));
                }
                else if (boost::python::len(value) == sha256_hash::size())
                {
                    p.info_hashes = info_hash_t(sha256_hash(
                            bytes(extract<bytes>(value)).arr.data()));
                }
                continue;
            }
            else if(key == "name")
            {
                p.name = extract<std::string>(value);
                continue;
            }
            else if(key == "save_path")
            {
                p.save_path = extract<std::string>(value);
                continue;
            }
#if TORRENT_ABI_VERSION == 1
            else if(key == "resume_data")
            {
                std::string resume = extract<std::string>(value);
                p.resume_data.assign(resume.begin(), resume.end());
                continue;
            }
#endif
            else if(key == "storage_mode")
            {
                p.storage_mode = extract<storage_mode_t>(value);
                continue;
            }
            else if(key == "trackers")
            {
                p.trackers = extract<std::vector<std::string>>(value);
                continue;
            }
            else if(key == "url_seeds")
            {
                p.url_seeds = extract<std::vector<std::string>>(value);
                continue;
            }
            else if(key == "http_seeds")
            {
                p.http_seeds =
                    extract<decltype(add_torrent_params::http_seeds)>(value);
                continue;
            }
            else if(key == "dht_nodes")
            {
                p.dht_nodes =
                    extract<std::vector<std::pair<std::string, int>>>(value);
                continue;
            }
            else if(key == "banned_peers")
            {
                p.banned_peers = extract<std::vector<lt::tcp::endpoint>>(value);
                continue;
            }
            else if(key == "peers")
            {
                p.peers = extract<std::vector<lt::tcp::endpoint>>(value);
                continue;
            }
            else if(key == "flags")
            {
                p.flags = extract<lt::torrent_flags_t>(value);
                continue;
            }
            else if(key == "trackerid")
            {
                p.trackerid = extract<std::string>(value);
                continue;
            }
#if TORRENT_ABI_VERSION == 1
            else if(key == "url")
            {
                p.url = extract<std::string>(value);
                continue;
            }
#endif
            else if(key == "renamed_files")
            {
                p.renamed_files =
                    extract<std::map<lt::file_index_t, std::string>>(value);
            }
            else if(key == "file_priorities")
            {
                p.file_priorities = extract<std::vector<download_priority_t>>(value);
            }
            else
            {
                PyErr_SetString(PyExc_KeyError,
                    ("unknown name in torrent params: " + key).c_str());
                throw_error_already_set();
            }
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

#if TORRENT_ABI_VERSION == 1
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
#endif // TORRENT_ABI_VERSION

    void alert_notify(object cb) try
    {
        lock_gil lock;
        if (cb)
        {
            cb();
        }
    }
    catch (boost::python::error_already_set const&)
    {
        // this callback isn't supposed to throw an error.
        // just swallow and ignore the exception
        TORRENT_ASSERT_FAIL_VAL("python notify callback threw exception");
    }

    void set_alert_notify(lt::session& s, object cb)
    {
        s.set_alert_notify(std::bind(&alert_notify, cb));
    }

#ifdef TORRENT_WINDOWS
    void alert_socket_notify(SOCKET const fd)
    {
        std::uint8_t dummy = 0;
        ::send(fd, reinterpret_cast<char const*>(&dummy), 1, 0);
    }
#endif

    void alert_fd_notify(int const fd)
    {
        std::uint8_t dummy = 0;
        while (::write(fd, &dummy, 1) < 0 && errno == EINTR);
    }

    void set_alert_fd(lt::session& s, std::intptr_t const fd)
    {
#ifdef TORRENT_WINDOWS
        auto const sock = static_cast<SOCKET>(fd);
        int res;
        int res_size = sizeof(res);
        if (sock != INVALID_SOCKET
            && ::getsockopt(sock, SOL_SOCKET, SO_ERROR,
                (char *)&res, &res_size) == 0)
        {
            s.set_alert_notify(std::bind(&alert_socket_notify, sock));
        }
        else
#endif
        {
            s.set_alert_notify(std::bind(&alert_fd_notify, fd));
        }
    }

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
        std::vector<torrent_status> torrents
            = s.get_torrent_status(std::bind(&wrap_pred, pred, std::placeholders::_1), status_flags_t(flags));

        list ret;
        for (std::vector<torrent_status>::iterator i = torrents.begin(); i != torrents.end(); ++i)
            ret.append(*i);
        return ret;
    }

    list refresh_torrent_status(lt::session& s, list in_torrents, int const flags)
    {
        std::vector<torrent_status> torrents;
        int const n = int(boost::python::len(in_torrents));
        for (int i = 0; i < n; ++i)
           torrents.push_back(extract<torrent_status>(in_torrents[i]));

        {
           allow_threading_guard guard;
           s.refresh_torrent_status(&torrents, status_flags_t(flags));
        }

        list ret;
        for (std::vector<torrent_status>::iterator i = torrents.begin(); i != torrents.end(); ++i)
            ret.append(*i);
        return ret;
    }

#if TORRENT_ABI_VERSION == 1
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
#endif

    entry save_state(lt::session const& s, std::uint32_t const flags)
    {
        entry e;
#if TORRENT_ABI_VERSION <= 2
        allow_threading_guard guard;
        s.save_state(e, save_state_flags_t(flags));
#endif
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

	void load_state(lt::session& ses, entry const& st, std::uint32_t const flags)
	{
#if TORRENT_ABI_VERSION <= 2
		allow_threading_guard guard;

		std::vector<char> buf;
		bencode(std::back_inserter(buf), st);
		bdecode_node e;
		error_code ec;
		bdecode(&buf[0], &buf[0] + buf.size(), e, ec);
		TORRENT_ASSERT(!ec);
		ses.load_state(e, save_state_flags_t(flags));
#endif
	}

	dict get_peer_class(lt::session& ses, lt::peer_class_t const pc)
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

	void set_peer_class(lt::session& ses, peer_class_t const pc, dict info)
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
        std::array<char, 32> public_key;
        std::copy(key.begin(), key.end(), public_key.begin());
        ses.dht_get_item(public_key, salt);
    }

    void put_string(entry& e, std::array<char, 64>& sig, std::int64_t& seq
        , std::string const& salt, std::string pk, std::string sk
        , std::string data)
    {
        using lt::dht::sign_mutable_item;

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
        ses.dht_put_item(key
            , [pk=std::move(public_key), sk=std::move(private_key), d=std::move(data)]
            (entry& e, std::array<char, 64>& sig, std::int64_t& seq, std::string const& salt)
            {
                put_string(e, sig, seq, salt, pk, sk, d);
            }
            , salt);
    }
#endif

    add_torrent_params read_resume_data_wrapper0(bytes const& b)
    {
        return read_resume_data(b.arr);
    }

    add_torrent_params read_resume_data_wrapper1(bytes const& b, dict cfg)
    {
        return read_resume_data(b.arr, dict_to_limits(cfg));
    }

	 int find_metric_idx_wrap(char const* name)
	 {
		 return lt::find_metric_idx(name);
	 }

	bytes write_resume_data_buf_(add_torrent_params const& atp)
	{
		bytes ret;
		auto buf = write_resume_data_buf(atp);
		ret.arr.resize(buf.size());
		std::copy(buf.begin(), buf.end(), ret.arr.begin());
		return ret;
	}

} // anonymous namespace

struct dummy1 {};
#if TORRENT_ABI_VERSION == 1
struct dummy2 {};
#endif
struct dummy9 {};
struct dummy10 {};
struct dummy11 {};

void bind_session()
{
#ifndef TORRENT_DISABLE_DHT
    void (lt::session::*dht_get_immutable_item)(sha1_hash const&) = &lt::session::dht_get_item;
    sha1_hash (lt::session::*dht_put_immutable_item)(entry data) = &lt::session::dht_put_item;
#endif // TORRENT_DISABLE_DHT

#if TORRENT_ABI_VERSION == 1
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
#endif // TORRENT_ABI_VERSION

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
        .add_property("flags", PROP(&add_torrent_params::flags))
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
        .def_readwrite("last_download", &add_torrent_params::last_download)
        .def_readwrite("last_upload", &add_torrent_params::last_upload)
        .def_readwrite("num_complete", &add_torrent_params::num_complete)
        .def_readwrite("num_incomplete", &add_torrent_params::num_incomplete)
        .def_readwrite("num_downloaded", &add_torrent_params::num_downloaded)
#if TORRENT_ABI_VERSION < 3
        .def_readwrite("info_hash", &add_torrent_params::info_hash)
#endif
        .def_readwrite("info_hashes", &add_torrent_params::info_hashes)
        .add_property("http_seeds", PROP(&add_torrent_params::http_seeds))
        .add_property("url_seeds", PROP(&add_torrent_params::url_seeds))
        .add_property("peers", PROP(&add_torrent_params::peers))
        .add_property("banned_peers", PROP(&add_torrent_params::banned_peers))
        .add_property("unfinished_pieces", PROP(&add_torrent_params::unfinished_pieces))
        .add_property("have_pieces", PROP(&add_torrent_params::have_pieces))
        .add_property("verified_pieces", PROP(&add_torrent_params::verified_pieces))
        .add_property("piece_priorities", PROP(&add_torrent_params::piece_priorities))
#if TORRENT_ABI_VERSION <= 2
        .add_property("merkle_tree", PROP(&add_torrent_params::merkle_tree))
#endif
        .add_property("renamed_files", PROP(&add_torrent_params::renamed_files))

#if TORRENT_ABI_VERSION == 1
        .def_readwrite("url", &add_torrent_params::url)
        .add_property("resume_data", PROP(&add_torrent_params::resume_data))
#endif
      ;

    enum_<storage_mode_t>("storage_mode_t")
        .value("storage_mode_allocate", storage_mode_allocate)
        .value("storage_mode_sparse", storage_mode_sparse)
    ;

    {
        scope s = class_<dummy11>("options_t");
        s.attr("delete_files") = lt::session::delete_files;
    }

    {
        scope s = class_<dummy10>("session_flags_t");
        s.attr("paused") = lt::session::paused;
#if TORRENT_ABI_VERSION <= 2
        s.attr("add_default_plugins") = lt::session::add_default_plugins;
#endif
#if TORRENT_ABI_VERSION == 1
        s.attr("start_default_features") = lt::session::start_default_features;
#endif
    }

    {
    scope s = class_<dummy1>("torrent_flags");
    s.attr("seed_mode") = torrent_flags::seed_mode;
    s.attr("upload_mode") = torrent_flags::upload_mode;
    s.attr("share_mode") = torrent_flags::share_mode;
    s.attr("apply_ip_filter") = torrent_flags::apply_ip_filter;
    s.attr("paused") = torrent_flags::paused;
    s.attr("auto_managed") = torrent_flags::auto_managed;
    s.attr("duplicate_is_error") = torrent_flags::duplicate_is_error;
    s.attr("update_subscribe") = torrent_flags::update_subscribe;
    s.attr("super_seeding") = torrent_flags::super_seeding;
    s.attr("sequential_download") = torrent_flags::sequential_download;
    s.attr("stop_when_ready") = torrent_flags::stop_when_ready;
    s.attr("override_trackers") = torrent_flags::override_trackers;
    s.attr("override_web_seeds") = torrent_flags::override_web_seeds;
    s.attr("disable_dht") = torrent_flags::disable_dht;
    s.attr("disable_lsd") = torrent_flags::disable_lsd;
    s.attr("disable_pex") = torrent_flags::disable_pex;
    s.attr("no_verify_files") = torrent_flags::no_verify_files;
    s.attr("default_flags") = torrent_flags::default_flags;
    }

#if TORRENT_ABI_VERSION == 1
    {
    scope s = class_<dummy2>("add_torrent_params_flags_t");
    s.attr("flag_seed_mode") = add_torrent_params::flag_seed_mode;
    s.attr("flag_upload_mode") = add_torrent_params::flag_upload_mode;
    s.attr("flag_share_mode") = add_torrent_params::flag_share_mode;
    s.attr("flag_apply_ip_filter") = add_torrent_params::flag_apply_ip_filter;
    s.attr("flag_paused") = add_torrent_params::flag_paused;
    s.attr("flag_auto_managed") = add_torrent_params::flag_auto_managed;
    s.attr("flag_duplicate_is_error") = add_torrent_params::flag_duplicate_is_error;
    s.attr("flag_update_subscribe") = add_torrent_params::flag_update_subscribe;
    s.attr("flag_super_seeding") = add_torrent_params::flag_super_seeding;
    s.attr("flag_sequential_download") = add_torrent_params::flag_sequential_download;
    s.attr("flag_stop_when_ready") = add_torrent_params::flag_stop_when_ready;
    s.attr("flag_override_trackers") = add_torrent_params::flag_override_trackers;
    s.attr("flag_override_web_seeds") = add_torrent_params::flag_override_web_seeds;
    s.attr("flag_pinned") = add_torrent_params::flag_pinned;
    s.attr("flag_override_resume_data") = add_torrent_params::flag_override_resume_data;
    s.attr("flag_merge_resume_trackers") = add_torrent_params::flag_merge_resume_trackers;
    s.attr("flag_use_resume_save_path") = add_torrent_params::flag_use_resume_save_path;
    s.attr("flag_merge_resume_http_seeds") = add_torrent_params::flag_merge_resume_http_seeds;
    s.attr("default_flags") = add_torrent_params::flag_default_flags;
    }
#endif
    ;

    enum_<lt::portmap_protocol>("portmap_protocol")
        .value("none", lt::portmap_protocol::none)
        .value("udp", lt::portmap_protocol::udp)
        .value("tcp", lt::portmap_protocol::tcp)
    ;

    enum_<lt::portmap_transport>("portmap_transport")
      .value("natpmp", lt::portmap_transport::natpmp)
      .value("upnp", lt::portmap_transport::upnp)
      ;

    enum_<lt::peer_class_type_filter::socket_type_t>("peer_class_type_filter_socket_type_t")
        .value("tcp_socket", peer_class_type_filter::tcp_socket)
        .value("utp_socket", peer_class_type_filter::utp_socket)
        .value("ssl_tcp_socket", peer_class_type_filter::ssl_tcp_socket)
        .value("ssl_utp_socket", peer_class_type_filter::ssl_utp_socket)
        .value("i2p_socket", peer_class_type_filter::i2p_socket)
        ;

    {
    scope s = class_<lt::peer_class_type_filter>("peer_class_type_filter")
        .def(init<>())
        .def("add", &lt::peer_class_type_filter::add)
        .def("remove", &lt::peer_class_type_filter::remove)
        .def("disallow", &lt::peer_class_type_filter::disallow)
        .def("allow", &lt::peer_class_type_filter::allow)
        .def("apply", &lt::peer_class_type_filter::apply)
        ;
    s.attr("tcp_socket") = peer_class_type_filter::tcp_socket;
    s.attr("utp_socket") = peer_class_type_filter::utp_socket;
    s.attr("ssl_tcp_socket") = peer_class_type_filter::ssl_tcp_socket;
    s.attr("ssl_utp_socket") = peer_class_type_filter::ssl_utp_socket;
    s.attr("i2p_socket") = peer_class_type_filter::i2p_socket;
    }

    {
    scope s = class_<lt::session, boost::noncopyable>("session", no_init)
        .def("__init__", boost::python::make_constructor(&make_session
                , default_call_policies()
                , (arg("settings"), arg("flags")=
#if TORRENT_ABI_VERSION <= 2
                    lt::session::add_default_plugins
#else
                    lt::session_flags_t{}
#endif
                    ))
              )
#if TORRENT_ABI_VERSION == 1
        .def(
            init<fingerprint, session_flags_t, alert_category_t>((
                arg("fingerprint")=fingerprint("LT",0,1,0,0)
                , arg("flags")=lt::session::start_default_features | lt::session::add_default_plugins
                , arg("alert_mask")=alert::error_notification))
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
#if TORRENT_ABI_VERSION == 1
        .def(
            "add_dht_router", &add_dht_router
          , (arg("router"), "port")
        )
#endif // TORRENT_ABI_VERSION
        .def("is_dht_running", allow_threads(&lt::session::is_dht_running))
#if TORRENT_ABI_VERSION <= 2
        .def("set_dht_settings", allow_threads(&lt::session::set_dht_settings))
        .def("get_dht_settings", allow_threads(&lt::session::get_dht_settings))
#endif
        .def("dht_get_immutable_item", allow_threads(dht_get_immutable_item))
        .def("dht_get_mutable_item", &dht_get_mutable_item)
        .def("dht_put_immutable_item", allow_threads(dht_put_immutable_item))
        .def("dht_put_mutable_item", &dht_put_mutable_item)
        .def("dht_get_peers", allow_threads(&lt::session::dht_get_peers))
        .def("dht_announce", allow_threads(&lt::session::dht_announce))
        .def("dht_live_nodes", allow_threads(&lt::session::dht_live_nodes))
        .def("dht_sample_infohashes", allow_threads(&lt::session::dht_sample_infohashes))
#endif // TORRENT_DISABLE_DHT
        .def("add_torrent", &add_torrent)
        .def("async_add_torrent", &async_add_torrent)
        .def("async_add_torrent", static_cast<void (session_handle::*)(lt::add_torrent_params const&)>(&lt::session::async_add_torrent))
        .def("add_torrent", allow_threads(static_cast<lt::torrent_handle (session_handle::*)(add_torrent_params const&)>(&lt::session::add_torrent)))
#ifndef BOOST_NO_EXCEPTIONS
#if TORRENT_ABI_VERSION == 1
        .def(
            "add_torrent", &add_torrent_depr
          , (
                arg("resume_data") = entry(),
                arg("storage_mode") = storage_mode_sparse,
                arg("paused") = false
            )
        )
#endif // TORRENT_ABI_VERSION
#endif // BOOST_NO_EXCEPTIONS
        .def("remove_torrent", allow_threads(&lt::session::remove_torrent), arg("option") = 0)
#if TORRENT_ABI_VERSION == 1
        .def("status", allow_threads(&lt::session::status))
#endif
        .def("get_settings", &session_get_settings)
        .def("apply_settings", &session_apply_settings)
#if TORRENT_ABI_VERSION == 1
#ifndef TORRENT_DISABLE_ENCRYPTION
        .def("set_pe_settings", allow_threads(&lt::session::set_pe_settings))
        .def("get_pe_settings", allow_threads(&lt::session::get_pe_settings))
#endif
#endif
        .def("load_state", &load_state, (arg("entry"), arg("flags") = 0xffffffff))
        .def("save_state", &save_state, (arg("entry"), arg("flags") = 0xffffffff))
        .def("pop_alerts", &pop_alerts)
        .def("wait_for_alert", &wait_for_alert, return_internal_reference<>())
        .def("set_alert_notify", &set_alert_notify)
        .def("set_alert_fd", &set_alert_fd)
        .def("add_extension", &add_extension)
#if TORRENT_ABI_VERSION == 1
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
        .def("add_port_mapping", allow_threads(&lt::session::add_port_mapping))
        .def("delete_port_mapping", allow_threads(&lt::session::delete_port_mapping))
        .def("reopen_network_sockets", allow_threads(&lt::session::reopen_network_sockets))
        .def("set_peer_class_filter", &lt::session::set_peer_class_filter)
        .def("set_peer_class_type_filter", &lt::session::set_peer_class_type_filter)
        .def("create_peer_class", &lt::session::create_peer_class)
        .def("delete_peer_class", &lt::session::delete_peer_class)
        .def("get_peer_class", &get_peer_class)
        .def("set_peer_class", &set_peer_class)

#if TORRENT_ABI_VERSION == 1
        .def("id", allow_threads(&lt::session::id))
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
        .def("set_peer_id", allow_threads(&lt::session::set_peer_id))
#endif // TORRENT_ABI_VERSION
        ;

    s.attr("tcp") = lt::portmap_protocol::tcp;
    s.attr("udp") = lt::portmap_protocol::udp;

    s.attr("global_peer_class_id") = session::global_peer_class_id;
    s.attr("tcp_peer_class_id") = session::tcp_peer_class_id;
    s.attr("local_peer_class_id") = session::local_peer_class_id;

    s.attr("reopen_map_ports") = lt::session::reopen_map_ports;

    s.attr("delete_files") = lt::session::delete_files;
    s.attr("delete_partfile") = lt::session::delete_partfile;
    }

#if TORRENT_ABI_VERSION == 1
    {
    scope s = class_<dummy>("protocol_type");
    s.attr("udp") = lt::portmap_protocol::udp;
    s.attr("tcp") =  lt::portmap_protocol::tcp;
    }
#endif

    {
        scope s = class_<dummy9>("save_state_flags_t");
        s.attr("save_settings") = lt::session::save_settings;
#if TORRENT_ABI_VERSION <= 2
        s.attr("save_dht_settings") = lt::session::save_dht_settings;
#endif
        s.attr("save_dht_state") = lt::session::save_dht_state;
#if TORRENT_ABI_VERSION == 1
        s.attr("save_encryption_settings") = lt::session:: save_encryption_settings;
        s.attr("save_as_map") = lt::session::save_as_map;
        s.attr("save_i2p_proxy") = lt::session::save_i2p_proxy;
        s.attr("save_proxy") = lt::session::save_proxy;
        s.attr("save_dht_proxy") = lt::session::save_dht_proxy;
        s.attr("save_peer_proxy") = lt::session::save_peer_proxy;
        s.attr("save_web_proxy") = lt::session::save_web_proxy;
        s.attr("save_tracker_proxy") = lt::session::save_tracker_proxy;
#endif
    }

#if TORRENT_ABI_VERSION == 1
    enum_<lt::session::listen_on_flags_t>("listen_on_flags_t")
        .value("listen_reuse_address", lt::session::listen_reuse_address)
        .value("listen_no_system_port", lt::session::listen_no_system_port)
    ;
#endif

    def("high_performance_seed", high_performance_seed_wrapper);
    def("min_memory_usage", min_memory_usage_wrapper);
    def("default_settings", default_settings_wrapper);
    def("read_resume_data", read_resume_data_wrapper0);
    def("read_resume_data", read_resume_data_wrapper1);
    def("write_resume_data", write_resume_data);
    def("write_resume_data_buf", write_resume_data_buf_);

	class_<stats_metric>("stats_metric")
		.def_readonly("name", &stats_metric::name)
		.def_readonly("value_index", &stats_metric::value_index)
		.def_readonly("type", &stats_metric::type)
	;

	enum_<metric_type_t>("metric_type_t")
		.value("counter", metric_type_t::counter)
		.value("gauge", metric_type_t::gauge)
		;

    def("session_stats_metrics", session_stats_metrics);
    def("find_metric_idx", find_metric_idx_wrap);

    scope().attr("create_ut_metadata_plugin") = "ut_metadata";
    scope().attr("create_ut_pex_plugin") = "ut_pex";
    scope().attr("create_smart_ban_plugin") = "smart_ban";
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif
