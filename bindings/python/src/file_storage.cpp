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
    void file_storage_check_index(file_storage const& fs, file_index_t index)
    {
        if ((index < file_index_t{0}) || (index >= fs.end_file()))
            throw std::out_of_range("file index out of range");
    }

#if TORRENT_ABI_VERSION == 1
    void add_file_deprecated(file_storage& ct, file_entry const& fe)
    {
        python_deprecated("this overload of add_file() is deprecated");
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
    {
        python_deprecated("__iter__ is deprecated");
        return FileIter(self, file_index_t(0));
    }

    FileIter end_files(file_storage const& self)
    { return FileIter(self, self.end_file()); }
#endif // TORRENT_ABI_VERSION

#if TORRENT_ABI_VERSION < 4
    void add_files_no_callback(file_storage& fs, std::string const& file
       , create_flags_t const flags)
    {
        python_deprecated("add_files is deprecated, use list_files() instead");
        add_files(fs, file, flags);
    }

    void add_files_callback(file_storage& fs, std::string const& file
       , boost::python::object cb, create_flags_t const flags)
    {
        python_deprecated("add_files is deprecated, use list_files() instead");
        add_files(fs, file, [&](std::string const& i) { return cb(i); }, flags);
    }
#endif

    void add_file0(file_storage& fs, string_view const file, std::int64_t size
       , file_flags_t const flags, std::time_t md, string_view const link)
    {
       fs.add_file(std::string(file), size, flags, md, std::string(link));
    }

    void add_file1(file_storage& fs, bytes const& file, std::int64_t size
       , file_flags_t const flags, std::time_t md, std::string link)
    {
       python_deprecated("add_file with bytes is deprecated");
       fs.add_file(file.arr, size, flags, md, link);
    }

    void add_file2(file_storage& fs, string_view const file, std::int64_t size
       , file_flags_t const flags, std::time_t md, bytes link)
    {
       python_deprecated("add_file with bytes is deprecated");
       fs.add_file(std::string(file), size, flags, md, link.arr);
    }

    struct dummy_file_flags {};

    std::string file_storage_symlink(file_storage const& fs, file_index_t index)
    {
        file_storage_check_index(fs, index);
        return fs.symlink(index);
    }

    std::string file_storage_file_path(file_storage const& fs, file_index_t index, std::string const& base)
    {
        file_storage_check_index(fs, index);
        return fs.file_path(index, base);
    }

    string_view file_storage_file_name(file_storage const& fs, file_index_t index)
    {
        file_storage_check_index(fs, index);
        return fs.file_name(index);
    }

    std::int64_t file_storage_file_size(file_storage const& fs, file_index_t index)
    {
        file_storage_check_index(fs, index);
        return fs.file_size(index);
    }

    std::int64_t file_storage_file_offset(file_storage const& fs, file_index_t index)
    {
        file_storage_check_index(fs, index);
        return fs.file_offset(index);
    }

    file_flags_t file_storage_file_flags(file_storage const& fs, file_index_t index)
    {
        file_storage_check_index(fs, index);
        return fs.file_flags(index);
    }

    void rename_file0(file_storage& fs, file_index_t index, string_view const path)
    {
        file_storage_check_index(fs, index);
        fs.rename_file(index, std::string(path));
    }

    void rename_file1(file_storage& fs, file_index_t index, bytes path)
    {
        python_deprecated("rename_file with bytes is deprecated");
        file_storage_check_index(fs, index);
        fs.rename_file(index, path.arr);
    }

#if TORRENT_ABI_VERSION == 1
    file_entry file_storage_at(file_storage const& fs, file_index_t index)
    {
        file_storage_check_index(fs, index);
        return fs.at(index);
    }
#endif

    void set_name0(file_storage& fs, string_view const name)
    {
        fs.set_name(std::string(name));
    }

    void set_name1(file_storage& fs, bytes name)
    {
        python_deprecated("set_name with bytes is deprecated");
        fs.set_name(name.arr);
    }
}

void bind_file_storage()
{
#if TORRENT_ABI_VERSION < 4
    sha1_hash (file_storage::*file_storage_hash)(file_index_t) const = &file_storage::hash;
#endif

    {
    scope s = class_<file_storage>("file_storage")
        .def("is_valid", &file_storage::is_valid)
        .def("add_file", add_file0, (arg("path"), arg("size"), arg("flags") = 0, arg("mtime") = 0, arg("linkpath") = ""))
        .def("add_file", add_file1, (arg("path"), arg("size"), arg("flags") = 0, arg("mtime") = 0, arg("linkpath") = ""))
        .def("add_file", add_file2, (arg("path"), arg("size"), arg("flags") = 0, arg("mtime") = 0, arg("linkpath") = ""))
        .def("num_files", &file_storage::num_files)
#if TORRENT_ABI_VERSION == 1
        .def("at", depr(file_storage_at))
        .def("add_file", add_file_deprecated, arg("entry"))
        .def("__iter__", boost::python::range(&begin_files, &end_files))
        .def("__len__", depr(&file_storage::num_files))
#endif // TORRENT_ABI_VERSION
#if TORRENT_ABI_VERSION < 4
        .def("hash", file_storage_hash)
#endif
        .def("symlink", file_storage_symlink)
        .def("file_path", file_storage_file_path, (arg("idx"), arg("save_path") = ""))
        .def("file_name", file_storage_file_name)
        .def("file_size", file_storage_file_size)
        .def("root", &file_storage::root)
        .def("file_offset", file_storage_file_offset)
        .def("file_flags", file_storage_file_flags)

        .def("file_index_for_root", &file_storage::file_index_for_root)
        .def("piece_index_at_file", &file_storage::piece_index_at_file)
        .def("file_index_at_piece", &file_storage::file_index_at_piece)
        .def("file_index_at_offset", &file_storage::file_index_at_offset)
        .def("file_absolute_path", &file_storage::file_absolute_path)

        .def("v2", &file_storage::v2)

        .def("total_size", &file_storage::total_size)
        .def("size_on_disk", &file_storage::size_on_disk)
        .def("set_num_pieces", &file_storage::set_num_pieces)
        .def("num_pieces", &file_storage::num_pieces)
        .def("set_piece_length", &file_storage::set_piece_length)
        .def("piece_length", &file_storage::piece_length)
        .def("piece_size", &file_storage::piece_size)
        .def("set_name", &set_name0)
        .def("set_name", &set_name1)
        .def("rename_file", &rename_file0)
        .def("rename_file", &rename_file1)
        .def("name", &file_storage::name, return_value_policy<copy_const_reference>())
        ;

     s.attr("flag_pad_file") = file_storage::flag_pad_file;
     s.attr("flag_hidden") = file_storage::flag_hidden;
     s.attr("flag_executable") = file_storage::flag_executable;
     s.attr("flag_symlink") = file_storage::flag_symlink;
     }

    {
       scope s = class_<dummy_file_flags>("file_flags_t");
       s.attr("flag_pad_file") = file_storage::flag_pad_file;
       s.attr("flag_hidden") = file_storage::flag_hidden;
       s.attr("flag_executable") = file_storage::flag_executable;
       s.attr("flag_symlink") = file_storage::flag_symlink;
    }

#if TORRENT_ABI_VERSION < 4
    def("add_files", add_files_no_callback, (arg("fs"), arg("path"), arg("flags") = 0));
    def("add_files", add_files_callback, (arg("fs"), arg("path")
        , arg("predicate"), arg("flags") = 0));
#endif

}

#ifdef _MSC_VER
#pragma warning(pop)
#endif


