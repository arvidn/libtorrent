// Copyright Daniel Wallin & Arvid Norberg 2009. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <boost/python.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/file_storage.hpp>
#include "libtorrent/intrusive_ptr_base.hpp"

using namespace boost::python;
using namespace libtorrent;

namespace
{
    void set_hash(create_torrent& c, int p, char const* hash)
    {
        c.set_hash(p, sha1_hash(hash));
    }

    void call_python_object(boost::python::object const& obj, int i)
    {
       obj(i);
    }

    void set_piece_hashes_callback(create_torrent& c, boost::filesystem::path const& p
        , boost::python::object cb)
    {
        set_piece_hashes(c, p, boost::bind(call_python_object, cb, _1));
    }
}

void bind_create_torrent()
{
    void (file_storage::*add_file0)(file_entry const&) = &file_storage::add_file;
    void (file_storage::*add_file1)(fs::path const&, size_type) = &file_storage::add_file;

    void (*set_piece_hashes0)(create_torrent&, boost::filesystem::path const&) = &set_piece_hashes;
    void (*add_files0)(file_storage&, boost::filesystem::path const&) = add_files;

    class_<file_storage>("file_storage")
        .def("is_valid", &file_storage::is_valid)
        .def("add_file", add_file0)
        .def("add_file", add_file1)
        .def("num_files", &file_storage::num_files)
        .def("at", &file_storage::at, return_internal_reference<>())
        .def("total_size", &file_storage::total_size)
        .def("set_num_pieces", &file_storage::set_num_pieces)
        .def("num_pieces", &file_storage::num_pieces)
        .def("set_piece_length", &file_storage::set_piece_length)
        .def("piece_length", &file_storage::piece_length)
        .def("piece_size", &file_storage::piece_size)
        .def("set_name", &file_storage::set_name)
        .def("name", &file_storage::name, return_internal_reference<>())
        ;

    class_<create_torrent>("create_torrent", no_init)
        .def(init<file_storage&>())
        .def(init<file_storage&, int>())

        .def("generate", &create_torrent::generate)

        .def("files", &create_torrent::files, return_internal_reference<>())
        .def("set_comment", &create_torrent::set_comment)
        .def("set_creator", &create_torrent::set_creator)
        .def("set_hash", &set_hash)
        .def("add_url_seed", &create_torrent::add_url_seed)
        .def("add_node", &create_torrent::add_node)
        .def("add_tracker", &create_torrent::add_tracker)
        .def("set_priv", &create_torrent::set_priv)
        .def("num_pieces", &create_torrent::num_pieces)
        .def("piece_length", &create_torrent::piece_length)
        .def("piece_size", &create_torrent::piece_size)
        .def("priv", &create_torrent::priv)
        ;

    def("add_files", add_files0);
    def("set_piece_hashes", set_piece_hashes0);
    def("set_piece_hashes", set_piece_hashes_callback);

}
