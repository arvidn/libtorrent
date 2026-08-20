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
#pragma warning(disable : 4996)
#endif

namespace {
	void file_storage_check_index(file_storage const& fs, file_index_t index)
	{
		if (index < file_index_t{0} || index >= fs.end_file())
		{
			PyErr_SetString(PyExc_IndexError, "invalid file index");
			throw_error_already_set();
		}
	}

	void file_storage_check_index(file_storage const& fs, piece_index_t index)
	{
		if (index < piece_index_t{0} || index >= fs.end_piece())
		{
			PyErr_SetString(PyExc_IndexError, "invalid piece index");
			throw_error_already_set();
		}
	}

#if TORRENT_ABI_VERSION < 5
	std::shared_ptr<file_storage> file_storage_constructor()
	{
		python_deprecated("constructing a file_storage directly is deprecated, use "
						  "create_torrent and create_file_entry instead");
		return std::make_shared<file_storage>();
	}
#endif

#if TORRENT_ABI_VERSION < 4
	void
	add_files_no_callback(file_storage& fs, std::string const& file, create_flags_t const flags)
	{
		python_deprecated("add_files is deprecated, use list_files() instead");
		add_files(fs, file, flags);
	}

	void add_files_callback(
		file_storage& fs,
		std::string const& file,
		boost::python::object cb,
		create_flags_t const flags
	)
	{
		python_deprecated("add_files is deprecated, use list_files() instead");
		add_files(fs, file, [&](std::string const& i) { return cb(i); }, flags);
	}
#endif

#if TORRENT_ABI_VERSION < 5
	void add_file0(
		file_storage& fs,
		string_view const file,
		std::int64_t size,
		file_flags_t const flags,
		std::time_t md,
		string_view const link
	)
	{
		python_deprecated("add_file is deprecated");
		fs.add_file(std::string(file), size, flags, md, std::string(link));
	}

	void add_file1(
		file_storage& fs,
		bytes const& file,
		std::int64_t size,
		file_flags_t const flags,
		std::time_t md,
		std::string link
	)
	{
		python_deprecated("add_file with bytes is deprecated");
		fs.add_file(file.arr, size, flags, md, link);
	}

	void add_file2(
		file_storage& fs,
		string_view const file,
		std::int64_t size,
		file_flags_t const flags,
		std::time_t md,
		bytes link
	)
	{
		python_deprecated("add_file with bytes is deprecated");
		fs.add_file(std::string(file), size, flags, md, link.arr);
	}
#endif

	struct dummy_file_flags
	{};

	std::string
	file_storage_file_path(file_storage const& fs, file_index_t index, std::string const& base)
	{
		file_storage_check_index(fs, index);
		return fs.file_path(index, base);
	}

	void set_name0(file_storage& fs, string_view const name) { fs.set_name(std::string(name)); }

	void set_name1(file_storage& fs, bytes name)
	{
		python_deprecated("set_name with bytes is deprecated");
		fs.set_name(name.arr);
	}

	template <typename Ret, Ret (file_storage::*fun)(file_index_t) const>
	Ret wrap_file_check(file_storage const& fs, file_index_t const i)
	{
		file_storage_check_index(fs, i);
		return (fs.*fun)(i);
	}

	template <typename Ret, Ret (file_storage::*fun)(piece_index_t) const>
	Ret wrap_piece_check(file_storage const& fs, piece_index_t const i)
	{
		file_storage_check_index(fs, i);
		return (fs.*fun)(i);
	}

	std::string
	renamed_files_file_name(renamed_files const& rf, file_storage const& fs, file_index_t index)
	{
		return std::string(rf.file_name(fs, index));
	}

	std::string file_storage_file_name(file_storage const& fs, file_index_t index)
	{
		file_storage_check_index(fs, index);
		return std::string(fs.file_name(index));
	}
}

void bind_file_storage()
{
	{
		scope s =
			class_<file_storage>("file_storage", no_init)
#if TORRENT_ABI_VERSION < 5
				.def("__init__", make_constructor(&file_storage_constructor))
#endif
				.def("is_valid", &file_storage::is_valid)
#if TORRENT_ABI_VERSION < 5
				.def("add_file",
					add_file0,
					(arg("path"),
						arg("size"),
						arg("flags") = 0,
						arg("mtime") = 0,
						arg("linkpath") = ""))
				.def("add_file",
					add_file1,
					(arg("path"),
						arg("size"),
						arg("flags") = 0,
						arg("mtime") = 0,
						arg("linkpath") = ""))
				.def("add_file",
					add_file2,
					(arg("path"),
						arg("size"),
						arg("flags") = 0,
						arg("mtime") = 0,
						arg("linkpath") = ""))
#endif
				.def("num_files", &file_storage::num_files)
#if TORRENT_ABI_VERSION < 4
				.def("hash", &wrap_file_check<sha1_hash, &file_storage::hash>)
#endif
				.def("symlink", &wrap_file_check<std::string, &file_storage::symlink>)
				.def("file_path", &file_storage_file_path, (arg("idx"), arg("save_path") = ""))
				.def("file_name", &file_storage_file_name, arg("idx"))
				.def("file_size", &wrap_file_check<std::int64_t, &file_storage::file_size>)
				.def("root", &file_storage::root)
				.def("file_offset", &wrap_file_check<std::int64_t, &file_storage::file_offset>)
				.def("file_flags", &wrap_file_check<file_flags_t, &file_storage::file_flags>)

				.def("file_index_for_root", &file_storage::file_index_for_root)
				.def("piece_index_at_file",
					&wrap_file_check<piece_index_t, &file_storage::piece_index_at_file>)
				.def("file_index_at_piece",
					&wrap_piece_check<file_index_t, &file_storage::file_index_at_piece>)
				.def("last_file_index_at_piece",
					&wrap_piece_check<file_index_t, &file_storage::last_file_index_at_piece>)
				.def("file_index_at_offset", &file_storage::file_index_at_offset)
				.def(
					"file_absolute_path", &wrap_file_check<bool, &file_storage::file_absolute_path>)

				.def("v2", &file_storage::v2)

				.def("total_size", &file_storage::total_size)
				.def("size_on_disk", &file_storage::size_on_disk)
				.def("set_num_pieces", &file_storage::set_num_pieces)
				.def("num_pieces", &file_storage::num_pieces)
				.def("set_piece_length", &file_storage::set_piece_length)
				.def("piece_length", &file_storage::piece_length)
				.def("piece_size", &wrap_piece_check<int, &file_storage::piece_size>)
				.def("set_name", &set_name0)
				.def("set_name", &set_name1)
				.def("name", &file_storage::name, return_value_policy<copy_const_reference>());

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
	def("add_files",
		add_files_callback,
		(arg("fs"), arg("path"), arg("predicate"), arg("flags") = 0));
#endif

	class_<renamed_files>("renamed_files")
		.def(
			"file_path", &renamed_files::file_path, (arg("fs"), arg("index"), arg("save_path") = "")
		)
		.def("file_name", &renamed_files_file_name, (arg("fs"), arg("index")))
		.def("file_absolute_path", &renamed_files::file_absolute_path, (arg("fs"), arg("index")))
		.def(
			"rename_file",
			&renamed_files::rename_file,
			(arg("fs"), arg("index"), arg("new_filename"))
		)
		.def("import_filenames", &renamed_files::import_filenames, (arg("fs"), arg("filenames")))
		.def("export_filenames", &renamed_files::export_filenames);
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif
