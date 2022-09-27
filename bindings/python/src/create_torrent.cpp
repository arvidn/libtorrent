// Copyright Daniel Wallin & Arvid Norberg 2009. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "boost_python.hpp"
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/file_storage.hpp>
#include "libtorrent/torrent_info.hpp"
#include <libtorrent/version.hpp>
#include "bytes.hpp"
#include "gil.hpp"

using namespace boost::python;
using namespace lt;

#ifdef _MSC_VER
#pragma warning(push)
// warning c4996: x: was declared deprecated
#pragma warning( disable : 4996 )
#endif

namespace
{
    void ct_check_piece_index(create_torrent& ct, piece_index_t index)
    {
        if ((index < 0) || (index >= ct.num_pieces()))
            throw std::out_of_range("piece index out of range");
    }

    void set_hash(create_torrent& c, piece_index_t p, bytes const& b)
    {
        ct_check_piece_index(c, p);
        if (b.arr.size() < 20)
            throw std::invalid_argument("short hash length");
        if (b.arr.size() > 20)
            python_deprecated("long hash length. this will work, but is deprecated");
        c.set_hash(p, sha1_hash(b.arr));
    }

#if TORRENT_ABI_VERSION < 3
    void set_file_hash(create_torrent& c, file_index_t f, bytes const& b)
    {
        if ((f < file_index_t{0}) || (f >= c.end_file()))
            throw std::out_of_range("file index out of range");
        if (b.arr.size() < 20)
            throw std::invalid_argument("short hash length");
        if (b.arr.size() > 20)
            python_deprecated("long hash length. this will work, but is deprecated");
        c.set_file_hash(f, sha1_hash(b.arr));
    }
#endif

#ifndef BOOST_NO_EXCEPTIONS
    void set_piece_hashes_callback(create_torrent& c, std::string const& p
        , boost::python::object cb)
    {
        set_piece_hashes(c, p, std::function<void(piece_index_t)>(
           [&](piece_index_t const i) { cb(i); }));
    }
#else
    void set_piece_hashes_callback(create_torrent& c, std::string const& p
        , boost::python::object cb)
    {
        error_code ec;
        set_piece_hashes(c, p, [&](piece_index_t const i) { cb(i); }, ec);
    }

    void set_piece_hashes0(create_torrent& c, std::string const & s)
    {
        error_code ec;
        set_piece_hashes(c, s, ec);
    }
#endif

    void add_node(create_torrent& ct, std::string const& addr, int port)
    {
        ct.add_node(std::make_pair(addr, port));
    }

    void add_tracker(create_torrent& ct, std::string url, int tier)
    {
      ct.add_tracker(url, tier);
    }

    struct dummy_create_torrent_flags {};
}

void bind_create_torrent()
{
#ifndef BOOST_NO_EXCEPTIONS
    void (*set_piece_hashes0)(create_torrent&, std::string const&) = &set_piece_hashes;
#endif

    {
    scope s = class_<create_torrent>("create_torrent", no_init)
        .def(init<file_storage&>())
        .def(init<torrent_info const&>(arg("ti")))
        .def(init<file_storage&, int, create_flags_t>((arg("storage"), arg("piece_size") = 0
            , arg("flags") = create_flags_t{})))

        .def("generate", &create_torrent::generate)

        .def("files", &create_torrent::files, return_internal_reference<>())
        .def("set_comment", &create_torrent::set_comment)
        .def("set_creator", &create_torrent::set_creator)
        .def("set_creation_date", &create_torrent::set_creation_date)
        .def("set_hash", &set_hash)
#if TORRENT_ABI_VERSION < 3
        .def("set_file_hash", depr(&set_file_hash))
#endif
        .def("add_url_seed", &create_torrent::add_url_seed)
#if TORRENT_ABI_VERSION < 4
        .def("add_http_seed", depr(&create_torrent::add_http_seed))
#endif
        .def("add_node", &add_node)
        .def("add_tracker", add_tracker, (arg("announce_url"), arg("tier") = 0))
        .def("set_priv", &create_torrent::set_priv)
        .def("num_pieces", &create_torrent::num_pieces)
        .def("piece_length", &create_torrent::piece_length)
        .def("piece_size", &create_torrent::piece_size)
        .def("priv", &create_torrent::priv)
        .def("set_root_cert", &create_torrent::set_root_cert, (arg("pem")))
        .def("add_collection", &create_torrent::add_collection)
        .def("add_similar_torrent", &create_torrent::add_similar_torrent)

        ;

#if TORRENT_ABI_VERSION <= 2
        s.attr("optimize_alignment") = create_torrent::optimize_alignment;
        s.attr("merkle") = create_torrent::merkle;
#endif
        s.attr("v2_only") = create_torrent::v2_only;
        s.attr("v1_only") = create_torrent::v1_only;
        s.attr("canonical_files") = create_torrent::canonical_files;
        s.attr("modification_time") = create_torrent::modification_time;
        s.attr("symlinks") = create_torrent::symlinks;
        s.attr("no_attributes") = create_torrent::no_attributes;
        s.attr("canonical_files_no_tail_padding") = create_torrent::canonical_files_no_tail_padding;
    }

    {
        scope s = class_<dummy_create_torrent_flags>("create_torrent_flags_t");
#if TORRENT_ABI_VERSION == 1
        s.attr("optimize") = create_torrent::optimize;
#endif
#if TORRENT_ABI_VERSION <= 2
        s.attr("optimize_alignment") = create_torrent::optimize_alignment;
        s.attr("merkle") = create_torrent::merkle;
#endif
        s.attr("v2_only") = create_torrent::v2_only;
        s.attr("modification_time") = create_torrent::modification_time;
        s.attr("symlinks") = create_torrent::symlinks;
    }

    def("set_piece_hashes", set_piece_hashes0);
    def("set_piece_hashes", set_piece_hashes_callback);

}

#ifdef _MSC_VER
#pragma warning(pop)
#endif

