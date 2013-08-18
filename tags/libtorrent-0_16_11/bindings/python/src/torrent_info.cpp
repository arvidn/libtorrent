// Copyright Daniel Wallin 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <boost/python.hpp>
#include <libtorrent/torrent_info.hpp>
#include "libtorrent/intrusive_ptr_base.hpp"
#include "libtorrent/session_settings.hpp"

using namespace boost::python;
using namespace libtorrent;

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

        typedef std::vector<std::pair<std::string, int> > list_type;

        for (list_type::const_iterator i = ti.nodes().begin(); i != ti.nodes().end(); ++i)
        {
            result.append(make_tuple(i->first, i->second));
        }

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
            d["extra_headers"] = i->extra_headers;
            d["retry"] = total_seconds(i->retry - min_time());
            d["resolving"] = i->resolving;
            d["removed"] = i->removed;
            d["endpoint"] = make_tuple(
                boost::lexical_cast<std::string>(i->endpoint.address()), i->endpoint.port());
            ret.append(d);
        }

        return ret;
    }

    list get_merkle_tree(torrent_info const& ti)
    {
        std::vector<sha1_hash> const& mt = ti.merkle_tree();
        list ret;
        for (std::vector<sha1_hash>::const_iterator i = mt.begin()
            , end(mt.end()); i != end; ++i)
        {
            ret.append(i->to_string());
        }
        return ret;
    }

    void set_merkle_tree(torrent_info& ti, list hashes)
    {
        std::vector<sha1_hash> h;
        for (int i = 0, e = len(hashes); i < e; ++i)
            h.push_back(sha1_hash(extract<char const*>(hashes[i])));

        ti.set_merkle_tree(h);
    }

    file_storage::iterator begin_files(torrent_info& i)
    {
        return i.begin_files();
    }

    file_storage::iterator end_files(torrent_info& i)
    {
        return i.end_files();
    }

    void remap_files(torrent_info& ti, list files) {
        file_storage st;
        for (int i = 0, e = len(files); i < e; ++i)
            st.add_file(extract<file_entry>(files[i]));

        ti.remap_files(st);
    }

    list files(torrent_info const& ti, bool storage) {
        list result;

        typedef torrent_info::file_iterator iter;

        for (iter i = ti.begin_files(); i != ti.end_files(); ++i)
            result.append(ti.files().at(i));

        return result;
    }

    list orig_files(torrent_info const& ti, bool storage) {
        list result;

        file_storage const& st = ti.orig_files();

        for (int i = 0; i < st.num_files(); ++i)
            result.append(st.at(i));

        return result;
    }

    std::string hash_for_piece(torrent_info const& ti, int i)
    {
        return ti.hash_for_piece(i).to_string();
    }

    std::string metadata(torrent_info const& ti) {
        std::string result(ti.metadata().get(), ti.metadata_size());
        return result;
    }

    torrent_info construct0(std::string path) {
        return torrent_info(path);
    }

    list map_block(torrent_info& ti, int piece, size_type offset, int size)
    {
       std::vector<file_slice> p = ti.map_block(piece, offset, size);
       list result;

       for (std::vector<file_slice>::iterator i(p.begin()), e(p.end()); i != e; ++i)
          result.append(*i);

       return result;
    }

    bool get_tier(announce_entry const& ae) { return ae.tier; }
    void set_tier(announce_entry& ae, bool v) { ae.tier = v; }
    bool get_fail_limit(announce_entry const& ae) { return ae.fail_limit; }
    void set_fail_limit(announce_entry& ae, int l) { ae.fail_limit = l; }
    bool get_fails(announce_entry const& ae) { return ae.fails; }
    bool get_source(announce_entry const& ae) { return ae.source; }
    bool get_verified(announce_entry const& ae) { return ae.verified; }
    bool get_updating(announce_entry const& ae) { return ae.updating; }
    bool get_start_sent(announce_entry const& ae) { return ae.start_sent; }
    bool get_complete_sent(announce_entry const& ae) { return ae.complete_sent; }
    bool get_send_stats(announce_entry const& ae) { return ae.send_stats; }


    size_type get_size(file_entry const& fe) { return fe.size; }
    size_type get_offset(file_entry const& fe) { return fe.offset; }
    size_type get_file_base(file_entry const& fe) { return fe.file_base; }
    void set_file_base(file_entry& fe, int b) { fe.file_base = b; }
    bool get_pad_file(file_entry const& fe) { return fe.pad_file; }
    bool get_executable_attribute(file_entry const& fe) { return fe.executable_attribute; }
    bool get_hidden_attribute(file_entry const& fe) { return fe.hidden_attribute; }
    bool get_symlink_attribute(file_entry const& fe) { return fe.symlink_attribute; }

} // namespace unnamed

void bind_torrent_info()
{
    return_value_policy<copy_const_reference> copy;

    void (torrent_info::*rename_file0)(int, std::string const&) = &torrent_info::rename_file;
#if TORRENT_USE_WSTRING
    void (torrent_info::*rename_file1)(int, std::wstring const&) = &torrent_info::rename_file;
#endif

    class_<file_slice>("file_slice")
        .def_readwrite("file_index", &file_slice::file_index)
        .def_readwrite("offset", &file_slice::offset)
        .def_readwrite("size", &file_slice::size)
        ;

    class_<torrent_info, boost::intrusive_ptr<torrent_info> >("torrent_info", no_init)
#ifndef TORRENT_NO_DEPRECATE
        .def(init<entry const&>(arg("e")))
#endif
        .def(init<sha1_hash const&, int>((arg("info_hash"), arg("flags") = 0)))
        .def(init<char const*, int, int>((arg("buffer"), arg("length"), arg("flags") = 0)))
        .def(init<std::string, int>((arg("file"), arg("flags") = 0)))
        .def(init<torrent_info const&, int>((arg("ti"), arg("flags") = 0)))
#if TORRENT_USE_WSTRING
        .def(init<std::wstring, int>((arg("file"), arg("flags") = 0)))
#endif

        .def("remap_files", &remap_files)
        .def("add_tracker", &torrent_info::add_tracker, arg("url"))
        .def("add_url_seed", &torrent_info::add_url_seed)
        .def("add_http_seed", &torrent_info::add_http_seed)
        .def("web_seeds", get_web_seeds)

        .def("name", &torrent_info::name, copy)
        .def("comment", &torrent_info::comment, copy)
        .def("creator", &torrent_info::creator, copy)
        .def("total_size", &torrent_info::total_size)
        .def("piece_length", &torrent_info::piece_length)
        .def("num_pieces", &torrent_info::num_pieces)
#ifndef TORRENT_NO_DEPRECATE
        .def("info_hash", &torrent_info::info_hash, copy)
#endif
        .def("hash_for_piece", &hash_for_piece)
        .def("merkle_tree", get_merkle_tree)
        .def("set_merkle_tree", set_merkle_tree)
        .def("piece_size", &torrent_info::piece_size)

        .def("num_files", &torrent_info::num_files, (arg("storage")=false))
        .def("file_at", &torrent_info::file_at)
        .def("file_at_offset", &torrent_info::file_at_offset)
        .def("files", &files, (arg("storage")=false))
        .def("orig_files", &orig_files, (arg("storage")=false))
        .def("rename_file", rename_file0)
#if TORRENT_USE_WSTRING
        .def("rename_file", rename_file1)
#endif

        .def("priv", &torrent_info::priv)
        .def("trackers", range(begin_trackers, end_trackers))

        .def("creation_date", &torrent_info::creation_date)

        .def("add_node", &add_node)
        .def("nodes", &nodes)
        .def("metadata", &metadata)
        .def("metadata_size", &torrent_info::metadata_size)
        .def("map_block", map_block)
        .def("map_file", &torrent_info::map_file)
        ;

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
        .add_property("file_base", &get_file_base, &set_file_base)
        ;

    class_<announce_entry>("announce_entry", init<std::string const&>())
        .def_readwrite("url", &announce_entry::url)
        .add_property("tier", &get_tier, &set_tier)
        .add_property("fail_limit", &get_fail_limit, &set_fail_limit)
        .add_property("fails", &get_fails)
        .add_property("source", &get_source)
        .add_property("verified", &get_verified)
        .add_property("updating", &get_updating)
        .add_property("start_sent", &get_start_sent)
        .add_property("complete_sent", &get_complete_sent)
        .add_property("send_stats", &get_send_stats)

        .def("reset", &announce_entry::reset)
        .def("failed", &announce_entry::failed, arg("retry_interval") = 0)
        .def("can_announce", &announce_entry::can_announce)
        .def("is_working", &announce_entry::is_working)
        .def("trim", &announce_entry::trim)
        ;

    enum_<announce_entry::tracker_source>("tracker_source")
        .value("source_torrent", announce_entry::source_torrent)
        .value("source_client", announce_entry::source_client)
        .value("source_magnet_link", announce_entry::source_magnet_link)
        .value("source_tex", announce_entry::source_tex)
    ;

    implicitly_convertible<boost::intrusive_ptr<torrent_info>, boost::intrusive_ptr<const torrent_info> >();
    boost::python::register_ptr_to_python<boost::intrusive_ptr<const torrent_info> >();
}

