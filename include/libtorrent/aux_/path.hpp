/*

Copyright (c) 2017-2020, 2022, Arvid Norberg
Copyright (c) 2017, Steven Siloti
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_PATH_HPP_INCLUDED
#define TORRENT_PATH_HPP_INCLUDED

#include <memory>
#include <string>
#include <functional>

#include "libtorrent/config.hpp"
#include "libtorrent/string_view.hpp"
#include "libtorrent/span.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

#ifdef TORRENT_WINDOWS
// windows part
#include "libtorrent/aux_/windows.hpp"
#include <winioctl.h>
#include <sys/types.h>
#else

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <unistd.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <dirent.h> // for DIR

#endif

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/error_code.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/flags.hpp"

namespace libtorrent {

	using file_attributes_t = flags::bitfield_flag<std::uint32_t, struct file_attribute_tag>;
	using file_status_flag_t = flags::bitfield_flag<std::uint32_t, struct file_status_flag_tag>;

	// internal flags for stat_file
	constexpr file_status_flag_t dont_follow_links = 0_bit;

	struct file_status
	{
		std::int64_t file_size = 0;
		std::uint64_t atime = 0;
		std::uint64_t mtime = 0;
		std::uint64_t ctime = 0;

		static constexpr file_attributes_t directory = 0_bit;
		static constexpr file_attributes_t hidden = 1_bit;
		static constexpr file_attributes_t executable = 2_bit;
		static constexpr file_attributes_t symlink = 3_bit;
		file_attributes_t mode = file_attributes_t{};
	};

	TORRENT_EXTRA_EXPORT void stat_file(std::string const& f, file_status* s
		, error_code& ec, file_status_flag_t flags = {});
	TORRENT_EXTRA_EXPORT void rename(std::string const& f
		, std::string const& newf, error_code& ec);
	TORRENT_EXTRA_EXPORT void create_directories(std::string const& f
		, error_code& ec);
	TORRENT_EXTRA_EXPORT void create_directory(std::string const& f
		, error_code& ec);
	TORRENT_EXTRA_EXPORT void remove_all(std::string const& f
		, error_code& ec);
	TORRENT_EXTRA_EXPORT void remove(std::string const& f, error_code& ec);
	TORRENT_EXTRA_EXPORT bool exists(std::string const& f, error_code& ec);
	TORRENT_EXTRA_EXPORT bool is_directory(std::string const& f
		, error_code& ec);

	// file is expected to exist, link will be created to point to it. If hard
	// links are not supported by the filesystem or OS, the file will be copied.
	TORRENT_EXTRA_EXPORT void hard_link(std::string const& file
		, std::string const& link, error_code& ec);

	// split out a path segment from the left side or right side
	TORRENT_EXTRA_EXPORT std::pair<string_view, string_view> rsplit_path(string_view p);
	TORRENT_EXTRA_EXPORT std::pair<string_view, string_view> lsplit_path(string_view p);
	TORRENT_EXTRA_EXPORT std::pair<string_view, string_view> lsplit_path(string_view p, std::size_t pos);

	TORRENT_EXTRA_EXPORT std::string extension(std::string const& f);
	TORRENT_EXTRA_EXPORT std::string remove_extension(std::string const& f);
	TORRENT_EXTRA_EXPORT bool is_root_path(std::string const& f);
	TORRENT_EXTRA_EXPORT bool path_equal(std::string const& lhs, std::string const& rhs);

	// compare each path element individually
	TORRENT_EXTRA_EXPORT int path_compare(string_view lhs, string_view lfile
		, string_view rhs, string_view rfile);

	// internal used by create_torrent.hpp
	TORRENT_EXTRA_EXPORT std::string parent_path(std::string const& f);
	TORRENT_EXTRA_EXPORT bool has_parent_path(std::string const& f);

	// internal used by create_torrent.hpp
	TORRENT_EXTRA_EXPORT std::string filename(std::string const& f);
	TORRENT_EXTRA_EXPORT std::string combine_path(string_view lhs
		, string_view rhs);
	TORRENT_EXTRA_EXPORT void append_path(std::string& branch
		, string_view leaf);
	TORRENT_EXTRA_EXPORT std::string lexically_relative(string_view base
		, string_view target);

	// internal used by create_torrent.hpp
	TORRENT_EXTRA_EXPORT std::string complete(string_view f);
	TORRENT_EXTRA_EXPORT bool is_complete(string_view f);
	TORRENT_EXTRA_EXPORT std::string current_working_directory();
#if TORRENT_USE_UNC_PATHS
	TORRENT_EXTRA_EXPORT std::string canonicalize_path(string_view f);
#endif

// internal type alias export should be used at unit tests only
	using native_path_string =
#if defined TORRENT_WINDOWS
		std::wstring;
#else
		std::string;
#endif

// internal export should be used at unit tests only
	TORRENT_EXTRA_EXPORT native_path_string convert_to_native_path_string(std::string const& path);

#if defined TORRENT_WINDOWS
// internal export should be used at unit tests only
	TORRENT_EXTRA_EXPORT std::string convert_from_native_path(wchar_t const* s);
#else
// internal export should be used at unit tests only
	TORRENT_EXTRA_EXPORT std::string convert_from_native_path(char const* s);
#endif
}

#endif // TORRENT_PATH_HPP_INCLUDED
