// Copyright Daniel Wallin 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4

#include "boost_python.hpp"
#include <memory>

#include "libtorrent/torrent_info.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/socket_io.hpp"
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/info_hash.hpp" // for protocol_version
#include "libtorrent/tracker_manager.hpp" // for event_t
#include "bytes.hpp"
#include "gil.hpp"

#ifdef _MSC_VER
#pragma warning(push)
// warning C4996: X: was declared deprecated
#pragma warning( disable : 4996 )
#endif

using namespace boost::python;
using namespace lt;

namespace
{

    std::vector<announce_entry>::const_iterator begin_trackers(torrent_info& i)
    {
        return i.trackers().begin();
    }

    std::vector<announce_entry>::const_iterator end_trackers(torrent_info& i)
    {
        return i.trackers().end();
    }

    void add_node(torrent_info& ti, char const* hostname, int port)
    {
        ti.add_node(std::make_pair(hostname, port));
    }

    list nodes(torrent_info const& ti)
    {
        list result;

        for (auto const& i : ti.nodes())
            result.append(boost::python::make_tuple(i.first, i.second));

        return result;
    }

    list get_web_seeds(torrent_info const& ti)
    {
        std::vector<web_seed_entry> const& ws = ti.web_seeds();
        list ret;
        for (std::vector<web_seed_entry>::const_iterator i = ws.begin()
            , end(ws.end()); i != end; ++i)
        {
            dict d;
            d["url"] = i->url;
            d["type"] = i->type;
            d["auth"] = i->auth;
            ret.append(d);
        }

        return ret;
    }

    void set_web_seeds(torrent_info& ti, list ws)
    {
        std::vector<web_seed_entry> web_seeds;
        int const len = static_cast<int>(boost::python::len(ws));
        for (int i = 0; i < len; i++)
        {
           dict e = extract<dict>(ws[i]);
           int const type = extract<int>(e["type"]);
           web_seeds.push_back(web_seed_entry(
              extract<std::string>(e["url"])
              , static_cast<web_seed_entry::type_t>(type)
              , extract<std::string>(e["auth"])));
        }
        ti.set_web_seeds(web_seeds);
    }

#if TORRENT_ABI_VERSION <= 2
    list get_merkle_tree(torrent_info const& ti)
    {
        std::vector<sha1_hash> const& mt = ti.merkle_tree();
        list ret;
        for (std::vector<sha1_hash>::const_iterator i = mt.begin()
            , end(mt.end()); i != end; ++i)
        {
            ret.append(bytes(i->to_string()));
        }
        return ret;
    }

    void set_merkle_tree(torrent_info& ti, list hashes)
    {
        std::vector<sha1_hash> h;
        for (int i = 0, e = int(len(hashes)); i < e; ++i)
            h.push_back(sha1_hash(bytes(extract<bytes>(hashes[i])).arr.data()));

        ti.set_merkle_tree(h);
    }
#endif

    bytes hash_for_piece(torrent_info const& ti, piece_index_t i)
    {
        return bytes(ti.hash_for_piece(i).to_string());
    }

#if TORRENT_ABI_VERSION <= 2
    bytes metadata(torrent_info const& ti)
    {
        auto const s = ti.info_section();
        return bytes(s.data(), s.size());
    }
#endif

    bytes get_info_section(torrent_info const& ti)
    {
        auto const s = ti.info_section();
        return bytes(s.data(), s.size());
    }

    list map_block(torrent_info& ti, piece_index_t piece, std::int64_t offset, int size)
    {
       std::vector<file_slice> p = ti.map_block(piece, offset, size);
       list result;

       for (std::vector<file_slice>::iterator i(p.begin()), e(p.end()); i != e; ++i)
          result.append(*i);

       return result;
    }

#if TORRENT_ABI_VERSION == 1
    // Create getters for announce_entry data members with non-trivial types which need converting.
    lt::time_point get_next_announce(announce_entry const& ae)
    {
        python_deprecated("next_announce is deprecated");
        return ae.endpoints.empty() ? lt::time_point() : lt::time_point(ae.endpoints.front().info_hashes[protocol_version::V1].next_announce);
    }
    lt::time_point get_min_announce(announce_entry const& ae)
    {
        python_deprecated("min_announce is deprecated");
        return ae.endpoints.empty() ? lt::time_point() : lt::time_point(ae.endpoints.front().info_hashes[protocol_version::V1].min_announce);
    }
    // announce_entry data member bit-fields.
    int get_fails(announce_entry const& ae)
    {
        python_deprecated("fails is deprecated");
        return ae.endpoints.empty() ? 0 : ae.endpoints.front().info_hashes[protocol_version::V1].fails;
    }
    bool get_updating(announce_entry const& ae)
    {
        python_deprecated("updating is deprecated");
        return ae.endpoints.empty() ? false : ae.endpoints.front().info_hashes[protocol_version::V1].updating;
    }
    bool get_start_sent(announce_entry const& ae)
    {
        python_deprecated("start_sent is deprecated");
        return ae.endpoints.empty() ? false : ae.endpoints.front().info_hashes[protocol_version::V1].start_sent;
    }
    bool get_complete_sent(announce_entry const& ae)
    {
        python_deprecated("complete_sent is deprecated");
        return ae.endpoints.empty() ? false : ae.endpoints.front().info_hashes[protocol_version::V1].complete_sent;
    }
    // announce_entry method requires lt::time_point.
    bool can_announce(announce_entry const& ae, bool is_seed) {
        python_deprecated("can_announce() is deprecated");
        // an entry without endpoints implies it has never been announced so it can be now
        if (ae.endpoints.empty()) return true;
        lt::time_point now = lt::clock_type::now();
        return ae.endpoints.front().can_announce(now, is_seed, ae.fail_limit);
    }
    bool is_working(announce_entry const& ae)
    {
        python_deprecated("is_working is deprecated");
        return ae.endpoints.empty() ? false : ae.endpoints.front().is_working();
    }
#endif
    int get_source(announce_entry const& ae) { return ae.source; }
    bool get_verified(announce_entry const& ae) { return ae.verified; }

#if TORRENT_ABI_VERSION == 1
    std::string get_message(announce_entry const& ae)
    {
        python_deprecated("message is deprecated");
        return ae.endpoints.empty() ? "" : ae.endpoints.front().info_hashes[protocol_version::V1].message;
    }
    error_code get_last_error(announce_entry const& ae)
    {
        python_deprecated("last_error is deprecated");
        return ae.endpoints.empty() ? error_code() : ae.endpoints.front().info_hashes[protocol_version::V1].last_error;
    }
    int get_scrape_incomplete(announce_entry const& ae)
    {
        python_deprecated("scrape_incomplete is deprecated");
        return ae.endpoints.empty() ? 0 : ae.endpoints.front().info_hashes[protocol_version::V1].scrape_incomplete;
    }
    int get_scrape_complete(announce_entry const& ae)
    {
        python_deprecated("scrape_complete is deprecated");
        return ae.endpoints.empty() ? 0 : ae.endpoints.front().info_hashes[protocol_version::V1].scrape_complete;
    }
    int get_scrape_downloaded(announce_entry const& ae)
    {
        python_deprecated("scrape_downloaded is deprecated");
        return ae.endpoints.empty() ? 0 : ae.endpoints.front().info_hashes[protocol_version::V1].scrape_downloaded;
    }
    int next_announce_in(announce_entry const&)
    {
        python_deprecated("next_announce_in is deprecated");
        return 0;
    }
    int min_announce_in(announce_entry const&)
    {
        python_deprecated("min_announce_in is deprecated");
        return 0;
    }
    bool get_send_stats(announce_entry const& ae)
    {
        python_deprecated("send_stats is deprecated");
        return ae.send_stats;
    }
    std::int64_t get_size(file_entry const& fe)
    {
        python_deprecated("file_entry is deprecated");
        return fe.size;
    }
    std::int64_t get_offset(file_entry const& fe)
    {
        python_deprecated("file_entry is deprecated");
        return fe.offset;
    }
    bool get_pad_file(file_entry const& fe)
    {
        python_deprecated("file_entry is deprecated");
        return fe.pad_file;
    }
    bool get_executable_attribute(file_entry const& fe)
    {
        python_deprecated("file_entry is deprecated");
        return fe.executable_attribute;
    }
    bool get_hidden_attribute(file_entry const& fe)
    {
        python_deprecated("file_entry is deprecated");
        return fe.hidden_attribute;
    }
    bool get_symlink_attribute(file_entry const& fe)
    {
        python_deprecated("file_entry is deprecated");
        return fe.symlink_attribute;
    }
#endif

} // namespace unnamed

load_torrent_limits dict_to_limits(dict limits)
{
    load_torrent_limits ret;

    list items = limits.items();
    int const len = int(boost::python::len(limits));
    for (int i = 0; i < len; i++)
    {
        boost::python::api::object_item item = items[i];
        std::string const key = extract<std::string>(item[0]);
        object const value = item[1];

        if (key == "max_buffer_size")
        {
            ret.max_buffer_size = extract<int>(value);
            continue;
        }
        else if (key == "max_pieces")
        {
            ret.max_pieces = extract<int>(value);
            continue;
        }
        else if (key == "max_decode_depth")
        {
            ret.max_decode_depth = extract<int>(value);
            continue;
        }
        else if (key == "max_decode_tokens")
        {
            ret.max_decode_tokens = extract<int>(value);
            continue;
        }
    }
    return ret;
}

std::shared_ptr<torrent_info> buffer_constructor0(bytes b)
{
   return std::make_shared<torrent_info>(b.arr, from_span);
}

std::shared_ptr<torrent_info> buffer_constructor1(bytes b, dict limits)
{
   std::shared_ptr<torrent_info> ret = std::make_shared<torrent_info>(b.arr
        , dict_to_limits(limits), from_span);
   return ret;
}

std::shared_ptr<torrent_info> file_constructor0(lt::string_view filename)
{
   return std::make_shared<torrent_info>(std::string(filename));
}

std::shared_ptr<torrent_info> file_constructor1(lt::string_view filename, dict limits)
{
   return std::make_shared<torrent_info>(std::string(filename), dict_to_limits(limits));
}

std::shared_ptr<torrent_info> sha1_constructor0(sha1_hash const& ih)
{
    return std::make_shared<torrent_info>(info_hash_t(ih));
}

std::shared_ptr<torrent_info> sha256_constructor0(sha256_hash const& ih)
{
    return std::make_shared<torrent_info>(info_hash_t(ih));
}

std::shared_ptr<torrent_info> bencoded_constructor0(dict d)
{
    entry ent = extract<entry>(d);
    std::vector<char> buf;
    bencode(std::back_inserter(buf), ent);
    return std::make_shared<torrent_info>(buf, lt::from_span);
}

std::shared_ptr<torrent_info> bencoded_constructor1(dict d, dict limits)
{
    entry ent = extract<entry>(d);
    std::vector<char> buf;
    bencode(std::back_inserter(buf), ent);
    return std::make_shared<torrent_info>(buf, dict_to_limits(limits)
        , lt::from_span);
}

using by_value = return_value_policy<return_by_value>;
void bind_torrent_info()
{
    return_value_policy<copy_const_reference> copy;

    void (torrent_info::*rename_file0)(file_index_t, std::string const&) = &torrent_info::rename_file;

    class_<file_slice>("file_slice")
        .add_property("file_index", make_getter((&file_slice::file_index), by_value()))
        .def_readwrite("offset", &file_slice::offset)
        .def_readwrite("size", &file_slice::size)
        ;

    enum_<lt::protocol_version>("protocol_version")
        .value("V1", protocol_version::V1)
        .value("V2", protocol_version::V2)
        ;

    enum_<announce_entry::tracker_source>("tracker_source")
        .value("source_torrent", announce_entry::source_torrent)
        .value("source_client", announce_entry::source_client)
        .value("source_magnet_link", announce_entry::source_magnet_link)
        .value("source_tex", announce_entry::source_tex)
    ;

    using add_tracker1 = void (torrent_info::*)(std::string const&, int, announce_entry::tracker_source);

    class_<torrent_info, std::shared_ptr<torrent_info>>("torrent_info", no_init)
        .def(init<info_hash_t const&>(arg("info_hash")))
        .def("__init__", make_constructor(&bencoded_constructor0))
        .def("__init__", make_constructor(&bencoded_constructor1))
        .def("__init__", make_constructor(&buffer_constructor0))
        .def("__init__", make_constructor(&buffer_constructor1))
        .def("__init__", make_constructor(&file_constructor0))
        .def("__init__", make_constructor(&file_constructor1))
        .def(init<torrent_info const&>((arg("ti"))))

        .def("__init__", make_constructor(&sha1_constructor0))
        .def("__init__", make_constructor(&sha256_constructor0))

        .def("add_tracker", (add_tracker1)&torrent_info::add_tracker
            , (arg("url"), arg("tier") = 0
            , arg("source") = announce_entry::source_client))
        .def("add_url_seed", &torrent_info::add_url_seed, (arg("url")
            , arg("extern_auth") = std::string{}
            , arg("extra_headers") = web_seed_entry::headers_t{}))
        .def("add_http_seed", &torrent_info::add_http_seed, (arg("url")
            , arg("extern_auth") = std::string{}
            , arg("extra_headers") = web_seed_entry::headers_t{}))
        .def("web_seeds", get_web_seeds)
        .def("set_web_seeds", set_web_seeds)

        .def("name", &torrent_info::name, copy)
        .def("comment", &torrent_info::comment, copy)
        .def("creator", &torrent_info::creator, copy)
        .def("total_size", &torrent_info::total_size)
        .def("piece_length", &torrent_info::piece_length)
        .def("num_pieces", &torrent_info::num_pieces)
        .def("info_hash", &torrent_info::info_hash)
        .def("info_hashes", &torrent_info::info_hashes, copy)
        .def("hash_for_piece", &hash_for_piece)
#if TORRENT_ABI_VERSION <= 2
        .def("merkle_tree", depr(&get_merkle_tree))
        .def("set_merkle_tree", depr(&set_merkle_tree))
#endif
        .def("piece_size", &torrent_info::piece_size)

        .def("similar_torrents", &torrent_info::similar_torrents)
        .def("collections", &torrent_info::collections)
        .def("ssl_cert", &torrent_info::ssl_cert)
        .def("num_files", &torrent_info::num_files)
        .def("rename_file", rename_file0)
        .def("remap_files", &torrent_info::remap_files)
        .def("files", &torrent_info::files, return_internal_reference<>())
        .def("orig_files", &torrent_info::orig_files, return_internal_reference<>())
#if TORRENT_ABI_VERSION == 1
        .def("file_at", depr(&torrent_info::file_at))
#endif // TORRENT_ABI_VERSION

        .def("is_valid", &torrent_info::is_valid)
        .def("priv", &torrent_info::priv)
        .def("is_i2p", &torrent_info::is_i2p)
#if TORRENT_ABI_VERSION <= 2
        .def("is_merkle_torrent", depr(&torrent_info::is_merkle_torrent))
#endif
        .def("trackers", range(begin_trackers, end_trackers))

        .def("creation_date", &torrent_info::creation_date)

        .def("add_node", &add_node)
        .def("nodes", &nodes)
#if TORRENT_ABI_VERSION <= 2
        .def("metadata", depr(&metadata))
        .def("metadata_size", depr(&torrent_info::metadata_size))
#endif
        .def("info_section", &get_info_section)
        .def("map_block", map_block)
        .def("map_file", &torrent_info::map_file)
        ;

#if TORRENT_ABI_VERSION == 1
    class_<file_entry>("file_entry")
        .def_readwrite("path", &file_entry::path)
        .def_readwrite("symlink_path", &file_entry::symlink_path)
        .def_readwrite("filehash", &file_entry::filehash)
        .def_readwrite("mtime", &file_entry::mtime)
        .add_property("pad_file", &get_pad_file)
        .add_property("executable_attribute", &get_executable_attribute)
        .add_property("hidden_attribute", &get_hidden_attribute)
        .add_property("symlink_attribute", &get_symlink_attribute)
        .add_property("offset", &get_offset)
        .add_property("size", &get_size)
        ;
#endif

    class_<announce_entry>("announce_entry", init<std::string const&>())
        .def_readwrite("url", &announce_entry::url)
        .def_readonly("trackerid", &announce_entry::trackerid)
#if TORRENT_ABI_VERSION == 1
        .add_property("message", &get_message)
        .add_property("last_error", &get_last_error)
        .add_property("next_announce", &get_next_announce)
        .add_property("min_announce", &get_min_announce)
        .add_property("scrape_incomplete", &get_scrape_incomplete)
        .add_property("scrape_complete", &get_scrape_complete)
        .add_property("scrape_downloaded", &get_scrape_downloaded)
#endif
        .def_readwrite("tier", &announce_entry::tier)
        .def_readwrite("fail_limit", &announce_entry::fail_limit)
        .add_property("source", &get_source)
        .add_property("verified", &get_verified)
#if TORRENT_ABI_VERSION == 1
        .add_property("fails", &get_fails)
        .add_property("updating", &get_updating)
        .add_property("start_sent", &get_start_sent)
        .add_property("complete_sent", &get_complete_sent)
        .add_property("send_stats", &get_send_stats)
        .def("next_announce_in", depr(&next_announce_in))
        .def("min_announce_in", depr(&min_announce_in))
        .def("can_announce", depr(&can_announce))
        .def("is_working", depr(&is_working))
#endif
#if TORRENT_ABI_VERSION <= 2
        .def("reset", depr(&announce_entry::reset))
        .def("trim", depr(&announce_entry::trim))
#endif
        ;

    enum_<event_t>("event_t")
        .value("none", event_t::none)
        .value("completed", event_t::completed)
        .value("started", event_t::started)
        .value("stopped", event_t::stopped)
        .value("paused", event_t::paused)
        ;

    implicitly_convertible<std::shared_ptr<torrent_info>, std::shared_ptr<const torrent_info>>();
    boost::python::register_ptr_to_python<std::shared_ptr<const torrent_info>>();
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif
