// Copyright Daniel Wallin 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <boost/python.hpp>
#include <libtorrent/torrent_info.hpp>
#include "libtorrent/intrusive_ptr_base.hpp"

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

    file_storage::iterator begin_files(torrent_info& i)
    {
        return i.begin_files();
    }

    file_storage::iterator end_files(torrent_info& i)
    {
        return i.end_files();
    }

    //list files(torrent_info const& ti, bool storage) {
    list files(torrent_info const& ti, bool storage) {
        list result;

        typedef std::vector<file_entry> list_type;

        for (list_type::const_iterator i = ti.begin_files(); i != ti.end_files(); ++i)
            result.append(*i);

        return result;
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

    bool get_verified(announce_entry const& ae)
    { return ae.verified; }
    bool get_updating(announce_entry const& ae)
    { return ae.updating; }
    bool get_start_sent(announce_entry const& ae)
    { return ae.start_sent; }
    bool get_complete_sent(announce_entry const& ae)
    { return ae.complete_sent; }
    bool get_send_stats(announce_entry const& ae)
    { return ae.send_stats; }

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
        .def(init<entry const&>())
#endif
        .def(init<sha1_hash const&>())
        .def(init<char const*, int>())
        .def(init<std::string>())
#if TORRENT_USE_WSTRING
        .def(init<std::wstring>())
#endif

        .def("add_tracker", &torrent_info::add_tracker, (arg("url"), arg("tier")=0))
        .def("add_url_seed", &torrent_info::add_url_seed)

        .def("name", &torrent_info::name, copy)
        .def("comment", &torrent_info::comment, copy)
        .def("creator", &torrent_info::creator, copy)
        .def("total_size", &torrent_info::total_size)
        .def("piece_length", &torrent_info::piece_length)
        .def("num_pieces", &torrent_info::num_pieces)
#ifndef TORRENT_NO_DEPRECATE
        .def("info_hash", &torrent_info::info_hash, copy)
#endif
        .def("hash_for_piece", &torrent_info::hash_for_piece)
        .def("piece_size", &torrent_info::piece_size)

        .def("num_files", &torrent_info::num_files, (arg("storage")=false))
        .def("file_at", &torrent_info::file_at, return_internal_reference<>())
        .def("file_at_offset", &torrent_info::file_at_offset)
        .def("files", &files, (arg("storage")=false))
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
       .add_property(
            "path"
          , make_getter(
                &file_entry::path, return_value_policy<copy_non_const_reference>()
            )
        )
        .def_readonly("offset", &file_entry::offset)
        .def_readonly("size", &file_entry::size)
        .def_readonly("file_base", &file_entry::file_base)
        ;

    class_<announce_entry>("announce_entry", init<std::string const&>())
        .def_readwrite("url", &announce_entry::url)
        .def_readwrite("tier", &announce_entry::tier)
        .add_property("fail_limit", &announce_entry::fail_limit)
        .add_property("fails", &announce_entry::fails)
        .add_property("source", &announce_entry::source)
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
}
