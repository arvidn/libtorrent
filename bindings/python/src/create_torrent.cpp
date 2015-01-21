// Copyright Daniel Wallin & Arvid Norberg 2009. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <boost/python.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/file_storage.hpp>
#include "libtorrent/torrent_info.hpp"
#include "bytes.hpp"

using namespace boost::python;
using namespace libtorrent;

namespace
{
    void set_hash(create_torrent& c, int p, bytes const& b)
    {
        c.set_hash(p, sha1_hash(b.arr));
    }

    void set_file_hash(create_torrent& c, int f, bytes const& b)
    {
        c.set_file_hash(f, sha1_hash(b.arr));
    }

    void call_python_object(boost::python::object const& obj, int i)
    {
       obj(i);
    }

#ifndef BOOST_NO_EXCEPTIONS
    void set_piece_hashes_callback(create_torrent& c, std::string const& p
        , boost::python::object cb)
    {
        set_piece_hashes(c, p, boost::bind(call_python_object, cb, _1));
    }
#else
    void set_piece_hashes_callback(create_torrent& c, std::string const& p
        , boost::python::object cb)
    {
        error_code ec;
        set_piece_hashes(c, p, boost::bind(call_python_object, cb, _1), ec);
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

#if !defined TORRENT_NO_DEPRECATE
    void add_file(file_storage& ct, file_entry const& fe)
    {
       ct.add_file(fe);
    }
#endif

    char const* filestorage_name(file_storage const& fs)
    { return fs.name().c_str(); }

    bool call_python_object2(boost::python::object const& obj, std::string& i)
    {
       return obj(i);
    }

    void add_files_callback(file_storage& fs, std::string const& file
       , boost::python::object cb, boost::uint32_t flags)
    {
        add_files(fs, file, boost::bind(&call_python_object2, cb, _1), flags);
    }
}

void bind_create_torrent()
{
    void (file_storage::*add_file0)(std::string const&, boost::int64_t
		 , int, std::time_t, std::string const&) = &file_storage::add_file;
#if !defined TORRENT_NO_DEPRECATE
#if TORRENT_USE_WSTRING
    void (file_storage::*add_file1)(std::wstring const&, boost::int64_t
		 , int, std::time_t, std::string const&) = &file_storage::add_file;
#endif // TORRENT_USE_WSTRING
#endif // TORRENT_NO_DEPRECATE

    void (file_storage::*set_name0)(std::string const&) = &file_storage::set_name;
    void (file_storage::*rename_file0)(int, std::string const&) = &file_storage::rename_file;
#if TORRENT_USE_WSTRING && !defined TORRENT_NO_DEPRECATE
    void (file_storage::*set_name1)(std::wstring const&) = &file_storage::set_name;
    void (file_storage::*rename_file1)(int, std::wstring const&) = &file_storage::rename_file;
#endif

#ifndef BOOST_NO_EXCEPTIONS
    void (*set_piece_hashes0)(create_torrent&, std::string const&) = &set_piece_hashes;
#endif
    void (*add_files0)(file_storage&, std::string const&, boost::uint32_t) = add_files;

	 std::string const& (file_storage::*file_storage_symlink)(int) const = &file_storage::symlink;
    sha1_hash (file_storage::*file_storage_hash)(int) const = &file_storage::hash;
	 std::string (file_storage::*file_storage_file_path)(int, std::string const&) const = &file_storage::file_path;
	 boost::int64_t (file_storage::*file_storage_file_size)(int) const = &file_storage::file_size;
	 boost::int64_t (file_storage::*file_storage_file_offset)(int) const = &file_storage::file_offset;
	 int (file_storage::*file_storage_file_flags)(int) const = &file_storage::file_flags;

#if !defined TORRENT_NO_DEPRECATE
    file_entry (file_storage::*at)(int) const = &file_storage::at;
#endif

    class_<file_storage>("file_storage")
        .def("is_valid", &file_storage::is_valid)
        .def("add_file", add_file0, (arg("path"), arg("size"), arg("flags") = 0, arg("mtime") = 0, arg("linkpath") = ""))
#if TORRENT_USE_WSTRING && !defined TORRENT_NO_DEPRECATE
        .def("add_file", add_file1, (arg("path"), arg("size"), arg("flags") = 0, arg("mtime") = 0, arg("linkpath") = ""))
#endif
        .def("num_files", &file_storage::num_files)
#if !defined TORRENT_NO_DEPRECATE
        .def("at", at)
        .def("add_file", add_file, arg("entry"))
#endif
        .def("hash", file_storage_hash)
        .def("symlink", file_storage_symlink, return_value_policy<copy_const_reference>())
//        .def("file_base", &file_storage::file_base)
//        .def("set_file_base", &file_storage::set_file_base)
        .def("file_path", file_storage_file_path, (arg("idx"), arg("save_path") = ""))
        .def("file_size", file_storage_file_size)
        .def("file_offset", file_storage_file_offset)
        .def("file_flags", file_storage_file_flags)

        .def("total_size", &file_storage::total_size)
        .def("set_num_pieces", &file_storage::set_num_pieces)
        .def("num_pieces", &file_storage::num_pieces)
        .def("set_piece_length", &file_storage::set_piece_length)
        .def("piece_length", &file_storage::piece_length)
        .def("piece_size", &file_storage::piece_size)
        .def("set_name", set_name0)
        .def("rename_file", rename_file0)
#if TORRENT_USE_WSTRING && !defined TORRENT_NO_DEPRECATE
        .def("set_name", set_name1)
        .def("rename_file", rename_file1)
#endif
        .def("name", &file_storage::name, return_value_policy<copy_const_reference>())
        ;

    enum_<file_storage::file_flags_t>("file_flags_t")
        .value("flag_pad_file", file_storage::flag_pad_file)
        .value("flag_hidden", file_storage::flag_hidden)
        .value("flag_executable", file_storage::flag_executable)
        .value("flag_symlink", file_storage::flag_symlink)
		  ;

    class_<create_torrent>("create_torrent", no_init)
        .def(init<file_storage&>())
        .def(init<torrent_info const&>(arg("ti")))
        .def(init<file_storage&, int, int, int>((arg("storage"), arg("piece_size") = 0
            , arg("pad_file_limit") = -1, arg("flags") = int(libtorrent::create_torrent::optimize))))

        .def("generate", &create_torrent::generate)

        .def("files", &create_torrent::files, return_internal_reference<>())
        .def("set_comment", &create_torrent::set_comment)
        .def("set_creator", &create_torrent::set_creator)
        .def("set_hash", &set_hash)
        .def("set_file_hash", &set_file_hash)
        .def("add_url_seed", &create_torrent::add_url_seed)
        .def("add_http_seed", &create_torrent::add_http_seed)
        .def("add_node", &add_node)
        .def("add_tracker", &create_torrent::add_tracker, (arg("announce_url"), arg("tier") = 0))
        .def("set_priv", &create_torrent::set_priv)
        .def("num_pieces", &create_torrent::num_pieces)
        .def("piece_length", &create_torrent::piece_length)
        .def("piece_size", &create_torrent::piece_size)
        .def("priv", &create_torrent::priv)
        .def("set_root_cert", &create_torrent::set_root_cert, (arg("pem")))
        ;

    enum_<create_torrent::flags_t>("create_torrent_flags_t")
        .value("optimize", create_torrent::optimize)
        .value("merkle", create_torrent::merkle)
        .value("modification_time", create_torrent::modification_time)
        .value("symlinks", create_torrent::symlinks)
    ;

    def("add_files", add_files0, (arg("fs"), arg("path"), arg("flags") = 0));
    def("add_files", add_files_callback, (arg("fs"), arg("path")
        , arg("predicate"), arg("flags") = 0));
    def("set_piece_hashes", set_piece_hashes0);
    def("set_piece_hashes", set_piece_hashes_callback);

}
