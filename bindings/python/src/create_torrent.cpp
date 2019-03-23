// Copyright Daniel Wallin & Arvid Norberg 2009. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "boost_python.hpp"
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/file_storage.hpp>
#include "libtorrent/torrent_info.hpp"
#include <libtorrent/version.hpp>
#include "bytes.hpp"

using namespace boost::python;
using namespace lt;

#ifdef _MSC_VER
#pragma warning(push)
// warning c4996: x: was declared deprecated
#pragma warning( disable : 4996 )
#endif

namespace
{
    void set_hash(create_torrent& c, piece_index_t p, bytes const& b)
    {
        c.set_hash(p, sha1_hash(b.arr));
    }

    void set_file_hash(create_torrent& c, file_index_t f, bytes const& b)
    {
        c.set_file_hash(f, sha1_hash(b.arr));
    }

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

#if TORRENT_ABI_VERSION == 1
    void add_file_deprecated(file_storage& ct, file_entry const& fe)
    {
       ct.add_file(fe);
    }

    struct FileIter
    {
        using value_type = lt::file_entry;
        using reference = lt::file_entry;
        using pointer = lt::file_entry*;
        using difference_type = int;
        using iterator_category = std::forward_iterator_tag;

        FileIter(file_storage const& fs, file_index_t i) : m_fs(&fs), m_i(i) {}
        FileIter(FileIter const&) = default;
        FileIter() : m_fs(nullptr), m_i(0) {}
        lt::file_entry operator*() const
        { return m_fs->at(m_i); }

        FileIter operator++() { m_i++; return *this; }
        FileIter operator++(int) { return FileIter(*m_fs, m_i++); }

        bool operator==(FileIter const& rhs) const
        { return m_fs == rhs.m_fs && m_i == rhs.m_i; }

        int operator-(FileIter const& rhs) const
        {
            assert(rhs.m_fs == m_fs);
            return m_i - rhs.m_i;
        }

        FileIter& operator=(FileIter const&) = default;

        file_storage const* m_fs;
        file_index_t m_i;
    };

    FileIter begin_files(file_storage const& self)
    { return FileIter(self, file_index_t(0)); }

    FileIter end_files(file_storage const& self)
    { return FileIter(self, self.end_file()); }

    void add_file_wstring(file_storage& fs, std::wstring const& file, std::int64_t size
       , file_flags_t const flags, std::time_t md, std::string link)
    {
       fs.add_file(file, size, flags, md, link);
    }
#endif // TORRENT_ABI_VERSION

    void add_files_callback(file_storage& fs, std::string const& file
       , boost::python::object cb, create_flags_t const flags)
    {
        add_files(fs, file, [&](std::string const& i) { return cb(i); }, flags);
    }

    void add_file(file_storage& fs, std::string const& file, std::int64_t size
       , file_flags_t const flags, std::time_t md, std::string link)
    {
       fs.add_file(file, size, flags, md, link);
    }

    void add_tracker(create_torrent& ct, std::string url, int tier)
    {
      ct.add_tracker(url, tier);
    }

    struct dummy13 {};
    struct dummy14 {};
}

void bind_create_torrent()
{
    void (file_storage::*set_name0)(std::string const&) = &file_storage::set_name;
    void (file_storage::*rename_file0)(file_index_t, std::string const&) = &file_storage::rename_file;
#if TORRENT_ABI_VERSION == 1
    void (file_storage::*set_name1)(std::wstring const&) = &file_storage::set_name;
    void (file_storage::*rename_file1)(file_index_t, std::wstring const&) = &file_storage::rename_file;
#endif

#ifndef BOOST_NO_EXCEPTIONS
    void (*set_piece_hashes0)(create_torrent&, std::string const&) = &set_piece_hashes;
#endif
    void (*add_files0)(file_storage&, std::string const&, create_flags_t) = add_files;

    std::string const& (file_storage::*file_storage_symlink)(file_index_t) const = &file_storage::symlink;
    sha1_hash (file_storage::*file_storage_hash)(file_index_t) const = &file_storage::hash;
    std::string (file_storage::*file_storage_file_path)(file_index_t, std::string const&) const = &file_storage::file_path;
    string_view (file_storage::*file_storage_file_name)(file_index_t) const = &file_storage::file_name;
    std::int64_t (file_storage::*file_storage_file_size)(file_index_t) const = &file_storage::file_size;
    std::int64_t (file_storage::*file_storage_file_offset)(file_index_t) const = &file_storage::file_offset;
    file_flags_t (file_storage::*file_storage_file_flags)(file_index_t) const = &file_storage::file_flags;

#if TORRENT_ABI_VERSION == 1
    file_entry (file_storage::*at)(int) const = &file_storage::at;
#endif

    // TODO: 3 move this to its own file
    {
    scope s = class_<file_storage>("file_storage")
        .def("is_valid", &file_storage::is_valid)
        .def("add_file", add_file, (arg("path"), arg("size"), arg("flags") = 0, arg("mtime") = 0, arg("linkpath") = ""))
        .def("num_files", &file_storage::num_files)
#if TORRENT_ABI_VERSION == 1
        .def("at", at)
        .def("add_file", add_file_deprecated, arg("entry"))
        .def("__iter__", boost::python::range(&begin_files, &end_files))
        .def("__len__", &file_storage::num_files)
        .def("add_file", add_file_wstring, (arg("path"), arg("size"), arg("flags") = 0, arg("mtime") = 0, arg("linkpath") = ""))
#endif // TORRENT_ABI_VERSION
        .def("hash", file_storage_hash)
        .def("symlink", file_storage_symlink, return_value_policy<copy_const_reference>())
        .def("file_path", file_storage_file_path, (arg("idx"), arg("save_path") = ""))
        .def("file_name", file_storage_file_name)
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
#if TORRENT_ABI_VERSION == 1
        .def("set_name", set_name1)
        .def("rename_file", rename_file1)
#endif
        .def("name", &file_storage::name, return_value_policy<copy_const_reference>())
        ;

     s.attr("flag_pad_file") = file_storage::flag_pad_file;
     s.attr("flag_hidden") = file_storage::flag_hidden;
     s.attr("flag_executable") = file_storage::flag_executable;
     s.attr("flag_symlink") = file_storage::flag_symlink;
     }

    {
       scope s = class_<dummy13>("file_flags_t");
       s.attr("flag_pad_file") = file_storage::flag_pad_file;
       s.attr("flag_hidden") = file_storage::flag_hidden;
       s.attr("flag_executable") = file_storage::flag_executable;
       s.attr("flag_symlink") = file_storage::flag_symlink;
    }

    {
    scope s = class_<create_torrent>("create_torrent", no_init)
        .def(init<file_storage&>())
        .def(init<torrent_info const&>(arg("ti")))
        .def(init<file_storage&, int, int, create_flags_t>((arg("storage"), arg("piece_size") = 0
            , arg("pad_file_limit") = -1, arg("flags") = lt::create_torrent::optimize_alignment)))

        .def("generate", &create_torrent::generate)

        .def("files", &create_torrent::files, return_internal_reference<>())
        .def("set_comment", &create_torrent::set_comment)
        .def("set_creator", &create_torrent::set_creator)
        .def("set_hash", &set_hash)
        .def("set_file_hash", &set_file_hash)
        .def("add_url_seed", &create_torrent::add_url_seed)
        .def("add_http_seed", &create_torrent::add_http_seed)
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

        s.attr("optimize_alignment") = create_torrent::optimize_alignment;
        s.attr("merkle") = create_torrent::merkle;
        s.attr("modification_time") = create_torrent::modification_time;
        s.attr("symlinks") = create_torrent::symlinks;
    }

    {
        scope s = class_<dummy14>("create_torrent_flags_t");
#if TORRENT_ABI_VERSION == 1
        s.attr("optimize") = create_torrent::optimize;
#endif
        s.attr("optimize_alignment") = create_torrent::optimize_alignment;
        s.attr("merkle") = create_torrent::merkle;
        s.attr("modification_time") = create_torrent::modification_time;
        s.attr("symlinks") = create_torrent::symlinks;
    }

    def("add_files", add_files0, (arg("fs"), arg("path"), arg("flags") = 0));
    def("add_files", add_files_callback, (arg("fs"), arg("path")
        , arg("predicate"), arg("flags") = 0));
    def("set_piece_hashes", set_piece_hashes0);
    def("set_piece_hashes", set_piece_hashes_callback);

}

#ifdef _MSC_VER
#pragma warning(pop)
#endif

