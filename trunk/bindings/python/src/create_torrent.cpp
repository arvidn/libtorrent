// Copyright Daniel Wallin & Arvid Norberg 2009. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <boost/python.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/file_storage.hpp>
#include "libtorrent/intrusive_ptr_base.hpp"

using namespace boost::python;
using namespace libtorrent;

void bind_create_torrent()
{
    void (file_storage::*add_file0)(file_entry const&) = &file_storage::add_file;
    void (file_storage::*add_file1)(fs::path const&, size_type, int) = &file_storage::add_file;
    void (file_storage::*add_file2)(fs::wpath const&, size_type, int) = &file_storage::add_file;

    void (file_storage::*set_name0)(std::string const&) = &file_storage::set_name;
    void (file_storage::*set_name1)(std::wstring const&) = &file_storage::set_name;

    class_<file_storage>("file_storage")
        .def("is_valid", &file_storage::is_valid)
        .def("add_file", add_file0)
        .def("add_file", add_file1, (arg("path"), arg("size"), arg("flags") = 0))
        .def("add_file", add_file2, (arg("path"), arg("size"), arg("flags") = 0))
        .def("num_files", &file_storage::num_files)
        .def("at", &file_storage::at, return_internal_reference<>())
        .def("total_size", &file_storage::total_size)
        .def("set_num_pieces", &file_storage::set_num_pieces)
        .def("num_pieces", &file_storage::num_pieces)
        .def("set_piece_length", &file_storage::set_piece_length)
        .def("piece_length", &file_storage::piece_length)
        .def("piece_size", &file_storage::piece_size)
        .def("set_name", set_name0)
        .def("set_name", set_name1)
        .def("name", &file_storage::name, return_internal_reference<>())
        ;

    class_<create_torrent>("create_torrent", no_init)
        .def(init<file_storage&>())
        .def(init<file_storage&, int>())

        .def("generate", &create_torrent::generate)

        .def("files", &create_torrent::files, return_internal_reference<>())
        .def("set_comment", &create_torrent::set_comment)
        .def("set_creator", &create_torrent::set_creator)
        .def("set_hash", &create_torrent::set_hash)
        .def("add_url_seed", &create_torrent::add_url_seed)
        .def("add_node", &create_torrent::add_node)
        .def("add_tracker", &create_torrent::add_tracker)
        .def("set_priv", &create_torrent::set_priv)
        .def("num_pieces", &create_torrent::num_pieces)
        .def("piece_length", &create_torrent::piece_length)
        .def("piece_size", &create_torrent::piece_size)
        .def("priv", &create_torrent::priv)
        ;
}
